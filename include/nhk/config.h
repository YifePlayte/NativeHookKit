//
// NativeHookKit: native switch transport for XYRE processes.
//
// XYRE processes have no ART runtime, so they cannot read the module's
// RemotePreferences.  A module's system_server endpoint publishes the switch
// snapshot over the clipboard service using a private transaction; this
// component queries that endpoint and keeps the authoritative answer.
//

#pragma once

#include <stdint.h>

namespace nhk::config {

struct Descriptor {
    uint32_t transaction_code;
    int32_t ack;
    uint32_t known_features;
};

// Must be called once before EnsureReady()/Enabled().
void Configure(const Descriptor* descriptor);

bool Refresh();
bool EnsureReady();
bool Enabled(uint32_t feature);
bool Ready();
uint32_t Flags();

}  // namespace nhk::config
