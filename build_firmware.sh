#!/usr/bin/env bash
# ============================================================================
# Node32-HUB — Linux build menu (equivalent of build_firmware.bat)
#
# Same boards/packs/options as the Windows .bat, plus Linux-specific extras:
#   * Builds go through ./pio.sh -> artifacts isolated in chatgpt/.pio-linux/
#     (Windows keeps using chatgpt/.pio via the .bat scripts — no collisions
#     in this Samba-shared folder).
#   * Parallel compile (-j4 by default; override with PIO_JOBS=N).
#   * BACKGROUND mode: the build survives closing the terminal / SSH session
#     (nohup + setsid). Check progress later with --status or tail -f.
#
# Usage:
#   ./build_firmware.sh                          interactive menus
#   ./build_firmware.sh -e n16r8 -p RAMBOX       direct build (-c clean, -m elf/map)
#   ./build_firmware.sh -e n16r8 -p RAMBOX --bg  same, detached from terminal
#   ./build_firmware.sh -e esp32p4 -p CAM_P4     P4 camera pack (isolated 3.3.12 world)
#   ./build_firmware.sh -e n16r8 -p CAM --thirdparty   also write third-party.zip
#   ./build_firmware.sh --all                    all boards, default packs
#   ./build_firmware.sh --allpacks n16r8         all packs for one board
#   ./build_firmware.sh --allpacks all           ALL boards x ALL packs
#   ./build_firmware.sh -t                       native C++ tests only
#   ./build_firmware.sh --status                 background build status
#
# Boards: n16r8 | waveshare_8di8do | s3_touch7 | esp32p4
# Packs : NONE RAMBOX CAM RS232 ENVIRONMENT RADIO(alpha) S3_CNC(OTG only, no UART) WAVESHARE S3_TOUCH7(alpha) P4_CNC P4_USB_SERVER(alpha) CAM_P4(alpha)
#
# third-party.zip (LGPL sources + relinkable objects) is OPT-IN: interactive
# builds ask "Include third-party .zip" (default N); CLI needs --thirdparty.
# Include it before redistributing/selling a .bin (compliance obligation).
#
# DUAL-PLATFORM: pack_env_override() below routes packs to build envs, so a
# platform bump can be trialled on one/two packs (S3 or P4) before a global
# rollout. Today: CAM_P4 -> esp32p4cam (Arduino 3.3.12, needs ESP_Video MIPI);
# everything else stays on 55.03.35 (heap regression arduino-esp32#12847).
# CAM_P4 build isolation (its own core dir + .pio-p4cam tree) lives in
# run_env_pio() — the whole flow is inside THIS script (no build_p4cam.sh).
# To trial another pack, e.g. RAMBOX on 3.3.12: add [env:n16r8next] in
# platformio.ini (extends = env:n16r8 + new platform URL) and one line here:
#     RAMBOX) echo "n16r8next" ;;
# ============================================================================
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="$HERE/$(basename "${BASH_SOURCE[0]}")"
CHATGPT="$HERE/chatgpt"
LOG_DIR="$CHATGPT/build/logs"
LAST_FILE="$HERE/.build_firmware.last"
PIO="$HERE/pio.sh"
PY="${PYTHON:-python3}"

# Keep Linux artifacts away from the Windows .pio/ tree (same values as pio.sh;
# exported here so rename_firmware.py --copy finds firmware.bin after the build).
export PLATFORMIO_BUILD_DIR="$CHATGPT/.pio-linux/build"
export PLATFORMIO_LIBDEPS_DIR="$CHATGPT/.pio-linux/libdeps"
export PLATFORMIO_BUILD_CACHE_DIR="$CHATGPT/.pio-linux/ccache"
export CCACHE_DIR="$PLATFORMIO_BUILD_CACHE_DIR"
export CCACHE_MAXSIZE="4G"

# ----------------------------------------------------------------------------
# P4-CAM isolated world (Arduino 3.3.12 firmware package).
# ----------------------------------------------------------------------------
# CAM_P4 builds on [env:esp32p4cam], which pulls a DIFFERENT arduino-esp32
# (3.3.12) than every other env (3.3.5). PlatformIO keeps ONE framework package
# per core dir, so sharing a tree re-downloads ~322 MB and rebuilds fully on
# every switch. CAM_P4 therefore gets its own core dir + build tree (the exact
# isolation build_p4cam.sh used to provide) while still running from THIS script.
P4CAM_CORE_DIR="${P4CAM_CORE_DIR:-$HOME/.platformio-p4cam}"
P4CAM_DIR="$CHATGPT/.pio-p4cam"

mkdir -p "$LOG_DIR"

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------
# DUAL-PLATFORM MAP: pack -> build env (empty = keep the board default env).
# Works for S3 and P4 packs alike; add one line per pack under trial.
pack_env_override() {
  case "$1" in
    # CAM_P4 — P4 camera pack (requires ESP_Video+USB)
    CAM_P4) echo "esp32p4cam" ;;
    *) echo "" ;;
  esac
}
pack_define() {
  case "$1" in
    RAMBOX)        echo "-DSELECTED_PACK=PACK_RAMBOX" ;;
    CAM)           echo "-DSELECTED_PACK=PACK_CAM -DTARGET_BOARD_S3_CAM=1" ;;
    RS232)         echo "-DSELECTED_PACK=PACK_RS232" ;;
    ENVIRONMENT)   echo "-DSELECTED_PACK=PACK_ENVIRONMENT" ;;
    RADIO)         echo "-DSELECTED_PACK=PACK_RADIO" ;;
    S3_CNC)        echo "-DSELECTED_PACK=PACK_S3_CNC" ;;
    WAVESHARE)     echo "-DSELECTED_PACK=PACK_WAVESHARE" ;;
    S3_TOUCH7)     echo "-DSELECTED_PACK=PACK_S3_TOUCH7 -DTARGET_BOARD_S3_TOUCH7=1" ;;
    P4_CNC)        echo "-DSELECTED_PACK=PACK_P4_CNC " ;;
    P4_USB_SERVER) echo "-DSELECTED_PACK=PACK_P4_USB_SERVER " ;;
    CAM_P4)        echo "-DSELECTED_PACK=PACK_CAM_P4 " ;;
    *)             echo "" ;;
  esac
}

# run_env_pio <env> <pack-define> <pio args...>
# Routes a PlatformIO invocation to the right world:
#   * esp32p4cam -> isolated core dir (~/.platformio-p4cam) + .pio-p4cam tree
#     (Arduino 3.3.12 / ESP_Video MIPI). The env is selected here, not by the
#     caller, so clean/build/rename all stay consistent.
#   * anything else -> shared .pio-linux tree via ./pio.sh (unchanged).
run_env_pio() {
  local env="$1" define="$2"; shift 2
  if [[ "$env" == "esp32p4cam" ]]; then
    local pio_bin="$P4CAM_CORE_DIR/penv/bin/pio"
    [[ -x "$pio_bin" ]] || pio_bin="pio"
    local -a ev=(
      "PLATFORMIO_CORE_DIR=$P4CAM_CORE_DIR"
      "PLATFORMIO_BUILD_DIR=$P4CAM_DIR/build"
      "PLATFORMIO_LIBDEPS_DIR=$P4CAM_DIR/libdeps"
      "PLATFORMIO_BUILD_CACHE_DIR=$P4CAM_DIR/ccache"
      "CCACHE_DIR=$P4CAM_DIR/ccache"
      "CCACHE_MAXSIZE=4G"
    )
    [[ -n "$define" ]] && ev+=("PLATFORMIO_BUILD_FLAGS=$define")
    (cd "$CHATGPT" && env "${ev[@]}" "$pio_bin" "$@")
  elif [[ -n "$define" ]]; then
    (cd "$CHATGPT" && PLATFORMIO_BUILD_FLAGS="$define" "$PIO" "$@")
  else
    (cd "$CHATGPT" && "$PIO" "$@")
  fi
}

# build_dir_for <env> — PlatformIO build root for <env>, used by rename_firmware.
build_dir_for() {
  if [[ "$1" == "esp32p4cam" ]]; then echo "$P4CAM_DIR/build"; else echo "$PLATFORMIO_BUILD_DIR"; fi
}

detect_env_from_user_config() {
  local uc="$CHATGPT/user_config.h" env="n16r8"
  [[ -f "$uc" ]] || { echo "$env"; return; }
  grep -Eq '^[[:space:]]*#define[[:space:]]+TARGET_BOARD_8DI8DO_POE[[:space:]]+1' "$uc" && env="waveshare_8di8do"
  grep -Eq '^[[:space:]]*#define[[:space:]]+TARGET_BOARD_P4[[:space:]]+1'      "$uc" && env="esp32p4"
  grep -Eq '^[[:space:]]*#define[[:space:]]+TARGET_BOARD_S3_CAM[[:space:]]+1'  "$uc" && env="n16r8"
  grep -Eq '^[[:space:]]*#define[[:space:]]+TARGET_BOARD_S3_TOUCH7[[:space:]]+1'  "$uc" && env="s3_touch7"
  echo "$env"
}

save_last() { # <profile> <items...>
  {
    echo "LAST_PROFILE=$1"
    echo "LAST_ITEMS=${*:2}"
    echo "LAST_DO_CLEAN=$DO_CLEAN"
    echo "LAST_ELF_MAP=$WANT_ELF"
    echo "LAST_THIRDPARTY=$WANT_THIRDPARTY"
  } > "$LAST_FILE"
}

load_last() {
  [[ -f "$LAST_FILE" ]] || return 1
  local line key val
  while IFS='=' read -r key val; do
    case "$key" in
      LAST_PROFILE)    LAST_PROFILE="$val" ;;
      LAST_ITEMS)      LAST_ITEMS="$val" ;;
      LAST_DO_CLEAN)   LAST_DO_CLEAN="$val" ;;
      LAST_ELF_MAP)    LAST_ELF_MAP="$val" ;;
      LAST_THIRDPARTY) LAST_THIRDPARTY="$val" ;;
    esac
  done < "$LAST_FILE"
  [[ -n "${LAST_ITEMS:-}" ]]
}

# build_one <env> <pack> <foreground 1|0>
build_one() {
  local env="$1" pack="$2" fg="$3"
  local define rc log bdir
  log="$LOG_DIR/${env}_${pack}.log"
  define="$(pack_define "$pack")"
  echo "============================================================"
  echo "  Building: $pack pack on $env"
  echo "  Log: chatgpt/build/logs/${env}_${pack}.log"
  echo "============================================================"
  run_env_pio "$env" "$define" run -e "$env" > "$log" 2>&1
  rc=$?
  if [[ "$fg" == "1" ]]; then
    cat "$log"
  fi
  if [[ $rc -ne 0 ]]; then
    echo "============================================================"
    echo "  ERROR: build $env ($pack) FAILED (exit $rc)"
    echo "  Log: $log"
    echo "============================================================"
    [[ "$fg" != "1" ]] && tail -n 40 "$log"
    return "$rc"
  fi
  echo "  Build OK — copying artifacts (rename_firmware.py)"
  bdir="$(build_dir_for "$env")"
  (cd "$CHATGPT" && PLATFORMIO_BUILD_DIR="$bdir" EXPORT_ELF_MAP="${EXPORT_ELF_MAP:-0}" DIST_THIRD_PARTY="$WANT_THIRDPARTY" "$PY" rename_firmware.py --copy --env "$env" --pack "$pack")
}

# run_builds <foreground 1|0> <items...>   (item format: env:pack)
run_builds() {
  local fg="$1"; shift
  local failed="" item env pack
  for item in "$@"; do
    env="${item%%:*}"
    pack="${item##*:}"
    env_override="$(pack_env_override "$pack")"
    [[ -n "$env_override" ]] && env="$env_override"
    if [[ "$DO_CLEAN" == "1" ]]; then
      echo "--- Cleaning $env ---"
      run_env_pio "$env" "$(pack_define "$pack")" run -e "$env" --target clean || true
    fi
    if ! build_one "$env" "$pack" "$fg"; then
      failed="$failed ${env}_${pack}"
    fi
    echo ""
  done
  echo "============================================================"
  if [[ -n "$failed" ]]; then
    echo "  Failed builds:$failed"
    echo "  Logs: chatgpt/build/logs/"
    return 1
  fi
  echo "  All builds completed successfully!"
  return 0
}

# launch_background <do_clean> <want_elf> <want_thirdparty> <items...>
# Detaches the build into its own session (setsid) immune to SIGHUP, so it
# survives closing the terminal window or the SSH session.
launch_background() {
  local clean="$1" elf="$2" tp="$3"; shift 3
  local ts bglog pid
  ts="$(date +%Y%m%d_%H%M%S)"
  bglog="$LOG_DIR/bg_${ts}.log"
  nohup setsid "$SCRIPT" --bg-worker "$clean" "$elf" "$tp" "$@" < /dev/null > "$bglog" 2>&1 &
  pid=$!
  disown 2>/dev/null || true
  echo "$pid" > "$LOG_DIR/build.pid"
  echo "$bglog" > "$LOG_DIR/build.logpath"
  echo ""
  echo "  Background build started."
  echo "    PID : $pid"
  echo "    Log : $bglog"
  echo ""
  echo "  Safe to close this window/SSH session now. Later, from any session:"
  echo "    ./build_firmware.sh --status          (is it running? last output)"
  echo "    tail -f $bglog"
  echo "    kill $pid                             (to abort)"
}

show_status() {
  local pidfile="$LOG_DIR/build.pid" pid log
  if [[ ! -f "$pidfile" ]]; then
    echo "No background build recorded (no $pidfile)."
    exit 0
  fi
  pid="$(cat "$pidfile")"
  log="$(cat "$LOG_DIR/build.logpath" 2>/dev/null || true)"
  if kill -0 "$pid" 2>/dev/null; then
    echo "Background build RUNNING (pid $pid)"
  else
    echo "Background build NOT running (pid $pid already finished)."
  fi
  [[ -n "$log" ]] && echo "Log: $log"
  echo "Last log lines:"
  tail -n 15 "$log" 2>/dev/null || echo "  (log not found)"
}

run_native_tests() {
  echo "============================================================"
  echo "  Running native C++ unit tests"
  echo "============================================================"
  (cd "$CHATGPT" && "$PY" scripts/run_native_tests.py)
}

die() { echo "ERROR: $*" >&2; exit 1; }

# ----------------------------------------------------------------------------
# State
# ----------------------------------------------------------------------------
DO_CLEAN=0
WANT_ELF=0
WANT_THIRDPARTY=0
ITEMS=()
[[ "${WANT_ELF_SET:-}" == "1" ]] && export EXPORT_ELF_MAP=1

# ----------------------------------------------------------------------------
# Background worker path (launched by launch_background; never prompts)
# ----------------------------------------------------------------------------
if [[ "${1:-}" == "--bg-worker" ]]; then
  shift
  DO_CLEAN="${1:-0}"; WANT_ELF="${2:-0}"; WANT_THIRDPARTY="${3:-0}"; shift 3
  [[ "$WANT_ELF" == "1" ]] && export EXPORT_ELF_MAP=1
  run_builds 0 "$@"
  exit $?
fi

# ----------------------------------------------------------------------------
# CLI parsing (non-interactive when flags are present)
# ----------------------------------------------------------------------------
CUR_ENV=""
BG_MODE=0
if [[ $# -gt 0 ]]; then
  while [[ $# -gt 0 ]]; do
    case "$1" in
      -e|--env)    shift; CUR_ENV="${1:-}"; [[ -n "$CUR_ENV" ]] || die "-e needs a value"; shift ;;
      -p|--pack)   shift; CUR_PACK="${1:-}"; [[ -n "$CUR_PACK" ]] || die "-p needs a value"
                   [[ -n "$CUR_ENV" ]] || die "use -e before -p"
                   ITEMS+=("$CUR_ENV:$CUR_PACK"); shift ;;
      -c|--clean)  DO_CLEAN=1; shift ;;
      -m|--elfmap) WANT_ELF=1; export EXPORT_ELF_MAP=1; shift ;;
      --thirdparty) WANT_THIRDPARTY=1; shift ;;
      --bg)        BG_MODE=1; shift ;;
      --all)       ITEMS+=("n16r8:default" "waveshare_8di8do:default" "esp32p4:default"); shift ;;
      --allpacks)  shift; AP="${1:-}"
                   case "$AP" in
                      n16r8)          ITEMS+=("n16r8:RAMBOX" "n16r8:CAM" "n16r8:RS232" "n16r8:ENVIRONMENT" "n16r8:RADIO" "n16r8:S3_CNC") ;;
                      waveshare_8di8do) ITEMS+=("waveshare_8di8do:WAVESHARE") ;;
                      s3_touch7)      ITEMS+=("s3_touch7:S3_TOUCH7") ;;
                      esp32p4)        ITEMS+=("esp32p4:P4_CNC" "esp32p4:P4_USB_SERVER" "esp32p4:CAM_P4") ;;  # CAM_P4 enabled
                      all)            ITEMS+=("n16r8:RAMBOX" "n16r8:CAM" "n16r8:RS232" "n16r8:ENVIRONMENT" "n16r8:RADIO" "n16r8:S3_CNC"
                                              "waveshare_8di8do:WAVESHARE"
                                              "s3_touch7:S3_TOUCH7"
                                              "esp32p4:P4_CNC" "esp32p4:P4_USB_SERVER" "esp32p4:CAM_P4") ;;  # +CAM_P4 now available
                      *) die "--allpacks needs n16r8|waveshare_8di8do|s3_touch7|esp32p4|all" ;;
                   esac
                   shift ;;
      -t|--tests)  run_native_tests; exit $? ;;
      --status)    show_status ;;
      --bg-worker) die "--bg-worker is internal (used by --bg launches)" ;;
      -h|--help)   sed -n '2,40p' "$SCRIPT" | sed 's/^# \{0,1\}//'; exit 0 ;;
      *)           die "unknown option: $1 (see --help)" ;;
    esac
  done
  [[ ${#ITEMS[@]} -gt 0 ]] || die "nothing to build (use -e/-p, --all, --allpacks or -t)"

  save_last "CLI" "${ITEMS[@]}"
  if [[ "$BG_MODE" == "1" ]]; then
    launch_background "${DO_CLEAN}" "${WANT_ELF}" "${WANT_THIRDPARTY}" "${ITEMS[@]}"
  else
    run_builds 1 "${ITEMS[@]}"
  fi
  exit $?
fi

# ----------------------------------------------------------------------------
# Interactive menus (no arguments) — mirrors build_firmware.bat
# ----------------------------------------------------------------------------
[[ -t 0 ]] || die "interactive menus need a terminal; use CLI flags (./build_firmware.sh --help) or --bg"

LAST_PROFILE=""; LAST_ITEMS=""; LAST_DO_CLEAN=""; LAST_ELF_MAP=""; LAST_THIRDPARTY=""
load_last && HAS_LAST=1 || HAS_LAST=0

while true; do
  echo ""
  echo "============================================================"
  echo "  Node32-HUB - Step 1/2: Choose board"
  echo "============================================================"
  echo ""
  echo "  1 - N16R8 (all packs: RAMBOX, CAM, RS232, ENVIRONMENT, Radio, S3 CNC, ...)"
  echo "  2 - Node32-OEE (Waveshare 8DI8DO) default build"
  echo "  3 - ESP32-P4 (pack flow)"
  echo "  4 - All boards (N16R8 + Node32-OEE + ESP32-P4)"
  echo "  5 - Build all packs for selected board (sequential)"
  echo "  6 - Build ALL boards with ALL packs (sequential)"
  echo "  7 - Run native C++ tests only (no firmware build)"
  echo "  8 - S3 Touch-LCD-7 TouchMeteo-7 default build [ALPHA]"
  if [[ "$HAS_LAST" == "1" ]]; then
    echo ""
    echo "  Last build: ${LAST_PROFILE:-?} | ${LAST_ITEMS:-?}"
    echo "  Press ENTER to repeat last build settings."
  fi
  echo ""
  read -r -p "Board [1/2/3/4/5/6/7/8] (Enter=repeat last): " BOARD_PICK
  if [[ -z "$BOARD_PICK" ]]; then
    if [[ "$HAS_LAST" == "1" ]]; then
      DO_CLEAN="$LAST_DO_CLEAN"; WANT_ELF="$LAST_ELF_MAP"; WANT_THIRDPARTY="${LAST_THIRDPARTY:-0}"
      [[ "$WANT_ELF" == "1" ]] && export EXPORT_ELF_MAP=1
      IFS=' ' read -r -a ITEMS <<< "$LAST_ITEMS"
      break
    fi
    BOARD_PICK=1
  fi
  case "$BOARD_PICK" in
    1) ENV_SEL=n16r8; PACK_MENU=n16r8; break ;;
    2) ITEMS=("waveshare_8di8do:WAVESHARE"); break ;;
    3) ENV_SEL=esp32p4; PACK_MENU=p4; break ;;
    4) ITEMS=("n16r8:default" "waveshare_8di8do:default" "esp32p4:default"); break ;;
    5) BOARD_PICK=5; break ;;
    6) BOARD_PICK=6; break ;;
    7) run_native_tests; exit $? ;;
    8) ITEMS=("s3_touch7:S3_TOUCH7"); break ;;
    *) echo "Invalid option. Try again." ;;
  esac
done

if [[ "${BOARD_PICK:-}" == "5" ]]; then
  echo "" ; read -r -p "Board for all-packs build [n16r8/waveshare_8di8do/s3_touch7/esp32p4]: " APB
  case "$APB" in
    n16r8)            ITEMS=("n16r8:RAMBOX" "n16r8:CAM" "n16r8:RS232" "n16r8:ENVIRONMENT" "n16r8:RADIO" "n16r8:S3_CNC") ;;
    waveshare_8di8do) ITEMS=("waveshare_8di8do:WAVESHARE") ;;
    s3_touch7)        ITEMS=("s3_touch7:S3_TOUCH7") ;;
    esp32p4)          ITEMS=("esp32p4:P4_CNC" "esp32p4:P4_USB_SERVER" "esp32p4:CAM_P4") ;;  # CAM_P4 via isolated 3.3.12 world
    *) echo "Invalid board."; exit 1 ;;
  esac
elif [[ "${BOARD_PICK:-}" == "6" ]]; then
  ITEMS=("n16r8:RAMBOX" "n16r8:CAM" "n16r8:RS232" "n16r8:ENVIRONMENT" "n16r8:RADIO" "n16r8:S3_CNC"
         "waveshare_8di8do:WAVESHARE"
         "s3_touch7:S3_TOUCH7"
         "esp32p4:P4_CNC" "esp32p4:P4_USB_SERVER" "esp32p4:CAM_P4")  # CAM_P4 enabled
elif [[ "${PACK_MENU:-}" == "n16r8" ]]; then
  while true; do
    echo ""
    echo "============================================================"
    echo "  Node32-HUB - Step 2/2: Choose pack for N16R8"
    echo "============================================================"
    echo ""
    echo "  1 - No pack"
    echo "  2 - Rambox pack"
    echo "  3 - RS232 pack"
    echo "  4 - Environment pack"
    echo "  5 - Radio pack [ALPHA]"
    echo "  6 - S3 CNC (GRBL via OTG only)"
    echo "  7 - CAM pack (ESP32-S3 Camera board)"
    echo ""
    read -r -p "Pack [1-7] (Enter=1): " PK
    [[ -z "$PK" ]] && PK=1
    case "$PK" in
      1) ITEMS=("$ENV_SEL:NONE"); break ;;
      2) ITEMS=("$ENV_SEL:RAMBOX"); break ;;
      3) ITEMS=("$ENV_SEL:RS232"); break ;;
      4) ITEMS=("$ENV_SEL:ENVIRONMENT"); break ;;
      5) ITEMS=("$ENV_SEL:RADIO"); break ;;
      6) ITEMS=("$ENV_SEL:S3_CNC"); break ;;
      7) ITEMS=("$ENV_SEL:CAM"); break ;;
      *) echo "Invalid pack option." ;;
    esac
  done
elif [[ "${PACK_MENU:-}" == "p4" ]]; then
  while true; do
    echo ""
    echo "============================================================"
    echo "  Node32-HUB - Step 2/2: Choose pack for ESP32-P4"
    echo "============================================================"
    echo ""
    echo "  1 - No pack"
    echo "  2 - P4 GcodeSender pack (USB Host + GcodeSender)"
    echo "  3 - P4 USB Server pack (USB Host + USB/IP) [ALPHA]"
    echo "  4 - P4 Camera pack (same pages as Cam-M) [ALPHA]"
    echo ""
    read -r -p "Pack [1/2/3/4] (Enter=1): " PK
    [[ -z "$PK" ]] && PK=1
    case "$PK" in
      1) ITEMS=("esp32p4:NONE"); break ;;
      2) ITEMS=("esp32p4:P4_CNC"); break ;;
      3) ITEMS=("esp32p4:P4_USB_SERVER"); break ;;
      4) ITEMS=("esp32p4:CAM_P4"); break ;;
      *) echo "Invalid pack option." ;;
    esac
  done
fi

# Options: clean / elf-map
while true; do
  DEF=N; [[ "${LAST_DO_CLEAN:-0}" == "1" ]] && DEF=Y
  read -r -p "Clean build first? [y/N] (Enter=$DEF): " ANS
  ANS="${ANS:-$DEF}"
  [[ "${ANS,,}" == "y" || "${ANS,,}" == "n" ]] && break
  echo "Invalid option."
done
[[ "${ANS,,}" == "y" ]] && DO_CLEAN=1
while true; do
  read -r -p "Include .elf and .map artifacts? [y/N] (Enter=N): " ANS
  ANS="${ANS:-N}"
  [[ "${ANS,,}" == "y" || "${ANS,,}" == "n" ]] && break
  echo "Invalid option."
done
if [[ "${ANS,,}" == "y" ]]; then
  WANT_ELF=1; export EXPORT_ELF_MAP=1
fi
# Include the third-party compliance .zip (LGPL sources + relinkable objects)?
# Default N (dev builds stay small); answer y before redistributing/selling.
while true; do
  DEF=N; [[ "${LAST_THIRDPARTY:-0}" == "1" ]] && DEF=Y
  read -r -p "Include third-party .zip? [y/N] (Enter=$DEF): " ANS
  ANS="${ANS:-$DEF}"
  [[ "${ANS,,}" == "y" || "${ANS,,}" == "n" ]] && break
  echo "Invalid option."
done
[[ "${ANS,,}" == "y" ]] && WANT_THIRDPARTY=1

# Summary
echo ""
echo "============================================================"
echo "  Build summary"
echo "============================================================"
echo "  Boards/packs:"
for it in "${ITEMS[@]}"; do echo "    - ${it%%:*}  pack: ${it##*:}"; done
[[ "$DO_CLEAN" == "1" ]] && echo "  Clean  : YES" || echo "  Clean  : NO"
[[ "$WANT_ELF" == "1" ]] && echo "  ELF/MAP: YES" || echo "  ELF/MAP: NO"
[[ "$WANT_THIRDPARTY" == "1" ]] && echo "  3rd-party .zip: YES" || echo "  3rd-party .zip: NO"
echo "============================================================"
echo ""
read -r -p "Run in BACKGROUND (survives closing this window/SSH)? [y/N]: " BGA
if [[ "${BGA,,}" == "y" ]]; then
  save_last "INTERACTIVE" "${ITEMS[@]}"
  launch_background "$DO_CLEAN" "$WANT_ELF" "$WANT_THIRDPARTY" "${ITEMS[@]}"
  exit 0
fi
read -r -p "Confirm build? [Y/n]: " CONF
[[ "${CONF,,}" == "n" ]] && { echo "Aborted."; exit 0; }

save_last "INTERACTIVE" "${ITEMS[@]}"
run_builds 1 "${ITEMS[@]}"
