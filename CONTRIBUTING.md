# Contributing to Node32-HUB

## New contributors: start here

1. Fork the repo and clone your fork — or open it in a Codespace / VS Code with the Dev Containers extension (Python, PlatformIO, Node.js and C++ tools come preinstalled, see `.devcontainer/`).
2. Local setup (without Docker): Python 3 + PlatformIO (`pip install platformio`), Node.js 22 (for `tests/web`), `g++` + `make` (for native tests).
3. First build: `./build_firmware.sh -e n16r8 -p RAMBOX` (or `pio run --project-dir chatgpt -e n16r8`).
4. Before a PR: run the native tests (`python chatgpt/scripts/run_native_tests.py`) and the web tests (`cd tests/web && npm ci && node run_all.js`); update the `PAGES/<page>.md` doc if you touched a web page.
5. Open the PR against `main` — CI builds all 11 packs and runs both test suites automatically.

## How to add a new module

1. **Register the flag**: Add `#define ENABLE_MODULENAME 1` to `chatgpt/user_config.h`. Do **not** edit `src/feature_flags.h` - it is a relay only.
2. **Create source files** in `chatgpt/src/`. Guard every `.cpp` with `#if ENABLE_MODULENAME==1` at the top. Header guards are fine without it (included only when enabled).
3. **Config persistence**:
   - **One-off settings**: Add the section in `ConfigManager` (`chatgpt/src/ConfigManager.{h,cpp}`) - defaults in `initDefaults()`, backfill in `ensureAllFieldsExist()`, save via `saveSection(stem)` / `markDirtyAndScheduleSave()`; files at `/config/<stem>.json`, atomic `.tmp` -> rename writes. See `AGENTS.md` ("Config & persistence").
   - **Sensor-style settings**: Same `ConfigManager` pattern (own `/config/sensor_<name>.json` stem), or a dedicated manager reusing the atomic `.tmp` -> rename writes.
4. **Web UI**: Create PROGMEM asset in `chatgpt/src/webpages/assets/`, handler in `chatgpt/src/webpages/handlers_unified/`, route registration in `chatgpt/src/webpages/routes/web_routes_*.cpp` - `server.on("/mypage", ...)` + `buildPageFrame()` for HTML pages, `registerApiRoute()` only for `/api/*` endpoints. See "How to add a new web page" below.
5. **i18n**: Add translation keys to both `lang_pt_ui.h` and `lang_en_ui.h`. The build fails if they drift (validated by `chatgpt/scripts/validate_i18n.py`).

## How to add a new web page

1. **Asset**: Create `page_mypage_asset.h` with `const char PAGE_MYPAGE[] PROGMEM = R"rawliteral(...)rawliteral";` - no `<!DOCTYPE>`, `<html>`, `<head>`, `<body>`; those are injected by `buildPageFrame()` / `streamPageShellStart()`.
2. **Handler**: Create `chatgpt/src/webpages/handlers_unified/mypage_handler.h` and `mypage_handler.cpp` with signature `void handleMyPage(HttpRequest& req, HttpResponse& res)`.
3. **Route**: HTML pages use `server.on("/mypage", ...)` in `chatgpt/src/webpages/routes/web_routes_*.cpp` (see `web_routes_bricon.cpp`); REST endpoints use `registerApiRoute("GET", "/api/mypage", handleMyPage)`.
4. **Required elements**:
   - Home button: `<div class='actions nav-inicio'><a href='/' class='btn' data-i18n='btnHome'>&#127968; Home</a></div>` as first child of `.wrap` (English fallback text; PT comes from the language packs).
   - `customAlert()` / `customConfirm()` — never native `alert()` / `confirm()`.
   - Save feedback via i18n keys (e.g. `bt('...BtnSave', ...)` / `t('...ToastConfigSaved', ...)`), never hardcoded Portuguese strings.
   - Error feedback the same way (i18n key + server `message`), never hardcoded text.
5. **Styling**: Page assets are self-contained - shared `core.css` classes plus a `<style>` block and inline styles in the asset; no per-page CSS files.
6. **Polling pages**: If the page uses continuous JS/polling, check the session-expiry rules for local vs static pages (`PAGES/WEB_PAGES_REFERENCE.md`, `window._localPagesNeverExpire`).

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
- **Styling**: Page assets are self-contained (shared `core.css` + page `<style>` block and inline styles); no per-page CSS files.
- **Comments**: English only (enforced by `lint_comments.py`); concise, no noise.

## Commit & versioning rules

- **Never bump the version during implementation** — only at commit/push time.
- **Single source of truth**: `FIRMWARE_VERSION_STR` in `chatgpt/src/smtp_core_impl.h`.
- **Format**: `v<major>.<minor><patch>` - e.g. v1.120.
- **Modbus slave**: Also update `_hreg[HR_FW_VERSION]` in `chatgpt/src/modbus_slave.cpp` — numeric, no dots (e.g. 1087).
- **Commit message**: Include the version only when bumping it (e.g. `v1.120: ...`); otherwise describe the change.

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
python chatgpt/scripts/format_check.py --fix

# Other checks
python chatgpt/scripts/lint_comments.py
python chatgpt/scripts/scan_secrets.py
```

**Pre-commit hooks**: Install with `git config core.hooksPath .githooks`. They run scan_secrets, lint_comments, validate_i18n, validate_packs, validate_feature_flags and format check automatically (native tests run at build time, not in the hook).

**Build variants**:
| Board | Command |
|---|---|
| ESP32-S3 N16R8 | `pio run -e n16r8` |
| Waveshare 8DI8DO | `pio run -e waveshare_8di8do` |
| ESP32-P4 | `pio run -e esp32p4` |

The P4 env excludes Xtensa/x86 assembly via `remove_arm_asm.py` (`extra_scripts` in `platformio.ini`). Native tests run automatically on build unless `SKIP_NATIVE_TESTS=1`.

## Questions?

Open an issue or refer to `AGENTS.md` (the comprehensive agent guide) and `.github/instructions/` (per-module rules).
