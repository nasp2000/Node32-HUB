#pragma once
#include <Arduino.h>

// RTU config page: RTU serial settings + RTU device/register editor.
extern const char PAGE_MODBUS_RTU[] PROGMEM;

// TCP config page: TCP device editor + subnet scan.
extern const char PAGE_MODBUS_TCP[] PROGMEM;

// Live data page: auto-refreshing register values for all devices.
extern const char PAGE_MODBUS_LIVE[] PROGMEM;
