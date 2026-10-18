#!/usr/bin/env bash
# Node32-HUB — Linux build wrapper.
#
# This folder is shared via Samba and can be built from BOTH Windows and
# Linux. Windows builds use chatgpt/.pio (PlatformIO default, via the .bat
# scripts). This wrapper redirects every Linux artifact to chatgpt/.pio-linux
# so the two toolchains (Xtensa/RISC-V ELF binaries are OS-specific) never
# collide in the shared folder.
#
# Usage (same as pio):
#   ./pio.sh run -e n16r8
#   ./pio.sh run -e waveshare_8di8do
#   ./pio.sh run -e esp32p4
#   ./pio.sh device monitor
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PLATFORMIO_BUILD_DIR="$DIR/chatgpt/.pio-linux/build"
export PLATFORMIO_LIBDEPS_DIR="$DIR/chatgpt/.pio-linux/libdeps"
export PLATFORMIO_BUILD_CACHE_DIR="$DIR/chatgpt/.pio-linux/ccache"
# ccache.py wraps the compiler with `ccache`; CCACHE_DIR guarantees the cache
# stays per-OS even if PlatformIO's own build_cache_dir wins over the env var.
export CCACHE_DIR="$PLATFORMIO_BUILD_CACHE_DIR"

# Parallel jobs: use every core by default (Pi 4 = 4). Override with
# PIO_JOBS=2 ./pio.sh ... if RAM pressure becomes an issue — firmware TUs
# compiled at -O2 can each take several hundred MB.
JOBS="${PIO_JOBS:-$(nproc)}"
if [[ "${1:-}" == "run" && " $* " != *" -j"* && " $* " != *" --jobs"* ]]; then
    set -- "$@" -j "$JOBS"
fi

exec pio "$@"
