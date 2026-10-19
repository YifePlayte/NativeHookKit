//
// NativeHookKit: cooperative madvise(MADV_DONTNEED) guard.
//
// XYRE memory cleanup can drop modified copy-on-write code pages and silently
// revert patches.  A module registers every patched range before writing; the
// guard keeps those pages mapped.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nhk {

// Installs the guard on first use.  The preferred backend is an LSPlt GOT chain
// on the images listed by the module descriptor's RuntimeImages (or the XYRE
// defaults), so several modules can share one chain.
bool EnsureMadviseGuard();

// Registers [address, address + size) as hook code that must survive
// MADV_DONTNEED.  A false result means the range is unprotected.
bool ProtectHookRange(uintptr_t address, size_t size);

}  // namespace nhk
