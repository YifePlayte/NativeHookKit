//
// NativeHookKit: small string helpers shared by every consumer.
//
// Paths, library names and process names are compared by prefix/suffix in all
// modules; keeping one implementation avoids divergent edge-case handling.
//

#pragma once

namespace nhk::text {

bool StringsEqual(const char* left, const char* right);
bool StartsWith(const char* value, const char* prefix);
bool EndsWith(const char* value, const char* suffix);

}  // namespace nhk::text
