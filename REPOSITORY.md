# Node32-HUB Repository Structure

## GitHub: https://github.com/nasp2000/Node32-HUB

### Tracked (essential for compilation)

```
Node32-HUB-S3-N16R8/
├── .gitignore
├── .github/                       # AI coding instructions & prompts
│   ├── copilot-instructions.md
│   ├── instructions/              # Module-specific rules
│   └── prompts/                   # Prompt templates
├── chatgpt/
│   ├── .gitignore
│   ├── .dummy/                    # IDF Component Manager trigger
│   │   ├── idf_component.yml
│   │   ├── CMakeLists.txt
│   │   └── sketch.cpp + 3 files
│   ├── boards.local.txt           # Arduino IDE board overrides
│   ├── build_opt.h                # Compilation options
│   ├── chatgpt.ino                # Main Arduino sketch
│   ├── CMakeLists.txt             # CMake project (IDF)
│   ├── dependencies.lock          # Locked IDF dependency versions
│   ├── fix_includes.py            # Pre-build script
│   ├── packs/                     # Board pack profiles (8 files)
│   │   ├── pack_selector.h
│   │   ├── pack_rambox.h, pack_cam.h, pack_psram.h, ...
│   ├── partitions.csv             # Flash partition table
│   ├── platform.local.txt         # Arduino IDE platform overrides
│   ├── platformio.ini             # Build configuration
│   ├── rename_firmware.py         # Post-build script
│   ├── sdkconfig.defaults         # IDF sdkconfig defaults
│   ├── sdkconfig.defaults.p4      # P4-specific IDF overrides
│   ├── scripts/
│   │   └── ccache.py              # Compilation cache helper
│   ├── src/                       # Source code (~172 files)
│   │   ├── *.cpp, *.h             # Runtime modules
│   │   ├── webpages/              # Web UI + handlers
│   │   ├── sensors/               # Sensor drivers (AHT20, MQ135, etc.)
│   │   └── languages/             # i18n (EN, PT)
│   ├── user_config.h              # User configuration
│   └── util/
│       └── delay.h
```

### NOT tracked (auto-generated)

| Path | Reason |
|---|---|
| `.pio/` | PlatformIO build cache |
| `build/` | Compiled binaries |
| `managed_components/` | Downloaded IDF components (regenerated on `pio run`) |
| `.vscode/` | Editor configuration |
| `sdkconfig.esp32p4` | Generated IDF config |
| `.build_firmware.last` | Last build state |
| `build_firmware.bat` | Local build script |
| `flash_firmware.bat` | Local flash script |
| `util/` | Local auxiliary scripts (Rambox debug, cables) |
| `*.bin`, `*.elf`, `*.map` | Build artifacts |
| `Docs/`, `Others/` | Non-essential documentation |

### Building

```bash
cd chatgpt
pio run -e n16r8              # N16R8 devkit (ESP32-S3)
pio run -e waveshare_8di8do   # Waveshare 8DI8DO POE
pio run -e esp32p4            # ESP32-P4
```
PlatformIO automatically downloads dependencies (`managed_components/`, `lib/`) on first build.

On the Linux build host the menu wrappers mirror the Windows `chatgpt/*.bat`
scripts and live at the repo root:

```bash
./build_firmware.sh              # build menu (all boards/packs; CAM_P4 uses its
                                 # own ~/.platformio-p4cam core dir + .pio-p4cam tree)
                                 # asks "Include third-party .zip" (default N)
./flash_firmware.sh              # esptool flasher (update / full / erase+full)
./flash_firmware.sh --list       # list flashable build folders
```

### Public mirror sync (Node32-HUB-MAIN → Node32-HUB)

The public repo receives a filtered copy of this tree:

- Excluded by `.gitignore` (never leave this machine): `agents/`,
  `.opencode/agents/`, `HISTORIC/`, build outputs, `tests/web` weight.
- `README.md` is owned by the public repo — every sync restores the remote
  version (`git checkout -- README.md`) before committing, so this tree can
  never overwrite or delete it.
