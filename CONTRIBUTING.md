# Contributing to Node32-HUB

## How to add a new module

1. **Register the flag**: Add `#define ENABLE_MODULENAME 1` to `user_config.h`. Do **not** edit `feature_flags.h` — it is a relay only.
2. **Create source files** in `chatgpt/src/`. Guard every `.cpp` with `#if ENABLE_MODULENAME==1` at the top. Header guards are fine without it (included only when enabled).
3. **Config persistence**:
   - **One-off settings**: Add a new stem to `ConfigManager` — create the slot in `saveToLittleFS()` + `tryLoadSet()` + `stems[]` + `buildSlotPath()` calls, then register in `/config/pages.reg`.
   - **Sensor-style settings**: Create a dedicated config manager (e.g. `SensorFooConfigManager`) with atomic `.tmp` + rename writes.
4. **Web UI**: Create PROGMEM asset in `webpages/assets/`, handler in `handlers_unified/`, route registration in `routes/web_routes_*.cpp` via `registerApiRoute()`. See "How to add a new web page" below.
5. **i18n**: Add translation keys to both `lang_pt_ui.h` and `lang_en_ui.h`. The build will fail if they drift (validated by `scripts/validate_i18n.py`).

## How to add a new web page

1. **Asset**: Create `page_mypage_asset.h` with `const char PAGE_MYPAGE[] PROGMEM = R"HTML(...)HTML";` — no `<!DOCTYPE>`, `<html>`, `<head>`, `<body>`; those are injected by `streamPageShellStart()`.
2. **Handler**: Create `handlers_unified/mypage_handler.h` and `mypage_handler.cpp` with signature `void handleMyPage(HttpRequest& req, HttpResponse& res)`.
3. **Route**: Call `registerApiRoute("GET", "/mypage", handleMyPage)` in `routes/web_routes_*.cpp`.
4. **Required elements**:
   - Home button: `<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Início</a></div>` as first child of `.wrap`.
   - `customAlert()` / `customConfirm()` — never native `alert()` / `confirm()`.
   - Save success: `customAlert('✅ Configuração guardada!')` + `showSavedCard()`.
   - Save error: `customAlert('❌ Erro: ' + message)`.
5. **No inline styles** — use CSS classes.
6. **Polling pages**: If the page uses continuous JS/polling, add it to the `charts_exempt_sessions` list (see AGENTS.md).

## How to add a new sensor

1. **Interface**: Implement `ISensor` (see existing sensors for reference).
2. **Config**: Create a config manager for sensor-specific settings in `/config/sensor_<name>.json`, saved atomically (`.tmp` + rename).
3. **Data injection**: Use `processLine` or a dedicated API endpoint.
4. **Web UI**: Add a configuration page + chart integration. Register routes and handlers as described above.
5. **Guard**: `#if ENABLE_SENSOR_FOO==1` on all source files.

## How to add a new pack

1. **Create pack header**: `packs/pack_mypack.h`. Define:
   - `PACK_SYSTEM_NAME` — system name (SD folders, email subject prefix)
   - `PACK_EMAIL_SUBJECT` — email subject (defaults to `PACK_SYSTEM_NAME`)
   - `PACK_EMOJI` — emoji for web UI branding
   - `PACK_BRAND_NAME` — brand name if different from system name (optional)
2. **Register**: Add `#include "pack_mypack.h"` to `packs/pack_selector.h`.
3. **Flags**: Override feature flags as needed for the target hardware.
4. **Validation**: The pack must pass rules in `packs/pack_validator.h` — hardware conflicts (sniffer+RS232, sniffer+DB25, ModbusRTU+DB25) and dependency chains (e.g. USBIP→USB_HOST) are checked at build time.

## Code style rules

- **English only**: identifiers, comments, log messages, commit messages, filenames — all English. Translations are the sole exception (`lang_pt_ui.h` / `lang_en_ui.h`).
- **Feature flags**: Use `#define ENABLE_X 1` (numeric). Never `#ifdef ENABLE_X`. Check with `#if ENABLE_X==1`.
- **Module guard**: Every `.cpp` file must start with `#if ENABLE_X==1` (or combined `#if ENABLE_A==1 || ENABLE_B==1`). Core files (log, config, network) are exempt.
- **No arrow functions**: Use `function(x){}` — ESP32 browsers don't support arrow functions.
- **No native alerts**: Use `customAlert()` and `customConfirm()` exclusively.
- **KeyboardEvent**: Use `.key`, never `.keyCode` (deprecated).
- **No inline styles**: HTML assets must use CSS classes; no `style=` attributes.
- **No comments in code** unless the logic is genuinely non-obvious.

## Commit & versioning rules

- **Never bump the version during implementation** — only at commit/push time.
- **Single source of truth**: `FIRMWARE_VERSION_STR` in `chatgpt/src/smtp_core_impl.h`.
- **Format**: `v<major>.<minor><patch>` — e.g. v1.087.
- **Modbus slave**: Also update `_hreg[HR_FW_VERSION]` in `chatgpt/src/modbus_slave.cpp` — numeric, no dots (e.g. 1087).
- **Commit message**: Include the version, e.g. `firmware v1.087`.

## Pack history (MANDATORY)

When investigating or resolving a pack problem, update the history in `HISTORIC/<pack>/HISTORICO.md` (e.g. `HISTORIC/P4-CNC/HISTORICO.md`, `HISTORIC/Waveshare/HISTORICO.md`).

**Single source of truth**: Do NOT create history files in `agents/docs/` or other repo folders — the single source of truth is `HISTORIC/<pack>/HISTORICO.md`.

Each entry must have:
- Date
- Symptom
- Root cause
- Fix applied
- Binary/commit reference
- Revert instructions

**Revert example**: If a workaround causes issues, include clear steps to revert.

## Build & test

```bash
# Build firmware
cd chatgpt && pio run -e n16r8

# Skip native tests (they run automatically on build)
SKIP_NATIVE_TESTS=1 pio run -e n16r8

# Run native tests standalone
python chatgpt/scripts/run_native_tests.py

# Format check (run before commit)
python scripts/format_check.py --fix

# Other checks
python scripts/lint_comments.py
python scripts/scan_secrets.py
```

**Pre-commit hooks**: Install with `git config core.hooksPath .githooks`. They run format check and native tests automatically.

**Build variants**:
| Board | Command |
|---|---|
| ESP32-S3 N16R8 | `pio run -e n16r8` |
| Waveshare 8DI8DO | `pio run -e waveshare_8di8do` |
| ESP32-P4 | `pio run -e esp32p4` |

The P4 build excludes Xtensa/x86 assembly automatically via `remove_arm_asm.py`. Native tests are Xtensa-only and skipped on P4.

## Questions?

Open an issue or refer to `AGENTS.md` (the comprehensive agent guide) and `.github/instructions/` (per-module rules).
