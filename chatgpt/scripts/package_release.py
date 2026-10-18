#!/usr/bin/env python3
"""package_release.py — zip a build folder for sharing/selling.

Default: a SINGLE archive inside the build folder, containing EVERYTHING
(firmware, bootloader/partitions, notices and the FULL third-party/ bundle
with LGPL sources + relinkable objects):

    <build folder>/<name>.zip

Optional split mode (--split) keeps the small firmware archive separate from
the heavy LGPL material (two zips), for when bandwidth matters:

    <build folder>/../<name>_firmware.zip           (licences-only third-party)
    <build folder>/../<name>_third-party-lgpl.zip   (full LGPL trees)

Usage:
  python chatgpt/scripts/package_release.py --dir chatgpt/build/esp32s3/S3_TOUCH7/v1.114/20260920_1633
  python chatgpt/scripts/package_release.py --env s3_touch7 --pack S3_TOUCH7
  python chatgpt/scripts/package_release.py --dir <folder> --split
"""

import argparse
import os
import shutil
import sys
import zipfile

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BUILD_ROOT = os.path.join(PROJECT_ROOT, "chatgpt", "build")

# Heavy LGPL trees, dropped from the small firmware zip in --split mode.
HEAVY_SUBDIRS = ("third-party/lgpl-sources", "third-party/lgpl-objects")


def _newest_build_dir(pack):
    cands = []
    if not os.path.isdir(BUILD_ROOT):
        return None
    for board in os.listdir(BUILD_ROOT):
        bdir = os.path.join(BUILD_ROOT, board)
        if not os.path.isdir(bdir):
            continue
        for ver in os.listdir(bdir):
            vdir = os.path.join(bdir, ver)
            if not os.path.isdir(vdir):
                continue
            for ts in os.listdir(vdir):
                d = os.path.join(vdir, ts)
                if os.path.isdir(d):
                    cands.append((os.path.getmtime(d), d))
    if not cands:
        return None
    if pack:
        pref = [c for c in cands if pack.lower() in c[1].lower()]
        if pref:
            cands = pref
    cands.sort(reverse=True)
    return cands[0][1]


def _should_skip(rel, include_heavy):
    rel = rel.replace("\\", "/")
    if not include_heavy:
        if any(rel == h or rel.startswith(h + "/") for h in HEAVY_SUBDIRS):
            return True
    # Never embed archives inside the archive.
    if rel.lower().endswith(".zip"):
        return True
    return False


def make_zip(src_dir, zip_path, include_heavy):
    n = 0
    zipl = os.path.abspath(zip_path)
    with zipfile.ZipFile(zipl, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _dirs, files in os.walk(src_dir):
            for f in files:
                full = os.path.join(root, f)
                if os.path.abspath(full) == zipl:
                    continue
                rel = os.path.relpath(full, src_dir)
                if _should_skip(rel, include_heavy):
                    continue
                z.write(full, rel)
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description="zip a Node32-HUB build folder for sharing")
    ap.add_argument("--dir", default="", help="build folder to package")
    ap.add_argument("--env", default="", help="(informational) env name")
    ap.add_argument("--pack", default="", help="pack label, e.g. S3_TOUCH7")
    ap.add_argument("--split", action="store_true",
                    help="two zips (small firmware + separate full LGPL bundle)")
    args = ap.parse_args()

    d = args.dir
    if not d:
        d = _newest_build_dir(args.pack)
        if d and args.env and args.env not in d:
            # keep whatever the newest is; env is informational here
            pass
    if not d:
        print("ERROR: build folder not found (use --dir or --pack)")
        return 1
    d = os.path.abspath(d)
    if not os.path.isdir(d):
        print(f"ERROR: not a folder: {d}")
        return 1
    name = os.path.basename(d)

    if args.split:
        fw_zip = os.path.abspath(os.path.join(d, "..", f"{name}_firmware.zip"))
        tp_zip = os.path.abspath(os.path.join(d, "..", f"{name}_third-party-lgpl.zip"))
        fw_cnt = make_zip(d, fw_zip, include_heavy=False)
        print(f">>> {fw_zip}  ({fw_cnt} files, licences-only third-party)")
        tp_src = os.path.join(d, "third-party")
        if os.path.isdir(tp_src):
            tp_cnt = make_zip(tp_src, tp_zip, include_heavy=True)
            print(f">>> {tp_zip}  ({tp_cnt} files, full LGPL sources + objects)")
        return 0

    # Default: the build folder keeps the firmware files loose; the big
    # third-party/ material is packed into a single zip inside the folder.
    tp_src = os.path.join(d, "third-party")
    if not os.path.isdir(tp_src):
        print(f"ERROR: no third-party/ in {d} (run package_third_party.py first)")
        return 1
    out = os.path.join(d, "third-party.zip")
    cnt = make_zip(tp_src, out, include_heavy=True)
    print(f">>> {out}  ({cnt} files, full LGPL sources + objects)")
    # Keep the folder clean: firmware loose + the zip only (no loose tree,
    # no empty dirs).
    shutil.rmtree(tp_src)
    print(">>> removed loose third-party/ (packed in third-party.zip)")
    print(">>> firmware files left outside the zip")
    return 0


if __name__ == "__main__":
    sys.exit(main())
