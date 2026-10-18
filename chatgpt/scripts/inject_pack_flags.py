# inject_pack_flags.py — PlatformIO pre: script
#
# Injects PLATFORMIO_BUILD_FLAGS environment variable into the build flags.
# Uses the PlatformIO-native env.Append(BUILD_FLAGS=...) mechanism.

Import("env")
import os

extra = os.environ.get("PLATFORMIO_BUILD_FLAGS", "").strip()
if extra:
    flags = extra.split()
    env.Append(BUILD_FLAGS=flags)
    print(f"\n>>> [inject_pack_flags] Injected: {extra}\n")