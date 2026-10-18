#include "file_header_builder.h"

#include <WiFi.h>

#include "config_manager_runtime.h"

extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD_DATETIME;

String buildFileHeader(const String& source) {
    if (source.equalsIgnoreCase("LPT") || source.equalsIgnoreCase("DB25")) {
        return "";
    }

    String ipAddress = "N/A";
    if (WiFi.status() == WL_CONNECTED) {
        ipAddress = WiFi.localIP().toString();
    }

    String header = "--- ";
    String sysName = configManager.getSystemName();

    if (sysName != "Node32-HUB") {
        header += sysName;
        header += " (Node32-HUB ";
        header += FIRMWARE_VERSION;
        header += ")";
    } else {
        header += sysName;
        header += " ";
        header += FIRMWARE_VERSION;
    }

    header += " - ";
    header += FIRMWARE_BUILD_DATETIME;
    header += " - IP: ";
    header += ipAddress;
    header += " ---\n\n";

    return header;
}

String getSystemNameForFilename() {
    String systemName = configManager.getSystemName();

    if (systemName.isEmpty()) {
        systemName = "Node32-HUB";
    }

    systemName.replace(' ', '_');
    if (systemName.length() > 20) {
        systemName = systemName.substring(0, 20);
    }

    return systemName;
}