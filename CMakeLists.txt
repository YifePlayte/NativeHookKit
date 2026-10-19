cmake_minimum_required(VERSION 3.22)

project(NativeHookKit LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "NativeHookKit must use the Android NDK toolchain")
endif()

if(NOT "${ANDROID_ABI}" STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported; got '${ANDROID_ABI}'")
endif()

find_package(lsplt REQUIRED CONFIG)

add_library(nhk STATIC
    src/entry.cpp
    src/dart_image.cpp
    src/native_api.cpp
    src/madvise_guard.cpp
    src/process.cpp
    src/text.cpp
    src/config.cpp
    src/log.cpp
)

target_include_directories(nhk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(nhk PUBLIC cxx_std_17)
target_compile_options(nhk PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
set_target_properties(nhk PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(nhk PUBLIC log dl lsplt::lsplt)

# Links a module target against the kit.  The shared native_init entry lives in
# a static archive, so it must be retained with --whole-archive.
function(nhk_link_module target)
    target_link_libraries(${target} PRIVATE
        -Wl,--whole-archive nhk -Wl,--no-whole-archive)
endfunction()
