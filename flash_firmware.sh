#!/usr/bin/env bash
# ============================================================================
# Node32-HUB — Linux firmware flasher (equivalent of chatgpt/flash_firmware.bat)
#
# Flow: choose build folder -> choose .bin -> flash mode -> serial port.
#
# Usage (interactive):
#   ./flash_firmware.sh
# Usage (CLI, no prompts):
#   ./flash_firmware.sh --dir <build/.../timestamp> [--bin <file.bin>]
#                       [--mode 1|2|3] [--port /dev/ttyACM0] [--baud 921600] [--yes]
#   ./flash_firmware.sh --list            list flashable build folders, exit
#
# Modes:
#   1  Update            firmware.bin only (keeps bootloader/partitions/config)
#   2  Full install      bootloader.bin + partitions.bin + firmware.bin
#   3  Erase + full      erase_flash, then bootloader + partitions + firmware
#
# Artifacts come from chatgpt/build/<board>/<pack>/<version>/<timestamp>/,
# written by rename_firmware.py (see build_firmware.sh). Bootloader offset is
# 0x0000 on ESP32-S3 and 0x2000 on ESP32-P4 (Arduino core defaults).
# ============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="$HERE/chatgpt/build"
SCRIPT="$HERE/$(basename "${BASH_SOURCE[0]}")"
PY="${PYTHON:-python3}"

# Partition layout (partitions.csv) — LittleFS storage region.
STORAGE_OFFSET="0xA10000"
STORAGE_SIZE="0x5E0000"
STORAGE_SIZE_DEC="6160384"
PARTITIONS_OFFSET="0x8000"
APP_OFFSET="0x10000"

say() { echo "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# ----------------------------------------------------------------------------
# esptool resolution: PlatformIO penv (most reliable) -> PATH -> python3.
# Result lands in the ESPTOOL array (command + base args).
# ----------------------------------------------------------------------------
resolve_esptool() {
  ESPTOOL=()
  local cand py
  for py in "$HOME/.platformio/penv/bin/python" "$HOME/.platformio-p4cam/penv/bin/python"; do
    [[ -x "$py" ]] || continue
    if "$py" -m esptool version >/dev/null 2>&1; then
      ESPTOOL=("$py" -m esptool)
      return 0
    fi
  done
  if command -v esptool.py >/dev/null 2>&1 && esptool.py version >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
    return 0
  fi
  if command -v esptool >/dev/null 2>&1 && esptool version >/dev/null 2>&1; then
    ESPTOOL=(esptool)
    return 0
  fi
  if command -v "$PY" >/dev/null 2>&1 && "$PY" -m esptool version >/dev/null 2>&1; then
    ESPTOOL=("$PY" -m esptool)
    return 0
  fi
  return 1
}

# chip_from_bin <file> -> esp32s3 | esp32p4 | esp32
chip_from_bin() {
  local base prefix
  base="$(basename "$1")"
  prefix="${base%%_*}"
  case "$prefix" in
    esp32p4|esp32p4cam) echo "esp32p4" ;;
    esp32s3|esp32)      echo "esp32s3" ;;
    n16r8|waveshare_8di8do|s3_touch7) echo "esp32s3" ;;
    *) echo "esp32s3" ;;
  esac
}

# bootloader_offset <chip>
bootloader_offset() {
  [[ "$1" == "esp32p4" ]] && echo "0x2000" || echo "0x0000"
}

# ----------------------------------------------------------------------------
# Discovery helpers
# ----------------------------------------------------------------------------
# Flashable build folders = contain a .bin that is not bootloader/partitions.
list_build_dirs() {
  find "$BUILD_ROOT" -type f -name '*.bin' \
    ! -name 'bootloader.bin' ! -name 'partitions.bin' -printf '%h\n' 2>/dev/null \
    | sort -u -r
}

# Firmware bins inside a folder (newest first).
list_bins() {
  find "$1" -maxdepth 1 -type f -name '*.bin' \
    ! -name 'bootloader.bin' ! -name 'partitions.bin' -printf '%f\n' 2>/dev/null \
    | sort -r
}

list_ports() {
  ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true
}

find_mklittlefs() {
  local p
  for p in "$HOME/.platformio/packages/tool-mklittlefs/mklittlefs" \
           "$HOME/.platformio-p4cam/packages/tool-mklittlefs/mklittlefs"; do
    [[ -x "$p" ]] && { echo "$p"; return 0; }
  done
  command -v mklittlefs 2>/dev/null || true
}

# ----------------------------------------------------------------------------
# esptool runner + retry ladder (mirrors the .bat behaviour)
# ----------------------------------------------------------------------------
run_esptool() {
  say "  $ ${ESPTOOL[*]} $*"
  "${ESPTOOL[@]}" "$@"
}

# do_write <chip> <port> <baud> <before> <write-args...>
do_write() {
  local chip="$1" port="$2" baud="$3" before="$4"; shift 4
  run_esptool --chip "$chip" --before "$before" --after hard-reset \
    --port "$port" --baud "$baud" write-flash "$@" >/tmp/node32_flash_last.log 2>&1
}

# do_erase <chip> <port> <baud> <before> [--no-stub]
do_erase() {
  local chip="$1" port="$2" baud="$3" before="$4"; shift 4
  run_esptool --chip "$chip" --before "$before" --after hard-reset \
    --port "$port" --baud "$baud" "$@" erase_flash >/tmp/node32_flash_last.log 2>&1
}

report_fail() {
  grep -iE "Failed to get PID|Write timeout|SerialException|A fatal error occurred" \
    /tmp/node32_flash_last.log 2>/dev/null || true
}

# flash_write <chip> <port> <no_stub_allowed> <write-args...>
flash_write() {
  local chip="$1" port="$2" no_stub="$3"; shift 3
  local ns=(); [[ "$no_stub" == "1" ]] && ns=(--no-stub)

  say "------------------------------------------------------------"
  say "Quick attempt (usb-reset, 115200)..."
  if do_write "$chip" "$port" 115200 usb-reset "${ns[@]}" "$@"; then
    say "OK: quick attempt completed."; return 0
  fi
  report_fail

  local b
  for b in 115200 460800 921600; do
    say "------------------------------------------------------------"
    say "Flash attempt at baud $b (default-reset)..."
    if do_write "$chip" "$port" "$b" default-reset "${ns[@]}" "$@"; then
      say "OK: flash completed at baud $b."; return 0
    fi
    report_fail
  done

  say "------------------------------------------------------------"
  say "Extra no-button attempt (usb-reset, 115200)..."
  if do_write "$chip" "$port" 115200 usb-reset "${ns[@]}" "$@"; then
    say "OK: extra no-button attempt completed."; return 0
  fi
  report_fail

  say "------------------------------------------------------------"
  say "Auto recovery: retry with no-reset (115200)..."
  if do_write "$chip" "$port" 115200 no-reset "${ns[@]}" "$@"; then
    say "OK: no-reset flash completed."; return 0
  fi
  report_fail
  return 1
}

# erase_chip <chip> <port> <no_stub_allowed>
erase_chip() {
  local chip="$1" port="$2" no_stub="$3"
  local ns=(); [[ "$no_stub" == "1" ]] && ns=(--no-stub)
  local b

  say "------------------------------------------------------------"
  say "Erasing flash..."
  for b in 115200 460800 921600; do
    if do_erase "$chip" "$port" "$b" usb-reset "${ns[@]}"; then
      say "OK: erase completed at baud $b (usb-reset)."; return 0
    fi
    report_fail
  done
  for b in 115200 460800 921600; do
    if do_erase "$chip" "$port" "$b" default-reset "${ns[@]}"; then
      say "OK: erase completed at baud $b (default-reset)."; return 0
    fi
    report_fail
  done
  if [[ "$no_stub" == "1" ]]; then
    for b in 115200 460800 921600; do
      if do_erase "$chip" "$port" "$b" usb-reset --no-stub; then
        say "OK: erase completed at baud $b (usb-reset + no-stub)."; return 0
      fi
      report_fail
    done
  fi
  if do_erase "$chip" "$port" 115200 no-reset "${ns[@]}"; then
    say "OK: no-reset erase completed."; return 0
  fi
  report_fail
  return 1
}

# storage_image -> prints a freshly built empty LittleFS image path (or nothing)
storage_image() {
  local mk img tmpdir
  mk="$(find_mklittlefs)"
  [[ -n "$mk" ]] || return 0
  tmpdir="$(mktemp -d)"
  img="$(mktemp --suffix=.bin)"
  if "$mk" -c "$tmpdir" -s "$STORAGE_SIZE_DEC" "$img" >/dev/null 2>&1; then
    echo "$img"
  else
    rm -f "$img"
  fi
  rm -rf "$tmpdir"
}

usage() { sed -n '2,22p' "$SCRIPT" | sed 's/^# \{0,1\}//'; }

# ----------------------------------------------------------------------------
# CLI parsing
# ----------------------------------------------------------------------------
CLI_DIR=""; CLI_BIN=""; FLASH_MODE=""; CLI_PORT=""; CLI_BAUD="921600"; ASSUME_YES=0
LIST_ONLY=0

if [[ $# -gt 0 ]]; then
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --dir)   shift; CLI_DIR="${1:-}"; [[ -n "$CLI_DIR" ]] || die "--dir needs a value"; shift ;;
      --bin)   shift; CLI_BIN="${1:-}"; shift ;;
      --mode)  shift; FLASH_MODE="${1:-}"; shift ;;
      --port)  shift; CLI_PORT="${1:-}"; shift ;;
      --baud)  shift; CLI_BAUD="${1:-}"; shift ;;
      --list)  LIST_ONLY=1; shift ;;
      -y|--yes) ASSUME_YES=1; shift ;;
      -h|--help) usage; exit 0 ;;
      *) die "unknown option: $1 (see --help)" ;;
    esac
  done
fi

[[ -d "$BUILD_ROOT" ]] || die "build folder not found: $BUILD_ROOT"

if [[ "$LIST_ONLY" == "1" ]]; then
  list_build_dirs | sed "s|$BUILD_ROOT/||" || true
  exit 0
fi

[[ -t 0 || -n "$CLI_DIR" ]] || die "interactive mode needs a terminal (or pass --dir)"
if [[ ! -t 0 ]]; then
  [[ -n "$FLASH_MODE" ]] || die "--mode is required when not running interactively"
  [[ -n "$CLI_PORT" ]] || die "--port is required when not running interactively"
fi

# ----------------------------------------------------------------------------
# 1) Choose build folder
# ----------------------------------------------------------------------------
if [[ -n "$CLI_DIR" ]]; then
  BUILD_DIR="$CLI_DIR"
  [[ -d "$BUILD_DIR" ]] || die "invalid folder: $BUILD_DIR"
else
  mapfile -t DIRS < <(list_build_dirs)
  [[ ${#DIRS[@]} -gt 0 ]] || die "no flashable build folders under $BUILD_ROOT"
  say ""
  say "Available build folders (newest first):"
  n=0
  for d in "${DIRS[@]}"; do
    n=$((n + 1))
    printf '  %2d - %s\n' "$n" "${d#"$BUILD_ROOT"/}"
  done
  while true; do
    read -r -p "Choose the folder number [1]: " PICK
    PICK="${PICK:-1}"
    [[ "$PICK" =~ ^[0-9]+$ ]] && (( PICK >= 1 && PICK <= ${#DIRS[@]} )) && break
    say "Invalid option."
  done
  BUILD_DIR="${DIRS[$((PICK - 1))]}"
fi
say "Selected folder: ${BUILD_DIR#"$BUILD_ROOT"/}"

# ----------------------------------------------------------------------------
# 2) Choose firmware .bin
# ----------------------------------------------------------------------------
mapfile -t BINS < <(list_bins "$BUILD_DIR")
[[ ${#BINS[@]} -gt 0 ]] || die "no firmware .bin in $BUILD_DIR"

if [[ -n "$CLI_BIN" ]]; then
  [[ -f "$CLI_BIN" ]] || die "file does not exist: $CLI_BIN"
  FIRMWARE_BIN="$CLI_BIN"
elif [[ ${#BINS[@]} -eq 1 ]]; then
  FIRMWARE_BIN="$BUILD_DIR/${BINS[0]}"
  say "Selected firmware: ${BINS[0]}"
else
  say ""
  say "Firmware images in this folder:"
  n=0
  for b in "${BINS[@]}"; do
    n=$((n + 1)); printf '  %2d - %s\n' "$n" "$b"
  done
  while true; do
    read -r -p "Choose firmware .bin number [1]: " PICK
    PICK="${PICK:-1}"
    [[ "$PICK" =~ ^[0-9]+$ ]] && (( PICK >= 1 && PICK <= ${#BINS[@]} )) && break
    say "Invalid option."
  done
  FIRMWARE_BIN="$BUILD_DIR/${BINS[$((PICK - 1))]}"
fi
say "Selected firmware: $(basename "$FIRMWARE_BIN")"

CHIP="$(chip_from_bin "$FIRMWARE_BIN")"
BOOT_OFFSET="$(bootloader_offset "$CHIP")"
NO_STUB_ALLOWED=1
[[ "$CHIP" == "esp32s3" || "$CHIP" == "esp32p4" ]] && NO_STUB_ALLOWED=0

# ----------------------------------------------------------------------------
# 3) Flash mode
# ----------------------------------------------------------------------------
if [[ -z "$FLASH_MODE" ]]; then
  say ""
  say "1 - Update board (firmware .bin only)"
  say "2 - Full flash rewrite (bootloader + partitions + firmware)"
  say "3 - Full erase + full flash rewrite (bootloader + partitions + firmware)"
  while true; do
    read -r -p "Mode [1/2/3]: " FLASH_MODE
    [[ "$FLASH_MODE" =~ ^[123]$ ]] && break
    say "Invalid option."
  done
fi
[[ "$FLASH_MODE" =~ ^[123]$ ]] || die "--mode must be 1, 2 or 3"

BOOTLOADER_BIN="$BUILD_DIR/bootloader.bin"
PARTITIONS_BIN="$BUILD_DIR/partitions.bin"
if (( FLASH_MODE >= 2 )); then
  [[ -f "$BOOTLOADER_BIN" ]] || die "bootloader.bin not found in selected folder"
  [[ -f "$PARTITIONS_BIN" ]] || die "partitions.bin not found in selected folder"
fi

# Build the write argument list.
WRITE_ARGS=()
LFS_IMAGE=""
if (( FLASH_MODE == 1 )); then
  WRITE_ARGS=("$APP_OFFSET" "$FIRMWARE_BIN")
else
  WRITE_ARGS=("$BOOT_OFFSET" "$BOOTLOADER_BIN" "$PARTITIONS_OFFSET" "$PARTITIONS_BIN" "$APP_OFFSET" "$FIRMWARE_BIN")
  if (( FLASH_MODE == 3 )); then
    LFS_IMAGE="$(storage_image)"
    if [[ -n "$LFS_IMAGE" ]]; then
      WRITE_ARGS+=("$STORAGE_OFFSET" "$LFS_IMAGE")
    else
      say "WARNING: mklittlefs not found; mode 3 will not pre-seed storage with an empty LittleFS image."
    fi
  fi
fi

# ----------------------------------------------------------------------------
# 4) Resolve esptool + serial port
# ----------------------------------------------------------------------------
resolve_esptool || die "esptool not found (pip install esptool, or PlatformIO penv)"

if [[ -n "$CLI_PORT" ]]; then
  PORTA_COM="$CLI_PORT"
  [[ -e "$PORTA_COM" ]] || die "port not found: $PORTA_COM"
else
  mapfile -t PORTS < <(list_ports)
  [[ ${#PORTS[@]} -gt 0 ]] || die "no serial port found (/dev/ttyUSB* or /dev/ttyACM*)"
  say ""
  say "Serial ports:"
  n=0
  for p in "${PORTS[@]}"; do
    n=$((n + 1)); printf '  %2d - %s\n' "$n" "$p"
  done
  DEF="${PORTS[0]}"
  while true; do
    read -r -p "Serial port [1]: " PICK
    PICK="${PICK:-1}"
    if [[ "$PICK" =~ ^[0-9]+$ ]] && (( PICK >= 1 && PICK <= ${#PORTS[@]} )); then
      PORTA_COM="${PORTS[$((PICK - 1))]}"; break
    fi
    [[ -e "$PICK" ]] && { PORTA_COM="$PICK"; break; }
    say "Invalid option (number or a /dev/... path). Default: $DEF"
  done
fi

# ----------------------------------------------------------------------------
# 5) Confirm + flash
# ----------------------------------------------------------------------------
say ""
say "============================================================"
case "$FLASH_MODE" in
  1) say "  Firmware update" ;;
  2) say "  Full install (bootloader + partitions + firmware)" ;;
  3) say "  Full erase + full install (bootloader + partitions + firmware)" ;;
esac
say "  Firmware: $(basename "$FIRMWARE_BIN")"
say "  Chip    : $CHIP    Port: $PORTA_COM"
say "  Baud    : $CLI_BAUD (quick attempt uses 115200)"
say "============================================================"
if [[ "$ASSUME_YES" != "1" ]]; then
  read -r -p "Confirm flash? [Y/n]: " CONF
  [[ "${CONF,,}" == "n" ]] && { say "Aborted."; exit 0; }
fi

say ""
if (( FLASH_MODE == 3 )); then
  if ! erase_chip "$CHIP" "$PORTA_COM" "$NO_STUB_ALLOWED"; then
    say "ERROR: full erase failed; aborting flash."
    say "Hint: close any serial monitor / other tool using $PORTA_COM and retry."
    exit 1
  fi
  say "Info: skipping separate storage erase_region (empty LittleFS image covers it)."
fi

if flash_write "$CHIP" "$PORTA_COM" "$NO_STUB_ALLOWED" "${WRITE_ARGS[@]}"; then
  say ""
  say "Flash completed successfully."
  [[ -n "$LFS_IMAGE" ]] && rm -f "$LFS_IMAGE"
  exit 0
fi

say ""
say "ERROR: flash failed at all bauds (921600, 460800, 115200)."
say "Log: /tmp/node32_flash_last.log"
say "Check USB cable, board power, and close tools using $PORTA_COM."
[[ -n "$LFS_IMAGE" ]] && rm -f "$LFS_IMAGE"
exit 1
