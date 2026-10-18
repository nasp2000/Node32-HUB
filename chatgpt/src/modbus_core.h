// modbus_core.h
// Modbus RTU master — types, UART1 init, CRC16, blocking frame exchange.
// UART1 (GPIO40 TX / GPIO41 RX) is shared with the NanoBridge; it is only
// available when ENABLE_DB25=0.  All real code is behind ENABLE_MODBUS==1.
// TCP and Slave modes are reserved for future phases.

#pragma once
#include "feature_flags.h"
#include <stdint.h>  // uint8_t, uint16_t, uint32_t — needed by both enabled and disabled stubs
#include <stddef.h>  // size_t

#if ENABLE_MODBUS==1

#include <Arduino.h>
#include <stdint.h>

// Default UART1 pin assignments — reuse NanoBridge TX/RX when ENABLE_DB25=0
#ifndef GPIO_MB_TX
  #define GPIO_MB_TX  40
#endif
#ifndef GPIO_MB_RX
  #define GPIO_MB_RX  41
#endif

// ---- Enumerations ------------------------------------------------

enum ModbusFunctionCode : uint8_t {
    MB_FC_READ_COILS           = 0x01, // Read coils (output bits)
    MB_FC_READ_DISCRETE        = 0x02, // Read discrete inputs (input bits)
    MB_FC_READ_HOLDING         = 0x03, // Read holding registers
    MB_FC_READ_INPUT           = 0x04, // Read input registers
    MB_FC_WRITE_COIL           = 0x05, // Force single coil
    MB_FC_WRITE_REGISTER       = 0x06, // Write single holding register
    MB_FC_WRITE_MULTIPLE_COILS = 0x0F, // Write multiple coils
    MB_FC_WRITE_MULTIPLE_REGS  = 0x10, // Write multiple holding registers
};

enum ModbusDataType : uint8_t {
    MB_TYPE_UINT16  = 0, // Unsigned 16-bit integer (1 register)
    MB_TYPE_INT16   = 1, // Signed 16-bit integer (1 register)
    MB_TYPE_UINT32  = 2, // Unsigned 32-bit integer (2 registers)
    MB_TYPE_INT32   = 3, // Signed 32-bit integer (2 registers)
    MB_TYPE_FLOAT32 = 4, // IEEE 754 single-precision float (2 registers)
    MB_TYPE_BIT     = 5, // Single bit extracted from a 16-bit register
};

enum ModbusByteOrder : uint8_t {
    MB_BYTE_ORDER_ABCD = 0, // Big-endian — Modbus standard (most PLCs)
    MB_BYTE_ORDER_CDAB = 1, // Word-swapped big-endian
    MB_BYTE_ORDER_BADC = 2, // Byte-swapped little-endian
    MB_BYTE_ORDER_DCBA = 3, // Little-endian
};

// ---- Data structures ---------------------------------------------

// Single register/coil mapped to a physical Modbus address
struct ModbusRegister {
    uint16_t           address;    // Register address (0-based, sent as-is in frame)
    ModbusFunctionCode fc;         // Function code to use when reading
    ModbusDataType     dataType;   // How to interpret raw 16-bit words
    ModbusByteOrder    byteOrder;  // Byte/word order for 32-bit types
    uint8_t            bitIndex;   // Bit to extract (0-15); only for MB_TYPE_BIT
    char               label[32];  // Human-readable name
    char               unit[8];    // Unit suffix (e.g. "°C", "bar", "V")
    float              scale;      // Multiplied with raw decoded value
    float              offset;     // Added after scale
    bool               logToSd;    // Push each read to SD/PSRAM data pipeline
    bool               logPretty;  // If true, log "label: value unit"; else CSV

    // Runtime state — NOT persisted
    float    lastValue;   // Last decoded value after scale+offset
    uint32_t lastReadMs;  // millis() when last read completed
    bool     lastOk;      // True if last read succeeded
};

#define MODBUS_MAX_REGISTERS 16 // Max registers per device
#define MODBUS_MAX_DEVICES    4 // Max devices polled simultaneously

// One Modbus device (slave) to poll
struct ModbusDevice {
    uint8_t  slaveId;                             // Modbus slave ID 1-247
    char     name[32];                            // Display name
    bool     enabled;                             // When false, skip polling
    bool     isTcp;                               // true = TCP; false = RTU
    char     tcpHost[64];                         // IP or hostname (TCP only)
    uint16_t tcpPort;                             // TCP port, default 502
    uint32_t pollIntervalMs;                      // How often to poll (ms)
    uint8_t  retries;                             // Max retry attempts per register
    uint16_t timeoutMs;                           // Response timeout per exchange (ms)
    ModbusRegister registers[MODBUS_MAX_REGISTERS];
    uint8_t  registerCount;

    // Runtime counters — NOT persisted
    uint32_t pollCount;   // Total successful register reads
    uint32_t errorCount;  // Total failed register reads
    uint32_t lastPollMs;  // millis() of last poll cycle start
};

// RTU physical layer configuration
struct ModbusRtuConfig {
    uint32_t baud;      // Baud rate (e.g. 9600, 19200, 115200)
    uint8_t  dataBits;  // 7 or 8
    char     parity;    // 'N'=none, 'E'=even, 'O'=odd
    uint8_t  stopBits;  // 1 or 2
    int8_t   dePin;     // GPIO for manual DE/RE control; -1 = auto-direction TTL module
};

// ---- Public API --------------------------------------------------

// Initialise UART1 for RTU master; must be called before any read/write.
void     modbusRtuBegin(const ModbusRtuConfig& cfg);

// Close UART1 and release resources.
void     modbusRtuEnd();

// True after a successful modbusRtuBegin().
bool     modbusRtuIsReady();

// Read registers or coils (FC01-FC04), blocking with timeout.
// outBuf must point to at least 'count' uint16_t elements.
// For coil FCs (01/02): outBuf[i] = 0 or 1 per coil.
// For register FCs (03/04): outBuf[i] = raw 16-bit word, big-endian → host.
bool     modbusRtuReadRegs(uint8_t slaveId, uint8_t fc,
                            uint16_t startAddr, uint8_t count,
                            uint16_t timeoutMs, uint16_t* outBuf);

// Write single holding register FC06, blocking.
bool     modbusRtuWriteSingleReg(uint8_t slaveId, uint16_t addr,
                                  uint16_t value, uint16_t timeoutMs);

// Force single coil FC05, blocking.
bool     modbusRtuWriteCoil(uint8_t slaveId, uint16_t addr,
                             bool value, uint16_t timeoutMs);

// Decode raw register words into a float, applying scale and offset.
// rawRegs[0] = first register, rawRegs[1] = second (required for 32-bit types).
float    modbusDecodeValue(const ModbusRegister& reg, const uint16_t* rawRegs);

// CRC16 using Modbus polynomial 0xA001.
uint16_t modbusCrc16(const uint8_t* data, size_t len);

#else // ENABLE_MODBUS==0 — stubs so unconditional #include compiles cleanly

struct ModbusRegister {
    char     label[32];
    char     unit[8];
    float    lastValue;
    uint32_t lastReadMs;
    bool     lastOk;
    float    scale;
    float    offset;
    uint8_t  bitIndex;
};
struct ModbusDevice {
    uint8_t  slaveId;
    char     name[32];
    bool     enabled;
    uint32_t pollCount;
    uint32_t errorCount;
    uint32_t lastPollMs;
    uint16_t timeoutMs;
    uint8_t  retries;
    uint32_t pollIntervalMs;
    uint8_t  registerCount;
    ModbusRegister registers[16];
};
struct ModbusRtuConfig { uint32_t baud; uint8_t dataBits; char parity; uint8_t stopBits; int8_t dePin; };
enum ModbusFunctionCode : uint8_t { MB_FC_READ_COILS=0x01, MB_FC_READ_DISCRETE=0x02, MB_FC_READ_HOLDING=0x03, MB_FC_READ_INPUT=0x04, MB_FC_WRITE_COIL=0x05, MB_FC_WRITE_REGISTER=0x06, MB_FC_WRITE_MULTIPLE_COILS=0x0F, MB_FC_WRITE_MULTIPLE_REGS=0x10 };
enum ModbusDataType     : uint8_t { MB_TYPE_UINT16=0, MB_TYPE_INT16=1, MB_TYPE_UINT32=2, MB_TYPE_INT32=3, MB_TYPE_FLOAT32=4, MB_TYPE_BIT=5 };
enum ModbusByteOrder    : uint8_t { MB_BYTE_ORDER_ABCD=0, MB_BYTE_ORDER_CDAB=1, MB_BYTE_ORDER_BADC=2, MB_BYTE_ORDER_DCBA=3 };
#define MODBUS_MAX_REGISTERS 16
#define MODBUS_MAX_DEVICES    4

static inline void     modbusRtuBegin(const ModbusRtuConfig&) {}
static inline void     modbusRtuEnd() {}
static inline bool     modbusRtuIsReady() { return false; }
static inline bool     modbusRtuReadRegs(uint8_t,uint8_t,uint16_t,uint8_t,uint16_t,uint16_t*) { return false; }
static inline bool     modbusRtuWriteSingleReg(uint8_t,uint16_t,uint16_t,uint16_t) { return false; }
static inline bool     modbusRtuWriteCoil(uint8_t,uint16_t,bool,uint16_t) { return false; }
static inline float    modbusDecodeValue(const ModbusRegister&, const uint16_t*) { return 0.0f; }
static inline uint16_t modbusCrc16(const uint8_t*, size_t) { return 0; }

#endif // ENABLE_MODBUS==1
