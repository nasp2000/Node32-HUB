// modbus_poll.h
// Modbus RTU master polling task — schedules reads for all configured devices,
// retries on error, and updates in-memory register state.
// SD/PSRAM logging uses a callback set by the host at init time.

#pragma once
#include "feature_flags.h"
#include "modbus_core.h"

#if ENABLE_MODBUS==1

// Callback for pushing a logged line to the SD/PSRAM data pipeline.
// source: e.g. "RS232", "LPT", "MODBUS" — determines the SD subfolder.
typedef void (*ModbusSdWriteFn)(const char* source, const char* line, size_t len, unsigned long timestamp);

// Callback for injecting a logged line into the web RX ring buffer (bufferDados).
// Called from the polling task (Core 0) after every successful register read when logToSd=true.
// Signature matches snifferInjectLine(const char* line, size_t len).
typedef void (*ModbusRxInjectFn)(const char* line, size_t len);

// Set the callbacks used by the polling task.
// sdWrite:   SD/PSRAM pipeline (optional, may be nullptr to skip SD logging).
// rxInject:  RX ring-buffer injection for "Receção de Dados" page (optional, may be nullptr).
// Must be called before modbusPollBegin() if logging is desired.
void modbusPollSetCallbacks(ModbusSdWriteFn sdWrite, ModbusRxInjectFn rxInject = nullptr);

// Start the FreeRTOS polling task on Core 0.
// devices: pointer to array of MODBUS_MAX_DEVICES structs (owned by caller).
// count:   number of entries that are in use.
// modbusRtuBegin() must have been called successfully before this.
void modbusPollBegin(ModbusDevice* devices, uint8_t count);

// Stop the polling task and wait for it to exit.
void modbusPollStop();

// True while the polling task is running.
bool modbusPollIsRunning();

// Request an out-of-schedule poll for a specific device (by index in the devices array).
// Safe to call from a web handler or command thread; the poll task picks it up within 50 ms.
void modbusPollRequestImmediate(uint8_t deviceIndex);

#else // ENABLE_MODBUS==0 — stubs

typedef void (*ModbusSdWriteFn)(bool, const char*, size_t, unsigned long);
typedef void (*ModbusRxInjectFn)(const char*, size_t);
static inline void modbusPollSetCallbacks(ModbusSdWriteFn /*sdWrite*/, ModbusRxInjectFn /*rxInject*/ = nullptr) {}
static inline void modbusPollBegin(void*, uint8_t) {}
static inline void modbusPollStop() {}
static inline bool modbusPollIsRunning() { return false; }
static inline void modbusPollRequestImmediate(uint8_t) {}

#endif // ENABLE_MODBUS==1
