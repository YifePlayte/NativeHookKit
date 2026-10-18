// NativeHookKit: LSPosed native backend and LSPlt madvise guard.

#include "nhk/page_guard.h"

#include "nhk/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/sysmacros.h>
#include <vector>

#include <lsplt.hpp>

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <link.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace nhk {
namespace {

const NativeAPIEntries* g_lsposed_api = nullptr;

size_t PageSize() {
    static size_t value = 0u;
    if (value == 0u) {
        const long queried = sysconf(_SC_PAGESIZE);
        value = queried > 0 ? static_cast<size_t>(queried) : size_t{4096};
    }
    return value;
}

uintptr_t PageStart(uintptr_t address) {
    return address & ~(static_cast<uintptr_t>(PageSize()) - 1u);
}

// ---------------------------------------------------------------------------
// madvise(MADV_DONTNEED) guard
//
// HyperOS memory cleanup drops cached pages of an XYRE app image.  For the
// APK-backed mapping that also drops our modified copy-on-write code pages and
// the kernel re-reads the original bytes, silently reverting every patch.
//
// The guard is a cooperative LSPlt GOT chain, not an inline hook on libc:
// every native module installs its own GuardedMadvise on the runtime image's
// madvise PLT slot and calls the backup it received from LSPlt.  The pinned
// compat AAR is shared by soname, so all guards operate on one LSPlt registry;
// each module still owns its own protected-page set.
// ---------------------------------------------------------------------------

using MadviseFn = int (*)(void*, size_t, int);

constexpr size_t kMaxProtectedPages = 128u;

// 0 = idle, 1 = installing, 2 = installed, 3 = failed.
uint32_t g_guard_state = 0u;
MadviseFn g_original_madvise = nullptr;
uintptr_t g_protected_pages[kMaxProtectedPages]{};
size_t g_protected_page_count = 0u;
uint32_t g_protected_page_lock = 0u;
uint32_t g_guard_hit_log_state = 0u;
uint32_t g_runtime_missing = 0u;

bool AddProtectedPage(uintptr_t address) {
    const uintptr_t page = PageStart(address);
    while (__atomic_exchange_n(&g_protected_page_lock, 1u, __ATOMIC_ACQUIRE) !=
            0u) {
        sched_yield();
    }
    const size_t count =
            __atomic_load_n(&g_protected_page_count, __ATOMIC_RELAXED);
    for (size_t index = 0u; index < count; ++index) {
        if (g_protected_pages[index] == page) {
            __atomic_store_n(&g_protected_page_lock, 0u, __ATOMIC_RELEASE);
            return true;
        }
    }
    if (count >= kMaxProtectedPages) {
        __atomic_store_n(&g_protected_page_lock, 0u, __ATOMIC_RELEASE);
        return false;
    }
    g_protected_pages[count] = page;
    __atomic_store_n(&g_protected_page_count, count + 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_protected_page_lock, 0u, __ATOMIC_RELEASE);
    return true;
}

int GuardedMadvise(void* address, size_t length, int advice) {
    MadviseFn original = __atomic_load_n(&g_original_madvise, __ATOMIC_ACQUIRE);
    if (original == nullptr) {
        errno = ENOSYS;
        return -1;
    }
    if (advice != MADV_DONTNEED || length == 0u) {
        return original(address, length, advice);
    }
    const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
    if (begin % PageSize() != 0u || length > UINTPTR_MAX - begin) {
        return original(address, length, advice);
    }
    const uintptr_t end = begin + length;
    uintptr_t cursor = begin;
    bool protected_page_found = false;
    while (cursor < end) {
        uintptr_t next_page = end;
        const size_t count =
                __atomic_load_n(&g_protected_page_count, __ATOMIC_ACQUIRE);
        for (size_t index = 0u; index < count; ++index) {
            const uintptr_t page = g_protected_pages[index];
            if (page >= cursor && page < end && page < next_page) {
                next_page = page;
            }
        }
        if (next_page == end) break;
        if (next_page > cursor &&
                original(reinterpret_cast<void*>(cursor),
                         next_page - cursor, advice) != 0) {
            return -1;
        }
        cursor = next_page + PageSize();
        if (cursor > end) cursor = end;
        protected_page_found = true;
    }
    if (!protected_page_found) {
        return original(address, length, advice);
    }
    if (__atomic_exchange_n(&g_guard_hit_log_state, 1u, __ATOMIC_ACQ_REL) ==
            0u) {
        NHK_LOGW("madvise guard preserved hook pages in %p+0x%zx",
                   address, length);
    }
    if (cursor < end &&
            original(reinterpret_cast<void*>(cursor), end - cursor, advice) !=
                    0) {
        return -1;
    }
    return 0;
}

bool InstallInlineMadviseFallback() {
    void* target = dlsym(RTLD_DEFAULT, "madvise");
    if (target == nullptr) {
        NHK_LOGE("madvise guard: symbol not found");
        return false;
    }
    void* backup = nullptr;
    if (InstallInlineHook(target, reinterpret_cast<void*>(GuardedMadvise),
                          &backup) != kHookSuccess ||
            backup == nullptr) {
        NHK_LOGE("madvise guard: inline fallback unavailable");
        return false;
    }
    __atomic_store_n(&g_original_madvise, reinterpret_cast<MadviseFn>(backup),
                     __ATOMIC_RELEASE);
    NHK_LOGW("madvise guard: installed inline fallback");
    return true;
}

// The XYRE runtime is the common caller image for app memory cleanup;
// MiuiBackGestureHook uses the same image.  Hooking the same PLT slot means
// both modules' protections are in one chain.
//
// Do not call lsplt::MapInfo::Scan() here.  That symbol changed from the old
// no-argument ABI to Scan(std::string_view); using either overload directly
// would make our module depend on which LSPlt copy won the soname race.  We
// only need dev/inode, so parse /proc/self/maps ourselves.
bool EndsWith(const char* value, const char* suffix) {
    if (value == nullptr || suffix == nullptr) return false;
    const size_t value_length = strlen(value);
    const size_t suffix_length = strlen(suffix);
    if (suffix_length > value_length) return false;
    return strcmp(value + value_length - suffix_length, suffix) == 0;
}

bool FindRuntimeIdentity(const char* name, dev_t* device, ino_t* inode) {
    if (name == nullptr || device == nullptr || inode == nullptr) return false;
    FILE* maps = fopen("/proc/self/maps", "re");
    if (maps == nullptr) return false;
    char* line = nullptr;
    size_t capacity = 0u;
    bool found = false;
    dev_t found_device = 0;
    ino_t found_inode = 0;
    while (getline(&line, &capacity, maps) > 0) {
        unsigned long long start = 0u;
        unsigned long long end = 0u;
        unsigned long long offset = 0u;
        unsigned int major = 0u;
        unsigned int minor = 0u;
        unsigned long map_inode = 0u;
        char permissions[5]{};
        int path_offset = 0;
        if (sscanf(line, "%llx-%llx %4s %llx %x:%x %lu %n", &start, &end,
                   permissions, &offset, &major, &minor, &map_inode,
                   &path_offset) != 7) {
            continue;
        }
        if (permissions[0] != 'r' || permissions[3] != 'p' || offset != 0u) {
            continue;
        }
        char* path = line + path_offset;
        while (*path == ' ' || *path == '\t') ++path;
        const size_t path_length = strlen(path);
        if (path_length > 0u && path[path_length - 1u] == '\n') {
            path[path_length - 1u] = '\0';
        }
        if (!EndsWith(path, name)) continue;
        const dev_t map_device = makedev(major, minor);
        const ino_t map_inode_value = static_cast<ino_t>(map_inode);
        if (found && (map_device != found_device ||
                      map_inode_value != found_inode)) {
            free(line);
            fclose(maps);
            return false;
        }
        found_device = map_device;
        found_inode = map_inode_value;
        found = true;
    }
    free(line);
    fclose(maps);
    if (!found) return false;
    *device = found_device;
    *inode = found_inode;
    return true;
}

bool InstallLspltMadviseHook() {
    static constexpr char kRuntimeName[] = "libhyper_os_flutter.so";
    dev_t device = 0;
    ino_t inode = 0;
    if (!FindRuntimeIdentity(kRuntimeName, &device, &inode)) {
        __atomic_store_n(&g_runtime_missing, 1u, __ATOMIC_RELEASE);
        NHK_LOGW("madvise guard: %s not mapped yet", kRuntimeName);
        return false;
    }
    __atomic_store_n(&g_runtime_missing, 0u, __ATOMIC_RELEASE);

    void* backup = nullptr;
    if (!lsplt::RegisterHook(device, inode, "madvise",
                             reinterpret_cast<void*>(GuardedMadvise),
                             &backup) ||
            !lsplt::CommitHook() || backup == nullptr ||
            backup == reinterpret_cast<void*>(GuardedMadvise)) {
        NHK_LOGE("madvise guard: LSPlt hook failed for %s", kRuntimeName);
        return false;
    }
    __atomic_store_n(&g_original_madvise, reinterpret_cast<MadviseFn>(backup),
                     __ATOMIC_RELEASE);
    NHK_LOGW("madvise guard installed: LSPlt GOT chain on %s",
               kRuntimeName);
    return true;
}

bool InstallMadviseHook() {
    if (InstallLspltMadviseHook()) return true;
    return InstallInlineMadviseFallback();
}

struct FindLibraryRequest {
    const char* name;
    void* base;
    size_t matches;
};

int FindLibraryCallback(dl_phdr_info* info, size_t, void* opaque) {
    auto* request = static_cast<FindLibraryRequest*>(opaque);
    const char* path = info->dlpi_name;
    if (path == nullptr) return 0;

    const char* basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    if (strcmp(basename, request->name) == 0 ||
            strstr(path, request->name) != nullptr) {
        request->base = reinterpret_cast<void*>(info->dlpi_addr);
        request->matches++;
    }
    return 0;
}

}  // namespace

bool InitializeHookBackend(const NativeAPIEntries* entries) {
    if (entries == nullptr || entries->hookFunc == nullptr) {
        NHK_LOGE("invalid LSPosed API entries");
        return false;
    }
    g_lsposed_api = entries;
    return true;
}

int InstallInlineHook(void* target, void* replacement, void** original) {
    if (g_lsposed_api == nullptr || g_lsposed_api->hookFunc == nullptr ||
            target == nullptr || replacement == nullptr || original == nullptr) {
        return kHookFailed;
    }
    *original = nullptr;
    if (g_lsposed_api->hookFunc(target, replacement, original) !=
            kHookSuccess ||
            *original == nullptr) {
        return kHookFailed;
    }
    return kHookSuccess;
}

void* FindLibraryBase(const char* library_name) {
    FindLibraryRequest request{};
    request.name = library_name;
    request.base = nullptr;
    request.matches = 0;
    dl_iterate_phdr(FindLibraryCallback, &request);
    if (request.matches == 1) return request.base;
    return nullptr;
}

bool EnsureMadviseGuard() {
    for (;;) {
        const uint32_t state =
                __atomic_load_n(&g_guard_state, __ATOMIC_ACQUIRE);
        if (state == 2u) return true;
        if (state == 3u) return false;
        if (state == 0u) {
            uint32_t expected = 0u;
            if (__atomic_compare_exchange_n(&g_guard_state, &expected, 1u,
                                            false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                const bool installed = InstallMadviseHook();
                const bool runtime_missing =
                        __atomic_load_n(&g_runtime_missing, __ATOMIC_ACQUIRE) !=
                        0u;
                if (!installed && runtime_missing) {
                    // The runtime library is not mapped yet.  Do not poison the
                    // process: a later library-load callback may retry.
                    __atomic_store_n(&g_guard_state, 0u, __ATOMIC_RELEASE);
                    return false;
                }
                __atomic_store_n(&g_guard_state, installed ? 2u : 3u,
                                 __ATOMIC_RELEASE);
                if (installed) {
                    NHK_LOGW("madvise guard installed");
                }
                return installed;
            }
            continue;
        }
        // Another thread is installing the guard.
        sched_yield();
    }
}

bool ProtectHookRange(uintptr_t address, size_t size) {
    if (address == 0u || size == 0u || size > UINTPTR_MAX - address) {
        return false;
    }
    if (!EnsureMadviseGuard()) return false;
    const uintptr_t end = address + size;
    for (uintptr_t page = PageStart(address); page < end;
         page += PageSize()) {
        if (!AddProtectedPage(page)) {
            NHK_LOGE("madvise guard: page table exhausted at 0x%" PRIxPTR,
                       page);
            return false;
        }
    }
    return true;
}

}  // namespace nhk
