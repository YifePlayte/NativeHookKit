//
// NativeHookKit: Dart AOT image discovery and code patching helpers.
//
// XYRE apps are Dart AOT processes: their business logic lives in the mapped
// Dart payload (for example libapp.so, straight out of the APK, no dex).
// Feature hooks locate instruction patterns in executable segments and patch
// them.  This component owns only the generic plumbing.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nhk::dart {

struct Segment {
    uintptr_t start;
    size_t size;
    uint32_t flags;
};

struct Image {
    uintptr_t bias;
    Segment segments[8];
    size_t segment_count;
    bool found;
};

bool AcquireImage(const char* library_name, Image* image);
bool AcquireImageAtBase(uintptr_t base, Image* image);

const Segment* FindSegment(const Image& image, uintptr_t address,
                           uint32_t required_flags);
bool RangeInExecutableSegment(const Image& image, uintptr_t address,
                              size_t size);
bool FindUniqueWordPattern(const Image& image, const uint32_t* pattern,
                           size_t count, uintptr_t* match,
                           uint32_t* match_count);

struct PatternWord {
    uint32_t value;
    uint32_t mask;
};

bool FindUniqueMaskedPattern(const Image& image, const PatternWord* pattern,
                             size_t count, uintptr_t* match,
                             uint32_t* match_count);

bool PatchCode(uintptr_t address, const uint32_t* words, size_t count);

}  // namespace nhk::dart
