# Node32-HUB — Agent Guide (short)

ESP32-S3/P4 industrial communication hub: RS232, LPT/DB25, Modbus RTU/TCP, CAN, GRBL, sensors (AHT20, MQ135, HX711, INA226, MAX31865, sound, proximity), email, scheduler, camera, radio, audio, web over WiFi or Ethernet (W5500). Packs (`packs/pack_*.h`) reconfigure per board (N16R8 class, Waveshare 8DI8DO, S3 TouchLCD-7, P4 CNC / USB-Server / CAM).

Design: modules independent (disabled at compile time = zero RAM), heap frugal (PSRAM + PROGMEM), no bloat.

## Build (owner runs, NOT agent)
```bash
cd chatgpt
pio run -e n16r8              # S3 N16R8
pio run -e waveshare_8di8do   # Waveshare 8DI8DO
pio run -e esp32p4            # P4 (RISC-V, ~5 min)
pio run -e esp32p4cam         # P4 CAM only (Arduino 3.3.12 dual-platform, see platformio.ini)
```

## Repo quirks (read before touching build/source layout)
- `src_dir = .` — project root IS the source tree. Unwanted dirs (`.pio/`, `.pio-linux/`, `.pio-p4cam/`, `build/`, some `managed_components/`) excluded via `build_src_filter` + pre-scripts. **`build/` must stay excluded**: it holds the `rename_firmware.py` output tree AND the generated LGPL bundle `build/third-party/lgpl-sources/**` (real `.cpp` — ESPAsyncWebServer examples, AsyncTCP, arduino-esp32); without `-<build/**>` PlatformIO compiles them and the build dies with `redefinition of 'class fs::File'`.
- `managed_components/` is auto-generated (IDF Component Manager), NOT in git.
- Pre-scripts run on every `pio run`: i18n validation, feature-flag validation, pack validation/injection, third-party + version validation, native tests (S3 envs), ccache, `fix_includes.py`. Post: `rename_firmware.py`. Manual: `scripts/format_check.py`, `scan_secrets.py`, etc.

## Feature flags
- Edit `chatgpt/user_config.h`. `src/feature_flags.h` is a relay — never add flags there.
- `#define ENABLE_X 1` (numeric), never `#ifdef`. Packs override per use-case (`SELECTED_PACK`).
- Every `.cpp` module needs `#if ENABLE_X==1` guard at top (`||` for deps). Core files (log/config/network) exempt.

## Packs
- `pack_*.h` define `PACK_SYSTEM_NAME`, `PACK_EMAIL_SUBJECT`, `PACK_EMOJI`, `PACK_BRAND_NAME`. Used by `ConfigManager.cpp` and `auto_js.h` (no if-else chains).
- Build-time validation: Sniffer+RS232, Sniffer+DB25, ModbusRTU+DB25 → error (UART1). Dependencies: USBIP→USB_HOST, MODBUS_*→MODBUS, etc.
- Telemetry/storage standard (all packs, present + future): `ARCHITECTURE.md` §6.1 — dual-sink identical paths, configured cadence, always-on PSRAM ring, merged API serving, cleanup-covered, email reads both. Consult it when adding any source/pack that writes files.
- **PINOUT.html at repo root is the single hardware truth** (boards, packs, GPIOs, buses, conflicts, wiring, 100% EN, short descriptions). Must be reviewed/updated in the same commit as any GPIO, bus, feature-flag or pack change.

## Display / screen (`chatgpt/src/screen/` views, core in `chatgpt/src/`)
- 3 layers: `display_profiles.h` (one profile per panel: geometry, VCC mode, pins, timings; `DISPLAY_*` registry + `activeDisplayProfile()`) → `screen.cpp` core in `chatgpt/src/` (`ScreenManager`: begin/update/scroll/blink/popups, pack-agnostic) → `view_*.h` (one status layout per pack) picked by `view_selector.h` via `ScreenStatusContext`.
- `screen.h` keeps the public API stable — external callers never include `screen/*` directly. All view/animation headers are header-only (`static inline`, no new build units).
- New panel: add a profile (+ a view only if the layout differs). New pack screen: add `view_<pack>.h` guarded by its `SELECTED_PACK` (same compile exposure as the old `#if` branch had) + one branch in `view_selector.h`.
- Never put pack `#if`s back into `screen.cpp` core; never add `.cpp` under `src/screen/` (headers only, so `build_src_filter` needs no changes).

## Web architecture
- 4 layers: `assets/` (PROGMEM) → `renderers/` → `handlers_unified/` (shared WiFi+Ethernet via `HttpRequest`/`HttpResponse`) → `routes/`.
- Register routes via `registerApiRoute("GET", "/api/foo", handleFoo)` — auto-registers WiFi+Ethernet. Do NOT edit `ethernet_bootstrap_routes.h` unless it's a streaming/body-accumulation route. MAX_API_ROUTES=256.
- `/api/configs` is frozen. New pages: dedicated endpoints, responses <4KB.
- UI: `customAlert`/`customConfirm` (never native), `d.success` not `d.ok`, no arrow functions (ESP32 browser compat), use `.key` not `.keyCode`. Home button = first child of `.wrap`.

## Page docs (`PAGES/`) — MANDATORY on any page change
- One file per web page: `PAGES/<page_name>.md` (English snake_case: `inactivity_alerts.md`, `email.md`, `proximity_sensors.md`). Central fix backlog: `PAGES/BACKLOG.md`.
- **Cross-page reference table** (`PAGES/WEB_PAGES_REFERENCE.md`): ONE table with every page — what it does, local-processing vs static (poll intervals), access rules per IP / per open page (session engine), which pages never kick by time, feature flags. **Consult it when wiring pages/access; keep it in sync on any page/route/poll/flag/access change** (same change, alongside `PAGES/<page>.md`).
- Fixed doc structure (all pages identical order): objective → access/routing → layout → inputs → buttons → client logic → server API → persistence → runtime engine → i18n keys → review findings table → source map.
- **RULE: any code change to a page (layout, buttons, inputs, handlers, routes, config schema) requires updating its `PAGES/<page>.md` in the same change.** New page ⇒ new doc.
- Issues found while auditing go to `PAGES/BACKLOG.md`; fixing a backlog item updates both code and the page doc.

## Language (code, not translations)
- Everything in code in English: identifiers, comments, logs, filenames, commits.
- Translations exempt: `lang_pt_ui.h`/`lang_en_ui.h` (full JSON PROGMEM, validated in sync at build). Firmware strings via `L(activeLang->field)`.
- HTML fallback text in English.

## Config & persistence
- ALL config via `ConfigManager` — never raw NVS. NVS only for boot-critical and secrets that must survive LittleFS issues (`cfg_wifi`, `cfg_saved_net`, SMTP password, plus segmented section mirrors). LittleFS for the rest (per-section files + independent sensor files, path `/config/<stem>.json`).
- **Atomic write** (all config): write `.tmp` → validate bytes>0 → remove original → rename. Never write final path directly.
- `littlefsBegin()` always (never `littlefsMounted()`). `saveAllConfigs()` is dead code. Prefer `markDirtyAndScheduleSave()`.
- Adding a config section: new stem → defaults in `initDefaults()` + backfill in `ensureAllFieldsExist()` + save via `saveSection(stem)` (save & load); files at `/config/<stem>.json`.

## Logging
- `sendVirtualMessage(F("msg"), LEVEL)` → Serial + Telnet + Web monitor.
- **RULE: every user-facing/status/log message MUST reach the Web Serial Monitor** (`/monitor`/`/serial-monitor`, reads `pushLogToPsram`). Prefer `sendVirtualMessage` for all of them.
- Do NOT call `sendVirtualMessage` in ways that drop the message from the monitor: never pass `level=DEBUG` for important messages, and when a message can repeat identically (e.g. a failing config save) pass `bypassDedup=true` so the monitor still shows each occurrence. Suppression/dedup in `logger_impl.h` (data-activity suppression + identical-message dedup) otherwise silently discards them.
- New error/status messages must be verified to appear on BOTH Serial and the Web Monitor, not just one.

## USB Host lessons (P4 + S3) — CRITICAL
- **Never add delays, power cycles, or openFailCount restarts to `usbHostBegin()`.** Regressed boards between v1.06 and HEAD; keep it simple (v1.06). Test on ALL available boards before changing.
- **S3 GPIO20 is NOT a strapping pin.** ~170ms power-up glitch pulls it HIGH, corrupting USB PHY with OTG connected. Hardware fix: 10kΩ GPIO20→GND. **Never use eFuse** (`DIS_USB_SERIAL_JTAG` bricks some S3 — ESP-IDF #13946). S3 has no root port power cycle support.

## Firmware versioning (MANDATORY on commit/push)
- NEVER bump during implementation. Only at commit/push.
- `FIRMWARE_VERSION_STR` in `chatgpt/src/smtp_core_impl.h` (currently v1.120) + `_hreg[HR_FW_VERSION]` in `chatgpt/src/modbus_slave.cpp` (numeric, no dots: 1120). Commit message includes version.

## Release gate (`release_check.sh`) — run before publishing
- One command, fail-fast, for the whole pre-release check: `./release_check.sh` (bench only) or `./release_check.sh --host <ip> [--packs waveshare,homee32] [--build] [--package] [--strict]`.
- Steps: **version** consistency (`validate_version.py`: `FIRMWARE_VERSION_STR` == `_hreg[HR_FW_VERSION]`) → **native** tests → **web** tests (jsdom, needs `node`; skipped if absent) → **i18n** audit (`audit_i18n.py`, non-fatal unless `--strict`) → **third-party** compliance (`validate_third_party.py --strict`) → optional **smoke build** (`smoke_build.py`) → optional **device audit** (`scripts/audit_device.py`, needs `--host`) → optional **pack persistence regression** (`tests/hw/run_pack_regression.sh`) → optional **package** (`package_release.py`).
- A FAIL means **do not publish** (and never OTA after a failed pack regression). SKIPs are honest gaps (no `node`, no `--host`, no `--build`) — review them.
- Version is bumped **manually, only on request**, at commit/push time (see "Firmware versioning"). The gate only *checks* it is consistent.
- Upgrade safety: `tests/hw/test_ota_upgrade.sh snapshot` (before) / `verify` (after) / `full --ota <bin>` checks that config + alert/OEE/shift schema survives an OTA from the previous version. Run it when the config schema changed.
- `validate_version.py`, `validate_third_party.py` also run automatically as `pio run` pre-scripts.

## Third-party licensing / compliance (MANDATORY on dependency or platform change)
- First-party code is **MIT** (`LICENSE`). The firmware **statically links LGPL-2.1/3.0** (arduino-esp32 core, AsyncTCP, ESPAsyncWebServer, CircularBuffer) plus MIT/BSD/Apache-2.0 libraries. LGPL requires shipping the library **source + relinkable objects + license texts**; MIT/BSD/Apache require their copyright/license notices. Nothing goes inside the `.bin` — the compliance material travels with the build folder.
- **Source of truth:** `SBOM.md` + `THIRD_PARTY_NOTICES` (library versions + licenses) and the generated `chatgpt/build/third-party/` bundle (git-ignored). The public release repo `REPOS/Node32-HUB/` keeps synced copies of `SBOM.md` + `THIRD_PARTY_NOTICES`.
- **Pin every `lib_deps` to an exact version — never `^` / `~`.** A caret range lets a fresh install or `pio pkg update` silently pull a newer release; ReadyMail did exactly that (`^0.4.0` resolved to 0.4.2, which switched MIT → CC-BY-NC-4.0 non-commercial). Git deps must carry a tag (`#vX.Y.Z`). ReadyMail is pinned to **0.4.0 (MIT)**; 0.4.1+ require a paid commercial licence.
- **Whenever a build changes** a library version, a `lib_deps` entry, the platform/core (arduino-esp32 / ESP-IDF), or a pack's feature set (which pulls new libraries), you MUST do all of this **in the same change**:
  1. Update the version/license rows in `SBOM.md` and `THIRD_PARTY_NOTICES` (and sync `REPOS/Node32-HUB/`).
  2. Regenerate the bundle after a successful build: `python chatgpt/scripts/package_third_party.py --env <board>` — repeat for **every** distributed board (objects are per architecture/env: S3 xtensa vs P4 RISC-V). (`rename_firmware.py` also refreshes it automatically per board during the build.)
  3. Verify sync: `python chatgpt/scripts/validate_third_party.py` (also runs automatically as a `pio run` pre-script; warnings are non-fatal, `--strict` fails).
- `chatgpt/rename_firmware.py` copies `LICENSE` + `THIRD_PARTY_NOTICES` + `SBOM.md` + `README_DISTRIBUTION.txt` into every build folder and packs `chatgpt/build/third-party/` into `<build>/third-party.zip` (removing the loose tree), so each build is distributable/sellable as-is (`DIST_LGPL_MATERIALS=0` skips the heavy LGPL trees). The bundle is **opt-in**: `build_firmware.sh` asks "Include third-party .zip" (default **N**) and only sets `DIST_THIRD_PARTY=1` on `y`/`--thirdparty`; without it no `third-party.zip` is written (so dev builds stay small — but you MUST include it before redistributing/selling a `.bin`).
- **Never** ship a `.bin` without the matching compliance bundle; never remove a library from the notices while it is still linked. Selling is allowed — LGPL permits commercial distribution with the obligations above; for high-stakes releases get a legal review (esp. LGPL relinking scope).

## Native tests (`tests/native/`, doctest, run before every build)
- Adding a suite: create `test_<name>.cpp` with `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`; register in BOTH `tests/native/Makefile` (TEST_SRCS + rule) and `chatgpt/scripts/run_native_tests.py` (TEST_BINS).
- 3 strategies: standalone reimpl (deep ESP32 deps), compile prod source (stubs cover deps — preferred, catches real bugs), extract header.
- Mocks available: `MockConfigStore`, `MockEmailService`, `MockLogger`, `stubs/Arduino.h`, `stubs/ArduinoJson.h`, `stubs/LittleFS.h`.
- Skip: `SKIP_NATIVE_TESTS=1`. Run: `python chatgpt/scripts/run_native_tests.py`.
- **Parallelism (MANDATORY):** `run_native_tests.py` builds with `make -j $(nproc)` (all cores) and `pio.sh` builds firmware with `-j $(nproc)`. NEVER limit to a fixed `-j4` or lower — use ALL available cores, otherwise builds/tests take forever on the Pi. Override only with `NATIVE_TEST_JOBS=N` / `PIO_JOBS=N` if RAM pressure causes OOM.

## Dev workflow (mandatory)
1. **Implement**: minimum change, match existing patterns, no new libraries.
2. **Logic tests always**: native C++ (doctest) + web JS tests, each with ≥2 variations. Test only what changed.
3. **Canvas/visual tests if UI changed**: projection math, rendering, view transforms, ≥2 variations.
4. **Full suite only on request**: `cd tests/web && node run_all.js` (node lives at `~/.local/bin/node` — absent from non-login PATH, use full path or `bash -lc`).
5. **Update the page doc** in `PAGES/` if the change touched a web page (see "Page docs").
6. **Log the work**: investigations, fixes, discoveries, decisions and HW findings go into `HISTORIC/<pack>/HISTORICO.md` (short entry, same session — see "Pack history" below).

## Agent artifacts layout (MANDATORY — no exceptions)
To keep the repo root clean, every agent-produced artifact goes in a dedicated folder. Never leave files in the repo root.
- **Agent briefs** (`_OPENCE_*.md`, instructions for opencode/big-pickle/other models): `agents/briefs/` (repo root). When launching `opencode run @_OPENCE_x.md`, point the path at `agents/briefs/_OPENCE_x.md`.
- **Agent process docs / specs** (e.g. `self-test.md` MASTER SPEC for HW/web testing): `agents/docs/` (repo root).
- **Pack history**: `HISTORIC/` (repo root, **git-ignored**, kept local) is the single source of truth for per-pack history: discoveries, issues, key decisions and relevant findings. One subfolder per pack, file `HISTORIC/<pack>/HISTORICO.md` (e.g. `HISTORIC/P4-CNC/HISTORICO.md`, `HISTORIC/Waveshare/HISTORICO.md`; folder names match the pack names, e.g. `P4-CNC`, `Waveshare`, `HomeE32`, `Pack Radio` — create the folder if a pack has no history yet). **MANDATORY: any agent work on a pack — investigations, fixes, new discoveries, decisions, HW findings — must be logged there by the LLM at session end** (short entry, same change/session, not later). Keep entries short: date, symptom, root cause, fix applied, binary/commit, revert instructions; drop stale detail on revision. Do NOT create history files in `agents/docs/`, `SHARE/` or other repo folders.
- **HW / integration tests**: `tests/hw/` (repo root). Shared lib: `tests/hw/lib_persist.sh`. Per-pack real tests live in `tests/packs/<pack>/test_real_<pack>.sh` (e.g. `waveshare/`, `homee32/`, `p4/`, `harmonica/`, `cam/`; `sim_*` are run_pack_regression keys, not folders). Each folder also holds that pack's API test scripts, `TESTE_API_*.md`, plans, and results (migrated from `SHARE/`). Do NOT duplicate tests into `SHARE/` — SHARE is for Nuno's working docs only.
- **Pack regression runner**: `tests/hw/run_pack_regression.sh` (runs all `tests/packs/*/test_real_*.sh` after a build; exits 1 if any pack loses persistence → "do NOT OTA"). Per-pack folders (`cam/`, `harmonica/`, `homee32/`, `p4/`, `waveshare/`) hold `test_real_<pack>.sh` plus that pack's API test scripts, `TESTE_API_*.md`, plans and results (consolidated here from the old `agent_docs/pack_tests/`).
- **Hardware / board guides** for humans (e.g. `px_8di8do.md`): `docs/` (repo root), not agent folders.
- Native tests stay in `tests/native/` (see "Native tests" section).
- Do NOT write test scripts, briefs, specs, or board guides anywhere else in the repo root.

## Docs (detail lives here)
`ARCHITECTURE.md`, `CONTRIBUTING.md`, `SECURITY.md`, `CHANGELOG.md`, `.github/instructions/`, `.github/prompts/`, `.github/copilot-instructions.md` (reconcile conflicts with this file).
