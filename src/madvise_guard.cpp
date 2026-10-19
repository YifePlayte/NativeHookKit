#include "nhk/madvise_guard.h"

#include "nhk/log.h"
#include "nhk/module.h"
#include "nhk/native_api.h"
#include "nhk/text.h"

#include <lsplt.hpp>

#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace nhk {
namespace {

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
MadviseFn g_inline_backup = nullptr;
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

int GuardedMadviseWith(MadviseFn original, void* address, size_t length,
                       int advice) {
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

// Libraries that may issue madvise(MADV_DONTNEED) against an XYRE app image.
//
// The 7751+ launcher runs its Dart payload on libhyper_os_flutter.so and the
// cleanup comes from that image.  The 7748 launcher maps the Dart payload
// straight from the APK instead; libhyper_os_flutter.so is only inherited as a
// stale data segment from the USAP parent, and the live cleanup goes through
// the Rust mmap crates.  Hook every candidate that is really mapped so the
// guard follows whichever runtime the launcher build uses.  Each slot keeps
// its own LSPlt backup because a sibling module may have hooked the same slot.
constexpr size_t kMaxGuardSlots = 8u;
// The images every HYOS app uses for memory cleanup.
constexpr const char* kDefaultMadviseImages[] = {
    "libhyper_os_flutter.so",
    "libmemmap2.dylib.so",
    "libnix.dylib.so",
};


MadviseFn g_slot_backup[kMaxGuardSlots]{};

template <size_t Slot>
int GuardedMadviseSlot(void* address, size_t length, int advice) {
    return GuardedMadviseWith(
            __atomic_load_n(&g_slot_backup[Slot], __ATOMIC_ACQUIRE),
            address, length, advice);
}

using GuardFn = int (*)(void*, size_t, int);

constexpr GuardFn kSlotWrappers[kMaxGuardSlots] = {
    &GuardedMadviseSlot<0>,
    &GuardedMadviseSlot<1>,
    &GuardedMadviseSlot<2>,
    &GuardedMadviseSlot<3>,
    &GuardedMadviseSlot<4>,
    &GuardedMadviseSlot<5>,
    &GuardedMadviseSlot<6>,
    &GuardedMadviseSlot<7>,
};

int GuardedMadviseInline(void* address, size_t length, int advice) {
    return GuardedMadviseWith(
            __atomic_load_n(&g_inline_backup, __ATOMIC_ACQUIRE),
            address, length, advice);
}

bool InstallInlineMadviseFallback() {
    void* target = dlsym(RTLD_DEFAULT, "madvise");
    if (target == nullptr) {
        NHK_LOGE("madvise guard: symbol not found");
        return false;
    }
    void* backup = nullptr;
    if (InstallInlineHook(target, reinterpret_cast<void*>(GuardedMadviseInline),
                          &backup) != kHookSuccess ||
            backup == nullptr) {
        NHK_LOGE("madvise guard: inline fallback unavailable");
        return false;
    }
    __atomic_store_n(&g_inline_backup, reinterpret_cast<MadviseFn>(backup),
                     __ATOMIC_RELEASE);
    NHK_LOGW("madvise guard: installed inline fallback");
    return true;
}

// Do not call lsplt::MapInfo::Scan() here.  That symbol changed from the old
// no-argument ABI to Scan(std::string_view); using either overload directly
// would make our module depend on which LSPlt copy won the soname race.  We
// only need dev/inode, so parse /proc/self/maps ourselves.

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
        if (!text::EndsWith(path, name)) continue;
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
    const nhk::ModuleDescriptor& descriptor = nhk::GetModuleDescriptor();
    const nhk::RuntimeImages* overrides = descriptor.images;
    const char* const* images = nullptr;
    size_t image_count = 0u;
    if (overrides != nullptr) {
        images = overrides->madvise;
        image_count = overrides->madvise_count;
    }
    if (images == nullptr || image_count == 0u) {
        images = kDefaultMadviseImages;
        image_count = sizeof(kDefaultMadviseImages) /
                      sizeof(kDefaultMadviseImages[0]);
    }
    if (image_count > kMaxGuardSlots) image_count = kMaxGuardSlots;

    size_t queued = 0u;
    bool candidate_present = false;
    for (size_t slot = 0u; slot < image_count; ++slot) {
        const char* name = images[slot];
        dev_t device = 0;
        ino_t inode = 0;
        if (name == nullptr || !FindRuntimeIdentity(name, &device, &inode)) {
            continue;
        }
        candidate_present = true;
        // LSPlt stores this pointer and assigns the backup during CommitHook,
        // so it must reference static storage, not a stack local.
        if (!lsplt::RegisterHook(
                    device, inode, "madvise",
                    reinterpret_cast<void*>(kSlotWrappers[slot]),
                    reinterpret_cast<void**>(&g_slot_backup[slot]))) {
            NHK_LOGE("madvise guard: LSPlt register failed for %s", name);
            continue;
        }
        NHK_LOGW("madvise guard: LSPlt slot queued for %s", name);
        ++queued;
    }
    if (queued == 0u) {
        if (!candidate_present) {
            // No candidate image is mapped yet.  Leave the guard retryable so
            // a later library-load callback can install it.
            __atomic_store_n(&g_runtime_missing, 1u, __ATOMIC_RELEASE);
            NHK_LOGW("madvise guard: no runtime image mapped yet");
        } else {
            NHK_LOGE("madvise guard: no madvise slot could be queued");
        }
        return false;
    }
    if (!lsplt::CommitHook()) {
        NHK_LOGE("madvise guard: LSPlt commit failed");
        return false;
    }
    size_t hooked = 0u;
    for (size_t slot = 0u; slot < kMaxGuardSlots; ++slot) {
        const MadviseFn backup =
                __atomic_load_n(&g_slot_backup[slot], __ATOMIC_ACQUIRE);
        if (backup != nullptr &&
                backup != reinterpret_cast<MadviseFn>(kSlotWrappers[slot])) {
            ++hooked;
        }
    }
    if (hooked == 0u) {
        NHK_LOGE("madvise guard: LSPlt commit hooked no slot");
        return false;
    }
    __atomic_store_n(&g_runtime_missing, 0u, __ATOMIC_RELEASE);
    NHK_LOGW("madvise guard installed: LSPlt GOT chain on %zu slot(s)", hooked);
    return true;
}

bool InstallMadviseHook() {
    if (InstallLspltMadviseHook()) return true;
    // The runtime image is not mapped yet.  Falling back to the inline guard
    // here would install a libc hook and make the state machine report
    // success, which would block the LSPlt GOT chain for the rest of the
    // process lifetime.  Leave the guard uninstalled instead; the next
    // library-load callback retries while the runtime is still missing.
    if (__atomic_load_n(&g_runtime_missing, __ATOMIC_ACQUIRE) != 0u) {
        return false;
    }
    return InstallInlineMadviseFallback();
}

}  // namespace

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
