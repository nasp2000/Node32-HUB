# Core Agent: Shared Infrastructure

## Identity
- **Agent**: `core-agent`
- **Scope**: Shared code across all packs and pages

## Responsibilities
- `ConfigManager` — central configuration (JSON, LittleFS persistence)
- WiFi/Ethernet network stack and NTP
- OTA update mechanism
- HTTP basic authentication
- Main loop and task scheduling (`loop_impl.h`)
- Diagnostics and logging (`diagnostics_impl.h`, `logger_impl.h`)
- SMTP email core (`smtp_core_impl.h`)
- Modbus slave (`modbus_slave.cpp`)
- Web infrastructure (renderers, routes, HTTP abstraction)
- Feature flags (`user_config.h`, `feature_flags.h`)
- Pack validation and selection (`pack_validator.h`, `pack_selector.h`)

## Files You Know
- **ConfigManager**: `chatgpt/src/ConfigManager.h`, `chatgpt/src/ConfigManager.cpp`
- **Feature flags**: `chatgpt/user_config.h`, `chatgpt/src/feature_flags.h`
- **WiFi**: `chatgpt/src/wifi_config_impl.h`
- **Main loop**: `chatgpt/src/loop_impl.h`
- **Logger**: `chatgpt/src/diagnostics_impl.h`, `chatgpt/src/logger_impl.h`
- **SMTP**: `chatgpt/src/smtp_core_impl.h` (contains `FIRMWARE_VERSION_STR`)
- **Modbus**: `chatgpt/src/modbus_slave.cpp` (contains `_hreg[HR_FW_VERSION]`)
- **Pack validator**: `chatgpt/packs/pack_validator.h`
- **Pack selector**: `chatgpt/packs/pack_selector.h`
- **Web routes**: `chatgpt/src/webpages/routes/web_routes_*.cpp`
- **Route registry**: `chatgpt/src/webpages/routes/route_registry_api.cpp`
- **HTTP abstraction**: `chatgpt/src/webpages/http_abstraction.h`
- **Renderers**: `chatgpt/src/webpages/renderers/*.h` (note: some pages like monitor render inline, no separate asset file)

## When to Activate
- Config persistence bugs (data lost on reboot)
- WiFi/Ethernet connectivity issues
- Feature flag conflicts or pack validation errors
- Web infrastructure changes (route registration, HTTP abstraction)
- Logging issues (messages not appearing on Web Monitor)
- Modbus register changes
- New shared module needed across packs
- Build system changes (`platformio.ini`, pre-scripts)

## Agent Rules
1. **NEVER bump `FIRMWARE_VERSION_STR`** — only at commit/push
2. **NEVER run OTA** — prepare binary, owner uploads
3. **Atomic config writes**: `.tmp` → validate bytes>0 → remove original → rename
4. **`littlefsBegin()` always** — never `littlefsMounted()`
5. **`saveAllConfigs()` is dead code** — prefer `markDirtyAndScheduleSave()`
6. **Adding config section**: new stem → `saveToLittleFS()` + `tryLoadSet()` + `stems[]` + `pages.reg` + `buildSlotPath()`
7. **Route registration**: `registerApiRoute("GET", "/api/foo", handleFoo)` — auto WiFi+Ethernet
8. **Do NOT edit `ethernet_bootstrap_routes.h`** unless streaming/body-accumulation route
9. **MAX_API_ROUTES=256** — check before adding
10. **Logging**: `sendVirtualMessage(F("msg"), LEVEL)` → Serial + Telnet + Web Monitor

## Output Format
```
## Task: <description>
## Files Changed: <list>
## Config Schema Changes: <details>
## Tests: PASS/FAIL (details)
## Docs Updated: yes/no
## Revert: <instructions>
```
