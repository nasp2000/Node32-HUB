#!/usr/bin/env python3
"""validate_third_party.py — keep SBOM/notices in sync with installed libraries.

Scans the PlatformIO libdeps of every environment for library names + versions
and compares them with the rows in THIRD_PARTY_NOTICES, then checks that the
generated third-party/ compliance bundle is present. Prints warnings so the
SBOM/notices are refreshed whenever a build pulls a new library or version.

Runs as a `pio run` pre-script (registered in platformio.ini) and standalone:

    python chatgpt/scripts/validate_third_party.py

Non-fatal by default (warnings only, exit 0) so builds keep working.
Set STRICT=1 (or pass --strict) to fail on drift — use before a release.
"""

import os
import re
import sys

try:
    _HERE = os.path.dirname(os.path.abspath(__file__))
    PROJECT_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
except NameError:
    # SCons exec() has no __file__; pre-scripts run with CWD = chatgpt/.
    PROJECT_ROOT = os.path.abspath(os.path.join(os.getcwd(), ".."))
CHATGPT_DIR = os.path.join(PROJECT_ROOT, "chatgpt")
NOTICE = os.path.join(PROJECT_ROOT, "THIRD_PARTY_NOTICES")
BUNDLE = os.path.join(CHATGPT_DIR, "build", "third-party")

# Libraries that are not PlatformIO libdeps entries (toolchain/framework).
SKIP_LIBS = {"integrity.dat", "framework-arduinoespressif32", "framework-arduinoespressif32-libs"}


def _norm(name):
    """Normalize a library name for comparison (case/space/separator-insensitive)."""
    return re.sub(r"[^a-z0-9]", "", (name or "").lower())


def _libdeps_roots():
    roots = []
    env_dir = os.environ.get("PLATFORMIO_LIBDEPS_DIR")
    if env_dir and os.path.isdir(env_dir):
        roots.append(env_dir)
    for rel in ((".pio-linux", "libdeps"), (".pio", "libdeps")):
        p = os.path.join(CHATGPT_DIR, *rel)
        if os.path.isdir(p) and p not in roots:
            roots.append(p)
    return roots


def _read_version(lib_dir):
    import json
    lj = os.path.join(lib_dir, "library.json")
    if os.path.exists(lj):
        try:
            return str(json.load(open(lj, encoding="utf-8", errors="ignore")).get("version") or "")
        except Exception:
            pass
    lp = os.path.join(lib_dir, "library.properties")
    if os.path.exists(lp):
        for line in open(lp, encoding="utf-8", errors="ignore"):
            m = re.match(r"version\s*=\s*(\S+)", line)
            if m:
                return m.group(1)
    return ""


def scan_libs():
    """Return {normalized_name: {'display': name, 'versions': set()}}."""
    libs = {}
    for root in _libdeps_roots():
        for env in os.listdir(root):
            envd = os.path.join(root, env)
            if not os.path.isdir(envd):
                continue
            for lib in os.listdir(envd):
                ld = os.path.join(envd, lib)
                if not os.path.isdir(ld) or lib in SKIP_LIBS:
                    continue
                key = _norm(lib)
                entry = libs.setdefault(key, {"display": lib, "versions": set()})
                v = _read_version(ld)
                if v:
                    entry["versions"].add(v)
    return libs


def parse_notice():
    """Return {normalized_name: version_cell_text} from THIRD_PARTY_NOTICES tables."""
    out = {}
    if not os.path.exists(NOTICE):
        return out
    for line in open(NOTICE, encoding="utf-8", errors="ignore"):
        if not line.lstrip().startswith("|"):
            continue
        cells = [c.strip().strip("`") for c in line.strip().strip("|").split("|")]
        if len(cells) < 3:
            continue
        name, version = cells[0], cells[1]
        if not name or set(name) <= set("-: "):
            continue
        # Drop parenthetical qualifiers, e.g. "lvgl/lvgl (s3_touch7 env only)".
        name = re.sub(r"\(.*?\)", "", name).strip()
        # Only keep rows whose 2nd column looks like a version (starts with a digit).
        if not re.match(r"^\d", version):
            continue
        out[_norm(name.split("/")[-1])] = version
    return out


def main():
    strict = "--strict" in sys.argv or os.environ.get("STRICT", "0").strip().lower() in ("1", "true", "yes", "on")
    libs = scan_libs()
    notice = parse_notice()
    warnings = []

    for key, entry in sorted(libs.items()):
        name = entry["display"]
        if key not in notice:
            warnings.append(f"library '{name}' {sorted(entry['versions']) or ['?']} is not in THIRD_PARTY_NOTICES")
            continue
        cell = notice[key]
        for v in entry["versions"]:
            if v and v not in cell:
                warnings.append(f"version drift '{name}': installed {v}, notices say '{cell}'")

    if not os.path.isdir(BUNDLE):
        warnings.append("third-party/ bundle missing — run "
                        "`python chatgpt/scripts/package_third_party.py --env <board>`")

    if warnings:
        print("[third-party] SBOM/notices may be out of date:")
        for w in warnings:
            print(f"  - {w}")
        print("[third-party] update SBOM.md + THIRD_PARTY_NOTICES (and REPOS/Node32-HUB/),")
        print("[third-party] then regenerate the bundle: "
              "python chatgpt/scripts/package_third_party.py --env <board>")
        if strict:
            return 1
    else:
        print(f"[third-party] SBOM/notices in sync ({len(libs)} libraries checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())


try:
    Import("env")  # noqa: F821 — SCons builtin, only defined under `pio run`
    _IN_SCONS = True
except NameError:
    _IN_SCONS = False

if _IN_SCONS:
    # Pre-script context: report drift as warnings only (never fail the build
    # here; release_check.sh uses --strict for the blocking gate).
    main()
