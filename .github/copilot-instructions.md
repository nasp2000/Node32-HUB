# Node32-HUB — Copilot / AI Instructions

**Multi-purpose ESP32-S3/P4 industrial hub**: RS232, LPT/DB25, Modbus, CAN, GRBL, sensors,
email, scheduler, camera, radio, audio, web (WiFi + W5500).
Packs per board. Modules independent — off at compile time = zero RAM. Heap frugal, PSRAM for big allocs.

**Chat**: PT. **Code/commits/comments**: EN, no emojis.

---

## Mandatory Rules

### 1. Home page — hide disabled sections
Never show module links with `ENABLE_*=0`.

### 2. Save pattern (ALL pages)
`/auto.js` + `/config-manager.js`.
Success: `customAlert(d.message || window._t('alertSaved','✅ Configuration saved!'))` + `showSavedCard()` (EN fallback in HTML, PT from the `alertSaved` pack key).
Error: `customAlert('❌ Error: ' + message)` (translated via the `sensorAlertError` pack key where available).
Never `showMsg()`, `alert()`, `d.ok` → `d.success`.
Confirm: `customConfirm()` — never native `confirm()`.

### 3. WiFi ↔ Ethernet parity (NON-NEGOTIABLE)
The **handler logic** is unified in `handlers_unified/` — shared by WiFi and Ethernet.
The **routing** is still separate. Each new endpoint requires 3 steps:
- **Handler**: `src/webpages/handlers_unified/` (single, shared)
- **WiFi route**: `src/webpages/routes/web_routes_*.cpp`
- **Ethernet route + wrapper**: `ethernet_bootstrap_routes.h` + `ethernet_bootstrap_handlers.h`

### 4. LittleFS atomic write (NO EXCEPTIONS)
`.tmp` → `bytes>0` → `remove(original)` + `rename(.tmp, original)`.
Never `LittleFS.open(finalPath, "w")`.

### 5. `/api/configs` frozen
New pages create their own endpoint (<4KB).

### 6. Local processing pages (`target='_blank'`)
`sensor-charts`, `grbl-controller`, `serial-monitor`, `camera`, `radio-ui`, `buffer-viewer`.
Open with `target='_blank'` on home. Exempt via the `charts_exempt_sessions` flag.

### 7. Home button
`<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Home</a></div>` — first child of `.wrap` (EN fallback; PT `btnHome` comes from the packs).

### 8. 4-layer web split
Asset → Renderer → Handler → Route. `webpage_impl.h` is a thin coordinator.

### 9. No GPIO/pin text in web pages.

### 10. ConfigManager — regras
- `saveAllConfigs()` is dead code. Prefer `markDirtyAndScheduleSave()`.
- New field: `initDefaults()` → `ensureAllFieldsExist()` → getter/setter → web handler → if boot-critical: NVS backup → `LAST GLOBAL CHANGE`.
- Never raw NVS keys.
- `littlefsBegin()` always (never `littlefsMounted()` — async mount).

### 11. Pin docs (MANDATORY)
Changed GPIO → update the pinout material under `docs/` (human board guides).

### 12. Build
**Never compile** (owner builds locally). `pio run -e n16r8 | waveshare_8di8do | s3_touch7 | esp32p4 | esp32p4cam`.

### 13. Versioning
Only at commit/push. Bump `FIRMWARE_VERSION_STR` + `_hreg[HR_FW_VERSION]`.

---

## Quick Reference

| Concept | How |
|----------|------|
| Logging | `sendVirtualMessage(F("msg"), LEVEL)` |
| PROGMEM page | `const char PAGE_X[] PROGMEM = R"rawliteral(...)rawliteral";` |
| Route guard | `ensureBasicAuth()` + `allowHtmlClient()` |
| i18n firmware | `L(activeLang->field)` |
| i18n UI | `data-i18n="key"` + `/api/lang` |
| Config GET | `GET /api/config?section=<...>` |
| Config POST | `POST /api/config` com `section=` no body |
| LittleFS mount | `littlefsBegin()` |
| Atomic write | `.tmp` → validate → `remove` + `rename` |

## Dependencies
`ESPAsyncWebServer`, `AsyncTCP`, `ArduinoJson`, `CircularBuffer`, `Adafruit SSD1306+GFX+Unified Sensor` (transitive dep), `Ethernet`, `SdFat`, `ReadyMail` (+ `ESP_SSLClient`), s3_touch7/P4 extras: lvgl stack, `ESP_Video` (bundled with core 3.3.12).

## Prompts (load as needed)
- New module: `.github/prompts/new-module.prompt.md`
- New config field: `.github/prompts/new-config-field.prompt.md`
- New recipient rule: `.github/prompts/new-recipient-rule.prompt.md`
- Crash diagnosis: `.github/prompts/debug-diagnosis.prompt.md`

## See also
- `AGENTS.md` — detailed rules (i18n, config files, web architecture, LittleFS recovery)
- `.github/instructions/` — per-module rules
