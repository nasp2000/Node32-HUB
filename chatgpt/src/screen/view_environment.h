#pragma once

// view_environment.h - Environment pack status layout for 2.42" displays.
// Moved verbatim from ScreenManager::showStatusScreen; only local line
// variables were rewritten to the shared ScreenStatusContext.

#include "feature_flags.h"

#if ENABLE_LCD_SCREEN==1
#include "screen.h"
#include "screen/view_context.h"
#include "structures.h"
#include "sdcard.h"
#include "psram_vfs.h"
#include "littlefs_manager.h"
#if ENABLE_AHT20_BMP280==1
#include "sensors/sensor_config.h"
#endif
#if ENABLE_MQ135==1
#include "sensors/sensor_mq135.h"
#endif

extern volatile StorageMode currentStorageMode;

// Same compile exposure as the original showStatusScreen branch.
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_ENVIRONMENT)

static inline void screenDrawEnvironmentView(ScreenManager &mgr, Adafruit_SSD1306 &oled,
                                             const ScreenStatusContext &ctx) {
    // Dedicated 2.42" profile for Environment pack.
    const int rowWidthChars = 21;
    String env1 = "Temp: N/A";
    String env2 = "Hum : N/A";
    String gas = "MQ135: N/A";

#if ENABLE_AHT20_BMP280==1
    {
        const SensorConfig &cfg = sensorConfigManager.getConfig();
        if (cfg.last_reading_time > 0) {
            char tbuf[22];
            char hbuf[22];
            snprintf(tbuf, sizeof(tbuf), "Temp: %.1fC", cfg.last_temp_avg);
            snprintf(hbuf, sizeof(hbuf), "Hum : %.0f%% P:%.0f", cfg.last_humidity, cfg.last_pressure_hpa);
            env1 = String(tbuf);
            env2 = String(hbuf);
        }
    }
#endif

#if ENABLE_MQ135==1
    {
        if (mq135Sensor.isInitialized()) {
            MQ135Reading mr = mq135Sensor.getLastReading();
            if (mr.valid) {
                char gbuf[22];
                snprintf(gbuf, sizeof(gbuf), "MQ135: %.0fppm", mr.ppm_co2_corrected);
                gas = String(gbuf);
            }
        }
    }
#endif

    String storageCompact = "ST:N/A";
    if (currentStorageMode == STORAGE_MODE_SD && sdcardIsMounted()) {
        uint64_t totalBytes = sdcardTotalBytes();
        uint64_t usedBytes = sdcardUsedBytes();
        uint64_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
        uint32_t freeMB = (uint32_t)(freeBytes / (1024ULL * 1024ULL));
        storageCompact = String("SD:") + String(freeMB) + "M";
    } else {
        uint64_t freeBytes = littlefsFreeBytes();
        uint64_t totalBytesFS = littlefsTotalBytes();
        if (totalBytesFS > 0) {
            storageCompact = String("FS:") + String((uint32_t)(freeBytes / 1024U)) + "K";
        }
    }

    String heapLine = String("Heap:") + String(ESP.getFreeHeap() / 1024U) + "K " + storageCompact;

    oled.setCursor(0, 0);
    oled.print(mgr.stripAccents(String(PACK_SYSTEM_NAME)).substring(0, rowWidthChars));
    oled.setCursor(0, 9);
    oled.print(mgr.stripAccents(String("IP:") + ctx.ipStr).substring(0, rowWidthChars));
    oled.setCursor(0, 20);
    oled.print(mgr.stripAccents(env1).substring(0, rowWidthChars));
    oled.setCursor(0, 29);
    oled.print(mgr.stripAccents(env2).substring(0, rowWidthChars));
    oled.setCursor(0, 40);
    oled.print(mgr.stripAccents(gas).substring(0, rowWidthChars));
    oled.setCursor(0, 49);
    oled.print(mgr.stripAccents(heapLine).substring(0, rowWidthChars));
    oled.display();}

#endif // SELECTED_PACK == PACK_ENVIRONMENT

#endif // ENABLE_LCD_SCREEN
