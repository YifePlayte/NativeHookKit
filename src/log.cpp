#include "nhk/log.h"

namespace nhk::log {
namespace {

const char* g_tag = "NativeHookKit";

}  // namespace

void SetTag(const char* tag) {
    if (tag != nullptr && tag[0] != '\0') g_tag = tag;
}

const char* Tag() { return g_tag; }

}  // namespace nhk::log
