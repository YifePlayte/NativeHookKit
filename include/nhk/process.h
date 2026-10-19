//
// NativeHookKit: process identity helpers shared by XYRE modules.
//
// LSPosed initializes the HYOS native entry in the spawner family before the
// app child is forked, so every module needs the same cmdline / executable
// checks.  They live here instead of being copied into each module.
//

#pragma once

#include <stddef.h>

namespace nhk::process {

// Reads argv[0] from /proc/self/cmdline.  Returns false on failure.
bool SelfCommandLine(char* buffer, size_t size);
bool SelfCommandLineIs(const char* name);

// Reads the resolved /proc/self/exe path.  Returns false on failure.
bool SelfExecutable(char* buffer, size_t size);
bool SelfExecutableIs(const char* path);

}  // namespace nhk::process
