#pragma once
#include <Arduino.h>

// Stream the Modbus RTU config page to a Print target.
size_t streamModbusRtuPageToPrint(Print& out);

// Stream the Modbus TCP config page to a Print target.
size_t streamModbusTcpPageToPrint(Print& out);

// Stream the Modbus live data page (auto-refresh register viewer) to a Print target.
size_t streamModbusLivePageToPrint(Print& out);
