# Node32-HUB — Build & Flash Instructions

Requires Python 3 + [PlatformIO](https://platformio.org/); all dependencies download automatically on the first build.

## Build

```bash
./build_firmware.sh                          # interactive menu: pick board, pack, build
./build_firmware.sh -e n16r8 -p RAMBOX       # direct build
./build_firmware.sh -e waveshare_8di8do -p WAVESHARE
./build_firmware.sh --all                    # all boards, default packs
```

Boards: `n16r8` | `waveshare_8di8do` | `s3_touch7` | `esp32p4` | `esp32p4cam`

Artifacts land in `chatgpt/build/<board>/<pack>/<version>/<timestamp>/`. Windows users run the equivalent `chatgpt/build_firmware.bat`.

## Flash

```bash
./flash_firmware.sh                          # interactive: pick build, .bin, mode, port
./flash_firmware.sh --list                   # list flashable builds
./flash_firmware.sh --dir chatgpt/build/esp32s3/WAVESHARE/<ver>/<ts> --mode 2 --port /dev/ttyACM0 --yes
```

Modes: `1` update (firmware only, keeps config) · `2` full install (bootloader + partitions + firmware) · `3` erase + full. Default baud `921600`.
