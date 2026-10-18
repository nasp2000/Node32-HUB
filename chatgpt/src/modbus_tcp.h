// modbus_tcp.h
// Modbus TCP client — wraps Modbus PDU in MBAP header, sends over TCP.
// Transport selection (ENABLE_ETHERNET==1): uses EthernetClient when the
// Ethernet link is up, WiFiClient otherwise.
// One fresh TCP connection per exchange (simple, robust for polling intervals).
// Only active when ENABLE_MODBUS==1 && ENABLE_MODBUS_TCP==1.

#pragma once
#include "feature_flags.h"
#include "modbus_core.h"

#if ENABLE_MODBUS==1 && ENABLE_MODBUS_TCP==1

// Read registers or coils over Modbus TCP (FC01-FC04), blocking.
// dev.tcpHost and dev.tcpPort (default 502) are used for the connection.
// outBuf must point to at least 'count' uint16_t elements.
// FC01/02: outBuf[i] = 0 or 1 per coil/discrete.
// FC03/04: outBuf[i] = raw 16-bit word, big-endian to host.
bool modbusTcpReadRegs(const ModbusDevice& dev, uint8_t fc,
                        uint16_t startAddr, uint8_t count,
                        uint16_t timeoutMs, uint16_t* outBuf);

// Write single holding register FC06 over Modbus TCP, blocking.
bool modbusTcpWriteSingleReg(const ModbusDevice& dev, uint16_t addr,
                              uint16_t value, uint16_t timeoutMs);

// Force single coil FC05 over Modbus TCP, blocking.
bool modbusTcpWriteCoil(const ModbusDevice& dev, uint16_t addr,
                         bool value, uint16_t timeoutMs);

#else // ENABLE_MODBUS_TCP==0 or ENABLE_MODBUS==0 — stubs

static inline bool modbusTcpReadRegs(const void*, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t*) { return false; }
static inline bool modbusTcpWriteSingleReg(const void*, uint16_t, uint16_t, uint16_t) { return false; }
static inline bool modbusTcpWriteCoil(const void*, uint16_t, bool, uint16_t) { return false; }

#endif // ENABLE_MODBUS==1 && ENABLE_MODBUS_TCP==1
