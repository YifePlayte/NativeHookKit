//
// NativeHookKit: LSPosed native API wrapper.
//
// Feature hooks use inline hooks for targets that have no PLT/GOT slot.
// This component owns only the LSPosed entries; it carries no module policy.
//

#pragma once

#include <stdint.h>

namespace nhk {

struct NativeAPIEntries {
    uint32_t version;
    int (*hookFunc)(void* target, void* replacement, void** backup);
    int (*unhookFunc)(void* target);
};

using NativeOnModuleLoaded = void (*)(const char* name, void* handle);

constexpr int kHookSuccess = 0;
constexpr int kHookFailed = 1;

bool InitializeHookBackend(const NativeAPIEntries* entries);

int InstallInlineHook(void* target, void* replacement, void** original);

// Load-bias lookup used by feature hooks that must locate an already-mapped
// image by name.  Fails when the name is not unique in the process.
void* FindLibraryBase(const char* library_name);

}  // namespace nhk
