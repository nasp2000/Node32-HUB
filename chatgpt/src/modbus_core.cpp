// modbus_core.cpp
// Modbus RTU master implementation: UART1 init, CRC16, frame exchange.
// Supports FC01-FC04 reads and FC05/FC06 writes.
// Custom implementation — no external Modbus library required.

#include "modbus_core.h"

#include "feature_flags.h"
#if ENABLE_MODBUS==1

#include <HardwareSerial.h>
#include <freertos/task.h>
#include "feature_flags.h"

// UART1 instance (pins configured in modbusRtuBegin)
static HardwareSerial _mbSerial(1);
static bool     _rtuReady         = false;
static uint32_t _interframeUs     = 4000; // 3.5 char-times at 9600 baud

// Calculate the minimum inter-frame silence required by the Modbus spec.
// At baud > 19200 the spec fixes the value at 1750 µs (= 3.5 char-times at 19200).
static void _calcInterframeDelay(uint32_t baud) {
    // 1 char = 11 bits (start + 8 data + 1 parity + 1 stop, worst case)
    uint32_t us = (uint32_t)(3.5f * 11.0f * 1000000.0f / (float)baud);
    _interframeUs = (us < 1750u) ? 1750u : us;
}

// ---- Public: CRC16 -----------------------------------------------

uint16_t modbusCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xA001u) : (crc >> 1);
        }
    }
    return crc;
}

// ---- Public: UART1 init/deinit ------------------------------------

void modbusRtuBegin(const ModbusRtuConfig& cfg) {
    // Map cfg fields to Arduino serial config constant
    uint32_t sc = SERIAL_8N1;
    if (cfg.dataBits == 8) {
        if      (cfg.parity == 'E') sc = SERIAL_8E1;
        else if (cfg.parity == 'O') sc = SERIAL_8O1;
        else sc = (cfg.stopBits == 2) ? SERIAL_8N2 : SERIAL_8N1;
    } else { // 7-bit (rare in Modbus, but allowed)
        if      (cfg.parity == 'E') sc = SERIAL_7E1;
        else if (cfg.parity == 'O') sc = SERIAL_7O1;
        else sc = SERIAL_7N1;
    }
    _mbSerial.begin(cfg.baud, sc, GPIO_MB_RX, GPIO_MB_TX);
    _calcInterframeDelay(cfg.baud);
    _rtuReady = true;
}

void modbusRtuEnd() {
    if (_rtuReady) {
        _mbSerial.end();
        _rtuReady = false;
    }
}

bool modbusRtuIsReady() { return _rtuReady; }

// ---- Internal: blocking RTU exchange -----------------------------

// Send txBuf, wait for exactly expectLen response bytes within timeoutMs.
// Returns true on success with a valid CRC and no exception flag.
static bool _rtuExchange(const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t expectLen,
                          uint16_t timeoutMs) {
    // Discard any stale bytes left in the RX buffer
    while (_mbSerial.available()) _mbSerial.read();

    // Enforce inter-frame silence before transmitting
    delayMicroseconds(_interframeUs);

    _mbSerial.write(txBuf, txLen);
    _mbSerial.flush(); // Wait for TX to complete

    // Read exactly expectLen bytes, yielding between polls to avoid starving RTOS
    uint32_t deadline = millis() + timeoutMs;
    size_t   rxLen    = 0;
    while (millis() < deadline && rxLen < expectLen) {
        if (_mbSerial.available()) {
            rxBuf[rxLen++] = (uint8_t)_mbSerial.read();
        } else {
            taskYIELD(); // Allow other tasks to run while waiting for bytes
        }
    }

    if (rxLen < expectLen) return false; // Timeout: incomplete frame

    // Validate CRC (appended as little-endian in last two bytes)
    uint16_t gotCrc  = (uint16_t)rxBuf[expectLen - 2]
                     | ((uint16_t)rxBuf[expectLen - 1] << 8);
    uint16_t calcCrc = modbusCrc16(rxBuf, expectLen - 2);
    if (gotCrc != calcCrc) return false;

    // Detect Modbus exception response: FC bit 7 set (e.g. 0x83 for FC03 exception)
    if (rxBuf[1] & 0x80u) return false;

    return true;
}

// ---- Public: RTU read (FC01-FC04) --------------------------------

bool modbusRtuReadRegs(uint8_t slaveId, uint8_t fc,
                        uint16_t startAddr, uint8_t count,
                        uint16_t timeoutMs, uint16_t* outBuf) {
    if (!_rtuReady || count == 0 || count > 125) return false;

    uint8_t req[8];
    req[0] = slaveId;
    req[1] = fc;
    req[2] = (uint8_t)(startAddr >> 8);
    req[3] = (uint8_t)(startAddr & 0xFF);
    req[4] = 0;
    req[5] = count;
    uint16_t crc = modbusCrc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    // Calculate expected response length based on function code
    bool   isCoil    = (fc == MB_FC_READ_COILS || fc == MB_FC_READ_DISCRETE);
    size_t dataBytes = isCoil ? ((count + 7u) / 8u) : (count * 2u);
    size_t expectLen = 3u + dataBytes + 2u; // slaveId + fc + byteCount + data + CRC

    uint8_t rx[256];
    if (expectLen > sizeof(rx)) return false;
    if (!_rtuExchange(req, 8, rx, expectLen, timeoutMs)) return false;

    if (isCoil) {
        // Coils are bit-packed; expand each bit to its own uint16_t (0 or 1)
        for (int i = 0; i < count; i++) {
            outBuf[i] = (rx[3 + i / 8] >> (i % 8)) & 1u;
        }
    } else {
        // Registers: big-endian words → host uint16_t
        for (int i = 0; i < count; i++) {
            outBuf[i] = ((uint16_t)rx[3 + i * 2] << 8) | rx[3 + i * 2 + 1];
        }
    }
    return true;
}

// ---- Public: RTU writes ------------------------------------------

bool modbusRtuWriteSingleReg(uint8_t slaveId, uint16_t addr,
                              uint16_t value, uint16_t timeoutMs) {
    if (!_rtuReady) return false;

    uint8_t req[8];
    req[0] = slaveId;
    req[1] = MB_FC_WRITE_REGISTER;
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)(addr & 0xFF);
    req[4] = (uint8_t)(value >> 8);
    req[5] = (uint8_t)(value & 0xFF);
    uint16_t crc = modbusCrc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t rx[8]; // FC06 echo response is identical to the request (8 bytes)
    return _rtuExchange(req, 8, rx, 8, timeoutMs);
}

bool modbusRtuWriteCoil(uint8_t slaveId, uint16_t addr,
                         bool value, uint16_t timeoutMs) {
    if (!_rtuReady) return false;

    uint8_t req[8];
    req[0] = slaveId;
    req[1] = MB_FC_WRITE_COIL;
    req[2] = (uint8_t)(addr >> 8);
    req[3] = (uint8_t)(addr & 0xFF);
    req[4] = value ? 0xFF : 0x00; // ON = 0xFF00, OFF = 0x0000
    req[5] = 0x00;
    uint16_t crc = modbusCrc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    uint8_t rx[8]; // FC05 echo response is 8 bytes
    return _rtuExchange(req, 8, rx, 8, timeoutMs);
}

// ---- Public: value decoding --------------------------------------

float modbusDecodeValue(const ModbusRegister& reg, const uint16_t* rawRegs) {
    float    raw = 0.0f;
    uint16_t r0  = rawRegs[0]; // First register (or only register for 16-bit)
    uint16_t r1  = rawRegs[1]; // Second register (only valid for 32-bit types)

    switch (reg.dataType) {
        case MB_TYPE_UINT16:
            raw = (float)r0;
            break;
        case MB_TYPE_INT16:
            raw = (float)(int16_t)r0;
            break;
        case MB_TYPE_BIT:
            // Extract a single bit from the 16-bit register word
            raw = (float)((r0 >> (reg.bitIndex & 0x0Fu)) & 1u);
            break;
        case MB_TYPE_UINT32:
        case MB_TYPE_INT32:
        case MB_TYPE_FLOAT32: {
            // Assemble two 16-bit registers into a 32-bit word per byte order
            uint32_t w = 0;
            switch (reg.byteOrder) {
                case MB_BYTE_ORDER_ABCD: // AB CD — big-endian standard
                    w = ((uint32_t)r0 << 16) | r1;
                    break;
                case MB_BYTE_ORDER_CDAB: // CD AB — word-swapped
                    w = ((uint32_t)r1 << 16) | r0;
                    break;
                case MB_BYTE_ORDER_BADC: { // BA DC — byte-swapped words
                    uint16_t a = (uint16_t)(r0 >> 8) | (uint16_t)(r0 << 8);
                    uint16_t b = (uint16_t)(r1 >> 8) | (uint16_t)(r1 << 8);
                    w = ((uint32_t)a << 16) | b;
                    break;
                }
                case MB_BYTE_ORDER_DCBA: { // DC BA — little-endian
                    uint16_t a = (uint16_t)(r0 >> 8) | (uint16_t)(r0 << 8);
                    uint16_t b = (uint16_t)(r1 >> 8) | (uint16_t)(r1 << 8);
                    w = ((uint32_t)b << 16) | a;
                    break;
                }
            }
            if (reg.dataType == MB_TYPE_FLOAT32) {
                union { uint32_t u; float f; } conv = { w };
                raw = conv.f;
            } else if (reg.dataType == MB_TYPE_INT32) {
                raw = (float)(int32_t)w;
            } else {
                raw = (float)w;
            }
            break;
        }
    }
    return raw * reg.scale + reg.offset;
}

#endif // ENABLE_MODBUS==1
