# fix_includes.py — PlatformIO pre-build script
#
# pioarduino 55.03.38 (arduino-esp32 3.3.x) does not declare inter-library
# include dependencies in its platform manifest.  As a result, framework
# libraries such as WiFi, LittleFS, AsyncTCP etc. fail to compile because
# headers like Network.h, NetworkInterface.h, FS.h are not on the GCC path.
#
# Fix: scan every libraries/<lib>/src directory under the framework package
# and inject them all as -I flags via CCFLAGS so GCC sees them regardless
# of which compilation unit or cloned env is being built.
#
# This is safe: duplicate -I paths are ignored by GCC.
# Arduino IDE ignores this file entirely.

Import("env")  # noqa: F821

import os

pkg_dir   = env.subst("$PROJECT_PACKAGES_DIR")
libs_root = os.path.join(pkg_dir, "framework-arduinoespressif32", "libraries")

if not os.path.isdir(libs_root):
    print(f"fix_includes: WARNING — framework libraries dir not found: {libs_root}")
else:
    # Exclude SPIFFS — on case-insensitive Windows, #include "spiffs.h" from
    # ESP-IDF's spiffs_api.h resolves to the Arduino SPIFFS.h (C++ wrapper)
    # instead of the actual spiffs.h (C library). The C wrapper pulls in FS.h
    # which #include <memory>, breaking C compilation of spiffs_api.c / esp_spiffs.c.
    exclude = {"SPIFFS"}
    flags = []
    for lib in sorted(os.listdir(libs_root)):
        if lib in exclude:
            continue
        src = os.path.join(libs_root, lib, "src")
        if os.path.isdir(src):
            flags.append(f"-I{src}")

    if flags:
        env.Append(CCFLAGS=flags)
        print(f"fix_includes: injected {len(flags)} framework library paths")

    # esp-modbus managed component: inject internal include dirs
    mc_root = os.path.join(env.subst("$PROJECT_DIR"), "managed_components", "espressif__esp-modbus")
    modbus_incs = [
        "freemodbus/modbus/include",
        "freemodbus/modbus/ascii",
        "freemodbus/modbus/functions",
        "freemodbus/modbus/rtu",
        "freemodbus/modbus/tcp",
        "freemodbus/port",
        "freemodbus/tcp_master/modbus_controller",
        "freemodbus/tcp_slave/modbus_controller",
        "freemodbus/common",
        "freemodbus/common/include",
        "freemodbus/serial_master/port",
        "freemodbus/serial_master/modbus_controller",
        "freemodbus/serial_slave/port",
        "freemodbus/serial_slave/modbus_controller",
        "freemodbus/tcp_master/port",
        "freemodbus/tcp_slave/port",
    ]
    if os.path.isdir(mc_root):
        count = 0
        for rel in modbus_incs:
            inc = os.path.join(mc_root, rel)
            if os.path.isdir(inc):
                env.Append(CCFLAGS=[f"-I{inc}"])
                count += 1
        if count:
            print(f"fix_includes: injected {count} esp-modbus include paths")

    # Helper: inject include paths for managed component subdirs
    mc_dir = os.path.dirname(mc_root)
    def _mc_includes(comp, *rel_dirs):
        root = os.path.join(mc_dir, comp)
        if not os.path.isdir(root):
            return 0
        for rel in rel_dirs:
            inc = os.path.join(root, rel)
            if os.path.isdir(inc):
                env.Append(CCFLAGS=[f"-I{inc}"])
        return len(rel_dirs)

    # Helper: inject compile definitions for managed component
    def _mc_defines(comp, *defines):
        root = os.path.join(mc_dir, comp)
        if not os.path.isdir(root):
            return 0
        env.Append(CCFLAGS=[f"-D{d}" for d in defines])
        return len(defines)

    # esp-mdns: inject private_include dir
    _mc_includes("espressif__mdns", "private_include")
    print("fix_includes: injected esp-mdns private_include path")

    # joltwallet__littlefs: needs -DLFS_CONFIG=lfs_config.h and src/ include paths
    # because its CMakeLists.txt sets these flags but PIO flat compilation ignores them
    if os.path.isdir(os.path.join(mc_dir, "joltwallet__littlefs")):
        _mc_defines("joltwallet__littlefs", "LFS_CONFIG=lfs_config.h")
        _mc_includes("joltwallet__littlefs", "src", "src/littlefs")
        print("fix_includes: injected littlefs defines/include paths")

    # espressif__network_provisioning: proto-c/ headers needed by src/*.c
    _mc_includes("espressif__network_provisioning", "proto-c")
