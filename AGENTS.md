# AGENTS.md

## Scope

NativeHookKit is a source-level C++ library shared by LSPosed modules that hook
apps running in the Xiaomi HyperOS Runtime Environment (XYRE).  The desktop,
gallery and weather apps are XYRE processes.  It must stay module-neutral: no
package names, preference keys, feature bits, UI strings or module-specific
descriptors.

## Layout

```text
include/nhk/module.h       thin module descriptor
include/nhk/dart_image.h   image lookup, pattern scan, verified patches
include/nhk/page_guard.h   LSPosed backend + LSPlt madvise GOT guard
include/nhk/config.h       native-config transport and feature gating
include/nhk/log.h          tag-settable logging
src/entry.cpp              shared native_init / library callback
src/*.cpp
```

## Integration

- Consume the kit as source through CMake `add_subdirectory`; the default sibling
  path is `native-hook-kit`, overridable with `NATIVE_HOOK_KIT_DIR`.
- Link `nhk` with `--whole-archive` so the shared `native_init` is retained.
- The app must depend on `org.lsposed.lsplt:lsplt-standalone` and enable prefab.
- Implement `nhk::GetModuleDescriptor()` with `log_tag`, `on_native_init` and
  `on_library_loaded`.
- The module keeps its own process checks, targets, native config and feature
  hook files.

## Page guard

- LSPlt is the preferred madvise backend. The guard calls the backup returned by
  LSPlt, so module guards form a GOT chain.
- Never install an inline hook on libc `madvise` in a new module.
- Register every patched page with `nhk::ProtectHookRange` before writing.
- The module native `.so` needs `DT_RUNPATH=$ORIGIN` so LSPosed can resolve
  `liblsplt.so` from the APK.
- The pinned compat AAR is shared by soname. All guards operate on the same
  LSPlt registry; each module preserves its own protected pages. Keep consumers
  on one AAR version to avoid soname/ABI collisions.
- If LSPlt is unavailable, the kit falls back to an inline guard only for
  backward compatibility; log which backend was used.

## Dart patch rules

- Locate a unique pattern in every supported XYRE app build.
- Validate the expected instruction words and a known return block.
- Fail closed on missing or ambiguous matches.
- Do not force a whole `isValid` predicate true at entry; bypass only the final
  form-factor/eligibility gate after the type and context checks.
- Verify the XYRE app builds the module supports before shipping a pattern; the
  kit does not own a version matrix.

## Verification

- Build both consumers after a kit change.
- Restart the target XYRE process and confirm the guard/feature logs.
- Inspect `DT_RUNPATH` and the packaged `liblsplt.so` in the APK.
- Read back patched words from `/proc/<pid>/mem` when diagnosing reversion.
