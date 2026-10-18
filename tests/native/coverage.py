#!/usr/bin/env python3
"""
Native C++ test coverage report.

Linux (GCC/Clang): builds with --coverage, runs tests, generates HTML report.
Windows (LLVM MinGW): builds with --coverage but runtime coverage with C++
exceptions is broken (LLVM bug). Gracefully warns and skips.

Usage:
  python tests/native/coverage.py
  python tests/native/coverage.py --html    # generate HTML report (requires gcovr|genhtml)
  python tests/native/coverage.py --keep    # keep intermediate files
"""

import subprocess
import sys
import os
import glob
import shutil
import platform

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEST_DIR = SCRIPT_DIR
MAKEFILE_DIR = SCRIPT_DIR
SRC_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", "chatgpt", "src"))
REPORT_DIR = os.path.join(SCRIPT_DIR, "coverage_report")

MINGW_PATTERNS = [
    r"MartinStorsjo.LLVM-MinGW",
    r"llvm-mingw-",
]


def find_mingw_gxx():
    """Search well-known locations for LLVM MinWin g++ if not on PATH."""
    local_app_data = os.environ.get("LOCALAPPDATA", "")
    program_files = os.environ.get("ProgramFiles", "")
    for root in [local_app_data, program_files]:
        if not root:
            continue
        for dirpath, dirnames, _ in os.walk(root):
            if any(p in dirpath for p in MINGW_PATTERNS):
                gxx = os.path.join(dirpath, "g++.exe")
                if os.path.isfile(gxx):
                    return dirpath
                if "bin" in dirnames:
                    bin_dir = os.path.join(dirpath, "bin")
                    gxx = os.path.join(bin_dir, "g++.exe")
                    if os.path.isfile(gxx):
                        return bin_dir
    return None


def run_cmd(cmd, cwd=None, env=None, capture=True):
    print(f"[coverage] {' '.join(cmd)}")
    result = subprocess.run(
        cmd, cwd=cwd, capture_output=capture, text=True, env=env
    )
    if result.returncode != 0 and capture:
        print(result.stderr, file=sys.stderr)
    return result


def find_tool(tool, mingw_bin):
    """Find a tool, checking mingw_bin first, then PATH."""
    if mingw_bin:
        candidate = os.path.join(mingw_bin, tool)
        if os.path.isfile(candidate):
            return candidate
        if platform.system() == "Windows":
            candidate_exe = candidate + ".exe"
            if os.path.isfile(candidate_exe):
                return candidate_exe
    candidate = shutil.which(tool)
    if candidate:
        return candidate
    return None


def check_coverage_compatible(mingw_bin):
    """Check if runtime coverage works on this platform/toolchain."""
    import tempfile

    gxx = find_tool("g++.exe" if platform.system() == "Windows" else "g++", mingw_bin)
    if not gxx:
        return False, "g++ not found"

    test_code = '#include <exception>\nint main() { try { throw 1; } catch(int) { return 0; } }\n'
    tmpdir = tempfile.mkdtemp()
    src_file = os.path.join(tmpdir, "cov_check.cpp")
    bin_file = os.path.join(tmpdir, "cov_check")
    if platform.system() == "Windows":
        bin_file += ".exe"
    gcda_pattern = os.path.join(tmpdir, "*.gcda")

    with open(src_file, "w") as f:
        f.write(test_code)

    env = os.environ.copy()
    if mingw_bin:
        env["PATH"] = mingw_bin + os.pathsep + env.get("PATH", "")

    result = run_cmd(
        [gxx, "-std=c++11", "--coverage", "-o", bin_file, src_file],
        env=env
    )
    if result.returncode != 0:
        shutil.rmtree(tmpdir)
        return False, f"build failed: {result.stderr}"

    result = run_cmd([bin_file], env=env)
    gcda_files = glob.glob(gcda_pattern)
    has_gcda = len(gcda_files) > 0
    shutil.rmtree(tmpdir)

    if result.returncode != 0 or not has_gcda:
        return False, f"runtime failed (exit={result.returncode}, gcda={has_gcda})"

    return True, "ok"


def main():
    generate_html = "--html" in sys.argv
    keep_files = "--keep" in sys.argv

    # Clean stray .gcov from interrupted prior runs (in test dir and CWD)
    for d in [SCRIPT_DIR, os.getcwd()]:
        for f in glob.glob(os.path.join(d, "*.gcov")):
            os.remove(f)

    mingw_bin = find_mingw_gxx()
    if mingw_bin:
        print(f"[coverage] Using MinGW at: {mingw_bin}")

    make = shutil.which("mingw32-make") if platform.system() == "Windows" else shutil.which("make")
    if not make and mingw_bin:
        make_candidate = os.path.join(mingw_bin, "mingw32-make.exe")
        if os.path.isfile(make_candidate):
            make = make_candidate
    if not make:
        make = shutil.which("make")
    if not make or not os.path.isfile(make):
        print("[coverage] ERROR: make/mingw32-make not found")
        sys.exit(1)

    # Check compatibility
    compatible, compat_msg = check_coverage_compatible(mingw_bin)
    if not compatible:
        print(f"[coverage] WARNING: Runtime coverage not supported on this platform/toolchain")
        print(f"[coverage]   Reason: {compat_msg}")
        print(f"[coverage]   Coverage requires Linux with GCC or Clang.")
        print(f"[coverage]   The Makefile COVERAGE=1 flag is ready for Linux CI.")
        print(f"[coverage]   Skipping coverage generation.")
        sys.exit(0)

    # Build with coverage
    print(f"[coverage] Building with COVERAGE=1...")
    env = os.environ.copy()
    if mingw_bin:
        env["PATH"] = mingw_bin + os.pathsep + env.get("PATH", "")
    env["COVERAGE"] = "1"
    # Export mingw_bin for sub-tools
    if mingw_bin:
        env["CMAKE_CXX_COMPILER"] = os.path.join(mingw_bin, "g++.exe")
        env["CC"] = os.path.join(mingw_bin, "gcc.exe")
        env["CXX"] = os.path.join(mingw_bin, "g++.exe")

    result = run_cmd([make, "clean"], cwd=MAKEFILE_DIR, env=env)
    if result.returncode != 0:
        print("[coverage] ERROR: make clean failed")
        sys.exit(1)

    result = run_cmd([make, "all"], cwd=MAKEFILE_DIR, env=env)
    if result.returncode != 0:
        print("[coverage] ERROR: build with coverage failed")
        sys.exit(1)

    # Run tests
    test_bins = [
        "test_modbus_crc",
        "test_charset",
        "test_string_utils",
        "test_recipient_matching",
        "test_unikon_checksum",
        "test_grbl_parser",
    ]
    for tb in test_bins:
        exe = tb + (".exe" if platform.system() == "Windows" else "")
        exe_path = os.path.join(TEST_DIR, exe)
        if not os.path.isfile(exe_path):
            print(f"[coverage] SKIP (not found): {exe}")
            continue
        print(f"[coverage] Running {exe}...")
        result = run_cmd([exe_path], env=env)
        if result.returncode != 0:
            print(f"[coverage] ERROR: {exe} failed (exit={result.returncode})")
            sys.exit(1)

    # Generate report
    gcovr_tool = shutil.which("gcovr")
    genhtml_tool = shutil.which("genhtml")
    gcov = find_tool("gcov", mingw_bin)
    llvm_cov = find_tool("llvm-cov", mingw_bin)

    if generate_html and gcovr_tool:
        print(f"[coverage] Generating HTML report with gcovr...")
        if os.path.isdir(REPORT_DIR):
            shutil.rmtree(REPORT_DIR)
        os.makedirs(REPORT_DIR)
        result = run(
            [
                gcovr_tool,
                "--root", SRC_DIR,
                "--filter", SRC_DIR,
                "--html-details",
                "-o", os.path.join(REPORT_DIR, "index.html"),
            ],
            cwd=TEST_DIR,
            env=env,
        )
        if result.returncode == 0:
            print(f"[coverage] HTML report: {os.path.join(REPORT_DIR, 'index.html')}")
        else:
            print(f"[coverage] gcovr failed, trying gcov/genhtml fallback...")
            generate_html = False
    elif generate_html and genhtml_tool:
        print(f"[coverage] Generating HTML report with lcov/genhtml...")
        info_file = os.path.join(REPORT_DIR, "coverage.info")
        if os.path.isdir(REPORT_DIR):
            shutil.rmtree(REPORT_DIR)
        os.makedirs(REPORT_DIR)

        # lcov capture
        lcov = shutil.which("lcov")
        if lcov:
            result = run(
                [lcov, "--capture", "--directory", TEST_DIR, "--output-file", info_file,
                 "--include", f"{SRC_DIR}/*"],
                cwd=TEST_DIR, env=env,
            )
            if result.returncode == 0:
                run_cmd([genhtml_tool, info_file, "--output-directory", REPORT_DIR], env=env)
                print(f"[coverage] HTML report: {os.path.join(REPORT_DIR, 'index.html')}")
            else:
                print(f"[coverage] lcov failed")
        else:
            print(f"[coverage] lcov not found")
    else:
        # Basic gcov summary
        print(f"[coverage] Generating coverage summary...")
        gcda_fullpaths = glob.glob(os.path.join(TEST_DIR, "*.gcda"))
        if not gcda_fullpaths:
            print(f"[coverage] No .gcda files found - coverage data not generated")
            sys.exit(1)
        gcda_names = [os.path.basename(f) for f in gcda_fullpaths]
        print(f"[coverage] Found {len(gcda_names)} .gcda files")

        if not llvm_cov and not gcov:
            print("[coverage] Neither gcov nor llvm-cov found, cannot generate report")
            sys.exit(1)
        gcov_tool = llvm_cov or gcov
        use_llvm = bool(llvm_cov)

        print()
        src_files = sorted(glob.glob(os.path.join(SRC_DIR, "*.cpp")))
        totals = {}
        for src in src_files:
            stem = os.path.splitext(os.path.basename(src))[0]
            # Find matching gcda+(gcno) file pairs: exact stem or with binary prefix
            # Exact match: <stem>.gcda; binary-prefixed: <exe>-<stem>.gcda
            matching = [f for f in gcda_names
                        if f == stem + ".gcda" or
                           f.endswith("-" + stem + ".gcda")]
            if not matching:
                continue
            total_lines = 0
            total_executed = 0
            for gcda_name in sorted(matching):
                gcno_name = gcda_name[:-4] + "gcno"  # .gcda -> .gcno
                gcda_path = os.path.join(TEST_DIR, gcda_name)
                gcno_path = os.path.join(TEST_DIR, gcno_name)
                if not os.path.isfile(gcno_path):
                    continue
                if use_llvm:
                    result = run_cmd(
                        [gcov_tool, "gcov", src, "--gcda", gcda_path, "--gcno", gcno_path],
                        cwd=TEST_DIR, env=env
                    )
                else:
                    result = run_cmd(
                        [gcov_tool, src, "-o", TEST_DIR],
                        cwd=TEST_DIR, env=env
                    )
                if result.returncode != 0:
                    continue
                # First "Lines executed:" is always for the src file itself
                for raw_line in result.stdout.splitlines():
                    line = raw_line.strip()
                    if line.startswith("Lines executed:"):
                        try:
                            pct_str, rest = line.split(" of ")
                            pct = float(pct_str.split(":")[1].rstrip("%"))
                            total = int(rest)
                            executed = int(total * pct / 100.0)
                            if total > total_lines:
                                total_lines = total
                                total_executed = executed
                        except (ValueError, IndexError):
                            pass
                        break  # only first match (source file coverage)
            if total_lines > 0:
                pct = 100.0 * total_executed / total_lines
                print(f"  {stem:35s} {total_executed:6d}/{total_lines:<6d} ({pct:5.1f}%)")
                totals[stem] = (total_executed, total_lines)

        if totals:
            overall_exec = sum(v[0] for v in totals.values())
            overall_total = sum(v[1] for v in totals.values())
            overall_pct = 100.0 * overall_exec / overall_total
            print(f"  {'-' * 55}")
            print(f"  {'TOTAL':35s} {overall_exec:6d}/{overall_total:<6d} ({overall_pct:5.1f}%)")

    if not keep_files:
        print(f"[coverage] Cleaning coverage artifacts...")
        run_cmd([make, "-C", MAKEFILE_DIR, "clean"], env=env)
        gcov_files = glob.glob(os.path.join(TEST_DIR, "*.gcov"))
        for gf in gcov_files:
            os.remove(gf)
        if os.path.isdir(REPORT_DIR) and not generate_html:
            shutil.rmtree(REPORT_DIR)

    print(f"[coverage] Done.")


if __name__ == "__main__":
    main()
