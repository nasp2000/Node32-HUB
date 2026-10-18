// modbus_slave.cpp
// Modbus slave mode -- RTU listener on UART1 + TCP server on port 502.
// Exposes sensor readings and master-polled values as Modbus holding registers.
// See modbus_slave.h for the register and coil map.

#include "feature_flags.h"
#include "modbus_slave.h"

#if ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1

#include <Arduino.h>
#include <math.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "modbus_runtime.h"
#include "feature_flags.h"
#include "rtc_state_util.h"

#if ENABLE_AHT20_BMP280==1
#include "sensors/sensor_aht20_bmp280.h"
extern SensorAHT20_BMP280 environmentalSensor;
#endif

#if ENABLE_MQ135==1
#include "sensors/sensor_mq135.h"
extern MQ135Sensor mq135Sensor;
#endif

// ---- Constants ---------------------------------------------------

#define SLAVE_HREG_COUNT 228   // 0x0000-0x005F (original 96) + 0x0064-0x00E3 (proximity: 8ch × 16regs = 128)
#define SLAVE_COIL_BYTES   4   // 32 coils packed as bits (enough for our map)
#define SLAVE_RX_MAX     260   // Max RTU frame byte buffer

// Holding register addresses
#define HR_DEVICE_ID    0x0000 // 0x4E33 = "N3" (Node32-HUB)
#define HR_FW_VERSION   0x0002 // e.g. 224 for v2.24
#define HR_UPTIME_HI    0x0002 // millis()/1000 >> 16
#define HR_UPTIME_LO    0x0003 // millis()/1000 & 0xFFFF
#define HR_AHT20_TEMP   0x0010 // AHT20 temperature x10 INT16
#define HR_AHT20_HUM    0x0011 // AHT20 humidity x10 UINT16
#define HR_BMP280_TEMP  0x0012 // BMP280 temperature x10 INT16
#define HR_BMP280_PRES  0x0013 // BMP280 pressure (hPa integer) UINT16
#define HR_MQ135_CO2    0x0014 // MQ135 CO2 estimate (ppm) UINT16
#define HR_MASTER_BASE  0x0020 // Master re-export start: dev[d]*16 + reg[r]
#define HR_PROX_BASE    0x0064 // Proximity sensor channels start (set via modbusSlaveSetHreg)

// Coil indexes
#define COIL_HEARTBEAT  0
#define COIL_AHT20_OK   1
#define COIL_BMP280_OK  2
#define COIL_MQ135_OK   3

// ---- Module-static state -----------------------------------------

static uint16_t     _hreg[SLAVE_HREG_COUNT];
static uint8_t      _coilByte[SLAVE_COIL_BYTES];

static uint8_t      _ownId;
static bool         _rtuEnabled;
static bool         _tcpEnabled;
static int8_t       _dePin;
static uint16_t     _tcpPort;
static uint32_t     _frameGapUs;  // 3.5-char silence threshold in microseconds

// ---- RTC RAM: continuous uptime ("UPTM") --------------------------
// millis() uptime resets on any reset; warm reboots (esp_restart/OTA/WDT) keep
// RTC_SLOW_MEM alive, so an accumulated whole-second counter in RTC RAM lets
// HR_UPTIME_HI/LO read as time-since-last-cold-power-on across reboots. A
// power cut clears the magic and the counter honestly restarts at zero.
#define UPTIME_RTC_MAGIC   0x5550544DUL   // "UPTM"
#define UPTIME_RTC_VERSION 1U
RTC_NOINIT_ATTR static struct {
    uint32_t magic;
    uint32_t version;
    uint32_t accumSec;  // whole seconds accumulated up to the last RTC mirror
} s_uptimeRtc;
static uint32_t s_uptimeBaseSec = 0; // resumed from RTC at boot (0 on power cut)
static uint32_t s_uptimeLastMs  = 0; // last whole-second sweep in this session
static uint32_t s_uptimeSweepMs = 0; // sweep guard (never sweeps >1/s)

static void _uptimeRtcBootstrap() {
    if (s_uptimeRtc.magic == UPTIME_RTC_MAGIC && s_uptimeRtc.version == UPTIME_RTC_VERSION) {
        s_uptimeBaseSec = s_uptimeRtc.accumSec;  // warm reset/OTA: continue
    }
    s_uptimeLastMs = millis();
    s_uptimeSweepMs = s_uptimeLastMs;
}

static void _uptimeRtcMirror() {
    s_uptimeRtc.accumSec = s_uptimeBaseSec;
    s_uptimeRtc.version  = UPTIME_RTC_VERSION;
    s_uptimeRtc.magic    = UPTIME_RTC_MAGIC; // written last: guards a partial mirror
}

// Advance the accumulated uptime by the elapsed whole seconds and persist the
// new base to RTC RAM (SRAM writes are wear-free). Call from the register
// refresh path (≤1×/s): a warm reset then loses at most one second.
static void _uptimeTickReflect() {
    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - s_uptimeSweepMs) < 1000UL) return;
    if (uptimeSweep(s_uptimeBaseSec, s_uptimeLastMs, nowMs)) _uptimeRtcMirror();
    s_uptimeSweepMs = nowMs;
}

static WiFiServer*  _tcpSrv       = nullptr;
static TaskHandle_t _slaveTaskHdl = nullptr;

// ---- Coil helpers ------------------------------------------------

static bool _getCoil(uint8_t n) {
    if (n >= SLAVE_COIL_BYTES * 8) return false;
    return (_coilByte[n / 8] >> (n % 8)) & 1u;
}

static void _setCoil(uint8_t n, bool v) {
    if (n >= SLAVE_COIL_BYTES * 8) return;
    if (v) _coilByte[n / 8] |=  (uint8_t)(1u << (n % 8));
    else   _coilByte[n / 8] &= ~(uint8_t)(1u << (n % 8));
}

// ---- Register refresh (reads live sensor and master data) --------

static void _refreshRegisters() {
    // Static system info
    _hreg[HR_DEVICE_ID]  = 0x4E33;  // "N3"
    _hreg[HR_FW_VERSION] = 1120;      // v1.120 (auto-increment with FIRMWARE_VERSION_STR)

    // Uptime in seconds (continuous across warm reboots via the RTC RAM
    // accumulator; honest time-since-last-power-on after a power cut)
    _uptimeTickReflect();
    uint32_t upSec = uptimeNow(s_uptimeBaseSec, s_uptimeLastMs, millis());
    _hreg[HR_UPTIME_HI] = (uint16_t)(upSec >> 16);
    _hreg[HR_UPTIME_LO] = (uint16_t)(upSec & 0xFFFF);

    // Heartbeat: toggle on each refresh
    _setCoil(COIL_HEARTBEAT, !_getCoil(COIL_HEARTBEAT));

#if ENABLE_AHT20_BMP280==1
    if (environmentalSensor.isInitialized()) {
        _setCoil(COIL_AHT20_OK, true);
        SensorReading r = environmentalSensor.getLastReading();
        // Store temperature as INT16 via uint16_t reinterpretation
        _hreg[HR_AHT20_TEMP] = (uint16_t)(int16_t)(r.temperature_aht20 * 10.0f);
        _hreg[HR_AHT20_HUM]  = (uint16_t)(r.humidity * 10.0f);
        if (environmentalSensor.isBMP280Available()) {
            _setCoil(COIL_BMP280_OK, true);
            _hreg[HR_BMP280_TEMP] = (uint16_t)(int16_t)(r.temperature_bmp280 * 10.0f);
            _hreg[HR_BMP280_PRES] = (uint16_t)(r.pressure);  // integer hPa
        }
    }
#endif

#if ENABLE_MQ135==1
    if (mq135Sensor.isInitialized()) {
        _setCoil(COIL_MQ135_OK, true);
        _hreg[HR_MQ135_CO2] = (uint16_t)mq135Sensor.getLastReading().ppm_co2;
    }
#endif

    // Re-export Modbus master device readings (up to 4 devices x 16 registers)
    // Hold the mutex to avoid reading a partially-reloaded device table.
    if (xSemaphoreTake(g_modbusMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (uint8_t d = 0; d < g_modbusDeviceCount && d < 4; ++d) {
            const ModbusDevice& dev = g_modbusDevices[d];
            for (uint8_t r = 0; r < dev.registerCount && r < 16; ++r) {
                uint16_t mapAddr = (uint16_t)(HR_MASTER_BASE + d * 16 + r);
                if (mapAddr < SLAVE_HREG_COUNT) {
                    // Store as fixed-point INT16 x10 (e.g. 23.7 → 237).
                    // Client must divide by 10 to recover the original value.
                    _hreg[mapAddr] = (uint16_t)(int16_t)roundf(dev.registers[r].lastValue * 10.0f);
                }
            }
        }
        xSemaphoreGive(g_modbusMutex);
    }
}

// ---- PDU dispatcher ----------------------------------------------
// Processes PDU bytes (starting at FC byte, no slave ID, no CRC).
// Fills respPdu[] and returns response PDU length.
// Returns 0 only if no response should be sent.

static uint8_t _dispatchPdu(const uint8_t* pdu, uint8_t pduLen, uint8_t* respPdu) {
    if (pduLen < 1) return 0;
    const uint8_t fc = pdu[0];

    if (fc == MB_FC_READ_HOLDING || fc == MB_FC_READ_INPUT) {
        // FC03 / FC04 -- read holding or input registers (input mirrors holding)
        if (pduLen < 5) { respPdu[0] = fc | 0x80; respPdu[1] = 0x03; return 2; }
        uint16_t startAddr = (uint16_t)(pdu[1] << 8 | pdu[2]);
        uint16_t count     = (uint16_t)(pdu[3] << 8 | pdu[4]);
        if (count == 0 || count > 125)                   { respPdu[0] = fc|0x80; respPdu[1] = 0x03; return 2; }
        if (startAddr + count > (uint16_t)SLAVE_HREG_COUNT) { respPdu[0] = fc|0x80; respPdu[1] = 0x02; return 2; }
        respPdu[0] = fc;
        respPdu[1] = (uint8_t)(count * 2);
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t v = _hreg[startAddr + i];
            respPdu[2 + i * 2]     = (uint8_t)(v >> 8);
            respPdu[2 + i * 2 + 1] = (uint8_t)(v & 0xFF);
        }
        return (uint8_t)(2 + count * 2);

    } else if (fc == MB_FC_READ_COILS || fc == MB_FC_READ_DISCRETE) {
        // FC01 / FC02 -- read coils or discrete inputs (inputs mirror coils)
        if (pduLen < 5) { respPdu[0] = fc|0x80; respPdu[1] = 0x03; return 2; }
        uint16_t startAddr = (uint16_t)(pdu[1] << 8 | pdu[2]);
        uint16_t count     = (uint16_t)(pdu[3] << 8 | pdu[4]);
        uint8_t maxCoils   = (uint8_t)(SLAVE_COIL_BYTES * 8);
        if (count == 0 || count > maxCoils)              { respPdu[0] = fc|0x80; respPdu[1] = 0x03; return 2; }
        if (startAddr + count > (uint16_t)maxCoils)      { respPdu[0] = fc|0x80; respPdu[1] = 0x02; return 2; }
        uint8_t byteCount = (uint8_t)((count + 7) / 8);
        respPdu[0] = fc;
        respPdu[1] = byteCount;
        memset(respPdu + 2, 0, byteCount);
        for (uint16_t i = 0; i < count; ++i) {
            if (_getCoil((uint8_t)(startAddr + i)))
                respPdu[2 + i / 8] |= (uint8_t)(1u << (i % 8));
        }
        return (uint8_t)(2 + byteCount);

    } else if (fc == MB_FC_WRITE_COIL) {
        // FC05 -- write single coil (heartbeat coil 0 is read-only)
        if (pduLen < 5) { respPdu[0] = fc|0x80; respPdu[1] = 0x03; return 2; }
        uint16_t addr = (uint16_t)(pdu[1] << 8 | pdu[2]);
        bool     val  = (pdu[3] == 0xFF);
        if (addr >= (uint16_t)(SLAVE_COIL_BYTES * 8)) { respPdu[0] = fc|0x80; respPdu[1] = 0x02; return 2; }
        if (addr != COIL_HEARTBEAT) _setCoil((uint8_t)addr, val);
        memcpy(respPdu, pdu, 5);  // echo back (standard FC05 response)
        return 5;

    } else if (fc == MB_FC_WRITE_REGISTER) {
        // FC06 -- write single holding reg (only master re-export range is writable)
        if (pduLen < 5) { respPdu[0] = fc|0x80; respPdu[1] = 0x03; return 2; }
        uint16_t addr = (uint16_t)(pdu[1] << 8 | pdu[2]);
        uint16_t val  = (uint16_t)(pdu[3] << 8 | pdu[4]);
        if (addr >= HR_MASTER_BASE && addr < (uint16_t)SLAVE_HREG_COUNT) {
            _hreg[addr] = val;
        } else {
            respPdu[0] = fc|0x80; respPdu[1] = 0x02; return 2;  // read-only system register
        }
        memcpy(respPdu, pdu, 5);  // echo back
        return 5;

    } else {
        // Unsupported function code
        respPdu[0] = fc | 0x80;
        respPdu[1] = 0x01;  // illegal function
        return 2;
    }
}

// ---- RTU frame handler -------------------------------------------

static void _processRtuFrame(const uint8_t* frame, uint8_t len) {
    if (len < 4) return;  // minimum valid RTU frame

    // Verify CRC (last 2 bytes, little-endian)
    uint16_t rxCrc   = (uint16_t)(frame[len - 1] << 8 | frame[len - 2]);
    uint16_t calcCrc = modbusCrc16(frame, (size_t)(len - 2));
    if (rxCrc != calcCrc) return;

    // Addressed to us?
    if (frame[0] != _ownId) return;

    // Dispatch PDU (skip slave ID byte at [0] and 2 CRC bytes at end)
    uint8_t pdu[260];
    uint8_t respPdu[260];
    uint8_t pduLen = (uint8_t)(len - 3);
    memcpy(pdu, frame + 1, pduLen);

    uint8_t respLen = _dispatchPdu(pdu, pduLen, respPdu);
    if (respLen == 0) return;

    // Build RTU response: [slaveId][respPdu bytes][CRC-lo][CRC-hi]
    uint8_t resp[260];
    resp[0] = _ownId;
    memcpy(resp + 1, respPdu, respLen);
    uint16_t crc = modbusCrc16(resp, (size_t)(1 + respLen));
    resp[1 + respLen]     = (uint8_t)(crc & 0xFF);
    resp[1 + respLen + 1] = (uint8_t)(crc >> 8);
    uint8_t totalLen = (uint8_t)(1 + respLen + 2);

    // Transmit: drive DE high if manual pin, send, wait for TX, lower DE
    if (_dePin >= 0) digitalWrite(_dePin, HIGH);
    Serial1.write(resp, totalLen);
    Serial1.flush();
    if (_dePin >= 0) {
        delayMicroseconds(150);  // ensure last byte has fully shifted out before releasing bus
        digitalWrite(_dePin, LOW);
    }
}

// ---- TCP request handler -----------------------------------------

static void _processTcpClient(WiFiClient& client) {
    // Read MBAP header: TransId[2] + ProtoId[2] + Length[2] + UnitId[1] = 7 bytes
    uint8_t mbap[7];
    if (client.readBytes(mbap, 7) < 7) return;

    uint16_t mbLen = (uint16_t)(mbap[4] << 8 | mbap[5]);
    if (mbLen < 2 || mbLen > 252) return;  // sanity: UnitId(1) + PDU(1..251)

    uint8_t unitId = mbap[6];
    // Respond if addressed to us, broadcast(0), or TCP broadcast(0xFF)
    if (unitId != 0 && unitId != 0xFF && unitId != _ownId) return;

    // Read PDU (mbLen - 1 to skip the UnitId byte already read)
    uint8_t pdu[252];
    uint8_t pduLen = (uint8_t)(mbLen - 1);
    if (client.readBytes(pdu, pduLen) < pduLen) return;

    uint8_t respPdu[256];
    uint8_t respLen = _dispatchPdu(pdu, pduLen, respPdu);
    if (respLen == 0) return;

    // Build MBAP response: echo TxId + 0x0000 proto + length + ownId + PDU
    uint8_t resp[264];
    resp[0] = mbap[0]; resp[1] = mbap[1];  // Transaction ID
    resp[2] = 0x00;    resp[3] = 0x00;     // Protocol ID
    uint16_t rLen = (uint16_t)(1 + respLen);
    resp[4] = (uint8_t)(rLen >> 8);
    resp[5] = (uint8_t)(rLen & 0xFF);
    resp[6] = _ownId;
    memcpy(resp + 7, respPdu, respLen);
    client.write(resp, 7 + respLen);
}

// ---- Main slave FreeRTOS task ------------------------------------

static void _slaveTask(void* /*pv*/) {
    uint8_t  rxBuf[SLAVE_RX_MAX];
    uint8_t  rxLen      = 0;
    uint32_t lastByteUs = 0;

    for (;;) {
        // Refresh register map every 500 ms
        static uint32_t lastRefreshMs = 0;
        if ((uint32_t)(millis() - lastRefreshMs) >= 500UL) {
            _refreshRegisters();
            lastRefreshMs = millis();
        }

        // RTU: accumulate bytes from Serial1 and process on inter-frame silence
        if (_rtuEnabled) {
            while (Serial1.available() > 0) {
                if (rxLen < SLAVE_RX_MAX)
                    rxBuf[rxLen++] = (uint8_t)Serial1.read();
                else
                    (void)Serial1.read();  // overflow: discard
                lastByteUs = micros();
            }
            if (rxLen > 0 && (uint32_t)(micros() - lastByteUs) >= _frameGapUs) {
                _processRtuFrame(rxBuf, rxLen);
                rxLen = 0;
            }
        }

        // TCP: accept one client connection and process one request per iteration
        if (_tcpEnabled && _tcpSrv) {
            WiFiClient client = _tcpSrv->available();
            if (client) {
                client.setTimeout(200);
                _processTcpClient(client);
                client.stop();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ---- Public API --------------------------------------------------

void modbusSlaveBegin(uint8_t slaveId, bool rtuEn, bool tcpEn,
                      int8_t dePin, uint32_t baud, uint16_t tcpPort) {
    if (_slaveTaskHdl) return;  // already running

    _ownId      = slaveId;
    _rtuEnabled = rtuEn;
    _tcpEnabled = tcpEn;
    _dePin      = dePin;
    _tcpPort    = tcpPort;

    // 3.5 char times = 3.5 x 10 bits / baud x 1e6 us; minimum 1750 us per spec
    _frameGapUs = 35000000UL / (baud ? baud : 9600);
    if (_frameGapUs < 1750) _frameGapUs = 1750;

    memset(_hreg,     0, sizeof(_hreg));
    memset(_coilByte, 0, sizeof(_coilByte));
    _uptimeRtcBootstrap();
    _refreshRegisters();

    if (_dePin >= 0) {
        pinMode(_dePin, OUTPUT);
        digitalWrite(_dePin, LOW);  // default: receive mode
    }

    if (_tcpEnabled) {
        _tcpSrv = new WiFiServer(_tcpPort);
        _tcpSrv->begin();
    }

    xTaskCreatePinnedToCore(_slaveTask, "mbSlave", 4096, nullptr, 1, &_slaveTaskHdl, 1);
}

void modbusSlaveEnd() {
    if (_slaveTaskHdl) {
        vTaskDelete(_slaveTaskHdl);
        _slaveTaskHdl = nullptr;
    }
    if (_tcpSrv) {
        _tcpSrv->end();
        delete _tcpSrv;
        _tcpSrv = nullptr;
    }
}

bool modbusSlaveIsRunning() {
    return _slaveTaskHdl != nullptr;
}

// Write a holding register from firmware (proximity, other sensor modules).
// Thread-safe: simple 16-bit store is atomic on both Xtensa and ARM cores.
void modbusSlaveSetHreg(uint16_t addr, uint16_t value) {
    if (addr < (uint16_t)SLAVE_HREG_COUNT) {
        _hreg[addr] = value;
    }
}

#endif // ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1
