#include "coordinator_hooks.h"

#include <nvs_flash.h>
#include "littlefs_manager.h"
#include "email_runtime_config_state.h"
#include "network_runtime_state.h"
#include "network_saved_profiles.h"

void performFactoryResetInternalCleanup() {
    // Erase and reinitialise the entire NVS partition so all stored config is wiped.
    nvs_flash_erase();
    nvs_flash_init();

    // Format LittleFS to remove any persisted files (logs, snapshots, etc.).
    littlefsFormat();

    try {
        savedNetworks.clear();
        saveSavedNetworks();
    } catch (...) {
    }

    ssid = "";
    password = "";
    smtpUser = "";
    smtpPass = "";
    emailDest = "";
    forceAPMode = true;
}