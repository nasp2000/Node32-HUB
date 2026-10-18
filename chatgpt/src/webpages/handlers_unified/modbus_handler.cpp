#include "../../languages/lang_active.h"
#include "modbus_handler.h"
#include "feature_flags.h"

#include "../../psram_helpers.h"
#if ENABLE_MODBUS==1

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../psram_helpers.h"
#include "../../config_manager_runtime.h"
#include "../../modbus_runtime.h"
#include "../../modbus_poll.h"
#include "feature_flags.h"
#include "../../modbus_core.h"
#include "../../modbus_tcp.h"
#include "../../commands.h"
#include "../../modbus_scan_api.h"

#include "../../psram_helpers.h"
// ---- RTU Bus Scan state (module-static, not on heap) ----------------
#define MB_SCAN_MAX_FOUND 32

static volatile bool     _scanRunning  = false;
static volatile uint16_t _scanCurrent  = 0;
static volatile uint16_t _scanTotal    = 0;
static TaskHandle_t      _scanTaskHdl  = nullptr;

struct ModbusScanResult {
    uint8_t  slaveId;
    uint16_t reg0val;
};
static ModbusScanResult  _scanFound[MB_SCAN_MAX_FOUND];
static volatile uint8_t  _scanFoundCount = 0;

struct ModbusScanArgs {
    uint8_t  startId;
    uint8_t  endId;
    uint16_t timeoutMs;
};
static ModbusScanArgs _scanArgs;

static void _scanRtuTask(void* param) {
    (void)param;
    _scanFoundCount = 0;
    _scanCurrent    = _scanArgs.startId;
    _scanTotal      = (uint16_t)(_scanArgs.endId - _scanArgs.startId + 1);
    for (uint8_t id = _scanArgs.startId; id <= _scanArgs.endId && _scanRunning; ++id) {
        _scanCurrent = (uint16_t)(id - _scanArgs.startId + 1);
        uint16_t buf = 0;
        bool ok = modbusRtuReadRegs(id, MB_FC_READ_HOLDING, 0, 1, _scanArgs.timeoutMs, &buf);
        if (ok && _scanFoundCount < MB_SCAN_MAX_FOUND) {
            _scanFound[_scanFoundCount].slaveId = id;
            _scanFound[_scanFoundCount].reg0val = buf;
            ++_scanFoundCount;
        }
        taskYIELD();
    }
    _scanRunning = false;
    _scanTaskHdl = nullptr;
    vTaskDelete(nullptr);
}

// ---- TCP Subnet Scan state ------------------------------------------
static uint8_t _scanType = 0;

struct ModbusTcpScanResult {
    char ip[16];
    char deviceId[40];
};
static ModbusTcpScanResult _tcpFound[MB_SCAN_MAX_FOUND];
static TaskHandle_t        _tcpScanTaskHdl = nullptr;

struct ModbusTcpScanArgs {
    char     subnet[16];
    uint8_t  startIp;
    uint8_t  endIp;
    uint16_t timeoutMs;
};
static ModbusTcpScanArgs _tcpScanArgs;

static bool _tryReadDeviceId(WiFiClient& cli, char* out, size_t outSize) {
    const uint8_t req[] = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
        0x01, 0x2B, 0x0E, 0x01, 0x00
    };
    cli.write(req, sizeof(req));
    unsigned long t0 = millis();
    while (cli.available() < 14 && (millis() - t0) < 400) delay(5);
    if (cli.available() < 14) return false;
    uint8_t resp[64] = {};
    int n = cli.read(resp, sizeof(resp) - 1);
    if (n < 14 || resp[7] != 0x2B || resp[8] != 0x0E) return false;
    if (resp[12] == 0) return false;
    uint8_t objLen = (n > 15) ? resp[15] : 0;
    if (objLen == 0 || objLen + 16 > (uint8_t)n) return false;
    size_t copyLen = (objLen < outSize - 1) ? objLen : outSize - 1;
    memcpy(out, resp + 16, copyLen);
    out[copyLen] = '\0';
    return copyLen > 0;
}

static void _scanTcpTask(void* param) {
    (void)param;
    _scanFoundCount = 0;
    _scanCurrent    = 0;
    _scanTotal      = (uint16_t)(_tcpScanArgs.endIp - _tcpScanArgs.startIp + 1);
    for (uint8_t octet = _tcpScanArgs.startIp;
         octet <= _tcpScanArgs.endIp && _scanRunning;
         ++octet) {
        ++_scanCurrent;
        char ipStr[16];
        snprintf(ipStr, sizeof(ipStr), "%s.%u", _tcpScanArgs.subnet, (unsigned)octet);
        WiFiClient cli;
        if (cli.connect(ipStr, 502, (int32_t)_tcpScanArgs.timeoutMs)
            && _scanFoundCount < MB_SCAN_MAX_FOUND) {
            uint8_t idx = _scanFoundCount;
            strncpy(_tcpFound[idx].ip, ipStr, sizeof(_tcpFound[idx].ip) - 1);
            _tcpFound[idx].ip[sizeof(_tcpFound[idx].ip) - 1] = '\0';
            _tcpFound[idx].deviceId[0] = '\0';
            _tryReadDeviceId(cli, _tcpFound[idx].deviceId, sizeof(_tcpFound[idx].deviceId));
            cli.stop();
            ++_scanFoundCount;
        }
        taskYIELD();
    }
    _scanRunning    = false;
    _tcpScanTaskHdl = nullptr;
    vTaskDelete(nullptr);
}

// ------------ Public scan interface (modbus_scan_api.h) ---------------

bool modbusApiStartRtuScan(uint8_t startId, uint8_t endId, uint16_t timeoutMs) {
    if (_scanRunning) return false;
    _scanArgs.startId   = startId;
    _scanArgs.endId     = endId;
    _scanArgs.timeoutMs = timeoutMs;
    _scanType       = 0;
    _scanRunning    = true;
    _scanFoundCount = 0;
    _scanCurrent    = 0;
    _scanTotal      = (uint16_t)(endId - startId + 1);
    xTaskCreatePinnedToCore(_scanRtuTask, "mbRtuScan", 4096, nullptr, 1, &_scanTaskHdl, 0);
    return true;
}

bool modbusApiStartTcpScan(const char* subnet, uint8_t startIp, uint8_t endIp, uint16_t timeoutMs) {
    if (_scanRunning) return false;
    strncpy(_tcpScanArgs.subnet, subnet, sizeof(_tcpScanArgs.subnet) - 1);
    _tcpScanArgs.subnet[sizeof(_tcpScanArgs.subnet) - 1] = '\0';
    _tcpScanArgs.startIp   = startIp;
    _tcpScanArgs.endIp     = endIp;
    _tcpScanArgs.timeoutMs = timeoutMs;
    _scanType       = 1;
    _scanRunning    = true;
    _scanFoundCount = 0;
    _scanCurrent    = 0;
    _scanTotal      = (uint16_t)(endIp - startIp + 1);
    xTaskCreatePinnedToCore(_scanTcpTask, "mbTcpScan", 6144, nullptr, 1, &_tcpScanTaskHdl, 0);
    return true;
}

void modbusApiScanStop() {
    _scanRunning = false;
}

void modbusApiWriteScanStatusJson(Print& out) {
    out.print("{\"running\":");
    out.print(_scanRunning ? "true" : "false");
    out.print(",\"type\":");
    out.print(_scanType == 1 ? "\"tcp\"" : "\"rtu\"");
    out.print(",\"current\":");
    out.print((unsigned)_scanCurrent);
    out.print(",\"total\":");
    out.print((unsigned)_scanTotal);
    out.print(",\"found\":[");
    for (uint8_t i = 0; i < _scanFoundCount; ++i) {
        if (i) out.print(',');
        if (_scanType == 1) {
            char buf[96];
            char safe[40];
            const char* src = _tcpFound[i].deviceId;
            size_t si = 0;
            for (size_t j = 0; j < sizeof(safe) - 2 && src[j]; ++j) {
                if (src[j] == '"') safe[si++] = '\\';
                safe[si++] = src[j];
            }
            safe[si] = '\0';
            snprintf(buf, sizeof(buf), "{\"ip\":\"%s\",\"deviceId\":\"%s\"}",
                     _tcpFound[i].ip, safe);
            out.print(buf);
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"slaveId\":%u,\"reg0\":%u}",
                     (unsigned)_scanFound[i].slaveId, (unsigned)_scanFound[i].reg0val);
            out.print(buf);
        }
    }
    out.print("]}");
}

// ------------ Unified HTTP handlers ---------------

void handleModbusConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String json;
    serializeJsonPretty(configManager.getSection("modbus"), json);
    res.send(200, "application/json", json);
}

void handleModbusConfigPost(HttpRequest& req, HttpResponse& res) {
    SpiRamJsonDocument doc(12288);
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
        return;
    }
    JsonObject incoming = doc.as<JsonObject>();
    bool hasFields = incoming.containsKey("enabled")
        || incoming.containsKey("rtu_enabled")
        || incoming.containsKey("tcp_enabled")
        || incoming.containsKey("slave_enabled")
        || incoming.containsKey("rtu_baud")
        || incoming.containsKey("rtu_data_bits")
        || incoming.containsKey("rtu_parity")
        || incoming.containsKey("rtu_stop_bits")
        || incoming.containsKey("rtu_de_pin")
        || incoming.containsKey("slave_id")
        || incoming.containsKey("devices");
    if (!hasFields) {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"Missing Modbus config fields\"}");
        return;
    }
    if (!configManager.getSection("modbus").is<JsonObject>()) {
        configManager.getSection("modbus").to<JsonObject>();
    }
    JsonObject mbCfg = configManager.getSection("modbus").as<JsonObject>();
    mbCfg["enabled"]       = incoming["enabled"]       | false;
    mbCfg["rtu_enabled"]   = incoming["rtu_enabled"]   | false;
    mbCfg["tcp_enabled"]   = incoming["tcp_enabled"]   | false;
    mbCfg["slave_enabled"] = incoming["slave_enabled"] | false;
    mbCfg["rtu_baud"]      = incoming["rtu_baud"]      | (uint32_t)9600;
    mbCfg["rtu_data_bits"] = incoming["rtu_data_bits"] | (uint8_t)8;
    mbCfg["rtu_parity"]    = incoming["rtu_parity"]    | "N";
    mbCfg["rtu_stop_bits"] = incoming["rtu_stop_bits"] | (uint8_t)1;
    mbCfg["rtu_de_pin"]    = incoming["rtu_de_pin"]    | (int8_t)-1;
    mbCfg["slave_id"]      = incoming["slave_id"]      | (uint8_t)1;
    mbCfg.remove("devices");
    JsonArray newDevs = mbCfg.createNestedArray("devices");
    if (incoming.containsKey("devices") && incoming["devices"].is<JsonArray>()) {
        for (JsonObject d : incoming["devices"].as<JsonArray>()) {
            JsonObject nd = newDevs.createNestedObject();
            nd["slaveId"]   = d["slaveId"]   | (uint8_t)1;
            nd["name"]      = d["name"]       | "Device";
            nd["enabled"]   = d["enabled"]    | false;
            nd["isTcp"]     = d["isTcp"]      | false;
            nd["tcpHost"]   = d["tcpHost"]    | "";
            nd["tcpPort"]   = d["tcpPort"]    | (uint16_t)502;
            nd["pollMs"]    = d["pollMs"]     | (uint32_t)5000;
            nd["retries"]   = d["retries"]    | (uint8_t)3;
            nd["timeoutMs"] = d["timeoutMs"]  | (uint16_t)500;
            if (d.containsKey("regs") && d["regs"].is<JsonArray>()) {
                JsonArray nr = nd.createNestedArray("regs");
                for (JsonObject r : d["regs"].as<JsonArray>()) {
                    JsonObject nreg = nr.createNestedObject();
                    nreg["addr"]   = r["addr"]   | (uint16_t)0;
                    nreg["fc"]     = r["fc"]     | (uint8_t)3;
                    nreg["type"]   = r["type"]   | (uint8_t)0;
                    nreg["order"]  = r["order"]  | (uint8_t)0;
                    nreg["bit"]    = r["bit"]    | (uint8_t)0;
                    nreg["label"]  = r["label"]  | "Reg";
                    nreg["unit"]   = r["unit"]   | "";
                    nreg["scale"]  = r["scale"]  | 1.0f;
                    nreg["offset"] = r["offset"] | 0.0f;
                    nreg["log"]    = r["log"]    | false;
                    nreg["pretty"] = r["pretty"] | true;
                }
            }
        }
    }
    configManager.markDirtyAndScheduleSave();
    if (!configManager.saveSection("modbus")) {
        res.send(500, "application/json", "{\"success\":false,\"message\":\""+L(activeLang->apiErrSaveModbus)+"\"}");
        return;
    }
    modbusLoadDevicesFromConfig();
    bool mbEn    = mbCfg["enabled"]    | false;
    bool mbRtuEn = mbCfg["rtu_enabled"] | false;
    if (mbEn && mbRtuEn) {
        ModbusRtuConfig mbRtuCfg;
        mbRtuCfg.baud     = mbCfg["rtu_baud"]      | (uint32_t)9600;
        mbRtuCfg.dataBits = mbCfg["rtu_data_bits"] | (uint8_t)8;
        const char* par   = mbCfg["rtu_parity"]    | "N";
        mbRtuCfg.parity   = (par && par[0]) ? par[0] : 'N';
        mbRtuCfg.stopBits = mbCfg["rtu_stop_bits"] | (uint8_t)1;
        mbRtuCfg.dePin    = mbCfg["rtu_de_pin"]    | (int8_t)-1;
        modbusRtuEnd();
        modbusRtuBegin(mbRtuCfg);
    }
    if (mbEn && g_modbusDeviceCount > 0) {
        if (modbusPollIsRunning()) modbusPollStop();
        modbusPollBegin(g_modbusDevices, g_modbusDeviceCount);
    } else if (!mbEn && modbusPollIsRunning()) {
        modbusPollStop();
    }
    sendVirtualMessage(F("Modbus config saved via web"), INFO);
    res.send(200, "application/json", "{\"success\":true}");
}

void handleModbusLiveGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String buf;
    buf.reserve(2048);
    buf += '[';
    for (uint8_t i = 0; i < g_modbusDeviceCount; ++i) {
        if (i) buf += ',';
        const ModbusDevice& d = g_modbusDevices[i];
        buf += "{\"name\":\"";
        buf += d.name;
        buf += "\",\"enabled\":";
        buf += d.enabled ? "true" : "false";
        buf += ",\"pollCount\":";
        buf += (unsigned long)d.pollCount;
        buf += ",\"errorCount\":";
        buf += (unsigned long)d.errorCount;
        buf += ",\"regs\":[";
        for (uint8_t j = 0; j < d.registerCount; ++j) {
            if (j) buf += ',';
            const ModbusRegister& r = d.registers[j];
            char valStr[24];
            dtostrf(r.lastValue, 1, 4, valStr);
            buf += "{\"label\":\"";
            buf += r.label;
            buf += "\",\"unit\":\"";
            buf += r.unit;
            buf += "\",\"val\":";
            buf += valStr;
            buf += ",\"ok\":";
            buf += r.lastOk ? "true" : "false";
            buf += '}';
        }
        buf += "]}";
    }
    buf += ']';
    res.send(200, "application/json", buf);
}

void handleModbusPollNowPost(HttpRequest& req, HttpResponse& res) {
    if (req.body.length() > 0) {
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok
            && doc.containsKey("device")) {
            modbusPollRequestImmediate((uint8_t)(doc["device"] | 0));
        } else {
            for (uint8_t i = 0; i < g_modbusDeviceCount; ++i)
                modbusPollRequestImmediate(i);
        }
    } else {
        for (uint8_t i = 0; i < g_modbusDeviceCount; ++i)
            modbusPollRequestImmediate(i);
    }
    res.send(200, "application/json", "{\"success\":true}");
}

void handleModbusRtuWritePost(HttpRequest& req, HttpResponse& res) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
        return;
    }
    uint8_t  slaveId = doc["slaveId"]   | (uint8_t)1;
    uint16_t addr    = doc["addr"]      | (uint16_t)0;
    uint16_t value   = doc["value"]     | (uint16_t)0;
    bool     isCoil  = doc["isCoil"]    | false;
    uint16_t toutMs  = doc["timeoutMs"] | (uint16_t)500;
    bool ok = false;
    const ModbusDevice* found = nullptr;
    for (uint8_t i = 0; i < g_modbusDeviceCount; ++i) {
        if (g_modbusDevices[i].slaveId == slaveId) { found = &g_modbusDevices[i]; break; }
    }
    if (found && found->isTcp) {
        ok = isCoil ? modbusTcpWriteCoil(*found, addr, value != 0, toutMs)
                    : modbusTcpWriteSingleReg(*found, addr, value, toutMs);
    } else {
        if (!modbusRtuIsReady()) {
            res.send(503, "application/json", "{\"success\":false,\"error\":\"RTU not ready\"}");
            return;
        }
        ok = isCoil ? modbusRtuWriteCoil(slaveId, addr, value != 0, toutMs)
                    : modbusRtuWriteSingleReg(slaveId, addr, value, toutMs);
    }
    res.send(200, "application/json",
        ok ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Write failed\"}");
}

void handleModbusScanRtuPost(HttpRequest& req, HttpResponse& res) {
    if (_scanRunning) {
        res.send(409, "application/json", "{\"success\":false,\"error\":\"Scan already running\"}");
        return;
    }
    if (!modbusRtuIsReady()) {
        res.send(503, "application/json", "{\"success\":false,\"error\":\"RTU not ready\"}");
        return;
    }
    _scanArgs.startId   = 1;
    _scanArgs.endId     = 247;
    _scanArgs.timeoutMs = 300;
    if (req.body.length() > 0) {
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            _scanArgs.startId   = doc["startId"]   | (uint8_t)1;
            _scanArgs.endId     = doc["endId"]     | (uint8_t)247;
            _scanArgs.timeoutMs = doc["timeoutMs"] | (uint16_t)300;
        }
    }
    if (_scanArgs.startId < 1) _scanArgs.startId = 1;
    if (_scanArgs.endId > 247) _scanArgs.endId = 247;
    if (_scanArgs.startId > _scanArgs.endId) _scanArgs.startId = 1;
    _scanRunning    = true;
    _scanFoundCount = 0;
    _scanCurrent    = 0;
    _scanTotal      = (uint16_t)(_scanArgs.endId - _scanArgs.startId + 1);
    xTaskCreatePinnedToCore(_scanRtuTask, "mbScan", 4096, nullptr, 1, &_scanTaskHdl, 0);
    res.send(200, "application/json", "{\"success\":true}");
}

void handleModbusScanStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String buf;
    buf.reserve(512);
    buf += "{\"running\":";
    buf += _scanRunning ? "true" : "false";
    buf += ",\"type\":";
    buf += (_scanType == 1) ? "\"tcp\"" : "\"rtu\"";
    buf += ",\"current\":";
    buf += (unsigned)_scanCurrent;
    buf += ",\"total\":";
    buf += (unsigned)_scanTotal;
    buf += ",\"found\":[";
    for (uint8_t i = 0; i < _scanFoundCount; ++i) {
        if (i) buf += ',';
        if (_scanType == 1) {
            char tmp[96];
            char safe[40];
            const char* src = _tcpFound[i].deviceId;
            size_t si = 0;
            for (size_t j = 0; j < sizeof(safe) - 2 && src[j]; ++j) {
                if (src[j] == '"') safe[si++] = '\\';
                safe[si++] = src[j];
            }
            safe[si] = '\0';
            snprintf(tmp, sizeof(tmp), "{\"ip\":\"%s\",\"deviceId\":\"%s\"}",
                     _tcpFound[i].ip, safe);
            buf += tmp;
        } else {
            char tmp[48];
            snprintf(tmp, sizeof(tmp), "{\"slaveId\":%u,\"reg0\":%u}",
                     (unsigned)_scanFound[i].slaveId, (unsigned)_scanFound[i].reg0val);
            buf += tmp;
        }
    }
    buf += "]}";
    res.send(200, "application/json", buf);
}

void handleModbusScanStopPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    _scanRunning = false;
    res.send(200, "application/json", "{\"success\":true}");
}

void handleModbusStatsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    unsigned long totalPolls = 0, totalErrors = 0;
    for (uint8_t i = 0; i < g_modbusDeviceCount; ++i) {
        totalPolls  += g_modbusDevices[i].pollCount;
        totalErrors += g_modbusDevices[i].errorCount;
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"rtuReady\":%s,\"pollTaskRunning\":%s,"
        "\"deviceCount\":%u,\"totalPolls\":%lu,\"totalErrors\":%lu}",
        modbusRtuIsReady()    ? "true" : "false",
        modbusPollIsRunning() ? "true" : "false",
        (unsigned)g_modbusDeviceCount,
        totalPolls, totalErrors);
    res.send(200, "application/json", String(buf));
}

void handleModbusResetStatsPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    for (uint8_t i = 0; i < g_modbusDeviceCount; ++i) {
        g_modbusDevices[i].pollCount  = 0;
        g_modbusDevices[i].errorCount = 0;
    }
    res.send(200, "application/json", "{\"success\":true}");
}

void handleModbusScanTcpPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_MODBUS_TCP==1
    if (_scanRunning) {
        res.send(409, "application/json", "{\"success\":false,\"error\":\"Scan already running\"}");
        return;
    }
    strncpy(_tcpScanArgs.subnet, "192.168.1", sizeof(_tcpScanArgs.subnet) - 1);
    _tcpScanArgs.subnet[sizeof(_tcpScanArgs.subnet) - 1] = '\0';
    _tcpScanArgs.startIp   = 1;
    _tcpScanArgs.endIp     = 254;
    _tcpScanArgs.timeoutMs = 200;
    if (req.body.length() > 0) {
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, req.body) == DeserializationError::Ok) {
            if (doc.containsKey("subnet")) {
                const char* sn = doc["subnet"] | "192.168.1";
                strncpy(_tcpScanArgs.subnet, sn, sizeof(_tcpScanArgs.subnet) - 1);
                _tcpScanArgs.subnet[sizeof(_tcpScanArgs.subnet) - 1] = '\0';
            }
            _tcpScanArgs.startIp   = doc["startIp"]   | (uint8_t)1;
            _tcpScanArgs.endIp     = doc["endIp"]     | (uint8_t)254;
            _tcpScanArgs.timeoutMs = doc["timeoutMs"] | (uint16_t)200;
        }
    }
    if (_tcpScanArgs.startIp < 1) _tcpScanArgs.startIp = 1;
    if (_tcpScanArgs.endIp > 254) _tcpScanArgs.endIp = 254;
    if (_tcpScanArgs.startIp > _tcpScanArgs.endIp) _tcpScanArgs.startIp = 1;
    if (_tcpScanArgs.timeoutMs < 50) _tcpScanArgs.timeoutMs = 50;
    if (_tcpScanArgs.timeoutMs > 5000) _tcpScanArgs.timeoutMs = 5000;
    _scanType       = 1;
    _scanRunning    = true;
    _scanFoundCount = 0;
    _scanCurrent    = 0;
    _scanTotal      = (uint16_t)(_tcpScanArgs.endIp - _tcpScanArgs.startIp + 1);
    xTaskCreatePinnedToCore(_scanTcpTask, "mbTcpScan", 6144, nullptr, 1, &_tcpScanTaskHdl, 0);
    res.send(200, "application/json", "{\"success\":true}");
#else
    (void)req;
    res.send(200, "application/json", "{\"success\":false,\"error\":\"Modbus TCP disabled at compile time\"}");
#endif
}

void handleModbusWritePost(HttpRequest& req, HttpResponse& res) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, req.body) != DeserializationError::Ok) {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
        return;
    }
    uint8_t devIdx = doc["deviceIndex"] | (uint8_t)255;
    uint8_t regIdx = doc["regIndex"]    | (uint8_t)255;
    float   fval   = doc["value"]       | 0.0f;
    if (devIdx >= g_modbusDeviceCount) {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid deviceIndex\"}");
        return;
    }
    const ModbusDevice& dev = g_modbusDevices[devIdx];
    if (regIdx >= dev.registerCount) {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid regIndex\"}");
        return;
    }
    const ModbusRegister& reg = dev.registers[regIdx];
    if (reg.fc != MB_FC_WRITE_COIL && reg.fc != MB_FC_WRITE_REGISTER) {
        res.send(400, "application/json",
            "{\"success\":false,\"error\":\"Register FC not writable (use FC05 or FC06)\"}");
        return;
    }
    uint16_t toutMs = dev.timeoutMs ? dev.timeoutMs : 500;
    bool     isCoil = (reg.fc == MB_FC_WRITE_COIL);
    uint16_t rawVal = (uint16_t)(fval);
    bool ok = false;
    if (dev.isTcp) {
        ok = isCoil ? modbusTcpWriteCoil(dev, reg.address, rawVal != 0, toutMs)
                    : modbusTcpWriteSingleReg(dev, reg.address, rawVal, toutMs);
    } else {
        if (!modbusRtuIsReady()) {
            res.send(503, "application/json", "{\"success\":false,\"error\":\"RTU not ready\"}");
            return;
        }
        ok = isCoil ? modbusRtuWriteCoil(dev.slaveId, reg.address, rawVal != 0, toutMs)
                    : modbusRtuWriteSingleReg(dev.slaveId, reg.address, rawVal, toutMs);
    }
    res.send(200, "application/json",
        ok ? "{\"success\":true}" : "{\"success\":false,\"error\":\"Write failed\"}");
}

// --- Chart handlers (guarded by ENABLE_RS232_CHARTS && ENABLE_SDCARD) ---

#if ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1
#include "../../chart_history.h"

#include "../../psram_helpers.h"
// Adapter: Print implementation that appends to a String.
struct _StringPrint : public Print {
    String* s;
    _StringPrint(String& str) : s(&str) {}
    size_t write(uint8_t c) override { *s += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t len) override {
        for (size_t i = 0; i < len; i++) *s += (char)buf[i];
        return len;
    }
};

void handleModbusChartFilesGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    String safeName = chartHistorySanitizeSystemName(configManager.getSystemName());
    String dir = "/" + safeName;
    String prefix = safeName + "_MODBUS_";
    String buf;
    buf.reserve(512);
    buf += "[";
    bool first = true;
    if (sdcardShouldUseSD() && sdMutexLock(pdMS_TO_TICKS(3000))) {
        File root = SD_MMC.open(dir.c_str());
        if (root && root.isDirectory()) {
            File entry = root.openNextFile();
            while (entry) {
                String fname = String(entry.name());
                int sl = fname.lastIndexOf('/');
                String base = sl >= 0 ? fname.substring(sl + 1) : fname;
                if (base.startsWith(prefix) && base.endsWith(".chart")) {
                    String datePart = base.substring(prefix.length(), base.length() - 6);
                    if (datePart.length() == 10
                        && datePart.charAt(2) == '-'
                        && datePart.charAt(5) == '-') {
                        if (!first) buf += ",";
                        buf += "\"";
                        buf += datePart;
                        buf += "\"";
                        first = false;
                    }
                }
                entry.close();
                entry = root.openNextFile();
            }
            root.close();
        }
        sdMutexUnlock();
    }
    buf += "]";
    res.send(200, "application/json", buf);
}

void handleModbusChartColsGet(HttpRequest& req, HttpResponse& res) {
    String dateParam;
    int eq = req.query.indexOf('=');
    int am = req.query.indexOf('&');
    if (eq > 0) {
        int end = (am > eq) ? am : (int)req.query.length();
        dateParam = req.query.substring(eq + 1, end);
    }
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char today[11];
    snprintf(today, sizeof(today), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    String resolvedDate  = (dateParam.length() == 10) ? dateParam : String(today);
    String chartPath = chartHistoryModbusPathForDate(configManager.getSystemName(), resolvedDate);
    uint8_t colCount = 0;
    ChartColumnDef defs[CHART_HISTORY_MODBUS_MAX_COLUMNS] = {};
    chartHistoryReadModbusMeta(chartPath, colCount, defs);
    String buf;
    buf.reserve(512);
    buf += "{\"cols\":";
    buf += colCount;
    buf += ",\"date\":\"";
    buf += resolvedDate;
    buf += "\",\"col_names\":[";
    for (uint8_t i = 0; i < colCount; i++) {
        if (i) buf += ",";
        buf += "\"";
        if (defs[i].key[0]) buf += defs[i].key;
        else { buf += "col_"; buf += (i + 1); }
        buf += "\"";
    }
    buf += "]}";
    res.send(200, "application/json", buf);
}

void handleModbusChartDataGet(HttpRequest& req, HttpResponse& res) {
    String dateParam, modeParam, fromParam;
    int start = 0;
    while (start < (int)req.query.length()) {
        int amp = req.query.indexOf('&', start);
        String seg = (amp < 0) ? req.query.substring(start) : req.query.substring(start, amp);
        int eq = seg.indexOf('=');
        if (eq > 0) {
            String k = seg.substring(0, eq);
            String v = seg.substring(eq + 1);
            if (k == "date") dateParam = v;
            if (k == "mode") modeParam = v;
            if (k == "from") fromParam = v;
        }
        if (amp < 0) break;
        start = amp + 1;
    }
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char today[11];
    snprintf(today, sizeof(today), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    String resolvedDate  = (dateParam.length() == 10) ? dateParam : String(today);
    String mode          = modeParam.length() ? modeParam : "day";
    uint32_t fromTs = 0;
    if (fromParam.length()) {
        fromTs = (uint32_t)strtoul(fromParam.c_str(), nullptr, 10);
    } else {
        time_t nowTs = time(nullptr);
        if      (mode == "live") fromTs = (uint32_t)(nowTs > 900   ? nowTs - 900   : 0);
        else if (mode == "8h")   fromTs = (uint32_t)(nowTs > 28800 ? nowTs - 28800 : 0);
        else                     fromTs = chartHistoryDayStartFromRs232DateId(resolvedDate);
    }
    uint32_t rangeSeconds = (uint32_t)(now > (time_t)fromTs ? now - (time_t)fromTs : 86400UL);
    const int maxOut = (rangeSeconds <= 3600UL) ? 72 : ((rangeSeconds <= 28800UL) ? 144 : 288);
    String chartPath = chartHistoryModbusPathForDate(configManager.getSystemName(), resolvedDate);
    String buf;
    buf.reserve(4096);
    _StringPrint sp(buf);
    // Merged serving (file history + PSRAM ring tail, always minute-fresh).
    extern bool mbChartServeMergedJson(Print& out, const String& path, uint32_t fromTs, uint16_t maxOut);
    if (!mbChartServeMergedJson(sp, chartPath, fromTs, (uint16_t)maxOut)) {
        buf = "{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}";
    }
    res.send(200, "application/json", buf);
}
#else
void handleModbusChartFilesGet(HttpRequest& req, HttpResponse& res) {
    (void)req; res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
}
void handleModbusChartColsGet(HttpRequest& req, HttpResponse& res) {
    (void)req; res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
}
void handleModbusChartDataGet(HttpRequest& req, HttpResponse& res) {
    (void)req; res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
}
#endif

#endif // ENABLE_MODBUS==1
