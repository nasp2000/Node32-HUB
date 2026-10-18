#include "runtime_environment.h"

#include <WiFi.h>

#include "config_manager_runtime.h"
#include "email_runtime_config_state.h"
#include "email_transport.h"
#include "eth_w5500.h"
#include "psram_vfs.h"
#include "sdcard.h"
#include "littlefs_manager.h"
#include "structures.h"

extern volatile StorageMode currentStorageMode;

bool ethernetHasIp() {
#include "feature_flags.h"
#if ENABLE_ETHERNET==1
    IPAddress ip = ethernetLocalIP();
    return (ip[0] | ip[1] | ip[2] | ip[3]);
#else
    return false;
#endif
}

namespace {

bool hasAnyConfiguredEmailRecipient() {
    return !emailDest.isEmpty() || !configManager.getEmaildayExtraEmails().isEmpty();
}

bool isEmailConfiguredForCurrentMethod() {
    String emailMethod = configManager.getEmailMethod();

#if ENABLE_GOOGLE_EMAIL==1
    if (emailMethod == "google") {
        return !configManager.getGoogleScriptUrl().isEmpty() && hasAnyConfiguredEmailRecipient();
    }
#endif

    bool hasValidPort = !smtpPort.isEmpty() && smtpPort != "0";
    return !smtpServer.isEmpty() && hasValidPort &&
           !smtpUser.isEmpty() && !smtpPass.isEmpty() &&
           hasAnyConfiguredEmailRecipient();
}

}  // namespace

String storageGetTodayFolder() {
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        return sdcardGetTodayFolder();
    }
    if (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled()) {
        return psramVFSGetTodayFolder();
    }
    return String("");
}

bool storageIsAvailable() {
    if ((currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) ||
        (currentStorageMode == STORAGE_MODE_PSRAM && psramVFSIsEnabled())) {
        return true;
    }
#if ENABLE_FILESYSTEM==1
    // Flash fallback mirror counts as storage when SD is out (headers + bodies
    // resolve SD -> PSRAM -> Flash per file, so an empty mirror still flows to
    // the senders' "no files" path instead of a hard refusal here).
    if (!sdcardShouldUseSD() && littlefsBegin() && littlefsMounted()) return true;
#endif
    return false;
}

bool hasNetworkConnectivity() {
#if ENABLE_WIFI==1
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        return true;
    }
#endif
    return ethernetHasIp();
}

String getCurrentNetworkIPString() {
#if ENABLE_WIFI==1
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress wifiIp = WiFi.localIP();
        if (wifiIp != IPAddress(0, 0, 0, 0)) {
            return wifiIp.toString();
        }
    }
#endif
#if ENABLE_ETHERNET==1
    IPAddress ethIp = ethernetLocalIP();
    if (ethIp != IPAddress(0, 0, 0, 0)) {
        return ethIp.toString();
    }
#endif
    return String(F("0.0.0.0"));
}

bool isEmailConfiguredAndNetworkConnected() {
    return isEmailConfiguredForCurrentMethod() && hasNetworkConnectivity();
}

bool isEmailConfiguredAndWifiConnected() {
    return isEmailConfiguredForCurrentMethod() && emailTransportCanSendEmail();
}