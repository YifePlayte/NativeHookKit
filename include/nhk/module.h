//
// NativeHookKit: thin module descriptor for the shared native entry.
//
// The kit owns only the LSPosed entry skeleton.  Process predicates, targets,
// feature tables, config and library handling stay in the concrete module.
//

#pragma once

namespace nhk {

struct ModuleDescriptor {
    const char* log_tag;
    // Returns false when the process is not a hook target.
    bool (*on_native_init)();
    // Called for every library load; the module must filter by process/target.
    void (*on_library_loaded)(const char* name, void* handle);
};

const ModuleDescriptor& GetModuleDescriptor();

}  // namespace nhk
