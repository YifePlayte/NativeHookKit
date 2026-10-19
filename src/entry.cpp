#include "nhk/module.h"

#include "nhk/log.h"
#include "nhk/native_api.h"

#include <dlfcn.h>

namespace {

void OnLibraryLoaded(const char* name, void* handle) {
    const nhk::ModuleDescriptor& descriptor = nhk::GetModuleDescriptor();
    if (descriptor.on_library_loaded != nullptr) {
        descriptor.on_library_loaded(name, handle);
    }
}

// The HYOS runtime images are often mapped before LSPosed invokes the module's
// native_init entry, so the on-library-loaded callback never sees them.  The
// module callback is invoked once for each image already loaded so its
// backfill logic runs, mirroring LSPosed's later callbacks.  RTLD_NOLOAD is
// intentional: this must not pull in a library the process does not already
// use.
void ReplayLoadedLibrary(const char* name) {
    if (name == nullptr) return;
    void* handle = dlopen(name, RTLD_NOW | RTLD_NOLOAD);
    if (handle == nullptr) return;
    NHK_LOGW("native_init backfilling %s", name);
    OnLibraryLoaded(name, handle);
    dlclose(handle);
}

// The runtime image every HYOS app maps before LSPosed injects the module.
constexpr const char* kDefaultPreloadedImages[] = {
    "libhyper_os_flutter.so",
};

void ReplayPreloadedImages() {
    const nhk::ModuleDescriptor& descriptor = nhk::GetModuleDescriptor();
    const nhk::RuntimeImages* overrides = descriptor.images;
    const char* const* images = nullptr;
    size_t count = 0u;
    if (overrides != nullptr) {
        images = overrides->preloaded;
        count = overrides->preloaded_count;
    }
    if (images == nullptr || count == 0u) {
        images = kDefaultPreloadedImages;
        count = sizeof(kDefaultPreloadedImages) /
                sizeof(kDefaultPreloadedImages[0]);
    }
    for (size_t index = 0u; index < count; ++index) {
        ReplayLoadedLibrary(images[index]);
    }
}

}  // namespace

extern "C" {

// Thin LSPosed native entry shared by NativeHookKit consumers.  The module
// keeps its own process checks, targets, config and feature dispatch.
__attribute__((visibility("default"), used))
nhk::NativeOnModuleLoaded native_init(const nhk::NativeAPIEntries* entries) {
    const nhk::ModuleDescriptor& descriptor = nhk::GetModuleDescriptor();
    nhk::log::SetTag(descriptor.log_tag != nullptr ? descriptor.log_tag
                                                   : "NativeHookKit");
    if (!nhk::InitializeHookBackend(entries)) return nullptr;
    if (descriptor.on_native_init == nullptr ||
            !descriptor.on_native_init()) {
        NHK_LOGW("native_init rejected by module");
        return nullptr;
    }
    ReplayPreloadedImages();
    return OnLibraryLoaded;
}

}  // extern "C"

