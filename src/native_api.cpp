#include "nhk/native_api.h"

#include "nhk/log.h"

#include <link.h>
#include <string.h>

namespace nhk {
namespace {

const NativeAPIEntries* g_lsposed_api = nullptr;

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

}  // namespace nhk
