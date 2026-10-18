#!/usr/bin/env python3
"""validate_version.py — firmware version consistency gate.

Checks that the three version sources agree:
  1. `FIRMWARE_VERSION_STR` in chatgpt/src/smtp_core_impl.h   (e.g. "v1.114")
  2. `_hreg[HR_FW_VERSION]` in chatgpt/src/modbus_slave.cpp    (e.g. 1114)
  3. optionally an expected version (--expect vX.Y or --expect-dir <build folder>)

Runs as a `pio run` pre-script (registered in platformio.ini) and standalone:

    python chatgpt/scripts/validate_version.py
    python chatgpt/scripts/validate_version.py --expect v1.114
    python chatgpt/scripts/validate_version.py --expect-dir chatgpt/build/esp32s3/S3_CNC/v1.114/20260920_1633

Exits non-zero on any mismatch (always strict — a version mismatch is never OK).
"""

import argparse
import os
import re
import sys

try:
    _HERE = os.path.dirname(os.path.abspath(__file__))
    PROJECT_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
except NameError:
    # SCons exec() has no __file__; pre-scripts run with CWD = chatgpt/.
    PROJECT_ROOT = os.path.abspath(os.path.join(os.getcwd(), ".."))
SMTP_IMPL = os.path.join(PROJECT_ROOT, "chatgpt", "src", "smtp_core_impl.h")
MODBUS_SLAVE = os.path.join(PROJECT_ROOT, "chatgpt", "src", "modbus_slave.cpp")


def read_version_str():
    with open(SMTP_IMPL, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.search(r'#define\s+FIRMWARE_VERSION_STR\s+"([^"]+)"', line)
            if m:
                return m.group(1).strip()
    return None


def read_version_num():
    with open(MODBUS_SLAVE, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.search(r'_hreg\s*\[\s*HR_FW_VERSION\s*\]\s*=\s*(\d+)\s*;', line)
            if m:
                return int(m.group(1))
    return None


def version_to_num(v):
    """'v1.114' -> 1114, '1.114' -> 1114. Non-numeric parts are dropped."""
    digits = re.sub(r"[^0-9]", "", v or "")
    return int(digits) if digits else None


def expected_from_dir(path):
    """Extract vX.Y from a build folder path like .../<pack>/v1.114/<timestamp>."""
    parts = os.path.normpath(path).split(os.sep)
    for p in reversed(parts):
        if re.fullmatch(r"v?\d+\.\d+", p):
            return p if p.startswith("v") else ("v" + p)
    return None


def run_check(expect="", expect_dir=""):
    vstr = read_version_str()
    vnum = read_version_num()
    errors = []

    if not vstr:
        errors.append("FIRMWARE_VERSION_STR not found in smtp_core_impl.h")
    if vnum is None:
        errors.append("_hreg[HR_FW_VERSION] not found in modbus_slave.cpp")

    if vstr and vnum is not None:
        n = version_to_num(vstr)
        if n != vnum:
            errors.append(f"version mismatch: smtp_core_impl.h '{vstr}' (=>{n}) "
                          f"!= modbus_slave.cpp {vnum}")

    expected = (expect or "").strip()
    if not expected and expect_dir:
        expected = expected_from_dir(expect_dir) or ""
        if not expected:
            errors.append(f"could not derive a version from --expect-dir {expect_dir}")
    if expected and vstr:
        if version_to_num(expected) != version_to_num(vstr):
            errors.append(f"expected version '{expected}' != FIRMWARE_VERSION_STR '{vstr}'")

    if errors:
        print("[version] FAIL")
        for e in errors:
            print(f"  - {e}")
        print("[version] keep FIRMWARE_VERSION_STR and _hreg[HR_FW_VERSION] in sync "
              "(bump only on commit/push).")
        return 1

    extra = f" (expected {expected})" if expected else ""
    print(f"[version] OK: {vstr} == {vnum}{extra}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="firmware version consistency gate")
    ap.add_argument("--expect", default="", help="expected version string, e.g. v1.114")
    ap.add_argument("--expect-dir", default="", help="build folder to derive the version from")
    args = ap.parse_args()
    return run_check(args.expect, args.expect_dir)


try:
    Import("env")  # noqa: F821 — SCons builtin, only defined under `pio run`
    _IN_SCONS = True
except NameError:
    _IN_SCONS = False

if _IN_SCONS:
    # Pre-script context: run the basic gate now (no argv parsing — sys.argv
    # belongs to PlatformIO here). A mismatch fails the build.
    if run_check("", "") != 0:
        raise SystemExit(1)
elif __name__ == "__main__":
    sys.exit(main())
