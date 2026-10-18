//
// NativeHookKit: shared logging for LSPosed native payloads.
//
// XYRE native modules must log at WARN or ERROR: XYRE app processes filter
// INFO/DEBUG.  The shared entry sets the tag from ModuleDescriptor::log_tag;
// SetTag remains available for standalone tests.
//

#pragma once

#include <android/log.h>

namespace nhk::log {

void SetTag(const char* tag);
const char* Tag();

}  // namespace nhk::log

#define NHK_LOGW(...) \
    __android_log_print(ANDROID_LOG_WARN, ::nhk::log::Tag(), __VA_ARGS__)
#define NHK_LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, ::nhk::log::Tag(), __VA_ARGS__)
