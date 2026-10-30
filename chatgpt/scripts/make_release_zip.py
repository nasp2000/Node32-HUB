#!/usr/bin/env python3
"""make_release_zip.py — build the distributable release archive from a build folder.

Input is the folder produced by `rename_firmware.py --copy`:

    build/<board>/<PACK>/<version>/<ts>/
      <env>_<version>_<PACK>_<ts>.bin
      bootloader.bin, partitions.bin, flash_command.txt
      LICENSE, SBOM.md, THIRD_PARTY_NOTICES, README_DISTRIBUTION.txt
      third-party/            (loose tree, if DIST_THIRD_PARTY=1)

It packs the loose `third-party/` tree into `third-party.zip` (via
package_release.py), then zips the whole folder into:

    <env>_<version>_<PACK>_<ts>.zip

which is the asset published on the GitHub release. The archive includes
`third-party.zip` (unlike package_release.py, which excludes nested archives).

Usage:
  python chatgpt/scripts/make_release_zip.py --env n16r8 --pack CAM
  python chatgpt/scripts/make_release_zip.py --dir chatgpt/build/esp32s3/CAM/v1.120/20261029_1200
  python chatgpt/scripts/make_release_zip.py --env n16r8 --pack CAM --print-path
"""

import argparse
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BUILD_ROOT = os.path.join(PROJECT_ROOT, "chatgpt", "build")
PACKAGE_RELEASE = os.path.join(HERE, "package_release.py")

SKIP_BINS = {"bootloader.bin", "partitions.bin"}


def log(msg, quiet):
    print(msg, file=sys.stderr if quiet else sys.stdout)


def newest_build_dir(pack):
    """Newest folder under build/ that holds exactly one firmware .bin.

    The layout is build/<board>/<pack>/<version>/<timestamp>/, but the depth
    varies (P4 CAM also uses build/esp32p4/CAM_P4/...), so walk the tree
    instead of assuming a fixed number of levels.
    """
    if not os.path.isdir(BUILD_ROOT):
        return None
    cands = []
    for root, dirs, files in os.walk(BUILD_ROOT):
        # never descend into the compliance bundle (large, no firmware inside)
        dirs[:] = [d for d in dirs if d != "third-party"]
        bins = [f for f in files if f.lower().endswith(".bin") and f not in SKIP_BINS]
        if len(bins) == 1:
            cands.append((os.path.getmtime(root), root))
    if not cands:
        return None
    if pack:
        # Match the pack directory component exactly first (CAM vs CAM_P4 are
        # different variants that may both exist locally).
        want = pack.lower()
        exact = [
            c for c in cands
            if want in [p.lower() for p in c[1].replace("\\", "/").split("/")]
        ]
        cands = exact or [c for c in cands if want in c[1].lower()] or cands
    cands.sort(reverse=True)
    return cands[0][1]


def ensure_third_party_zip(folder, quiet, required=False):
    """Pack a loose third-party/ tree, if one is present.

    The LGPL bundle is opt-in in `build_firmware.sh --thirdparty`, so its absence
    is the normal case: the release asset is just the build folder. Pass
    --require-third-party (or REQUIRE_THIRD_PARTY=1) when the bundle must ship.
    """
    tp_dir = os.path.join(folder, "third-party")
    tp_zip = os.path.join(folder, "third-party.zip")
    if os.path.isdir(tp_dir):
        log(">>> packing third-party/ into third-party.zip", quiet)
        subprocess.run(
            [sys.executable, PACKAGE_RELEASE, "--dir", folder],
            cwd=PROJECT_ROOT,
            check=True,
            stdout=subprocess.DEVNULL if quiet else None,
        )
    elif not os.path.isfile(tp_zip):
        if required:
            raise SystemExit(
                "ERROR: third-party.zip/third-party/ missing and --require-third-party was given"
            )
        log(">>> no third-party bundle in this folder (opt-in, not shipped)", quiet)
    return tp_zip


def find_firmware_bin(folder):
    bins = [
        f for f in os.listdir(folder)
        if f.endswith(".bin") and f not in SKIP_BINS
    ]
    if len(bins) != 1:
        raise SystemExit(
            f"expected exactly one firmware .bin in {folder}, found {bins or 'none'}"
        )
    return bins[0]


def make_zip(folder, out_path):
    n = 0
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _dirs, files in os.walk(folder):
            for f in files:
                full = os.path.join(root, f)
                if os.path.abspath(full) == os.path.abspath(out_path):
                    continue
                z.write(full, os.path.relpath(full, folder))
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description="build the distributable release zip")
    ap.add_argument("--dir", default="", help="build folder (default: newest for --pack)")
    ap.add_argument("--env", default="", help="(informational) environment name")
    ap.add_argument("--pack", default="", help="pack label, e.g. CAM")
    ap.add_argument("--print-path", action="store_true", help="print only the zip path")
    ap.add_argument("--require-third-party", action="store_true",
                    help="fail when the LGPL bundle is missing (default: it is optional)")
    args = ap.parse_args()

    folder = args.dir or newest_build_dir(args.pack)
    if not folder:
        raise SystemExit("ERROR: build folder not found (use --dir or --pack)")
    folder = os.path.abspath(folder)
    if not os.path.isdir(folder):
        raise SystemExit(f"ERROR: not a folder: {folder}")

    quiet = args.print_path
    required = args.require_third_party or os.environ.get("REQUIRE_THIRD_PARTY", "0").strip().lower() in ("1", "true", "yes", "on")
    ensure_third_party_zip(folder, quiet, required)

    fw = find_firmware_bin(folder)
    out = os.path.join(folder, os.path.splitext(fw)[0] + ".zip")
    count = make_zip(folder, out)
    log(f">>> RELEASE ZIP: {out}  ({count} files)", quiet)

    if quiet:
        print(out)


if __name__ == "__main__":
    main()
