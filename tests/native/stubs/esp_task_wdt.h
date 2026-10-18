#pragma once
// Host stand-in for esp_task_wdt.h — ConfigManager includes it but never calls
// the watchdog API, so an empty header satisfies the include.
inline void esp_task_wdt_reset() {}