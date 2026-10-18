#include "nhk/feature.h"

#include "nhk/log.h"

#include <inttypes.h>

namespace nhk {
namespace {

uint32_t g_logged_off = 0u;
uint32_t g_logged_success = 0u;
uint32_t g_logged_failure = 0u;

void LogFeatureOff(const Feature& feature) {
    if (feature.bit == 0u) return;
    const uint32_t previous =
            __atomic_fetch_or(&g_logged_off, feature.bit, __ATOMIC_ACQ_REL);
    if ((previous & feature.bit) != 0u) return;
    NHK_LOGW("feature '%s' skipped: switch off (bit=0x%" PRIx32 ")",
             feature.name, feature.bit);
}

void LogFeatureResult(size_t index, const Feature& feature, bool ok) {
    if (index >= 32u) return;
    const uint32_t bit = 1u << index;
    uint32_t* logged = ok ? &g_logged_success : &g_logged_failure;
    if ((__atomic_fetch_or(logged, bit, __ATOMIC_ACQ_REL) & bit) != 0u) return;
    const char* name = feature.name != nullptr ? feature.name : "<unnamed>";
    if (ok) {
        NHK_LOGW("Inited hook: %s", name);
    } else {
        NHK_LOGE("Failed init hook: %s", name);
    }
}

}  // namespace

bool RunFeatures(const dart::Image& image, const Feature* features,
                 size_t count, FeatureEnabled enabled) {
    if (features == nullptr && count != 0u) return false;
    bool all_ok = true;
    for (size_t index = 0u; index < count; ++index) {
        const Feature& feature = features[index];
        if (feature.install == nullptr || feature.name == nullptr) {
            LogFeatureResult(index, feature, false);
            all_ok = false;
            continue;
        }
        if (feature.bit != 0u &&
                (enabled == nullptr || !enabled(feature.bit))) {
            LogFeatureOff(feature);
            continue;
        }
        const bool installed = feature.install(image);
        LogFeatureResult(index, feature, installed);
        if (!installed) all_ok = false;
    }
    return all_ok;
}

}  // namespace nhk
