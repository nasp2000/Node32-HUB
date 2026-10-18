// grbl_sender_baud.cpp
// Auto baud-rate detection — extracted from grbl_sender.cpp
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <stdarg.h>
#include "direct_log.h"

// ---------------------------------------------------------------------------
// Local logging helper (same pattern as grbl_sender.cpp)
// ---------------------------------------------------------------------------
static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

// ---------------------------------------------------------------------------
// Auto baud-rate detection
// ---------------------------------------------------------------------------
static const uint32_t s_autoDetectBaudRates[] = {
    115200, 57600, 38400, 19200, 9600, 250000, 230400, 74880
};
static const size_t s_autoDetectBaudCount =
    sizeof(s_autoDetectBaudRates) / sizeof(s_autoDetectBaudRates[0]);

#define AUTO_BAUD_SETTLE_MS      300U
#define AUTO_BAUD_WAIT_MS        1200U
#define AUTO_BAUD_POLLS_PER_RATE   5

uint32_t grblAutoDetectBaud(uint8_t slot) {
    if (!ensureRuntimeBuffers()) return 0;
    if (!grblTransportIsRunning()) return 0;

    const uint32_t savedPollInterval = grblGetStatusPollInterval();
    grblSetStatusPollInterval(0);

    s_autoDetectActive  = true;
    s_autoDetectFound   = false;
    s_autoDetectTask    = xTaskGetCurrentTaskHandle();

    vTaskDelay(pdMS_TO_TICKS(500));

    uint32_t detected = 0;

    for (size_t i = 0; i < s_autoDetectBaudCount; i++) {
        const uint32_t rate = s_autoDetectBaudRates[i];

        if (!grblTransportSetBaud(slot, rate)) {
            continue;
        }

        ulTaskNotifyTake(pdTRUE, 0);
        s_autoDetectFound = false;
        s_hasMachineStatus = false;

        vTaskDelay(pdMS_TO_TICKS(AUTO_BAUD_SETTLE_MS));

        for (uint32_t p = 0; p < AUTO_BAUD_POLLS_PER_RATE; p++) {
            grblTransportWriteStr(slot, "?");
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUTO_BAUD_WAIT_MS));

        if (notified > 0 && s_autoDetectFound) {
            detected = rate;
            directLogPrintf("[GRBL] auto-baud: detected %lu baud at slot %u\n",
                            (unsigned long)rate, (unsigned)slot);
            break;
        }
    }

    s_autoDetectActive = false;
    s_autoDetectTask   = nullptr;
    s_autoDetectFound  = false;

    grblSetStatusPollInterval(savedPollInterval);

    if (detected > 0) {
        grblTransportSetBaud(slot, detected);
        vTaskDelay(pdMS_TO_TICKS(200));
        grblTransportWriteStr(slot, "\x18");
        vTaskDelay(pdMS_TO_TICKS(300));
        grblTransportWriteStr(slot, "?");
    }

    return detected;
}

#endif // ENABLE_GRBL == 1
