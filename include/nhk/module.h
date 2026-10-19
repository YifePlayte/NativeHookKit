//
// NativeHookKit: thin module descriptor for the shared native entry.
//
// The kit owns only the LSPosed entry skeleton.  Process predicates, targets,
// feature tables, config and library handling stay in the concrete module.
//

#pragma once

#include <stddef.h>

namespace nhk {

// Optional image lists for apps whose runtime differs from the XYRE defaults.
// `preloaded` lists images already mapped when native_init runs; they are
// replayed once through on_library_loaded.  `madvise` lists images whose
// madvise PLT slot the guard chains onto.  A null list or zero count keeps the
// kit default.  The lists must stay valid for the process lifetime.
struct RuntimeImages {
    const char* const* preloaded;
    size_t preloaded_count;
    const char* const* madvise;
    size_t madvise_count;
};

struct ModuleDescriptor {
    const char* log_tag;
    // Returns false when the process is not a hook target.
    bool (*on_native_init)();
    // Called for every library load; the module must filter by process/target.
    // The handle is only valid until the callback returns: the kit reuses
    // dlopen references when it replays preloaded images.
    void (*on_library_loaded)(const char* name, void* handle);
    // Optional runtime overrides; nullptr keeps the kit defaults.
    const RuntimeImages* images;
};

// Keeps module definitions terse and forward compatible: optional fields can be
// appended without touching every module.
constexpr ModuleDescriptor MakeDescriptor(
        const char* log_tag, bool (*on_native_init)(),
        void (*on_library_loaded)(const char* name, void* handle),
        const RuntimeImages* images = nullptr) {
    return ModuleDescriptor{log_tag, on_native_init, on_library_loaded, images};
}

const ModuleDescriptor& GetModuleDescriptor();

}  // namespace nhk
