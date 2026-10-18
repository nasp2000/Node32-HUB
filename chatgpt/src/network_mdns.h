#pragma once

// mDNS / Bonjour advertisement — lets users open the web UI by name instead of
// hunting for the IP (http://harmonica-box.local, ...). Works on macOS, iOS,
// Windows (with Bonjour), Linux (avahi). Called from the main loop; starts once
// as soon as WiFi (STA or AP) is up. Enabled by default on all packs; the
// System Settings page checkbox (mdns_enabled) toggles it. The hostname follows
// the configured System Name, so renaming the device re-advertises it.
//
// Implemented in network_mdns.cpp (kept out of this header to avoid a circular
// include through ConfigManager.h).

#include "feature_flags.h"

#if ENABLE_MDNS == 1
void ensureMdns();
void stopMdns();
#else
inline void ensureMdns() {}
inline void stopMdns() {}
#endif
