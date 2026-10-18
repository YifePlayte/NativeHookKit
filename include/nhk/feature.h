//
// NativeHookKit: feature dispatch for Dart AOT hooks.
//
// A module supplies a feature table.  The runner applies the module's feature
// bit gate and emits one Inited/Failed entry log per feature and process.  It
// logs disabled features once; installers must remain idempotent because the
// runner does not deduplicate successful installs.
//

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "nhk/dart_image.h"

namespace nhk {

struct Feature {
    const char* name;
    uint32_t bit;
    bool (*install)(const dart::Image& image);
};

using FeatureEnabled = bool (*)(uint32_t bit);

// Installs every enabled feature.  A zero-bit feature is always attempted.
// Returns true only when every attempted installer succeeded.
bool RunFeatures(const dart::Image& image, const Feature* features,
                 size_t count, FeatureEnabled enabled);

}  // namespace nhk
