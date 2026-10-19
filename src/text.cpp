#include "nhk/text.h"

#include <stddef.h>
#include <string.h>

namespace nhk::text {

bool StringsEqual(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    return strcmp(left, right) == 0;
}

bool StartsWith(const char* value, const char* prefix) {
    if (value == nullptr || prefix == nullptr) return false;
    const size_t length = strlen(prefix);
    return strncmp(value, prefix, length) == 0;
}

bool EndsWith(const char* value, const char* suffix) {
    if (value == nullptr || suffix == nullptr) return false;
    const size_t value_length = strlen(value);
    const size_t suffix_length = strlen(suffix);
    if (suffix_length > value_length) return false;
    return strcmp(value + value_length - suffix_length, suffix) == 0;
}

}  // namespace nhk::text
