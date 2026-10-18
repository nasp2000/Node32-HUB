#include "network_mdns.h"
#include "feature_flags.h"

#if ENABLE_MDNS == 1
#include <ESPmDNS.h>
#include <WiFi.h>
#include "ConfigManager.h"

extern ConfigManager configManager;

static bool mdnsStarted = false;
static String mdnsLastHost;

static String mdnsHostname() {
    String h = configManager.getSystemName();
    h.trim();
    if (!h.length()) {
#if defined(PACK_SYSTEM_NAME)
        h = PACK_SYSTEM_NAME;
#else
        h = F("node32hub");
#endif
    }
    h.toLowerCase();
    String out;
    for (unsigned int i = 0; i < h.length(); i++) {
        char c = h[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
        } else if (out.length() && out[out.length() - 1] != '-') {
            out += '-';
        }
    }
    while (out.length() && out[out.length() - 1] == '-') out.remove(out.length() - 1);
    if (!out.length()) out = F("node32hub");
    return out;
}

void ensureMdns() {
    bool enabled = configManager.getMdnsEnabled();
    String host = mdnsHostname();

    if (mdnsStarted) {
        if (!enabled || host != mdnsLastHost) {
            MDNS.end();
            mdnsStarted = false;
        } else {
            return;
        }
    }
    if (!enabled) return;

    bool netUp = (WiFi.status() == WL_CONNECTED) || (WiFi.getMode() & WIFI_MODE_AP);
    if (!netUp) return;

    if (MDNS.begin(host.c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
        mdnsLastHost = host;
    }
}

void stopMdns() {
    if (mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
    }
}

#endif // ENABLE_MDNS == 1
