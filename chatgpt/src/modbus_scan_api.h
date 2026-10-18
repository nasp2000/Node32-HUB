#pragma once
// modbus_scan_api.h
// Thin public interface for the Modbus RTU/TCP scan engine.
// Declared here so both the WiFi and Ethernet web stacks can start/stop/query scans
// without including ESPAsyncWebServer headers.
// Implementations live in web/routes/web_routes_modbus.cpp (inlined).

#include <Print.h>
#include "feature_flags.h"

#if ENABLE_MODBUS==1

// Start an RTU bus scan (background FreeRTOS task on core 0).
// Returns false if a scan is already running.
bool modbusApiStartRtuScan(uint8_t startId, uint8_t endId, uint16_t timeoutMs);

// Start a TCP subnet scan. subnet = "192.168.1" (without trailing dot).
// Returns false if a scan is already running.
bool modbusApiStartTcpScan(const char* subnet, uint8_t startIp, uint8_t endIp, uint16_t timeoutMs);

// Abort a running scan (RTU or TCP).
void modbusApiScanStop();

// Write the current scan status as JSON to any Print target.
void modbusApiWriteScanStatusJson(Print& out);

#else

// Stubs when Modbus is disabled.
inline bool modbusApiStartRtuScan(uint8_t, uint8_t, uint16_t) { return false; }
inline bool modbusApiStartTcpScan(const char*, uint8_t, uint8_t, uint16_t) { return false; }
inline void modbusApiScanStop() {}
inline void modbusApiWriteScanStatusJson(Print& out) {
    out.print(F("{\"running\":false,\"current\":0,\"total\":0,\"found\":[]}"));
}

#endif // ENABLE_MODBUS==1
