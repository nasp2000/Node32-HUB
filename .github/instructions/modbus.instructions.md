---
applyTo: "chatgpt/src/modbus_*,chatgpt/src/webpages/handlers/modbus_*,chatgpt/src/webpages/routes/web_routes_modbus*"
---

# Modbus Instructions

## Hardware constraints
- UART1 TX = GPIO 40, RX = GPIO 41 — shared with `ENABLE_DB25` and `ENABLE_SNIFFER`.
- `ENABLE_MODBUS_RTU=1` + `ENABLE_DB25=1` = compile error.
- `ENABLE_MODBUS_TCP` = no pin conflicts (WiFi/Ethernet only).

## Feature flags
```
ENABLE_MODBUS        master guard (auto-disabled if all sub-flags off)
ENABLE_MODBUS_RTU    UART1 RTU — conflicts with DB25/Sniffer
ENABLE_MODBUS_TCP    TCP client — no conflicts
ENABLE_MODBUS_SLAVE  responds to external masters
```
Guards: `#if ENABLE_MODBUS==1`, never `#ifdef`.

## Module map
| File | Purpose |
|------|---------|
| `modbus_core` | Types, UART1 init, CRC16, frame exchange |
| `modbus_poll` | FreeRTOS task (Core 0), polls devices |
| `modbus_tcp` | TCP client, MBAP framing |
| `modbus_slave` | RTU listener + TCP server |
| `modbus_runtime` | `g_modbusDevices[]`, JSON load/save |

## Data flow
```
ConfigManager JSON → modbusLoadDevicesFromConfig() → g_modbusDevices[]
                                                          ↓
                                            modbusPollBegin() (Core 0)
                                                          ↓
                              ModbusSdWriteFn → SD     ModbusRxInjectFn → monitor
```

## Config JSON (`configManager.config["modbus"]`)
```json
{"rtu":{"baud":9600,"parity":"N","stop_bits":1},"devices":[{"slave_id":1,"label":"PLC","transport":"rtu","poll_ms":1000,"registers":[{"address":40001,"fc":3,"data_type":0,"label":"Temp"}]}]}
```
Never modify `g_modbusDevices[]` directly from web handlers — use `g_modbusMutex`.

## Mutex: `g_modbusMutex` protects `g_modbusDevices[]`. Never hold during blocking I/O.

## Adding register type: add enum in `modbus_core.h` → decode in `modbus_core.cpp` → label in `modbus_api_handler.cpp`.
## Adding transport: add flag in `user_config.h` → dispatch in `modbus_poll.cpp`.
