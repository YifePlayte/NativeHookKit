#include "nhk/module.h"

#include "nhk/log.h"
#include "nhk/page_guard.h"

namespace {

void OnLibraryLoaded(const char* name, void* handle) {
    const nhk::ModuleDescriptor& descriptor = nhk::GetModuleDescriptor();
    if (descriptor.on_library_loaded != nullptr) {
        descriptor.on_library_loaded(name, handle);
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
    return OnLibraryLoaded;
}

}  // extern "C"
