# Pinned LSPlt artifact

- Upstream: `https://github.com/LSPosed/LSPlt`
- Source commit: `7d609ac3c2d8faa0c830b0904024ef5c81a98e6e`
- Artifact: `lsplt-standalone-2.1.1-7d609ac-compat.aar`
- Local Maven: `third_party/maven`
- License: LGPL-3.0 (included in the AAR at `META-INF/LICENSE-LSPlt`)

The AAR is a shared-library prefab build with:

- arm64-v8a only
- NDK r28 (`28.2.13676358`)
- `c++_static`
- `-Wl,-z,max-page-size=16384`
- an additional legacy `MapInfo::Scan()` overload so the old MiuiBackGestureHook
  LSPlt ABI can resolve its symbol when this library is loaded first
- patch: `third_party/lsplt/patches/0001-legacy-scan-overload.patch`

Build command (from the LSPlt source tree, after adding the overload):

```bash
cmake -S lsplt/src/main/jni -B build \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 \
  -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SHARED_LINKER_FLAGS='-Wl,-z,max-page-size=16384' \
  -DLSPLT_BUILD_SHARED=ON
cmake --build build --target lsplt
```

The AAR is committed so consumers do not need an LSPlt checkout, Gradle
network access, or a local rebuild.
