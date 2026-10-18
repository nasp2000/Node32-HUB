// modbus_slave.h
// Modbus slave mode — respond to FC01-FC06 from external RTU/TCP masters.
// Exposes internal sensor readings and Modbus master re-exported values
// as holding registers readable via FC03.
// Only compiled when ENABLE_MODBUS==1 AND ENABLE_MODBUS_SLAVE==1.
//
// Register map (holding registers, FC03 read):
//   0x0000  Device ID constant (0x4E33 = "N3" for Node32-HUB)
//   0x0001  Firmware version integer (e.g. 224 for v2.24)
//   0x0002  Uptime high word (seconds >> 16)
//   0x0003  Uptime low word (seconds & 0xFFFF)
//   0x0010  AHT20 temperature x10 as INT16  (e.g. 25.4 C -> 254)
//   0x0011  AHT20 humidity    x10 as UINT16 (e.g. 65.3% -> 653)
//   0x0012  BMP280 temperature x10 as INT16
//   0x0013  BMP280 pressure integer hPa as UINT16 (e.g. 1013)
//   0x0014  MQ135 CO2 estimate (ppm) as UINT16
//   0x0020..0x005F  Modbus master re-export: device[0..3] x 16 regs each
//                   addr = 0x0020 + (deviceIndex * 16) + registerIndex
//                   value = INT16(reg.lastValue) -- integer truncation
//   0x0064..0x00AF  Proximity channels (ENABLE_PROXIMITY==1 required; else zero)
//                   Per channel i (0..7): base = 0x0064 + i*10
//                   base+0  count_in  (UINT16, wraps at 65535)
//                   base+1  count_out (UINT16)
//                   base+2  net stock = count_in - count_out (INT16)
//                   base+3  goal      (UINT16)
//                   base+4  rate_ppm x10 (UINT16, e.g. 12.3 ppm -> 123)
//                   base+5  state 0=no-object 1=object-present (UINT16)
//
// Coil map (FC01 read, FC05 write for non-read-only coils):
//   0x0000  Heartbeat  -- toggles every 500 ms, read-only
//   0x0001  AHT20 initialized
//   0x0002  BMP280 available
//   0x0003  MQ135 initialized
//
// NOTE: RTU slave uses UART1 (same as RTU master). Running both master and
// slave on RTU simultaneously on the same bus may cause conflicts. In practice,
// use TCP slave together with RTU master, or use RTU slave in slave-only mode.

#pragma once
#include "feature_flags.h"
#include "modbus_core.h"
#include <stdint.h>

#if ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1

// Start the Modbus slave FreeRTOS task (Core 1, priority 1).
// slaveId  -- own Modbus address (1-247)
// rtuEn    -- listen on UART1 for RTU requests
// tcpEn    -- start WiFiServer on tcpPort for TCP requests
// dePin    -- GPIO for DE/RE control; -1 = auto-direction TTL module
// baud     -- UART1 baud rate (used to calculate inter-frame gap)
// tcpPort  -- TCP server port (default 502)
void modbusSlaveBegin(uint8_t slaveId, bool rtuEn, bool tcpEn,
                      int8_t dePin, uint32_t baud, uint16_t tcpPort = 502);

// Stop the slave task and release all resources.
void modbusSlaveEnd();

// True if the slave task is currently running.
bool modbusSlaveIsRunning();

// Write a holding register directly from firmware (sensor/proximity updates).
// addr must be < SLAVE_HREG_COUNT; writes outside that range are ignored.
void modbusSlaveSetHreg(uint16_t addr, uint16_t value);

#else // ENABLE_MODBUS==0 or ENABLE_MODBUS_SLAVE==0 -- empty stubs

static inline void modbusSlaveBegin(uint8_t, bool, bool, int8_t, uint32_t, uint16_t = 502) {}
static inline void modbusSlaveEnd()       {}
static inline bool modbusSlaveIsRunning() { return false; }
static inline void modbusSlaveSetHreg(uint16_t, uint16_t) {}

#endif // ENABLE_MODBUS==1 && ENABLE_MODBUS_SLAVE==1
