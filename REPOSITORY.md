# Node32-HUB Repository Structure

## GitHub: https://github.com/nasp2000/Node32-HUB

### Tracked (essential for compilation)

```
Node32-HUB/
├── .gitignore                       # also ignores: agents/, HISTORIC/, build outputs, tests/web weight
├── .github/                         # AI coding instructions & prompts
│   ├── copilot-instructions.md
│   ├── instructions/                # Module-specific rules (13 files)
│   └── prompts/                     # Prompt templates (5 files)
├── chatgpt/
│   ├── .gitignore                   # ota_private.pem, /build/, /build_out/, *.bat live here untracked
│   ├── .dummy/                      # IDF Component Manager trigger (currently disabled via .disabled suffix)
│   │   ├── idf_component.yml.disabled
│   │   ├── CMakeLists.txt
│   │   └── sketch.cpp + 3 arduino-lib-builder stubs
│   ├── boards.local.txt           # Arduino IDE board overrides
│   ├── build_opt.h                # Compilation options
│   ├── chatgpt.ino                # Main Arduino sketch
│   ├── fix_includes.py            # Pre-build script (repo root, runs for every env)
│   ├── packs/                     # Board pack profiles (13 headers)
│   │   ├── pack_selector.h
│   │   ├── pack_rambox.h, pack_cam.h, pack_psram.h, ...
│   ├── partitions.csv             # Flash partition table
│   ├── platform.local.txt         # Arduino IDE platform overrides
│   ├── platformio.ini             # Build environments (n16r8, waveshare_8di8do, s3_touch7, esp32p4, esp32p4cam)
│   ├── rename_firmware.py         # Post-build script
│   ├── sdkconfig.defaults         # IDF sdkconfig defaults
│   ├── sdkconfig.defaults.p4      # P4-specific IDF overrides
│   ├── scripts/                   # Python build & quality scripts (21 files: validate_*, run_native_tests, ccache, ...)
│   ├── src/                       # Source code (781 files)
│   │   ├── *.cpp, *.h             # Runtime modules
│   │   ├── webpages/              # Web UI + handlers
│   │   ├── sensors/               # Sensor drivers (AHT20, MQ135, HX711, INA226, MAX31865, sound, proximity, ...)
│   │   └── languages/             # i18n (EN, PT)
│   └── user_config.h              # User configuration (feature flags live here)
├── tests/
│   ├── native/                    # C++ unit tests (sources only; *.o and *_stub.* untracked)
│   ├── web/                       # jsdom tests + fixtures (node_modules/, logs/, shots/ untracked)
│   ├── hw/                        # HW integration scripts (no hardcoded device IPs)
│   └── packs/                     # Per-pack API tests and plans
├── PAGES/                         # One doc file per web page (mandatory on page changes)
├── docs/                          # Human board guides — local only, NOT tracked
└── *.sh                           # build_firmware.sh, flash_firmware.sh, ota.sh, pio.sh, release_check.sh
```

### NOT tracked (local only — never pushed)

| Path | Reason |
|---|---|
| `chatgpt/.pio/`, `.pio-linux/`, `.pio-p4cam/` | PlatformIO per-env build trees |
| `chatgpt/build/` | Compiled firmware outputs |
| `chatgpt/managed_components/` | Downloaded IDF components (regenerated on `pio run`) |
| `chatgpt/*.bat`, `chatgpt/util/` | Local Windows scripts / aux scripts |
| `chatgpt/ota_private.pem` | OTA signing private key — never leaves this machine |
| `chatgpt/sdkconfig.esp32p4` | Generated IDF config |
| `.build_firmware.last` | Last build state |
| `agents/`, `.opencode/agents/` | Local agent instructions and briefs |
| `HISTORIC/` | Per-pack work history (local by design) |
| `REPOS/`, `Others/` | Release mirrors / scratch working docs |
| `docs/` | Human board guides (local) |
| `tests/web/node_modules/`, `tests/web/logs/`, `tests/web/shots/` | Test weight and generated output |
| `tests/native/*.o`, `tests/native/*_stub.*` | Compiled test artifacts |
| `*.bin`, `*.elf`, `*.map` | Build artifacts |
| `*.log`, `.tmp` | Logs and temp files |

### Building

```bash
./build_firmware.sh -e n16r8 -p RAMBOX   # build menu (all boards/packs)
cd chatgpt && pio run -e n16r8          # ...or raw PlatformIO

# Boards: n16r8 | waveshare_8di8do | s3_touch7 | esp32p4 | esp32p4cam
# Packs : NONE RAMBOX CAM RS232 ENVIRONMENT RADIO S3_CNC WAVESHARE S3_TOUCH7 P4_CNC P4_USB_SERVER CAM_P4
```
PlatformIO automatically downloads dependencies (`managed_components/`, `lib/`) on first build.

CAM_P4 builds in an isolated Arduino 3.3.12 world (own `~/.platformio-p4cam` core dir + `.pio-p4cam` tree); everything else stays on 55.03.35.

### Repo hygiene (public repo — hard rules)

- The old private history (agent instructions, personal emails, build binaries) must never be pushed here. Old `testing`/`main` branches from the private remote are gone — work on `main` going forward.
- `README.md` is curated — don't overwrite it with generated content.
- Before any `git push`, confirm no removed file type is coming back: `git log --all -- agents/ HISTORIC/ | head`, `git ls-files | grep -E '\.(o|bin)$|ota_private'`.
