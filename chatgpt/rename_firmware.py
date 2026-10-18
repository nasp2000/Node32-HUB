# rename_firmware.py — PlatformIO post-build script
#
# Runs automatically after every successful build (extra_scripts = post:rename_firmware.py).
# Copies firmware to build/<board>/<pack|user_config>/<version>/<build_timestamp> with a short versioned name.
#
# Output (example):
#   chatgpt/build/esp32p4/Rambox/v2.29/20260623_1430/
#     esp32p4_v2.29_Rambox_20260623_1430.bin <- OTA upload via Web UI -> /ota
#     bootloader.bin                       <- first-time esptool flash only
#     partitions.bin                       <- first-time esptool flash only
#     flash_command.txt                    <- WebFlasher / OTA / esptool instructions

try:
    env = Import("env")  # noqa: F821  (SCons builtin — only defined under pio run)
except NameError:
    env = None
if env is None:
    # Import("env") is not available in some extra_script contexts; fall back to
    # the default SCons environment, which is the project env under `pio run`.
    try:
        from SCons.Script import DefaultEnvironment  # pylint: disable=import-outside-toplevel
        env = DefaultEnvironment()
    except Exception:
        env = None
print("[rename_firmware] extra_script loaded; env = %s" % ("SET" if env is not None else "None"))

import os
import re
import shutil
import subprocess
import sys
import zipfile
from datetime import datetime


def read_firmware_version(project_dir):
    """Read FIRMWARE_VERSION_STR from src/smtp_core_impl.h."""
    path = os.path.join(project_dir, "src", "smtp_core_impl.h")
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                m = re.search(r'#define\s+FIRMWARE_VERSION_STR\s+"([^"]+)"', line)
                if m:
                    return m.group(1)
    except Exception:
        pass
    return "vX.XX"


# Map numeric SELECTED_PACK values to pack labels. Kept uppercase so the folder
# name is identical whether the pack came from the bat or a plain `pio run`.
PACK_ID_MAP = {
    "0": "",            # PACK_NONE
    "1": "RAMBOX",      # PACK_RAMBOX
    "2": "CAM",         # PACK_CAM
    "3": "RS232",       # PACK_RS232
    "4": "ENVIRONMENT", # PACK_ENVIRONMENT
    "5": "RADIO",       # PACK_RADIO
    "6": "P4_CNC",      # PACK_P4_CNC
    "7": "P4_USB_SERVER",  # PACK_P4_USB_SERVER
    "8": "S3_CNC",      # PACK_S3_CNC
    "10": "WAVESHARE",  # PACK_WAVESHARE
    "11": "CAM_P4",     # PACK_CAM_P4
    "12": "S3_TOUCH7",  # PACK_S3_TOUCH7 (TouchMeteo-7)
}


def read_user_config_pack(project_dir):
    """Read the active (non-commented) SELECTED_PACK define from user_config.h.

    user_config.h ships with `#define SELECTED_PACK PACK_S3_CNC` as the default
    for N16R8. That define is not a compile flag, so it never appears in
    CPPDEFINES — without this fallback the default build would be mislabeled
    `user_config` instead of its real pack name.
    """
    path = os.path.join(project_dir, "user_config.h")
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                m = re.match(r'^\s*#define\s+SELECTED_PACK\s+(PACK_\w+)\b', line)
                if m:
                    return m.group(1)
    except Exception:
        pass
    return None


def resolve_pack_name(pack_name, cpp_defines, user_config_pack=None):
    """Resolve the output folder label for a build.

    Priority: explicit PACK_NAME (from build_firmware.bat), then the real
    compile-time SELECTED_PACK define, then the user_config.h source default.
    "NONE"/empty means "no pack chosen" and must NOT mask a SELECTED_PACK the
    build forces (platformio.ini hardcodes PACK_WAVESHARE on waveshare_8di8do
    and PACK_P4_CNC on esp32p4) — otherwise the .bin lands in the user_config
    folder while the firmware is a pack build.

    Returns "" only when the effective pack really is PACK_NONE (user_config
    folder); any active pack resolves to its uppercase name.
    """
    pack_name = (pack_name or "").strip()
    if pack_name.upper() == "NONE":
        pack_name = ""
    if not pack_name:
        found_define = False
        for define in cpp_defines or []:
            if isinstance(define, tuple) and len(define) == 2 and define[0] == "SELECTED_PACK":
                found_define = True
                val = str(define[1]).strip()
                if val.startswith("PACK_") and val != "PACK_NONE":
                    pack_name = val[5:]
                elif val not in ("0", "PACK_NONE", "NONE"):
                    pack_name = val
                break
            if isinstance(define, str) and define.startswith("SELECTED_PACK="):
                found_define = True
                val = define.split("=", 1)[1].strip()
                if val.startswith("PACK_") and val != "PACK_NONE":
                    pack_name = val[5:]
                elif val not in ("0", "PACK_NONE", "NONE"):
                    pack_name = val
                break
        # Only fall back to the user_config.h source default when the build did
        # not pass any SELECTED_PACK flag at all (an explicit PACK_NONE define
        # must win over the source default).
        if not found_define and user_config_pack:
            val = (user_config_pack or "").strip()
            if val.startswith("PACK_") and val != "PACK_NONE":
                pack_name = val[5:]
    if pack_name.upper() == "NONE":
        pack_name = ""
    if pack_name in PACK_ID_MAP:
        pack_name = PACK_ID_MAP[pack_name]
    if pack_name:
        pack_name = pack_name.upper()
    return pack_name


def copy_compliance(project_dir, out_dir, env_name, include_third_party=True):
    """Copy the license/compliance material into the build folder.

    Makes each build folder self-contained for redistribution (share or sell):
    LICENSE + THIRD_PARTY_NOTICES + SBOM.md + the generated third-party/ bundle
    (LGPL sources/objects + license texts). The heavy LGPL trees can be skipped
    by setting DIST_LGPL_MATERIALS=0 (notices/license texts are still copied).
    When include_third_party is False the LGPL bundle is neither generated nor
    copied (opt-in: build_firmware.sh asks "Include third-party .zip", default N).
    """
    copied = []
    # project_dir is the build CWD (chatgpt/); the repo root is one level up.
    # Accept a repo root too (standalone/tests).
    if os.path.isdir(os.path.join(project_dir, "chatgpt")):
        repo_root = project_dir
    else:
        repo_root = os.path.dirname(project_dir)
    chatgpt_dir = os.path.join(repo_root, "chatgpt")

    for name in ("LICENSE", "THIRD_PARTY_NOTICES", "SBOM.md"):
        src = os.path.join(repo_root, name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(out_dir, name))
            copied.append(name)

    if not include_third_party:
        print(">>> third-party compliance bundle skipped (DIST_THIRD_PARTY=0)")
        return copied

    # Refresh the LGPL bundle for THIS board first: the relinkable objects are
    # per architecture (S3 Xtensa vs P4 RISC-V), so each env needs its own copy.
    # Best-effort — never fail the build if this cannot run.
    pkg = os.path.join(chatgpt_dir, "scripts", "package_third_party.py")
    if os.path.isfile(pkg):
        try:
            r = subprocess.run([sys.executable, pkg, "--env", env_name], cwd=repo_root,
                               check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if r.returncode != 0:
                print(f">>> WARNING: third-party bundle refresh exited {r.returncode}")
        except Exception as e:
            print(f">>> WARNING: third-party bundle refresh failed: {e}")

    tp_src = os.path.join(chatgpt_dir, "build", "third-party")
    if not os.path.isdir(tp_src):
        print(f">>> WARNING: third-party bundle not found ({tp_src}) - run "
              f"`python chatgpt/scripts/package_third_party.py --env {env_name}` before publishing")
        return copied

    tp_dst = os.path.join(out_dir, "third-party")
    if os.path.exists(tp_dst):
        shutil.rmtree(tp_dst)
    include_lgpl = os.environ.get("DIST_LGPL_MATERIALS", "1").strip().lower() not in ("0", "false", "no", "n", "off")
    if include_lgpl:
        shutil.copytree(tp_src, tp_dst)
        copied.append("third-party/")
    else:
        # Keep the small, required parts (notices + license texts), skip the
        # large LGPL source/object trees.
        os.makedirs(tp_dst, exist_ok=True)
        for sub in ("licences", "README.txt", "THIRD_PARTY_NOTICES"):
            s = os.path.join(tp_src, sub)
            d = os.path.join(tp_dst, sub)
            if os.path.isdir(s):
                shutil.copytree(s, d)
            elif os.path.exists(s):
                shutil.copy2(s, d)
        copied.append("third-party/ (licences only)")
    # Pack the compliance material into a single zip inside the folder and drop
    # the loose tree, so the build output is clean with no manual step.
    if os.path.isdir(tp_dst):
        zip_path = os.path.join(out_dir, "third-party.zip")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
            for root, _dirs, files in os.walk(tp_dst):
                for f in files:
                    full = os.path.join(root, f)
                    z.write(full, os.path.relpath(full, tp_dst))
        shutil.rmtree(tp_dst)
        copied = [c for c in copied if not c.startswith("third-party")]
        copied.append("third-party.zip")
    return copied


def write_distribution_readme(out_dir, fw_name, env_name, board_name, include_third_party=True):
    """Human-readable note explaining what this build folder contains."""
    path = os.path.join(out_dir, "README_DISTRIBUTION.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("Node32-HUB — firmware build folder\n")
        f.write("==================================\n\n")
        f.write(f"Firmware : {fw_name}\n")
        f.write(f"Board    : {board_name}   Env: {env_name}\n\n")
        f.write("This folder is self-contained and may be shared or sold:\n")
        f.write("  <firmware>.bin          the firmware image (OTA upload via /ota)\n")
        f.write("  bootloader.bin          first-time esptool flash only\n")
        f.write("  partitions.bin          first-time esptool flash only\n")
        f.write("  flash_command.txt       WebFlasher / OTA / esptool instructions\n")
        f.write("  LICENSE                 first-party MIT license\n")
        f.write("  THIRD_PARTY_NOTICES     bundled libraries and their licenses\n")
        f.write("  SBOM.md                 software bill of materials\n")
        if include_third_party:
            f.write("  third-party.zip         LGPL compliance: sources, relinkable\n")
            f.write("                          objects and all license texts (unzip to view)\n\n")
            f.write("The firmware statically links LGPL-2.1 / LGPL-3.0 libraries. When you\n")
            f.write("convey it to someone else, keep this folder (with third-party.zip)\n")
            f.write("together with the binary so the recipient has the required source,\n")
            f.write("relinkable objects and license texts. See third-party.zip/README.txt.\n\n")
        else:
            f.write("\n")
            f.write("NOTE: third-party.zip (LGPL sources + relinkable objects) is NOT in\n")
            f.write("this folder. If you redistribute/sell the .bin, generate it first:\n")
            f.write("  python chatgpt/scripts/package_third_party.py --env <env>\n")
            f.write("then keep third-party/ (or its zip) with this folder.\n\n")
        f.write("Project: https://github.com/nasp2000/Node32-HUB\n")
    return path


def copy_artifacts(project_dir, build_dir, env_name, pack_name, include_elf_map=False, include_third_party=True):
    version     = read_firmware_version(project_dir)
    date_str    = datetime.now().strftime("%Y%m%d_%H%M")

    board_name = "esp32p4" if "p4" in env_name.lower() else "esp32s3"
    pack_dir = "user_config" if not pack_name else re.sub(r"[^A-Za-z0-9_-]", "", pack_name)

    # Destination: chatgpt/build/<board>/<pack>/<version>/<build_timestamp>/
    out_dir = os.path.join(project_dir, "build", board_name, pack_dir, version, date_str)
    os.makedirs(out_dir, exist_ok=True)

    # Short filename: <env>_<version>_<pack|user_config>_<date>.bin
    base_name = f"{env_name}_{version}_{date_str}"
    if pack_dir:
        base_name = f"{env_name}_{version}_{pack_dir}_{date_str}"
    fw_src  = os.path.join(build_dir, "firmware.bin")
    fw_name = f"{base_name}.bin"
    fw_dst  = os.path.join(out_dir, fw_name)

    if os.path.exists(fw_src):
        shutil.copy2(fw_src, fw_dst)
        # Sign the firmware binary with RSA-2048 + chip magic (post-build OTA security)
        chip = "P4__" if "p4" in env_name.lower() else "S3__"
        sign_script = os.path.join(project_dir, "scripts", "sign_firmware.py")
        if os.path.exists(sign_script):
            try:
                subprocess.run(
                    [sys.executable, sign_script, "--chip", chip, fw_dst],
                    cwd=project_dir, check=True, capture_output=True, text=True
                )
                signed_path = fw_dst.replace(".bin", "_signed.bin")
                if os.path.exists(signed_path):
                    os.replace(signed_path, fw_dst)
                    print(f">>> OTA signed  : {fw_name} (chip={chip})")
            except subprocess.CalledProcessError as e:
                print(f">>> WARNING: OTA signing failed: {e.stderr.strip()}")
        else:
            print(f">>> WARNING: sign_firmware.py not found, skipping OTA signing")
        size_kb = os.path.getsize(fw_dst) / 1024
        print(f"\n>>> OTA binary  : build/{board_name}/{pack_dir}/{version}/{date_str}/{fw_name}  ({size_kb:.1f} KB)")
    else:
        print(f"\n>>> WARNING: firmware.bin not found at {fw_src}")
        return

    if include_elf_map:
        elf_src = os.path.join(build_dir, "firmware.elf")
        if os.path.exists(elf_src):
            elf_name = f"{base_name}.elf"
            shutil.copy2(elf_src, os.path.join(out_dir, elf_name))
            elf_kb = os.path.getsize(elf_src) / 1024
            print(f">>> ELF (debug) : build/{board_name}/{pack_dir}/{version}/{date_str}/{elf_name}  ({elf_kb:.1f} KB)")
        else:
            print(f">>> WARNING: firmware.elf not found at {elf_src}")

        map_src = os.path.join(build_dir, "firmware.map")
        if os.path.exists(map_src):
            map_name = f"{base_name}.map"
            shutil.copy2(map_src, os.path.join(out_dir, map_name))
            map_kb = os.path.getsize(map_src) / 1024
            print(f">>> Linker map  : build/{board_name}/{pack_dir}/{version}/{date_str}/{map_name}  ({map_kb:.1f} KB)")
    else:
        print(">>> Optional artifacts disabled: skipping .elf/.map copy")

    # bootloader.bin and partitions.bin — needed for first-time esptool flash only.
    # Overwrite without date suffix (they rarely change between builds).
    for fname in ("bootloader.bin", "partitions.bin"):
        src = os.path.join(build_dir, fname)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(out_dir, fname))

    # Ready-to-paste esptool command
    chip = "esp32p4" if "p4" in env_name.lower() else "esp32s3"
    bootloader_offset = "0x2000" if chip == "esp32p4" else "0x0000"
    flash_cmd = (
        f"esptool.py --chip {chip} --baud 921600 write_flash "
        f"{bootloader_offset} bootloader.bin "
        f"0x8000 partitions.bin "
        f"0x10000 {fw_name}"
    )
    cmd_path = os.path.join(out_dir, "flash_command.txt")
    with open(cmd_path, "w", encoding="utf-8") as f:
        f.write("=" * 66 + "\n")
        f.write(f"Node32-HUB firmware: {fw_name}\n")
        f.write(f"Board: {board_name}   Pack: {pack_dir or 'user_config'}   Version: {version}\n")
        f.write("=" * 66 + "\n\n")

        f.write("# Option 1 - Node32-HUB WebFlasher (browser, no Python/esptool) - recommended for first flash\n")
        f.write("#   https://github.com/nasp2000/webflasher_Node32-HUB\n")
        f.write("#   WebSerial flasher for ESP32-S3 (N16R8) and ESP32-P4. Needs Chrome or Edge.\n")
        f.write("#\n")
        f.write("#   1. Connect the board via USB.\n")
        f.write("#   2. Run start-dual.bat (or serve the HTML on http://localhost:8765/).\n")
        f.write("#   3. Click Connect and pick the serial port - the chip is auto-detected.\n")
        f.write("#   4. Choose the mode:\n")
        f.write("#        Update       -> firmware.bin only (keeps bootloader, partitions and config)\n")
        f.write("#        Full Install -> bootloader.bin + partitions.bin + firmware.bin (optionally erase first)\n")
        f.write("#   5. Select the files, or click 'Select build folder...' and pick THIS folder.\n")
        f.write("#   6. Click Flash Update / Full Install / Erase + Full Install.\n")
        f.write("#   7. Wait for the reboot - the Serial Monitor shows the boot log and the board IP.\n")
        f.write("#\n")
        f.write("#   WiFi (one-time, after flash): use the WebFlasher's 'WiFi Config' section\n")
        f.write("#   (SSID + password -> Save WiFi & Reboot; sends WIFIADD over serial and resets).\n\n")

        f.write("# Option 2 - OTA update via web UI (board already running and on the network)\n")
        f.write(f"#   Open http://<device-ip>/ota and upload {fw_name}\n\n")

        f.write("# Option 3 - First-time flash via esptool (run from this folder)\n")
        f.write("#   " + flash_cmd + "\n")

    # License/compliance material so the folder can be redistributed as-is.
    copied = copy_compliance(project_dir, out_dir, env_name, include_third_party)
    write_distribution_readme(out_dir, fw_name, env_name, board_name, include_third_party)
    print(f">>> Compliance : {', '.join(copied) if copied else '(none found)'} + README_DISTRIBUTION.txt")
    print(f">>> Flash cmd   : build/{board_name}/{pack_dir}/{version}/{date_str}/flash_command.txt\n")


def read_pio_forced_pack(project_dir, env_name):
    """Read -DSELECTED_PACK=... from the [env:<name>] section of platformio.ini.

    Accepts both symbolic (PACK_P4_CNC) and numeric (6) values; resolve_pack_name
    maps the numeric form through PACK_ID_MAP.
    """
    path = os.path.join(project_dir, "platformio.ini")
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            in_env = False
            for line in f:
                ls = line.strip()
                if ls.startswith("["):
                    in_env = (ls == f"[env:{env_name}]")
                    continue
                if in_env:
                    m = re.search(r"-D\s*SELECTED_PACK\s*=\s*(\w+)", ls)
                    if m:
                        return m.group(1)
    except Exception:
        pass
    return None


def main_copy(env_name, pack_name):
    """Copy the just-built firmware into build/<board>/<pack>/<version>/<ts>/.

    Invoked by build_firmware.bat after `pio run` returns, so a pack folder is
    produced on EVERY build — including incremental ones where SCons does not
    rebuild firmware.bin (and a SCons post-action would never fire).
    """
    project_dir = os.getcwd()
    # Build root: PlatformIO default (.pio/build) unless overridden.
    # 1. Env var PLATFORMIO_BUILD_DIR (definido pelo pio.sh)
    # 2. /tmp/pio-build (definido em platformio.ini: build_dir = /tmp/pio-build)
    # 3. Default .pio/build
    build_root = os.environ.get("PLATFORMIO_BUILD_DIR")
    if build_root is None:
        tmp_pio = "/tmp/pio-build"
        if os.path.exists(tmp_pio):
            build_root = tmp_pio
        else:
            build_root = os.path.join(project_dir, ".pio", "build")
    build_dir = os.path.join(build_root, env_name)
    include_elf_map = os.environ.get("EXPORT_ELF_MAP", "0").strip().lower() in ("1", "true", "yes", "y", "on")
    # Third-party compliance bundle is opt-in (build_firmware.sh asks, default N).
    # Default when unset stays "include" so the .bat / plain --copy keep working.
    include_third_party = os.environ.get("DIST_THIRD_PARTY", "1").strip().lower() not in ("0", "false", "no", "n", "off")
    if not pack_name or pack_name.upper() == "NONE" or pack_name.lower() == "default":
        forced = read_pio_forced_pack(project_dir, env_name)
        defines = [("SELECTED_PACK", forced)] if forced else []
        pack_name = resolve_pack_name(pack_name, defines, read_user_config_pack(project_dir))
    copy_artifacts(project_dir, build_dir, env_name, pack_name, include_elf_map, include_third_party)


if __name__ == "__main__":
    # CLI mode used by build_firmware.bat after `pio run`: copy the artifacts.
    # A plain `python rename_firmware.py` still runs the pack-resolution test.
    import argparse
    _parser = argparse.ArgumentParser(description="rename_firmware post-build helper")
    _parser.add_argument("--copy", action="store_true", help="copy built artifacts into the pack folder")
    _parser.add_argument("--env", default="", help="PlatformIO environment name (n16r8/esp32p4/...)")
    _parser.add_argument("--pack", default="", help="pack label from build_firmware.bat (e.g. P4_CNC)")
    _args = _parser.parse_args()

    if _args.copy:
        main_copy(_args.env, _args.pack)
        sys.exit(0)

    # Self-test of pack-name resolution. Not reached when --copy is used.
    # Run: python rename_firmware.py
    cases = [
        # (pack_name env, cpp_defines, user_config_pack, expected label)
        ("RAMBOX", [], None, "RAMBOX"),
        ("NONE", [], "PACK_S3_CNC", "S3_CNC"),                # n16r8 "no pack" default = S3_CNC
        ("NONE", [], None, ""),                                # no pack anywhere
        ("", [], "PACK_S3_CNC", "S3_CNC"),                     # plain pio run n16r8 (default)
        ("", [], "PACK_NONE", ""),                             # genuine no-pack config
        ("", [], None, ""),                                    # plain pio run, nothing forced
        ("", [("SELECTED_PACK", "PACK_WAVESHARE")], "PACK_S3_CNC", "WAVESHARE"),
        ("NONE", [("SELECTED_PACK", "PACK_WAVESHARE")], "PACK_S3_CNC", "WAVESHARE"),  # all-boards waveshare
        ("NONE", [("SELECTED_PACK", "6")], "PACK_S3_CNC", "P4_CNC"),  # all-boards / "no pack" esp32p4
        ("P4_CNC", [], None, "P4_CNC"),
        ("P4_USB_SERVER", [], "PACK_P4_CNC", "P4_USB_SERVER"),
        ("", [("SELECTED_PACK", "10")], None, "WAVESHARE"),   # numeric waveshare via plain pio run
        ("", ["SELECTED_PACK=PACK_RAMBOX"], "PACK_S3_CNC", "RAMBOX"),  # string form beats source default
        ("", [("SELECTED_PACK", "PACK_NONE")], "PACK_S3_CNC", ""),     # explicit NONE beats source default
        ("CAM", [("SELECTED_PACK", "2")], None, "CAM"),       # bat env wins over forced define
        ("", [("SELECTED_PACK", "1")], None, "RAMBOX"),       # numeric rambox via plain pio run
    ]
    failed = 0
    for env_name, cpp_defines, uc_pack, expected in cases:
        got = resolve_pack_name(env_name, cpp_defines, uc_pack)
        status = "OK " if got == expected else "FAIL"
        if got != expected:
            failed += 1
        print(f"  [{status}] env={env_name!r} defines={cpp_defines!r} uc={uc_pack!r} -> {got!r} (expected {expected!r})")
    print(f"\n  {len(cases) - failed}/{len(cases)} passed, {failed} failed")
    sys.exit(1 if failed else 0)
