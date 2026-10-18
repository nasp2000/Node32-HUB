"""
Pre-build validation: ensure every .cpp includes feature_flags.h before ENABLE_ guards.
Fails the build if any .cpp has #if ENABLE_X in the first 10 lines without a prior
#include "feature_flags.h".
"""

import os
import re
import sys

RE_IF_ENABLE = re.compile(r'#\s*if\s+ENABLE_')
RE_FEATURE_FLAGS = re.compile(r'#include\s+["<].*feature_flags\.h')


def check_file(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    if not lines:
        return None
    guard_line = None
    include_line = None
    for i, line in enumerate(lines[:10]):
        stripped = line.strip()
        if RE_IF_ENABLE.search(stripped) and guard_line is None:
            guard_line = i + 1
        if RE_FEATURE_FLAGS.search(stripped) and include_line is None:
            include_line = i + 1
    if guard_line is not None and (include_line is None or guard_line < include_line):
        return (guard_line, include_line)
    return None


def run(src_dir):
    errors = []
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if not f.endswith(".cpp"):
                continue
            path = os.path.join(root, f)
            rel = os.path.relpath(path, os.path.join(src_dir, ".."))
            result = check_file(path)
            if result is not None:
                guard_line, include_line = result
                if include_line is None:
                    errors.append(
                        "{}:{}: #if ENABLE_X with NO #include \"feature_flags.h\" found"
                        .format(rel, guard_line))
                else:
                    errors.append(
                        "{}:{}: #if ENABLE_X at line {} before #include \"feature_flags.h\" at line {}"
                        .format(rel, guard_line, guard_line, include_line))
    return errors


if __name__ == "__main__":
    # Standalone mode — guess PROJECT_DIR as two levels up from scripts/
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.normpath(os.path.join(script_dir, ".."))
    src_dir = os.path.join(project_dir, "src")
    errors = run(src_dir)
    if errors:
        print("\n[validate_feature_flags] ERROR: Feature flag guard before feature_flags.h:")
        for e in errors:
            print("  {}".format(e))
        print("\n[validate_feature_flags] {} issue(s) — fix before build.\n".format(len(errors)))
        sys.exit(1)
    else:
        print("[validate_feature_flags] OK")
else:
    # PlatformIO pre-script mode
    try:
        Import("env")
        src_dir = os.path.join(env.subst("$PROJECT_DIR"), "src")
        errors = run(src_dir)
        if errors:
            print("\n[validate_feature_flags] ERROR: Feature flag guard before feature_flags.h:")
            for e in errors:
                print("  {}".format(e))
            print("\n[validate_feature_flags] {} issue(s) — fix before build.\n".format(len(errors)))
            sys.exit(1)
    except Exception as e:
        print("[validate_feature_flags] WARNING: could not run as PIO script: {}".format(e))
