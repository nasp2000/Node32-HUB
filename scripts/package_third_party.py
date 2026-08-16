# package_third_party.py — assemble LGPL/third-party compliance materials
#
# Creates / refreshes the `third-party/` folder at the project root with the
# materials required to distribute firmware binaries that statically link
# LGPL libraries:
#
#   third-party/
#     lgpl-sources/     unchanged sources of the LGPL libraries
#     lgpl-objects/     relinkable objects (.o) compiled from those libs
#     licences/         LGPL-2.1 / LGPL-3.0 license texts
#
# Run before publishing a release. Nothing from this script is embedded in
# the firmware .bin; it only prepares the repo-side compliance folder.
#
# Usage:
#   python package_third_party.py                       # default env: n16r8
#   python package_third_party.py --env waveshare_8di8do
#   python package_third_party.py --dry-run             # report only

import argparse
import os
import shutil
import sys
import urllib.request

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CHATGPT_DIR = os.path.join(PROJECT_ROOT, "chatgpt")
OUT_DIR = os.path.join(PROJECT_ROOT, "third-party")

# (folder_name, libdeps_dir)
LGPL_LIBS = [
    ("AsyncTCP", "AsyncTCP"),
    ("ESPAsyncWebServer", "ESPAsyncWebServer"),
    ("CircularBuffer", "CircularBuffer"),
]

# Official FSF texts (fallback when not present in local packages).
LGPL_3_URL = "https://www.gnu.org/licenses/lgpl-3.0.txt"
LGPL_2_1_URL = "https://www.gnu.org/licenses/lgpl-2.1.txt"

LGPLLICENCE_FILENAMES = {
    "lgpl-3.0": "LGPL-3.0.txt",
    "lgpl-2.1": "LGPL-2.1.txt",
}


def find_framework_dir():
    for name in ("framework-arduinoespressif32", "framework-arduinoespressif32-libs"):
        c = os.path.expanduser(os.path.join("~", ".platformio", "packages", name))
        if os.path.isdir(c):
            return c
    return None


def find_build_dir(chatgpt_dir, env):
    d = os.path.join(chatgpt_dir, ".pio", "build", env)
    return d if os.path.isdir(d) else None


def find_lib_objects(build_dir, lib_name):
    """Find .o files of a library in the hashed PlatformIO lib build dirs."""
    if not build_dir:
        return []
    out = []
    for entry in os.listdir(build_dir):
        full = os.path.join(build_dir, entry)
        if not os.path.isdir(full):
            continue
        for lib_dir in os.listdir(full):
            if lib_dir == lib_name:
                lib_path = os.path.join(full, lib_name)
                if os.path.isdir(lib_path):
                    for root, _, files in os.walk(lib_path):
                        for f in files:
                            if f.endswith(".o"):
                                out.append(os.path.join(root, f))
    return out


def copy_tree(src, dst):
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def fetch_licence(kind, chatgpt_dir):
    """Return LGPL text for the requested major version.

    LGPL-3.0 prefers a real text shipped with an installed lib; LGPL-2.1
    only accepts local copies whose header actually says "Version 2", then
    falls back to the official FSF text. A wrong-version text is never
    written under a mismatched filename.
    """
    if kind == "lgpl-3.0":
        for lib in ("AsyncTCP", "ESPAsyncWebServer", "CircularBuffer"):
            p = os.path.join(chatgpt_dir, ".pio", "libdeps", "n16r8", lib, "LICENSE")
            if os.path.exists(p):
                with open(p, "r", encoding="utf-8", errors="ignore") as f:
                    return f.read()
        return _fetch_url_or_pointer(LGPL_3_URL)

    # LGPL-2.1 — local toolchain copies, but only if they really are 2.1.
    for pkg in ("toolchain-riscv32-esp", "toolchain-xtensa-esp-elf"):
        base = os.path.expanduser(os.path.join("~", ".platformio", "packages", pkg))
        if not os.path.isdir(base):
            continue
        for root, _, files in os.walk(base):
            if "licenses" not in root:
                continue
            for name in ("COPYING.LESSER", "COPYING.LESSER.2.1", "lgpl-2.1.txt"):
                if name in files:
                    path = os.path.join(root, name)
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        head = f.read(2000)
                    if "Version 2" in head or "version 2.1" in head:
                        with open(path, "r", encoding="utf-8", errors="ignore") as f:
                            return f.read()
    return _fetch_url_or_pointer(LGPL_2_1_URL)


def _fetch_url_or_pointer(url):
    """Download the official FSF text, or emit a pointer file when offline."""
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            return r.read().decode("utf-8", errors="ignore")
    except Exception as e:
        return f"# LGPL text unavailable offline ({url})\n# Download manually: {url}\n({e})\n"


def main():
    ap = argparse.ArgumentParser(description="assemble third-party compliance folder")
    ap.add_argument("--env", default="n16r8", help="PlatformIO env to pull relinkable objects from")
    ap.add_argument("--source", default=None,
                    help="path to a local Node32-HUB build clone (default: <repo>/chatgpt)")
    ap.add_argument("--dry-run", action="store_true", help="report what would be copied")
    args = ap.parse_args()

    chatgpt_dir = os.path.abspath(args.source) if args.source else CHATGPT_DIR
    print(f"Project root : {PROJECT_ROOT}")
    print(f"Source dir   : {chatgpt_dir}")
    print(f"Output dir   : {OUT_DIR}\n")

    build_dir = find_build_dir(chatgpt_dir, args.env)
    framework_dir = find_framework_dir()
    licences_dir = os.path.join(OUT_DIR, "licences")

    # ---- 1. LGPL library sources from libdeps ----
    for folder, lib in LGPL_LIBS:
        src = os.path.join(chatgpt_dir, ".pio", "libdeps", args.env, lib)
        if not os.path.isdir(src):
            src = os.path.join(chatgpt_dir, ".pio", "libdeps", "n16r8", lib)
        if os.path.isdir(src):
            print(f"[src ] {lib}  -> third-party/lgpl-sources/{folder}/")
            if not args.dry_run:
                copy_tree(src, os.path.join(OUT_DIR, "lgpl-sources", folder))
        else:
            print(f"[warn] libdeps source not found for {lib} (env {args.env})")

    # ---- 2. arduino-esp32 framework source ----
    if framework_dir:
        print("[src ] arduino-esp32 framework -> third-party/lgpl-sources/arduino-esp32/")
        if not args.dry_run:
            copy_tree(framework_dir, os.path.join(OUT_DIR, "lgpl-sources", "arduino-esp32"))
    else:
        print("[warn] arduino-esp32 framework not found in ~/.platformio/packages")

    # ---- 3. Relinkable objects from the build tree ----
    for folder, lib in LGPL_LIBS:
        objs = find_lib_objects(build_dir, lib)
        if objs:
            dst = os.path.join(OUT_DIR, "lgpl-objects", folder)
            if not args.dry_run:
                os.makedirs(dst, exist_ok=True)
            for o in objs:
                if not args.dry_run:
                    shutil.copy2(o, os.path.join(dst, os.path.basename(o)))
                print(f"[obj ] {lib}  {os.path.basename(o)}")
        else:
            print(f"[warn] no relinkable objects found for {lib} (build env {args.env}); "
                  f"run `pio run -e {args.env}` first")

    # ---- 4. LGPL licence texts ----
    for kind, filename in LGPLLICENCE_FILENAMES.items():
        text = fetch_licence(kind, chatgpt_dir)
        print(f"[lic ] {filename}")
        if not args.dry_run:
            os.makedirs(licences_dir, exist_ok=True)
            with open(os.path.join(licences_dir, filename), "w", encoding="utf-8") as f:
                f.write(text)

    if not args.dry_run:
        print(f"\nDone. Compliance folder: {OUT_DIR}")
        print("Referenced by THIRD_PARTY_NOTICES (repo root).")


if __name__ == "__main__":
    sys.exit(main())