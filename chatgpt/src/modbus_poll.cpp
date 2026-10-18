// modbus_poll.cpp
// FreeRTOS polling task for Modbus RTU master.
// Iterates over all enabled devices, reads each register on schedule,
// retries on error, updates in-memory state, and optionally logs to SD.

#include "modbus_poll.h"

#include "feature_flags.h"
#if ENABLE_MODBUS==1

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "feature_flags.h"
#include "modbus_tcp.h" // TCP client for isTcp devices
#include "modbus_runtime.h" // g_modbusMutex
#include "log.h"        // logEvent()

// Task configuration
#define MB_POLL_TASK_STACK  6144
#define MB_POLL_TASK_PRIO   2
#define MB_POLL_TASK_CORE   0   // Core 0: same as SD and email background tasks

// State
static TaskHandle_t      _taskHandle     = nullptr;
static ModbusDevice*     _devices        = nullptr;
static uint8_t           _deviceCount    = 0;
static volatile bool     _running        = false;
static volatile uint8_t  _immediateReq   = 0xFF; // 0xFF = no pending request
static ModbusSdWriteFn   _sdWriteFn      = nullptr;
static ModbusRxInjectFn  _rxInjectFn     = nullptr; // Inject into web RX ring buffer (bufferDados)

// ---- Internal: log one register value to SD pipeline ----------------

static void _logToSd(const ModbusDevice& dev, const ModbusRegister& reg) {
    if (!_sdWriteFn) return;

    // Build sanitized CSV key: "mb_<slaveId>_<label>", lowercase, spaces→'_'
    char key[52];
    snprintf(key, sizeof(key), "mb_%u_%s", (unsigned)dev.slaveId, reg.label);
    for (char* p = key; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') { *p += 32; }
        else if (*p == ' ')         { *p = '_'; }
        else if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '_') { *p = '_'; }
    }

    char line[192];
    if (reg.logPretty) {
        // Human-readable with [MODBUS] prefix for easy SD file filtering:
        // "[MODBUS] DEV:<name> | FC<fc> | REG:<addr> | <label> | <value> <unit> | OK"
        snprintf(line, sizeof(line), "[MODBUS] DEV:%s | FC%02X | REG:%04X | %s | %.4g %s | OK",
                 dev.name, (unsigned)reg.fc, (unsigned)reg.address,
                 reg.label, reg.lastValue, reg.unit[0] ? reg.unit : "-");
    } else {
        // Minimal CSV — compatible with recipient rule engine: "mb_<id>_<label>, <value>"
        snprintf(line, sizeof(line), "%s, %.4g", key, reg.lastValue);
    }
    // Use source "MODBUS" so data is written to /{sdcardName}/MODBUS_{date}/ folder.
    _sdWriteFn("MODBUS", line, strlen(line), (unsigned long)millis());
}

// ---- Internal: poll all registers of one device --------------------

static void _pollDevice(ModbusDevice& dev) {
    if (!dev.enabled || dev.registerCount == 0) return;

    for (uint8_t ri = 0; ri < dev.registerCount; ri++) {
        ModbusRegister& reg = dev.registers[ri];

        // 32-bit types require reading two consecutive registers
        uint8_t rawCount = 1;
        if (reg.dataType == MB_TYPE_UINT32 ||
            reg.dataType == MB_TYPE_INT32  ||
            reg.dataType == MB_TYPE_FLOAT32) {
            rawCount = 2;
        }

        uint16_t rawBuf[2] = { 0, 0 };
        bool ok = false;

        for (uint8_t attempt = 0; attempt <= dev.retries; attempt++) {
            if (dev.isTcp) {
                ok = modbusTcpReadRegs(dev, reg.fc,
                                       reg.address, rawCount,
                                       dev.timeoutMs, rawBuf);
            } else {
                ok = modbusRtuReadRegs(dev.slaveId, reg.fc,
                                       reg.address, rawCount,
                                       dev.timeoutMs, rawBuf);
            }
            if (ok) break;
        }

        reg.lastOk   = ok;
        reg.lastReadMs = (uint32_t)millis();

        if (ok) {
            dev.pollCount++;
            reg.lastValue = modbusDecodeValue(reg, rawBuf);
            if (reg.logToSd) _logToSd(dev, reg);
        } else {
            dev.errorCount++;
        }
    }
    dev.lastPollMs = (uint32_t)millis();

    // Aggregate all successfully read registers into a single monitor line:
    // "[DEV_NAME] Label: value unit | Label2: value2 unit2"
    // This mirrors the sensor injection pattern instead of injecting one line per register.
    if (_rxInjectFn) {
        char agg[256];
        int pos = snprintf(agg, sizeof(agg), "[%s]", dev.name);
        bool first = true;
        for (uint8_t ri = 0; ri < dev.registerCount && pos < (int)sizeof(agg) - 1; ri++) {
            const ModbusRegister& reg = dev.registers[ri];
            if (!reg.logToSd || !reg.lastOk) continue;
            int w = snprintf(agg + pos, sizeof(agg) - pos,
                             "%s %s: %.4g%s%s",
                             first ? "" : " |",
                             reg.label, reg.lastValue,
                             reg.unit[0] ? " " : "", reg.unit);
            if (w > 0) pos += w;
            first = false;
        }
        if (!first) { // Only inject if at least one register was included
            _rxInjectFn(agg, (size_t)pos);
        }
    }
}

// ---- FreeRTOS task function ----------------------------------------

static void _pollTask(void* /*param*/) {
    while (_running) {
        uint32_t now = (uint32_t)millis();

        // Service an immediate poll request (from web handler or command)
        uint8_t imm = _immediateReq;
        if (imm < _deviceCount) {
            _immediateReq = 0xFF;
            _pollDevice(_devices[imm]);
        }

        // Regular schedule: poll devices whose interval has elapsed
        // Hold the mutex while iterating so a concurrent config reload
        // (modbusLoadDevicesFromConfig) does not zero the array mid-loop.
        if (xSemaphoreTake(g_modbusMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (uint8_t i = 0; i < _deviceCount; i++) {
                ModbusDevice& dev = _devices[i];
                if (!dev.enabled) continue;
                // Skip TCP devices with no host configured
                if (dev.isTcp && !dev.tcpHost[0]) continue;
                // Skip RTU devices if UART1 is not ready
                if (!dev.isTcp && !modbusRtuIsReady()) continue;
                if ((now - dev.lastPollMs) >= dev.pollIntervalMs) {
                    _pollDevice(dev);
                }
            }
            xSemaphoreGive(g_modbusMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // 50 ms granularity; low-overhead between polls
    }
    vTaskDelete(nullptr); // Self-delete when stopped
}

// ---- Public API ----------------------------------------------------

void modbusPollSetCallbacks(ModbusSdWriteFn sdWrite, ModbusRxInjectFn rxInject) {
    _sdWriteFn   = sdWrite;
    _rxInjectFn  = rxInject;
}

void modbusPollBegin(ModbusDevice* devices, uint8_t count) {
    if (_running) return;
    // Allow starting even when RTU is not ready — TCP-only setups don't need UART1.
    _devices     = devices;
    _deviceCount = count;
    _running     = true;
    xTaskCreatePinnedToCore(_pollTask, "modbus_poll",
                             MB_POLL_TASK_STACK, nullptr,
                             MB_POLL_TASK_PRIO, &_taskHandle,
                             MB_POLL_TASK_CORE);
    logEvent("MODBUS", "poll task started");
}

void modbusPollStop() {
    _running = false;
    // Wait up to 300 ms for the task to exit naturally via _running=false check
    for (int i = 0; i < 6 && _taskHandle != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    _taskHandle = nullptr;
    logEvent("MODBUS", "poll task stopped");
}

bool modbusPollIsRunning() { return _running; }

void modbusPollRequestImmediate(uint8_t deviceIndex) {
    if (deviceIndex < _deviceCount) _immediateReq = deviceIndex;
}

#endif // ENABLE_MODBUS==1
