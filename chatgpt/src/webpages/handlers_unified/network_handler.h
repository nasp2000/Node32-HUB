#pragma once
#include "../http_abstraction.h"
#include "../../feature_flags.h"

// WiFi scan
void handleWifiScanGet(HttpRequest& req, HttpResponse& res);
// WiFi intelligent status
void handleWifiIntelligentStatusGet(HttpRequest& req, HttpResponse& res);
// WiFi runtime status (disabled flag + connection)
void handleWifiRuntimeStatusGet(HttpRequest& req, HttpResponse& res);
// WiFi reconnect to best saved network
void handleWifiReconnectPost(HttpRequest& req, HttpResponse& res);
// WiFi switch to specific saved network
void handleWifiSwitchNetworkPost(HttpRequest& req, HttpResponse& res);
// Force AP activation
void handleWifiForceApPost(HttpRequest& req, HttpResponse& res);
// Disable AP (if STA connected)
void handleWifiDisableApPost(HttpRequest& req, HttpResponse& res);
// Disable WiFi (persist flag)
void handleWifiDisablePost(HttpRequest& req, HttpResponse& res);
// Enable WiFi (clear flag)
void handleWifiEnablePost(HttpRequest& req, HttpResponse& res);
// Clear AP lock
void handleClearApLockPost(HttpRequest& req, HttpResponse& res);
// Access Point config GET
void handleAccessPointConfigGet(HttpRequest& req, HttpResponse& res);
// Access Point config POST
void handleAccessPointConfigPost(HttpRequest& req, HttpResponse& res);

// Saved networks GET/POST/DELETE
void handleSavedNetworksGet(HttpRequest& req, HttpResponse& res);
void handleSavedNetworksPost(HttpRequest& req, HttpResponse& res);
void handleSavedNetworksDelete(HttpRequest& req, HttpResponse& res);

// Ethernet status
void handleEthStatusGet(HttpRequest& req, HttpResponse& res);
// Ethernet config POST
void handleEthConfigPost(HttpRequest& req, HttpResponse& res);

// Telnet
void handleTelnetStatusGet(HttpRequest& req, HttpResponse& res);
void handleTelnetTogglePost(HttpRequest& req, HttpResponse& res);
void handleTelnetConfigPost(HttpRequest& req, HttpResponse& res);

// USB Server
void handleUsbServerStatusGet(HttpRequest& req, HttpResponse& res);
void handleUsbServerConfigGet(HttpRequest& req, HttpResponse& res);
void handleUsbServerConfigPost(HttpRequest& req, HttpResponse& res);
void handleUsbServerDevicesGet(HttpRequest& req, HttpResponse& res);
