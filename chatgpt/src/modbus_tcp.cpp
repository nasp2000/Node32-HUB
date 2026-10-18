// modbus_tcp.cpp
// Modbus TCP client implementation.
// Sends MBAP-framed PDUs over a TCP connection and parses responses.
// When ENABLE_ETHERNET==1, uses EthernetClient if the Ethernet link is up;
// falls back to WiFiClient otherwise.
// Each call opens a fresh TCP connection (connect, exchange, disconnect).
// This is intentionally simple — polling intervals are typically >= 1 s, so
// connection setup overhead (~20-50 ms) is negligible.

#include "modbus_tcp.h"
#include "feature_flags.h"

#if ENABLE_MODBUS==1 && ENABLE_MODBUS_TCP==1

#include <Arduino.h>
#include <WiFiClient.h>
#if ENABLE_ETHERNET==1
#include <Ethernet.h>
#endif

// ---- Internal: core MBAP exchange (abstract Client) ----------------

// Send MBAP+PDU over an already-connected Client and read the response PDU.
// Returns the number of PDU bytes received (>=1), or -1 on any error.
// respPdu buffer must be at least maxRespLen bytes.
static int _tcpExchangeWith(Client& cli, uint8_t slaveId,
                             const uint8_t* pdu, size_t pduLen,
                             uint8_t* respPdu, size_t maxRespLen,
                             uint16_t timeoutMs) {
    // Build MBAP header (7 bytes): TxID(2) + ProtoID(2=0) + Length(2) + UnitID(1)
    static uint16_t s_txId = 0;
    uint16_t txId    = ++s_txId;
    uint16_t mbapLen = 1 + (uint16_t)pduLen; // Unit ID byte + PDU

    uint8_t hdr[7];
    hdr[0] = (uint8_t)(txId    >> 8); hdr[1] = (uint8_t)(txId    & 0xFF);
    hdr[2] = 0x00;                    hdr[3] = 0x00; // Protocol ID
    hdr[4] = (uint8_t)(mbapLen >> 8); hdr[5] = (uint8_t)(mbapLen & 0xFF);
    hdr[6] = slaveId;

    cli.write(hdr, 7);
    cli.write(pdu, pduLen);
    cli.flush();

    // Read 7-byte response header
    unsigned long tEnd = millis() + timeoutMs;
    uint8_t rHdr[7] = {};
    for (uint8_t i = 0; i < 7; ) {
        if (millis() > tEnd) { cli.stop(); return -1; }
        if (cli.available()) rHdr[i++] = (uint8_t)cli.read();
        else delay(1);
    }

    // Validate: Protocol ID must be 0x0000, length >= 2 (unit + FC)
    if (rHdr[2] != 0x00 || rHdr[3] != 0x00) { cli.stop(); return -1; }
    uint16_t respLen    = ((uint16_t)rHdr[4] << 8) | rHdr[5];
    if (respLen < 2 || respLen > 256) { cli.stop(); return -1; }

    uint16_t pduRespLen = respLen - 1; // subtract Unit ID byte
    if (pduRespLen > (uint16_t)maxRespLen) { cli.stop(); return -1; }

    // Read PDU bytes
    for (uint16_t i = 0; i < pduRespLen; ) {
        if (millis() > tEnd) { cli.stop(); return -1; }
        if (cli.available()) respPdu[i++] = (uint8_t)cli.read();
        else delay(1);
    }
    cli.stop();

    // Modbus exception: high bit of FC set
    if (pduRespLen >= 1 && (respPdu[0] & 0x80)) return -1;
    return (int)pduRespLen;
}

// ---- Internal: transport dispatcher --------------------------------

// Open a fresh TCP connection then delegate to _tcpExchangeWith.
// When ENABLE_ETHERNET==1 and the Ethernet link is up, EthernetClient is used
// (Ethernet is the primary path). WiFiClient is the fallback.
static int _tcpExchange(const char* host, uint16_t port, uint8_t slaveId,
                         const uint8_t* pdu, size_t pduLen,
                         uint8_t* respPdu, size_t maxRespLen,
                         uint16_t timeoutMs) {
#if ENABLE_ETHERNET==1
    if (Ethernet.linkStatus() == LinkON) {
        EthernetClient eth;
        if (!eth.connect(host, port)) return -1;
        return _tcpExchangeWith(eth, slaveId, pdu, pduLen, respPdu, maxRespLen, timeoutMs);
    }
#endif
    WiFiClient wifi;
    if (!wifi.connect(host, (uint16_t)port, (int32_t)timeoutMs)) return -1;
    return _tcpExchangeWith(wifi, slaveId, pdu, pduLen, respPdu, maxRespLen, timeoutMs);
}

// ---- Public API ----------------------------------------------------

bool modbusTcpReadRegs(const ModbusDevice& dev, uint8_t fc,
                        uint16_t startAddr, uint8_t count,
                        uint16_t timeoutMs, uint16_t* outBuf) {
    if (!dev.tcpHost[0] || count == 0) return false;
    uint16_t port = dev.tcpPort ? dev.tcpPort : 502;

    uint8_t pdu[5] = {
        fc,
        (uint8_t)(startAddr >> 8), (uint8_t)(startAddr & 0xFF),
        0x00, count
    };

    // Max PDU response: FC(1) + ByteCount(1) + 2*count data bytes
    uint8_t rBuf[3 + 2 * MODBUS_MAX_REGISTERS] = {};
    int n = _tcpExchange(dev.tcpHost, port, dev.slaveId,
                          pdu, sizeof(pdu), rBuf, sizeof(rBuf), timeoutMs);
    if (n < 3) return false;

    if (fc == 0x01 || fc == 0x02) {
        // Coil/discrete: bit-packed in response
        for (uint8_t i = 0; i < count; ++i) {
            outBuf[i] = (rBuf[2 + i / 8] >> (i % 8)) & 1;
        }
        return true;
    }

    // FC03/FC04: raw 16-bit registers, big-endian
    if (n < 2 + 2 * count) return false;
    for (uint8_t i = 0; i < count; ++i) {
        outBuf[i] = ((uint16_t)rBuf[2 + i * 2] << 8) | rBuf[3 + i * 2];
    }
    return true;
}

bool modbusTcpWriteSingleReg(const ModbusDevice& dev, uint16_t addr,
                              uint16_t value, uint16_t timeoutMs) {
    if (!dev.tcpHost[0]) return false;
    uint16_t port = dev.tcpPort ? dev.tcpPort : 502;

    uint8_t pdu[5] = {
        0x06, // FC06 Write Single Register
        (uint8_t)(addr  >> 8), (uint8_t)(addr  & 0xFF),
        (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)
    };
    uint8_t rBuf[8] = {};
    int n = _tcpExchange(dev.tcpHost, port, dev.slaveId,
                          pdu, sizeof(pdu), rBuf, sizeof(rBuf), timeoutMs);
    return (n >= 5 && rBuf[0] == 0x06);
}

bool modbusTcpWriteCoil(const ModbusDevice& dev, uint16_t addr,
                         bool value, uint16_t timeoutMs) {
    if (!dev.tcpHost[0]) return false;
    uint16_t port = dev.tcpPort ? dev.tcpPort : 502;

    uint16_t coilVal = value ? 0xFF00 : 0x0000;
    uint8_t pdu[5] = {
        0x05, // FC05 Force Single Coil
        (uint8_t)(addr    >> 8), (uint8_t)(addr    & 0xFF),
        (uint8_t)(coilVal >> 8), (uint8_t)(coilVal & 0xFF)
    };
    uint8_t rBuf[8] = {};
    int n = _tcpExchange(dev.tcpHost, port, dev.slaveId,
                          pdu, sizeof(pdu), rBuf, sizeof(rBuf), timeoutMs);
    return (n >= 5 && rBuf[0] == 0x05);
}

#endif // ENABLE_MODBUS==1 && ENABLE_MODBUS_TCP==1
