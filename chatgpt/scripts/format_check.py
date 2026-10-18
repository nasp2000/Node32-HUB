"""
Check or fix C++ formatting with clang-format.

Usage:
    python scripts/format_check.py              # check only, report violations
    python scripts/format_check.py --fix        # auto-format in place
    python scripts/format_check.py --tree src   # check a specific subdirectory (default: project root)
"""

import subprocess
import sys
import os

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_TREE = PROJECT_DIR
CLANG_FORMAT = ".clang-format"


def find_sources(tree: str) -> list[str]:
    sources = []
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d not in ("languages", ".pio", "build", "managed_components", "scripts", ".vscode")]
        for f in files:
            if f.endswith((".h", ".cpp", ".ino")):
                sources.append(os.path.join(root, f))
    return sorted(sources)


def main():
    import argparse
    parser = argparse.ArgumentParser(description="C++ format checker")
    parser.add_argument("--fix", action="store_true", help="Auto-format files in place")
    parser.add_argument("--tree", default=DEFAULT_TREE, help="Root directory to scan")
    args = parser.parse_args()

    tree = os.path.abspath(args.tree)
    if not os.path.isdir(tree):
        print(f"ERROR: directory not found: {tree}", file=sys.stderr)
        return 1

    sources = find_sources(tree)
    if not sources:
        print(f"No source files found under {tree}")
        return 0

    # Check clang-format is available
    try:
        subprocess.run(["clang-format", "--version"], capture_output=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("ERROR: clang-format not found in PATH. Install it and try again.", file=sys.stderr)
        return 1

    clang_format_path = os.path.join(PROJECT_DIR, CLANG_FORMAT)
    if not os.path.isfile(clang_format_path):
        print(f"WARNING: {CLANG_FORMAT} not found at project root, using clang-format defaults")

    cmd = ["clang-format", f"--style=file:{clang_format_path}" if os.path.isfile(clang_format_path) else "--style=LLVM"]
    if args.fix:
        cmd.append("-i")

    unformatted = []
    for src in sources:
        rel = os.path.relpath(src, PROJECT_DIR)
        if args.fix:
            subprocess.run(cmd + [src], check=True)
            print(f"  formatted: {rel}")
        else:
            result = subprocess.run(cmd + [src], capture_output=True)
            with open(src, "rb") as f:
                original = f.read()
            if result.stdout != original:
                unformatted.append(rel)

    if args.fix:
        print(f"\nFormatted {len(sources)} files.")
        return 0

    if unformatted:
        print(f"\n{'='*70}")
        print(f"  FORMAT CHECK FAILED: {len(unformatted)} file(s) need formatting")
        print(f"{'='*70}\n")
        for f in unformatted:
            print(f"  {f}")
        print(f"\n  Run: python scripts/format_check.py --fix")
        print(f"{'='*70}\n")
        return 1
    else:
        print(f"  format_check.py: OK — {len(sources)} files are properly formatted.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
