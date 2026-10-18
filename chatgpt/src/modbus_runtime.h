// modbus_runtime.h
// Global in-memory Modbus device/register table.
// Loaded from ConfigManager on boot and after web config saves.
// Exposed to both the polling task and the web API handler.

#pragma once
#include "modbus_core.h"

#if ENABLE_MODBUS==1

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Global device array, sized for MODBUS_MAX_DEVICES slots.
// Populated by modbusLoadDevicesFromConfig() at boot and after web saves.
extern ModbusDevice  g_modbusDevices[MODBUS_MAX_DEVICES];
extern uint8_t       g_modbusDeviceCount;

// Mutex protecting g_modbusDevices and g_modbusDeviceCount.
// Held briefly during modbusLoadDevicesFromConfig() and during polling/slave iteration.
extern SemaphoreHandle_t g_modbusMutex;

// Read device definitions from configManager.config["modbus"]["devices"]
// and fill g_modbusDevices[]. Returns the number of devices loaded.
uint8_t modbusLoadDevicesFromConfig();

// Serialize g_modbusDevices[] back into configManager.config["modbus"]["devices"].
// You must call configManager.save() separately to persist.
void modbusSaveDevicesToConfig();

#else // ENABLE_MODBUS==0 — stubs

static inline uint8_t modbusLoadDevicesFromConfig() { return 0; }
static inline void    modbusSaveDevicesToConfig()    {}

#endif // ENABLE_MODBUS==1
