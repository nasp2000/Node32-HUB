"""
Smoke build: compile all environments and/or pack variants locally.

Usage:
    python scripts/smoke_build.py                    # build all 3 envs
    python scripts/smoke_build.py --env n16r8         # single env
    python scripts/smoke_build.py --packs             # also test each SELECTED_PACK
    python scripts/smoke_build.py --skip-tests        # skip native C++ tests
    python scripts/smoke_build.py --only-tests        # only run native C++ tests, no build
"""

import subprocess
import sys
import os

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
ENVIRONMENTS = ["n16r8", "waveshare_8di8do", "s3_touch7", "esp32p4"]

PACK_VARIANTS = [
    "PACK_NONE",
    "PACK_RAMBOX",
    "PACK_CAM",
    "PACK_RS232",
    "PACK_P4_CNC",
    "PACK_S3_CNC",
    "PACK_PROXIMITY",
    "PACK_RS232",
    "PACK_3D_PRINTER",
]


def run_native_tests() -> int:
    """Run native C++ unit tests via the PlatformIO pre-script."""
    from run_native_tests import run_tests
    return run_tests()


def run_pio(env: str, extra_flags: list[str] | None = None) -> int:
    cmd = ["pio", "run", "-e", env]
    if extra_flags:
        cmd.extend(extra_flags)
    print(f"\n{'='*70}")
    print(f"  Building: {env}")
    print(f"{'='*70}\n")
    result = subprocess.run(cmd, cwd=PROJECT_DIR)
    if result.returncode == 0:
        print(f"  PASS: {env}")
    else:
        print(f"  FAIL: {env} (exit code {result.returncode})")
    return result.returncode


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Smoke build for Node32-HUB")
    parser.add_argument("--env", choices=ENVIRONMENTS, help="Single environment to build")
    parser.add_argument("--packs", action="store_true", help="Also build each pack variant")
    parser.add_argument("--skip-tests", action="store_true", help="Skip native C++ tests")
    parser.add_argument("--only-tests", action="store_true", help="Only run native C++ tests, skip build")
    args = parser.parse_args()

    # Run native tests first (unless --skip-tests)
    tests_ok = True
    if not args.skip_tests:
        print(f"\n{'='*70}")
        print(f"  Running native C++ unit tests")
        print(f"{'='*70}\n")
        if run_native_tests() != 0:
            print(f"\n  NATIVE TESTS FAILED — aborting build")
            return 1
        print(f"  Native tests passed\n")

    if args.only_tests:
        print(f"  --only-tests: build skipped")
        return 0

    targets = [args.env] if args.env else ENVIRONMENTS
    failures = 0

    for env in targets:
        if run_pio(env) != 0:
            failures += 1

    if args.packs and not args.env:
        print(f"\n{'='*70}")
        print(f"  Testing each pack variant")
        print(f"{'='*70}\n")
        for pack in PACK_VARIANTS:
            env = "n16r8"
            print(f"\n  Pack: {pack}")
            subprocess_env = os.environ.copy()
            subprocess_env["PLATFORMIO_BUILD_FLAGS"] = f"-DSELECTED_PACK={pack}"
            result = subprocess.run(
                ["pio", "run", "-e", env],
                cwd=PROJECT_DIR,
                env=subprocess_env,
            )
            if result.returncode != 0:
                failures += 1
                print(f"  FAIL: {env} + {pack}")
            else:
                print(f"  PASS: {env} + {pack}")

    if failures:
        print(f"\n{'='*70}")
        print(f"  SMOKE BUILD FAILED: {failures} failure(s)")
        print(f"{'='*70}\n")
    else:
        print(f"\n{'='*70}")
        print(f"  SMOKE BUILD PASSED")
        print(f"{'='*70}\n")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
