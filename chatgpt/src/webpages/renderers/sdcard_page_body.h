// sdcard_page_body.h — body of /sdcard in render order.
//
// Kept in a header (no AsyncWebServer / ConfigManager) so a native test can
// render it and assert the shipped markup:
//   PAGE_SDCARD            settings sections (status, save-data, auto cleanup)
//   PAGE_SDCARD_PART2      Save button + message + closing tags
//
// The EMAILDAY/LOGDAY/SENSORDAY help card used to sit between the two. It was
// removed on request (2026-11-28): those commands are about e-mail recipients,
// so their examples live on /recipients only.
//
// SD-3: the commands card was defined in the asset and never printed. SD-2: the
// duplicate initSdcardPage() that lived in PART2 was dead (auto.js, loaded
// later, defines the same global and the renderer calls it after the footer) and
// referenced helpers that do not exist in that scope (bindClick/apiGet/apiPost);
// it is gone, so PART2 is markup only.
#pragma once

#include <Arduino.h>

#include "../assets/page_sdcard_asset.h"

inline size_t streamSdcardPageBody(Print& out) {
    size_t n = 0;
    out.print(FPSTR(PAGE_SDCARD));
    n += strlen_P(PAGE_SDCARD);
    out.print(FPSTR(PAGE_SDCARD_PART2));
    n += strlen_P(PAGE_SDCARD_PART2);
    return n;
}
