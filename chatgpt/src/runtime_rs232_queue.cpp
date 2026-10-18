// runtime_rs232_queue.cpp — RS232 send queue: mutex primitives + enqueueRS232Command.
// Extracted from chatgpt.ino (Phase 5 modularization).
// The variables (rs232SendQueue, rs232SendQueueMutex, …) remain in chatgpt.ino;
// this module accesses them through the extern declarations in runtime_rs232_queue.h.
#include "runtime_rs232_queue.h"

#include "feature_flags.h"
#include "psram_helpers.h"
#if ENABLE_RS232==1

#include <ArduinoJson.h>
#include "commands.h"               // INFO, DEBUG, COMMAND_RESPONSE log levels
#include "config_manager_runtime.h" // extern ConfigManager configManager
#include "screen_hooks.h"          // triggerScreenCommandSent
#include "feature_flags.h"
#include "psram_helpers.h"

// sendVirtualMessage declared in commands.h (included above)

// ---------------------------------------------------------------------------
// Queue mutex primitives (moved from chatgpt.ino static inline helpers).
// ---------------------------------------------------------------------------

bool rs232QueueLock(TickType_t timeoutTicks) {
    if (!rs232SendQueueMutex) return true;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (rs232SendQueueMutexOwnerTask == self) {
        // Re-entrant access by the same task.
        rs232SendQueueMutexDepth++;
        return true;
    }
    if (xSemaphoreTakeRecursive(rs232SendQueueMutex, timeoutTicks) != pdTRUE) {
        rs232SendQueueMutexFailCount++;
        return false;
    }
    rs232SendQueueMutexOwnerTask = self;
    rs232SendQueueMutexDepth = 1;
    return true;
}

void rs232QueueUnlock() {
    if (!rs232SendQueueMutex) return;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (rs232SendQueueMutexOwnerTask != self || rs232SendQueueMutexDepth == 0) {
        rs232SendQueueMutexBadUnlockCount++;
        return;
    }
    rs232SendQueueMutexDepth--;
    if (rs232SendQueueMutexDepth == 0) {
        rs232SendQueueMutexOwnerTask = NULL;
        xSemaphoreGiveRecursive(rs232SendQueueMutex);
    }
}

bool rs232QueueHasPending(unsigned *pendingCountOut) {
    if (!rs232QueueLock(pdMS_TO_TICKS(5))) {
        // Conservative: treat as pending to avoid incorrect state decisions.
        if (pendingCountOut) *pendingCountOut = 0;
        return true;
    }
    bool has = !rs232SendQueue.isEmpty();
    if (pendingCountOut) *pendingCountOut = (unsigned)rs232SendQueue.size();
    rs232QueueUnlock();
    return has;
}

bool rs232QueueTryPop(String &out) {
    if (!rs232QueueLock(pdMS_TO_TICKS(20))) return false;
    if (rs232SendQueue.isEmpty()) {
        rs232QueueUnlock();
        return false;
    }
    out = rs232SendQueue.first();
    rs232SendQueue.shift();
    rs232QueueUnlock();
    return true;
}

bool rs232QueueTryPush(const String &payload) {
    if (!rs232QueueLock(pdMS_TO_TICKS(50))) return false;
    if (rs232SendQueue.isFull()) {
        rs232QueueUnlock();
        return false;
    }
    rs232SendQueue.push(payload);
    rs232QueueUnlock();
    return true;
}

// ---------------------------------------------------------------------------
// enqueueRS232Command — moved from chatgpt.ino.
// Enqueues a command for later RS232 transmission, optionally with a template.
// scheduledId allows the scheduler to identify/dequeue specific entries.
// ---------------------------------------------------------------------------
bool enqueueRS232Command(const String &command,
                         const String &templateName,
                         const String &scheduledId,
                         const String &type) {
    // Fast pre-check before building payloads (avoid wasted work if the queue is full).
    {
        if (!rs232QueueLock(pdMS_TO_TICKS(20))) return false;
        bool full = rs232SendQueue.isFull();
        rs232QueueUnlock();
        if (full) return false;
    }

    // If a template name is provided, resolve its fields now and enqueue a JSON payload.
    if (templateName.length()) {
        JsonArray arr = configManager.getRs232Templates();
        for (size_t i = 0; i < arr.size(); ++i) {
            JsonObject o = arr[i];
            const char* oName = o["name"].as<const char*>();
            if (oName && strcmp(oName, templateName.c_str()) == 0) {
                // Build a wrapper JSON with template fields embedded to avoid lookup at send-time.
                SpiRamJsonDocument wrapper(512);
                JsonObject tpl = wrapper.createNestedObject("template");
                tpl["name"] = templateName;
                if (o.containsKey("baudrate_index")) tpl["baudrate_index"] = o["baudrate_index"].as<int>();
                if (o.containsKey("baudrate"))        tpl["baudrate"]       = o["baudrate"].as<int>();
                if (o.containsKey("data_bits"))       tpl["data_bits"]      = o["data_bits"].as<int>();
                if (o.containsKey("parity"))          tpl["parity"]         = o["parity"].as<const char*>();
                if (o.containsKey("stop_bits"))       tpl["stop_bits"]      = o["stop_bits"].as<int>();
                if (o.containsKey("invert_pins"))     tpl["invert_pins"]    = o["invert_pins"].as<bool>();
                // flow_control intentionally ignored (RTS/CTS not supported).
                if (o.containsKey("read_timeout_ms")) tpl["read_timeout_ms"] = o["read_timeout_ms"].as<int>();
                if (o.containsKey("use_xon_xoff"))    tpl["use_xon_xoff"]   = o["use_xon_xoff"].as<bool>();
                wrapper["command"] = command;
                wrapper["type"]    = type;
                if (scheduledId.length()) wrapper["scheduled_id"] = scheduledId;

                // Serialize to stack buffer to avoid intermediate String allocation.
                size_t jsonLen = measureJson(wrapper) + 1;
                char* jsonBuf = (char*)malloc(jsonLen);
                if (jsonBuf) {
                    serializeJson(wrapper, jsonBuf, jsonLen);
                    if (!rs232QueueTryPush(String(jsonBuf))) { free(jsonBuf); return false; }
                    free(jsonBuf);
                } else {
                    String s;
                    s.reserve(jsonLen);
                    serializeJson(wrapper, s);
                    if (!rs232QueueTryPush(s)) return false;
                }

                // Log a compact enqueue message for diagnostics (snprintf avoids String concat).
                char logBuf[192];
                if (o.containsKey("baudrate"))
                    snprintf(logBuf, sizeof(logBuf), "RS232 queued template: %s baud=%d",
                             templateName.c_str(), o["baudrate"].as<int>());
                else if (o.containsKey("baudrate_index"))
                    snprintf(logBuf, sizeof(logBuf), "RS232 queued template: %s idx=%d",
                             templateName.c_str(), o["baudrate_index"].as<int>());
                else
                    snprintf(logBuf, sizeof(logBuf), "RS232 queued template: %s",
                             templateName.c_str());
                sendVirtualMessage(String(logBuf), DEBUG);

                // Rate-limited UI notification (max once every 3 minutes).
                static unsigned long lastEnqueueLogMs = 0;
                unsigned long now = millis();
                if (now - lastEnqueueLogMs > 180000UL) {
                    char buf[192];
                    snprintf(buf, sizeof(buf), "RS232 enfileirado: %s", command.c_str());
                    sendVirtualMessage(String(buf), COMMAND_RESPONSE);
                    lastEnqueueLogMs = now;
                }

                triggerScreenCommandSent(command, "RS232");

                // Manual mode: update active template in RAM only (no flash write).
                bool isScheduled = (scheduledId.startsWith(F("CMD_")) ||
                                    scheduledId.startsWith(F("DT_")));
                if (!isScheduled && templateName.length()) {
                    configManager.setRs232CurrentTemplateVolatile(templateName);
                    char buf[192];
                    snprintf(buf, sizeof(buf), "Template active (volatile): %s", templateName.c_str());
                    sendVirtualMessage(String(buf), DEBUG);
                } else if (isScheduled) {
                    char buf[192];
                    snprintf(buf, sizeof(buf), "Execucao agendada mantem template atual: %s", templateName.c_str());
                    sendVirtualMessage(String(buf), DEBUG);
                }
                return true;
            }
        }

        // Template name provided but not found: fall back to legacy format.
        String qpayload;
        if (scheduledId.length()) {
            SpiRamJsonDocument legacyWrapper(256);
            legacyWrapper["command"] = command;
            legacyWrapper["template"] = templateName;
            legacyWrapper["type"] = type;
            legacyWrapper["scheduled_id"] = scheduledId;
            qpayload.reserve(measureJson(legacyWrapper) + 1);
            serializeJson(legacyWrapper, qpayload);
            if (!rs232QueueTryPush(qpayload)) return false;
        } else {
            qpayload = templateName + String("::") + command;
            if (!rs232QueueTryPush(qpayload)) return false;
        }

        // Rate-limited UI notification for legacy format.
        static unsigned long lastLegacyEnqueueLogMs = 0;
        unsigned long now2 = millis();
        if (now2 - lastLegacyEnqueueLogMs > 180000UL) {
            char buf[256];
            snprintf(buf, sizeof(buf), "RS232 enfileirado: %s", qpayload.c_str());
            sendVirtualMessage(String(buf), COMMAND_RESPONSE);
            lastLegacyEnqueueLogMs = now2;
        }
        triggerScreenCommandSent(command, "RS232");
        return true;
    }

    // No template: simple command (plain string or JSON wrapper with scheduled_id).
    if (scheduledId.length()) {
        SpiRamJsonDocument plainWrapper(256);
        plainWrapper["command"] = command;
        plainWrapper["type"]    = type;
        plainWrapper["scheduled_id"] = scheduledId;
        String s;
        s.reserve(measureJson(plainWrapper) + 1);
        serializeJson(plainWrapper, s);
        if (!rs232QueueTryPush(s)) return false;
    } else {
        if (!rs232QueueTryPush(command)) return false;
    }

    // Rate-limited UI notification for plain commands.
    static unsigned long lastPlainEnqueueLogMs = 0;
    unsigned long now3 = millis();
    if (now3 - lastPlainEnqueueLogMs > 180000UL) {
        char buf[192];
        snprintf(buf, sizeof(buf), "RS232 enfileirado: %s", command.c_str());
        sendVirtualMessage(String(buf), COMMAND_RESPONSE);
        lastPlainEnqueueLogMs = now3;
    }
    triggerScreenCommandSent(command, "RS232");
    return true;
}

#endif // ENABLE_RS232==1
