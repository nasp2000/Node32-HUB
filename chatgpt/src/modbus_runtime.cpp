// modbus_runtime.cpp
// Definitions for global Modbus device/register table and JSON load/save helpers.
// JSON keys use short names (addr, fc, type, order…) to reduce NVS footprint.

#include "modbus_runtime.h"

#include "feature_flags.h"
#if ENABLE_MODBUS==1

#include "config_manager_runtime.h"
#include "feature_flags.h"

ModbusDevice     g_modbusDevices[MODBUS_MAX_DEVICES] = {};
uint8_t          g_modbusDeviceCount = 0;
SemaphoreHandle_t g_modbusMutex       = xSemaphoreCreateMutex();

uint8_t modbusLoadDevicesFromConfig() {
    // Hold the mutex for the entire reload so the poll/slave tasks
    // never see a partially-zeroed device table.
    xSemaphoreTake(g_modbusMutex, portMAX_DELAY);
    memset(g_modbusDevices, 0, sizeof(g_modbusDevices));
    g_modbusDeviceCount = 0;

    JsonVariant mbSection = configManager.getSection("modbus");
    if (!mbSection.is<JsonObject>()) return 0;
    JsonObject mbObj = mbSection.as<JsonObject>();

    if (!mbObj.containsKey("devices") || !mbObj["devices"].is<JsonArray>()) return 0;
    JsonArray devArr = mbObj["devices"].as<JsonArray>();

    for (JsonObject dev : devArr) {
        if (g_modbusDeviceCount >= MODBUS_MAX_DEVICES) break;

        ModbusDevice& d = g_modbusDevices[g_modbusDeviceCount];
        d.slaveId        = dev["slaveId"]   | (uint8_t)1;
        strlcpy(d.name,    dev["name"]      | "Device", sizeof(d.name));
        d.enabled        = dev["enabled"]   | false;
        d.isTcp          = dev["isTcp"]     | false;
        strlcpy(d.tcpHost, dev["tcpHost"]   | "", sizeof(d.tcpHost));
        d.tcpPort        = dev["tcpPort"]   | (uint16_t)502;
        d.pollIntervalMs = dev["pollMs"]    | (uint32_t)5000;
        d.retries        = dev["retries"]   | (uint8_t)3;
        d.timeoutMs      = dev["timeoutMs"] | (uint16_t)500;
        d.registerCount  = 0;
        d.pollCount      = 0;
        d.errorCount     = 0;
        d.lastPollMs     = 0;

        if (dev.containsKey("regs") && dev["regs"].is<JsonArray>()) {
            JsonArray regs = dev["regs"].as<JsonArray>();
            for (JsonObject reg : regs) {
                if (d.registerCount >= MODBUS_MAX_REGISTERS) break;
                ModbusRegister& r = d.registers[d.registerCount];
                r.address   = reg["addr"]   | (uint16_t)0;
                r.fc        = (ModbusFunctionCode)(reg["fc"]    | (uint8_t)3);
                r.dataType  = (ModbusDataType)   (reg["type"]  | (uint8_t)0);
                r.byteOrder = (ModbusByteOrder)  (reg["order"] | (uint8_t)0);
                r.bitIndex  = reg["bit"]    | (uint8_t)0;
                strlcpy(r.label, reg["label"] | "Reg", sizeof(r.label));
                strlcpy(r.unit,  reg["unit"]  | "",    sizeof(r.unit));
                r.scale     = reg["scale"]  | 1.0f;
                r.offset    = reg["offset"] | 0.0f;
                r.logToSd   = reg["log"]    | false;
                r.logPretty = reg["pretty"] | true;
                r.lastValue  = 0.0f;
                r.lastReadMs = 0;
                r.lastOk     = false;
                ++d.registerCount;
            }
        }
        ++g_modbusDeviceCount;
    }
    xSemaphoreGive(g_modbusMutex);
    return g_modbusDeviceCount;
}

void modbusSaveDevicesToConfig() {
    JsonVariant mbSection = configManager.getSection("modbus");
    if (!mbSection.is<JsonObject>()) return;

    JsonObject mbObj = mbSection.as<JsonObject>();
    mbObj.remove("devices");
    JsonArray devArr = mbObj.createNestedArray("devices");

    for (uint8_t i = 0; i < g_modbusDeviceCount; ++i) {
        const ModbusDevice& d = g_modbusDevices[i];
        JsonObject dev = devArr.createNestedObject();
        dev["slaveId"]   = d.slaveId;
        dev["name"]      = d.name;
        dev["enabled"]   = d.enabled;
        dev["isTcp"]     = d.isTcp;
        dev["tcpHost"]   = d.tcpHost;
        dev["tcpPort"]   = d.tcpPort;
        dev["pollMs"]    = d.pollIntervalMs;
        dev["retries"]   = d.retries;
        dev["timeoutMs"] = d.timeoutMs;

        JsonArray regs = dev.createNestedArray("regs");
        for (uint8_t j = 0; j < d.registerCount; ++j) {
            const ModbusRegister& r = d.registers[j];
            JsonObject reg = regs.createNestedObject();
            reg["addr"]   = r.address;
            reg["fc"]     = (uint8_t)r.fc;
            reg["type"]   = (uint8_t)r.dataType;
            reg["order"]  = (uint8_t)r.byteOrder;
            reg["bit"]    = r.bitIndex;
            reg["label"]  = r.label;
            reg["unit"]   = r.unit;
            reg["scale"]  = r.scale;
            reg["offset"] = r.offset;
            reg["log"]    = r.logToSd;
            reg["pretty"] = r.logPretty;
        }
    }
}

#endif // ENABLE_MODBUS==1
