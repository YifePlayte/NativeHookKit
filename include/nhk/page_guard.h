// NativeHookKit: LSPosed native entry hooks and the cooperative madvise guard.
//
// The guard is shared infrastructure, not a feature.  It is deliberately
// independent of any module's own preference/config store: a module only calls
// ProtectHookRange before modifying code pages.

#pragma once

#include <stddef.h>
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

// Stores the LSPosed native API entries for inline feature hooks.
bool InitializeHookBackend(const NativeAPIEntries* entries);

// Inline hook wrapper for feature hooks that cannot use a PLT/GOT slot.
int InstallInlineHook(void* target, void* replacement, void** original);

// Load-bias lookup used by a few feature hooks.
void* FindLibraryBase(const char* library_name);

// Installs the madvise guard on first use.  The preferred implementation is an
// LSPlt GOT chain on the XYRE runtime, so every module can keep its own guard
// and call the backup it receives from LSPlt.
bool EnsureMadviseGuard();

// Registers [address, address + size) as hook code that must survive
// MADV_DONTNEED.  A false result means the range is unprotected.
bool ProtectHookRange(uintptr_t address, size_t size);

}  // namespace nhk
