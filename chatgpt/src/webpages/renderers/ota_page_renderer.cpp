#include "ota_page_renderer.h"

#include "../assets/page_ota_asset.h"
#include "../../feature_flags.h" // PACK_SYSTEM_NAME (per-pack, compile-time)

extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;
extern const char* FIRMWARE_BUILD_DATETIME;

size_t streamOtaPageToPrint(Print& out, OtaPageTransportMode mode) {
    size_t n = 0;

    out.print(FPSTR(PAGE_OTA_HEAD_PREFIX));
    n += strlen_P(PAGE_OTA_HEAD_PREFIX);
    out.print(FIRMWARE_VERSION);
    n += strlen(FIRMWARE_VERSION);
    out.print(F(" ("));
    n += 2;
    out.print(PACK_SYSTEM_NAME);
    n += strlen(PACK_SYSTEM_NAME);
    out.print(F(")"));
    n += 1;
    out.print(FPSTR(PAGE_OTA_HEAD_MIDDLE));
    n += strlen_P(PAGE_OTA_HEAD_MIDDLE);
    out.print(FIRMWARE_BUILD_DATETIME);
    n += strlen(FIRMWARE_BUILD_DATETIME);
    out.print(FPSTR(PAGE_OTA_HEAD_SUFFIX));
    n += strlen_P(PAGE_OTA_HEAD_SUFFIX);

    if (mode == OtaPageTransportMode::EthernetBootstrap) {
        out.print(FPSTR(PAGE_OTA_SCRIPT_ETHERNET));
        n += strlen_P(PAGE_OTA_SCRIPT_ETHERNET);
    } else {
        out.print(FPSTR(PAGE_OTA_SCRIPT_WIFI));
        n += strlen_P(PAGE_OTA_SCRIPT_WIFI);
    }

    out.print(FPSTR(PAGE_OTA_FOOTER_VERSION_PREFIX));
    n += strlen_P(PAGE_OTA_FOOTER_VERSION_PREFIX);
    out.print(FIRMWARE_VERSION);
    n += strlen(FIRMWARE_VERSION);
    out.print(FPSTR(PAGE_OTA_FOOTER_BUILD_PREFIX));
    n += strlen_P(PAGE_OTA_FOOTER_BUILD_PREFIX);
    out.print(FIRMWARE_BUILD);
    n += strlen(FIRMWARE_BUILD);
    out.print(FPSTR(PAGE_OTA_FOOTER_SUFFIX));
    n += strlen_P(PAGE_OTA_FOOTER_SUFFIX);

    return n;
}
