#pragma once

#include <pgmspace.h>

// Mobile "System" screen shared by every pack: controller summary (firmware,
// uptime, heap, PSRAM, SD, WiFi), firmware update (OTA), the Web Serial Monitor
// log and a guarded restart. Drop MOBILE_SYSTEM_HTML inside the page container
// and include MOBILE_COMMON_JS + MOBILE_SYSTEM_JS after it.
//
// The host page calls MPSystem.open() when the screen becomes visible and
// MPSystem.close() when it leaves, and can veto a restart with
// MPSystem.setBlocker(function () { return true; }).
extern const char MOBILE_SYSTEM_HTML[];
extern const char MOBILE_SYSTEM_JS[];
