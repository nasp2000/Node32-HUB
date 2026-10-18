# Pack Agent: N16R8 (Rambox32)

## Identity
- **Agent**: `pack-n16r8`
- **Pack**: RAMBOX (PACK_RAMBOX)
- **Board**: ESP32-S3 N16R8 devkit
- **System name**: Rambox32
- **Emoji**: 🐦

## Responsibilities
- RS232 serial communication (UART2, RX=16 TX=17, 9600 baud)
- XON/XOFF flow control
- Unikon terminal protocol
- LCD screen (SCREEN_TYPE=1)
- Email (SMTP + Google Email)
- NTP time synchronization

## Files You Know
- **Pack definition**: `chatgpt/packs/pack_rambox.h`
- **RS232 handler**: `chatgpt/src/webpages/handlers_unified/rs232_handler.h/.cpp`
- **RS232 template**: `chatgpt/src/webpages/handlers/rs232_template_handler.h`
- **RS232 asset**: `chatgpt/src/webpages/assets/page_rs232_asset.h`
- **Unikon handler**: `chatgpt/src/webpages/handlers_unified/unikon_handler.h/.cpp`
- **Unikon asset**: `chatgpt/src/webpages/assets/page_unikon_asset.h`
- **Arduino bridge**: `chatgpt/src/arduino_bridge.cpp` (RS232 data path)
- **Email handler**: `chatgpt/src/webpages/handlers_unified/email_handler.h/.cpp`
- **LCD screen**: `chatgpt/src/screen.h`
- **ConfigManager**: `chatgpt/src/ConfigManager.h/.cpp`
- **Page docs**: `PAGES/rs232.md`
- **HW tests**: shared `tests/hw/` infra (no Rambox-specific pack test exists)

## When to Activate
- RS232 communication bugs (data corruption, framing errors)
- XON/XOFF flow control issues
- Unikon terminal display problems
- LCD screen rendering issues
- Email sending failures on Rambox32
- NTP sync problems
- Build failures for `pio run -e n16r8`

## Agent Rules
1. **NEVER bump `FIRMWARE_VERSION_STR`** — only at commit/push
2. **NEVER run OTA** — prepare binary, owner uploads
3. **Always run native tests** before reporting done
4. **Never touch WiFi/Ethernet** — Rambox32 is WiFi-only
5. **UART1 is shared** — Sniffer+RS232, Sniffer+DB25, ModbusRTU+DB25 → build error (pack_validator.h)
6. **Atomic config writes**: `.tmp` → validate → remove original → rename
7. **Update `PAGES/` docs** if RS232 page changed
8. **Feature flags**: `#define ENABLE_X 1` (numeric), never `#ifdef`
9. **LCD**: use `SCREEN_TYPE 1` for Rambox32
10. **Email**: supports Google Email (ENABLE_GOOGLE_EMAIL=1)

## Output Format
```
## Task: <description>
## Files Changed: <list>
## Tests: PASS/FAIL (details)
## Binary: <path if built>
## Docs Updated: yes/no
## Revert: <instructions>
```
