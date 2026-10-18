# package_third_party.py — assemble third-party compliance materials
#
# Creates / refreshes the `third-party/` folder at the project root with the
# materials required to distribute firmware binaries that statically link
# LGPL libraries (and to honour the attribution clauses of MIT/BSD/Apache):
#
#   third-party/
#     README.txt        what this bundle is + the LGPL written offer
#     THIRD_PARTY_NOTICES  copy of the repo-root notice
#     lgpl-sources/     unchanged sources of the LGPL libraries
#     lgpl-objects/     relinkable objects (.o) compiled from those libs
#     licences/         LGPL-2.1 / LGPL-3.0 / Apache-2.0 texts
#     licences/permissive/  per-library MIT / BSD / Apache / permissive texts
#
# Run before publishing a release. Nothing from this script is embedded in
# the firmware .bin; it only prepares the repo-side compliance folder.
# `rename_firmware.py` copies this folder (plus LICENSE/THIRD_PARTY_NOTICES/
# SBOM.md) into every per-build output folder so each build is distributable.
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

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CHATGPT_DIR = os.path.join(PROJECT_ROOT, "chatgpt")
OUT_DIR = os.path.join(CHATGPT_DIR, "build", "third-party")

# LGPL libraries statically linked by the firmware.
#   (folder_name, libdeps_name)
LGPL_LIBS = [
    ("AsyncTCP", "AsyncTCP"),
    ("ESPAsyncWebServer", "ESPAsyncWebServer"),
    ("CircularBuffer", "CircularBuffer"),
]

# Permissive libraries (MIT / BSD / Apache-2.0 / permissive). Their license
# texts are reproduced under licences/permissive/.
#   (libdeps_name, output_stem, SPDX)
PERMISSIVE_LIBS = [
    ("ArduinoJson", "ArduinoJson", "MIT"),
    ("ReadyMail", "ReadyMail", "MIT"),
    ("ESP_SSLClient", "ESP_SSLClient", "MIT"),
    ("Adafruit BMP280 Library", "Adafruit_BMP280", "MIT"),
    ("Adafruit BusIO", "Adafruit_BusIO", "MIT"),
    ("Ethernet", "Ethernet", "MIT"),
    ("SdFat", "SdFat", "MIT"),
    ("Adafruit SSD1306", "Adafruit_SSD1306", "BSD-3-Clause"),
    ("Adafruit SH110X", "Adafruit_SH110X", "BSD-3-Clause"),
    ("Adafruit GFX Library", "Adafruit_GFX", "BSD-3-Clause"),
    ("Adafruit AHTX0", "Adafruit_AHTX0", "BSD-3-Clause"),
    ("Adafruit Unified Sensor", "Adafruit_Unified_Sensor", "Apache-2.0"),
    ("ESP32_Display_Panel", "ESP32_Display_Panel", "Apache-2.0"),
    ("ESP32_IO_Expander", "ESP32_IO_Expander", "Apache-2.0"),
    ("esp-lib-utils", "esp_lib_utils", "Apache-2.0"),
    ("ESP_Video", "ESP_Video", "Apache-2.0"),
    ("lvgl", "lvgl", "MIT"),
    ("JPEGDecoder", "JPEGDecoder", "ChaN permissive"),
]

LICENSE_CANDIDATES = (
    "LICENSE", "LICENSE.txt", "LICENSE.md", "license.txt", "license.md",
    "LICENCE", "LICENCE.txt", "COPYING", "COPYING.txt",
)

# Official texts (fallback when not present in local packages).
LGPL_3_URL = "https://www.gnu.org/licenses/lgpl-3.0.txt"
LGPL_2_1_URL = "https://www.gnu.org/licenses/lgpl-2.1.txt"
APACHE_2_URL = "https://www.apache.org/licenses/LICENSE-2.0.txt"

LGPLLICENCE_FILENAMES = {
    "lgpl-3.0": "LGPL-3.0.txt",
    "lgpl-2.1": "LGPL-2.1.txt",
}

# Canonical texts used when a library ships no LICENSE file (still reproduced
# so the bundle is self-contained; the per-library copyright lives in that
# library's own source headers / AUTHORS file).
MIT_TEXT = """MIT License

Copyright (c) <year> <copyright holders>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

BSD3_TEXT = """BSD 3-Clause License

Copyright (c) <year>, <copyright holder>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""


def canonical_license(spdx):
    """Standard text for a permissive SPDX when the library ships none."""
    if spdx == "MIT":
        return MIT_TEXT
    if spdx == "BSD-3-Clause":
        return BSD3_TEXT
    if spdx == "Apache-2.0":
        return fetch_licence("apache")
    return None


def find_framework_dir():
    for name in ("framework-arduinoespressif32", "framework-arduinoespressif32-libs"):
        c = os.path.expanduser(os.path.join("~", ".platformio", "packages", name))
        if os.path.isdir(c):
            return c
    return None


def _first_existing(*paths):
    for p in paths:
        if p and os.path.isdir(p):
            return p
    return None


def find_build_dir(env):
    """Resolve the PlatformIO build dir for `env`.

    build_firmware.sh exports PLATFORMIO_BUILD_DIR (chatgpt/.pio-linux/build);
    platformio.ini sets build_dir=/tmp/pio-build; a plain `pio run` uses
    .pio/build. Honour all of them.
    """
    candidates = []
    env_dir = os.environ.get("PLATFORMIO_BUILD_DIR")
    if env_dir:
        candidates.append(os.path.join(env_dir, env))
    candidates += [
        os.path.join("/tmp/pio-build", env),
        os.path.join(CHATGPT_DIR, ".pio-linux", "build", env),
        os.path.join(CHATGPT_DIR, ".pio", "build", env),
    ]
    return _first_existing(*candidates)


def find_libdeps_dir(env):
    """Resolve the PlatformIO libdeps dir for `env` (honours LIBDEPS_DIR)."""
    candidates = []
    env_dir = os.environ.get("PLATFORMIO_LIBDEPS_DIR")
    if env_dir:
        candidates.append(os.path.join(env_dir, env))
    candidates += [
        os.path.join(CHATGPT_DIR, ".pio-linux", "libdeps", env),
        os.path.join(CHATGPT_DIR, ".pio", "libdeps", env),
    ]
    return _first_existing(*candidates)


def find_lib_objects(build_dir, lib_name):
    """Find .o files of a library in the hashed PlatformIO lib build dirs."""
    if not build_dir:
        return []
    out = []
    for entry in os.listdir(build_dir):
        full = os.path.join(build_dir, entry)
        if not os.path.isdir(full):
            continue
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


def find_license_file(lib_dir):
    if not lib_dir or not os.path.isdir(lib_dir):
        return None
    for name in LICENSE_CANDIDATES:
        p = os.path.join(lib_dir, name)
        if os.path.exists(p):
            return p
    return None


def find_license_in_all_envs(lib):
    """Look for a library's LICENSE in any installed env (a lib may only be
    pulled by another board, e.g. ESP_Video for esp32p4cam)."""
    for root in (os.path.join(CHATGPT_DIR, ".pio-linux", "libdeps"),
                 os.path.join(CHATGPT_DIR, ".pio", "libdeps")):
        if not os.path.isdir(root):
            continue
        for env in os.listdir(root):
            p = find_license_file(os.path.join(root, env, lib))
            if p:
                return p
    return None


def fetch_licence(kind):
    """Return an official license text for the requested kind.

    LGPL-3.0 prefers a real text shipped with an installed lib; LGPL-2.1
    only accepts local copies whose header actually says "Version 2", then
    falls back to the official FSF text. A wrong-version text is never
    written under a mismatched filename.
    """
    if kind == "lgpl-3.0":
        for lib in ("AsyncTCP", "ESPAsyncWebServer", "CircularBuffer"):
            for env in ("n16r8",):
                for root in (os.path.join(CHATGPT_DIR, ".pio-linux", "libdeps"),
                             os.path.join(CHATGPT_DIR, ".pio", "libdeps")):
                    p = find_license_file(os.path.join(root, env, lib))
                    if p:
                        with open(p, "r", encoding="utf-8", errors="ignore") as f:
                            return f.read()
        return _fetch_url_or_pointer(LGPL_3_URL)

    if kind == "lgpl-2.1":
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

    return _fetch_url_or_pointer(APACHE_2_URL)


def _fetch_url_or_pointer(url):
    """Download the official text, or emit a pointer file when offline."""
    try:
        with urllib.request.urlopen(url, timeout=20) as r:
            return r.read().decode("utf-8", errors="ignore")
    except Exception as e:
        return f"# License text unavailable offline ({url})\n# Download manually: {url}\n({e})\n"


def write_bundle_readme(out_dir, env):
    path = os.path.join(out_dir, "README.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(
            "Node32-HUB — third-party compliance bundle\n"
            "==========================================\n\n"
            "This folder accompanies Node32-HUB firmware binaries so they can be\n"
            "redistributed (shared or sold). Nothing here is embedded in the .bin;\n"
            "it is the license/attribution material required when you convey the\n"
            "firmware to someone else.\n\n"
            "Contents:\n"
            "  THIRD_PARTY_NOTICES      full list of bundled libraries and licenses\n"
            "  licences/                LGPL-2.1, LGPL-3.0 and Apache-2.0 texts\n"
            "  licences/permissive/     per-library MIT / BSD / Apache / permissive texts\n"
            "  lgpl-sources/            unchanged sources of the LGPL libraries\n"
            "  lgpl-objects/<env>/      relinkable objects compiled from those libraries\n\n"
            "LGPL written offer\n"
            "------------------\n"
            "The firmware statically links LGPL-2.1 and LGPL-3.0 libraries\n"
            "(AsyncTCP, ESPAsyncWebServer, CircularBuffer and the arduino-esp32 core).\n"
            "In accordance with the LGPL, the complete corresponding source code and\n"
            "the relinkable object files needed to relink a modified version of those\n"
            "libraries are provided in lgpl-sources/ and lgpl-objects/.\n\n"
            "See the repository for the exact versions and the full notice:\n"
            "  https://github.com/nasp2000/Node32-HUB\n\n"
            f"Objects in this bundle were collected from PlatformIO env '{env}'.\n"
        )
    return path


def main():
    ap = argparse.ArgumentParser(description="assemble third-party compliance folder")
    ap.add_argument("--env", default="n16r8", help="PlatformIO env to pull relinkable objects from")
    ap.add_argument("--dry-run", action="store_true", help="report what would be copied")
    args = ap.parse_args()

    print(f"Project root : {PROJECT_ROOT}")
    print(f"Output dir   : {OUT_DIR}\n")

    build_dir = find_build_dir(args.env)
    libdeps_dir = find_libdeps_dir(args.env)
    framework_dir = find_framework_dir()
    licences_dir = os.path.join(OUT_DIR, "licences")
    permissive_dir = os.path.join(licences_dir, "permissive")

    print(f"Build dir    : {build_dir or '(not found)'}")
    print(f"Libdeps dir  : {libdeps_dir or '(not found)'}\n")

    # Refresh the shared sources, but only THIS env's relinkable objects:
    # different architectures (S3 Xtensa vs P4 RISC-V) must coexist in the
    # bundle, so never wipe another env's object tree.
    if not args.dry_run:
        p = os.path.join(OUT_DIR, "lgpl-sources")
        if os.path.isdir(p):
            shutil.rmtree(p)
        p = os.path.join(OUT_DIR, "lgpl-objects", args.env)
        if os.path.isdir(p):
            shutil.rmtree(p)

    # ---- 1. LGPL library sources ----
    for folder, lib in LGPL_LIBS:
        src = os.path.join(libdeps_dir, lib) if libdeps_dir else None
        if src and os.path.isdir(src):
            print(f"[src ] {lib}  -> third-party/lgpl-sources/{folder}/")
            if not args.dry_run:
                copy_tree(src, os.path.join(OUT_DIR, "lgpl-sources", folder))
        else:
            print(f"[warn] libdeps source not found for {lib} (env {args.env})")

    # ---- 2. arduino-esp32 framework source (LGPL-2.1) ----
    if framework_dir:
        print("[src ] arduino-esp32 framework -> third-party/lgpl-sources/arduino-esp32/")
        if not args.dry_run:
            copy_tree(framework_dir, os.path.join(OUT_DIR, "lgpl-sources", "arduino-esp32"))
    else:
        print("[warn] arduino-esp32 framework not found in ~/.platformio/packages")

    # ---- 3. Relinkable objects from the build tree (namespaced per env, since
    #         different boards/architectures produce different object files) ----
    for folder, lib in LGPL_LIBS:
        objs = find_lib_objects(build_dir, lib)
        if objs:
            dst = os.path.join(OUT_DIR, "lgpl-objects", args.env, folder)
            if not args.dry_run:
                os.makedirs(dst, exist_ok=True)
            for o in objs:
                if not args.dry_run:
                    shutil.copy2(o, os.path.join(dst, os.path.basename(o)))
                print(f"[obj ] {lib}  {os.path.basename(o)}")
        else:
            # Header-only libs (e.g. CircularBuffer) have no compile units, so
            # there is nothing relinkable to ship; that is not a warning.
            src_root = os.path.join(libdeps_dir, lib) if libdeps_dir else ""
            has_src = False
            for _r, _d, fs in os.walk(src_root):
                if any(f.endswith((".c", ".cpp", ".cc", ".cxx", ".S")) for f in fs):
                    has_src = True
                    break
            if has_src:
                print(f"[warn] no relinkable objects found for {lib} (build env {args.env}); "
                      f"run `pio run -e {args.env}` first")
            else:
                print(f"[info] {lib}: header-only (no relinkable objects to ship)")

    # ---- 4. License texts (LGPL + Apache) ----
    for kind, filename in LGPLLICENCE_FILENAMES.items():
        text = fetch_licence(kind)
        print(f"[lic ] {filename}")
        if not args.dry_run:
            os.makedirs(licences_dir, exist_ok=True)
            with open(os.path.join(licences_dir, filename), "w", encoding="utf-8") as f:
                f.write(text)
    apache = fetch_licence("apache")
    print("[lic ] Apache-2.0.txt")
    if not args.dry_run:
        os.makedirs(licences_dir, exist_ok=True)
        with open(os.path.join(licences_dir, "Apache-2.0.txt"), "w", encoding="utf-8") as f:
            f.write(apache)

    # ---- 5. Permissive per-library license texts ----
    copied = 0
    for lib, stem, spdx in PERMISSIVE_LIBS:
        src = find_license_file(os.path.join(libdeps_dir, lib)) if libdeps_dir else None
        if not src:
            src = find_license_in_all_envs(lib)
        if src:
            copied += 1
            print(f"[lic ] {spdx:16} {lib}  -> licences/permissive/{stem}.txt")
            if not args.dry_run:
                os.makedirs(permissive_dir, exist_ok=True)
                with open(src, "r", encoding="utf-8", errors="ignore") as f:
                    text = f.read()
                with open(os.path.join(permissive_dir, f"{stem}.txt"), "w", encoding="utf-8") as f:
                    f.write(text)
            continue
        text = canonical_license(spdx)
        if text is None:
            print(f"[warn] no license text for {lib} ({spdx})")
            continue
        copied += 1
        print(f"[lic ] {spdx:16} {lib}  -> licences/permissive/{stem}.txt (canonical)")
        if not args.dry_run:
            os.makedirs(permissive_dir, exist_ok=True)
            with open(os.path.join(permissive_dir, f"{stem}.txt"), "w", encoding="utf-8") as f:
                f.write(text)

    # ---- 6. Bundle README + notice copy ----
    notice_src = os.path.join(PROJECT_ROOT, "THIRD_PARTY_NOTICES")
    if not args.dry_run:
        os.makedirs(OUT_DIR, exist_ok=True)
        if os.path.exists(notice_src):
            shutil.copy2(notice_src, os.path.join(OUT_DIR, "THIRD_PARTY_NOTICES"))
        write_bundle_readme(OUT_DIR, args.env)

    if not args.dry_run:
        print(f"\nDone. Compliance folder: {OUT_DIR}")
        print(f"  permissive license texts: {copied}/{len(PERMISSIVE_LIBS)}")
        print("Referenced by THIRD_PARTY_NOTICES (repo root); copied into every")
        print("build folder by rename_firmware.py.")


if __name__ == "__main__":
    sys.exit(main())
