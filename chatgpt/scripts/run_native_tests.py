"""
Native C++ test runner — runs the host-compiled unit tests from tests/native/.

Used as a PlatformIO pre-script: executes before every `pio run` and aborts the
build if any test fails.

Environment variables:
    SKIP_NATIVE_TESTS=1    Skip tests even if compiler is available.
    FORCE_NATIVE_TESTS=1   Fail the build if compiler is not found.

Standalone usage:
    python scripts/run_native_tests.py
"""

import subprocess
import sys
import os
import platform
from datetime import datetime

try:
    _SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    _PARENT = os.path.dirname(_SCRIPT_DIR)  # chatgpt/ from scripts/
except NameError:
    _PARENT = os.getcwd()  # PlatformIO pre-script: CWD is project root (chatgpt/)

TESTS_DIR = os.path.normpath(os.path.join(_PARENT, "..", "tests", "native"))
TEST_BINS = ["test_modbus_crc", "test_charset", "test_string_utils",
             "test_recipient_matching", "test_unikon_checksum", "test_unikon_training", "test_grbl_parser",
             "test_grbl_resume",
             "test_email_builder", "test_config_utils", "test_config_validator",
              "test_pack_validator", "test_ftp_client", "test_probe_grid",
             "test_grbl_sender", "test_proximity_debounce", "test_proximity_oee",
             "test_proximity_goal_do", "test_proximity_persist",
             "test_dualfile_utils", "test_atomic_write", "test_storage_default", "test_bricon_mode", "test_config_registry",
             "test_config_parse", "test_littlefs_store", "test_route_registry",
             "test_auth", "test_alert_pipeline",
             "test_scheduler_commands", "test_day_report_fallback", "test_sensor_config_cycle",
             "test_config_defaults", "test_handler_json", "test_sensor_base_persist",
             "test_handler_post_persist", "test_radio_redirect", "test_radio_browser",
             "test_i2c_scan_logic", "test_radio_schedule_core", "test_radio_elapsed",
             "test_upnp_didl", "test_audio_i2s_poc", "test_audio_mp3_decoder",
             "test_audio_flac_decoder", "test_audio_source_buffer", "test_adts_parser",
             "test_wav_parser", "test_audio_limiter", "test_radio_favicon",
             "test_audio_timing", "test_audio_rolling_buffer", "test_audio_mp3_eof",
             "test_icy_demux", "test_audio_ring", "test_do_config",
             "test_shift_window", "test_email_body_format", "test_proximity_chart", "test_proximity_inactivity", "test_proximity_setup_routing",
"test_alerts_save_safe", "test_chart_history_stream", "test_eth_config_mac",
               "test_usb_chip_table", "test_usb_ch340_divisor", "test_usb_ch340_init_seq", "test_usbip_protocol", "test_proximity_day_aligned", "test_proximity_day_oee", "test_proximity_oee_alerts", "test_proximity_oee_csv", "test_proximity_shiftbase",
               "test_alerts_persist_real", "test_system_persist_real", "test_ntp_persist_real",
               "test_camera_stream", "test_camera_api", "test_camera_motion", "test_camera_stream_fps", "test_camera_sensor", "test_camera_autostart", "test_camera_initguard", "test_camera_p4_h264", "test_camera_p4_vision", "test_camera_record_naming", "test_camera_record_error", "test_camera_motion_uyvy", "test_camera_p4_perf", "test_camera_psram_ring", "test_camera_resolution",
                "test_touch7_logic", "test_touch7_geo", "test_sd_layout", "test_sensor_history_sources", "test_sensor_rtc_tail", "test_proximity_csv_restore", "test_rtc_state_util",                 "test_reboot_anchor_logic", "test_nvs_write_guard", "test_chart_ring_serve", "test_email_day_dedupe",
                 "test_ogg_parser", "test_opus_head", "test_stb_vorbis_decode", "test_sensor_day_report", "test_email_body_archive",
             "test_lfs_mutex_guard", "test_email_flash_rpc",
             "test_email_recipients", "test_email_event_fields"]
LOGS_DIR = os.path.normpath(os.path.join(_PARENT, "..", "logs"))
LOG_FILE = os.path.join(LOGS_DIR, "test_results_latest.txt")


def _find_llvm_mingw() -> str | None:
    """Search common LLVM MinGW installation directories, return bin dir if found."""
    candidates = [
        os.path.join(os.environ.get("LOCALAPPDATA", ""),
                     "Microsoft", "WinGet", "Packages",
                     "MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe",
                     "llvm-mingw-20260616-ucrt-x86_64", "bin"),
        "C:\\Program Files\\LLVM\\bin",
        "C:\\tools\\llvm-mingw\\bin",
    ]
    for d in candidates:
        if os.path.isfile(os.path.join(d, "g++.exe")):
            return d
    return None


def _check_compiler() -> tuple[str | None, str | None]:
    """
    Return (make_path, gpp_path) if a suitable C++ toolchain exists.
    Both paths are absolute (or just the command name if on PATH).
    """
    gpp_cmd = "g++"
    make_cmd = None

    if os.name == "nt":
        # Check if g++ is already on PATH
        try:
            subprocess.run([gpp_cmd, "--version"], capture_output=True, check=True, timeout=15)
        except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
            gpp_found = False
        else:
            gpp_found = True

        if not gpp_found:
            mingw_bin = _find_llvm_mingw()
            if mingw_bin is None:
                return None, None
            gpp_cmd = os.path.join(mingw_bin, "g++.exe")
            # Verify using full path
            try:
                subprocess.run([gpp_cmd, "--version"], capture_output=True, check=True, timeout=15)
            except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
                return None, None

        # Find make — using full path if we have mingw_bin
        if not gpp_found:
            mingw_bin = os.path.dirname(gpp_cmd)
            for exe in ["mingw32-make.exe", "make.exe"]:
                candidate = os.path.join(mingw_bin, exe)
                if os.path.isfile(candidate):
                    make_cmd = candidate
                    break
        else:
            for cmd in ["mingw32-make", "make"]:
                try:
                    subprocess.run([cmd, "--version"], capture_output=True, check=True, timeout=15)
                    make_cmd = cmd
                    break
                except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
                    continue

        if make_cmd is None:
            return None, None
        return make_cmd, gpp_cmd
    else:
        # Linux/macOS — standard g++ + make
        try:
            subprocess.run(["g++", "--version"], capture_output=True, check=True, timeout=15)
            subprocess.run(["make", "--version"], capture_output=True, check=True, timeout=15)
            return "make", "g++"
        except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired):
            return None, None


def _ensure_logs_dir():
    """Create logs/ directory if it doesn't exist."""
    if not os.path.isdir(LOGS_DIR):
        os.makedirs(LOGS_DIR, exist_ok=True)


def _tee(msg: str, lines: list):
    """Print to console and accumulate in log lines list."""
    print(msg)
    lines.append(msg)


def run_tests() -> int:
    """Build and run native tests. Returns 0 on success, 1 on failure."""
    _ensure_logs_dir()
    log_lines = []

    def log(msg):
        _tee(msg, log_lines)

    if not os.path.isdir(TESTS_DIR):
        log(f"[native-tests] SKIP: tests directory not found at {TESTS_DIR}")
        _write_log_file(log_lines)
        return 0

    make_cmd, gpp_cmd = _check_compiler()
    if make_cmd is None:
        msg = "[native-tests] SKIP: C++ toolchain (g++/make) not found"
        if os.environ.get("FORCE_NATIVE_TESTS") == "1":
            log(f"[native-tests] FAIL: {msg}")
            _write_log_file(log_lines)
            return 1
        log(f"[native-tests] WARN: {msg}")
        log("[native-tests] WARN: set FORCE_NATIVE_TESTS=1 to fail on missing toolchain")
        log("[native-tests] WARN: set SKIP_NATIVE_TESTS=1 to suppress this message")
        _write_log_file(log_lines)
        return 0

    # Add toolchain PATH so test binaries can find MinGW runtime DLLs
    mingw_bin = os.path.dirname(gpp_cmd)
    extpath = mingw_bin + os.pathsep + os.environ.get("PATH", "")
    env = {**os.environ, "CXX": gpp_cmd, "PATH": extpath}

    # ---- Build ----
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    log(f"=== [{now}] Native C++ Tests ===")
    log("")
    log("--- Build output ---")
    log(f"[native-tests] Building (make={make_cmd})...")
    clean = subprocess.run(
        [make_cmd, "clean"],
        cwd=TESTS_DIR,
        capture_output=True,
        text=True,
        env=env,
    )
    if clean.stdout:
        log(clean.stdout.rstrip())
        _tee("", log_lines)
    if clean.stderr:
        log(clean.stderr.rstrip())
        _tee("", log_lines)
    if clean.returncode != 0:
        log("[native-tests] FAIL: make clean failed")
        _write_log_file(log_lines)
        return 1

    # Parallel build — all CPU cores by default (override: NATIVE_TEST_JOBS=N).
    # NOTE: clean and all are separate make invocations; with -j a combined
    # "make clean all" could delete objects while other jobs compile.
    jobs = os.environ.get("NATIVE_TEST_JOBS") or str(os.cpu_count() or 1)
    log(f"[native-tests] Building with {jobs} parallel job(s)...")
    build = subprocess.run(
        [make_cmd, "-j", jobs, "all"],
        cwd=TESTS_DIR,
        capture_output=True,
        text=True,
        env=env,
    )
    if build.stdout:
        log(build.stdout.rstrip())
        _tee("", log_lines)
    if build.stderr:
        log(build.stderr.rstrip())
        _tee("", log_lines)

    if build.returncode != 0:
        log("[native-tests] FAIL: build failed")
        _write_log_file(log_lines)
        return 1

    # ---- Run tests ----
    log("[native-tests] Running tests...")
    failures = 0
    passed = 0
    for bin_name in TEST_BINS:
        bin_path = os.path.join(TESTS_DIR, bin_name)
        if os.name == "nt":
            bin_path += ".exe"
        if not os.path.isfile(bin_path):
            log(f"[native-tests]  {bin_name}: binary not found")
            failures += 1
            continue

        log(f"\n--- {bin_name} ---")
        result = subprocess.run([bin_path], capture_output=True, text=True, env=env)
        if result.stdout:
            log(result.stdout.rstrip())
        if result.stderr:
            log(result.stderr.rstrip())

        if result.returncode != 0:
            log(f"[native-tests]  {bin_name}: FAIL (exit {result.returncode})")
            failures += 1
        else:
            log(f"[native-tests]  {bin_name}: PASS")
            passed += 1

    # ---- Summary ----
    total = passed + failures
    log("")
    log("--- Summary ---")
    log(f"{passed}/{total} suites passed")
    if failures:
        log(f"[native-tests] FAIL: {failures} test(s) failed")
        _write_log_file(log_lines)
        return 1
    log("[native-tests] All tests passed")
    _write_log_file(log_lines)
    return 0


def _write_log_file(lines: list):
    """Write accumulated log lines to logs/test_results_latest.txt."""
    try:
        _ensure_logs_dir()
        with open(LOG_FILE, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    except OSError as e:
        print(f"[native-tests] WARN: could not write log file: {e}")


def _should_skip_pio_target() -> bool:
    """When used as a PIO pre-script, skip non-build targets (clean, erase, etc.)."""
    import json
    targets_raw = os.environ.get("PLATFORMIO_TARGETS", "")
    if not targets_raw:
        return False
    try:
        targets = json.loads(targets_raw)
    except (json.JSONDecodeError, TypeError):
        return False
    if not targets:
        return False
    # If ALL targets are non-build, skip tests
    build_targets = {"buildprog", "build", "upload"}
    return not bool(set(targets) & build_targets)


def main():
    if os.environ.get("SKIP_NATIVE_TESTS") == "1":
        print("[native-tests] SKIP: SKIP_NATIVE_TESTS=1")
        return 0
    if _should_skip_pio_target():
        print("[native-tests] SKIP: non-build target (clean/erase/etc)")
        return 0
    return run_tests()


main()
