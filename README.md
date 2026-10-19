# NativeHookKit

Source-level native hook scaffolding for LSPosed modules that patch apps
running in the Xiaomi HyperOS Runtime Environment (XYRE), including their Dart
AOT payloads.  The desktop, gallery and weather apps are XYRE processes.

The library contains no module-specific preference keys, package names,
feature bits or UI. A module supplies those in its own descriptor and feature
hooks.

## Components

```text
include/nhk/
  module.h          thin module descriptor for the shared native entry
  dart_image.h      ELF image lookup, pattern scanning, verified code patches
  page_guard.h      LSPosed native API wrapper + LSPlt madvise GOT guard
  config.h          clipboard native-config transport (module descriptor)
  log.h             module-settable log tag
src/
  entry.cpp         shared native_init + library callback forwarding
  dart_image.cpp
  page_guard.cpp
  config.cpp
  log.cpp
```

The page guard is an external LSPlt (`org.lsposed.lsplt:lsplt-standalone`)
dependency. The preferred backend is a GOT chain: every module installs its
own guard on the runtime image's `madvise` slot and calls the backup returned
by LSPlt. An inline fallback exists only for backward compatibility and logs
when it is used; do not add a new inline `madvise` hook.

## Use from a module

1. Make the module's app dependency available to CMake via prefab:

   ```kotlin
   implementation("org.lsposed.lsplt:lsplt-standalone:2.1.1-7d609ac-compat")
   buildFeatures.prefab = true
   ```

2. Add the kit and link it.  `--whole-archive` is required because `nhk`
   provides the exported `native_init` and nothing in the module references it:

   ```cmake
   set(NATIVE_HOOK_KIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../native-hook-kit"
       CACHE PATH "NativeHookKit source directory")
   add_subdirectory("${NATIVE_HOOK_KIT_DIR}" native_hook_kit)
   target_link_libraries(my_native PRIVATE log dl z
       -Wl,--whole-archive nhk -Wl,--no-whole-archive)
   ```

3. Implement the module descriptor.  The kit sets the log tag from
   `log_tag`, calls `on_native_init` for each native entry invocation, and
   forwards every library callback to `on_library_loaded`:

   ```cpp
   namespace nhk {
   const ModuleDescriptor& GetModuleDescriptor() {
       static const ModuleDescriptor kDescriptor{
           "MyModuleNative", OnNativeInit, OnLibraryLoaded};
       return kDescriptor;
   }
   }  // namespace nhk
   ```

   `OnNativeInit` must return `false` for processes the module does not hook.
   `OnLibraryLoaded` must do its own process and library filtering.

4. If existing hooks use a private namespace, define the alias in the
   module's own hook header; do not add a separate compatibility header:

   ```cpp
   #include <nhk/dart_image.h>
   namespace mymodule { namespace dart = nhk::dart; }
   ```

5. Configure the native switch transport once in `OnNativeInit`:

   ```cpp
   constexpr nhk::config::Descriptor kConfig{
       0x00xxxxxx, 0xXXXXXX, kKnownFeatures};
   nhk::config::Configure(&kConfig);
   ```

   The matching Kotlin endpoint must use the same transaction code, ACK and
   feature-key order.

6. Use `nhk::dart` for pattern work and `nhk::ProtectHookRange` before writing
   any code page. Feature hooks still live in the module.

## APK packaging

The module's native `.so` links `liblsplt.so`. LSPosed loads the module payload
directly from the APK, so the native target must carry a `DT_RUNPATH` of
`$ORIGIN`; otherwise `liblsplt.so` is not found. The kit's consumers pass a
linker response file containing:

```text
-rpath $ORIGIN
```

CMake/Ninja otherwise reduces `$ORIGIN` to `$`.

## Design rules

- Fail closed on missing or ambiguous patterns; never patch an unverified
  address.
- Register a page with `ProtectHookRange` before modifying it.
- Keep installers idempotent across repeated library-load callbacks.
- Do not force an entire `isValid` predicate true at entry. Patch the narrow
  final gate after the module's type/context checks.
- Verify every pattern against the XYRE app builds the module supports; the
  kit does not own a version matrix.
- Feature/config/UI naming stays in the module, not in this kit.

## Current limitation

The shared entry is intentionally thin.  Target lists, process predicates,
feature tables, config keys and library-specific callbacks remain in the
concrete module; the kit does not try to own desktop/weather/gallery policy.
Kotlin endpoints are also module-local.
