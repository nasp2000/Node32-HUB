"""
Report large functions (>200 lines) and large files (>1000 lines) in src/.

Usage:
    python scripts/size_report.py
    python scripts/size_report.py --threshold-func 150  # custom threshold
    python scripts/size_report.py --threshold-file 800
"""

import re
import sys
import os

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_TREE = os.path.join(PROJECT_DIR, "src")
FUNC_THRESHOLD = 200
FILE_THRESHOLD = 1000


def find_sources(tree: str) -> list[str]:
    sources = []
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d not in ("languages", ".pio", "build", "managed_components")]
        for f in files:
            if f.endswith((".h", ".cpp", ".ino")):
                sources.append(os.path.join(root, f))
    return sorted(sources)


def extract_functions(filepath: str) -> list[tuple[str, int, int]]:
    """
    Return list of (name, start_line, line_count) for each function definition.
    Uses a simple heuristic: lines starting with a type-like token followed by a name + '('.
    """
    functions = []
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except Exception:
        return []

    # Patterns for function definitions
    func_re = re.compile(
        r'^(?:static\s+)?(?:inline\s+)?'
        r'(?:void|bool|int|uint\d+_t|int\d+_t|size_t|char|String|float|double|long|unsigned|JsonObject|JsonArray|'
        r'HttpResponse|HttpRequest|AsyncWebServerResponse\*|SpiRamJsonDocument|std::\w+)'
        r'\s+\**\s*(\w+)\s*\('
    )

    brace_count = 0
    func_start = -1
    func_name = ""
    in_function = False

    for lineno, line in enumerate(lines, 1):
        stripped = line.strip()

        if not in_function:
            m = func_re.match(stripped)
            if m:
                func_start = lineno
                func_name = m.group(1)
                # Check if opening brace is on same line
                if "{" in stripped:
                    in_function = True
                    brace_count = stripped.count("{") - stripped.count("}")
                    if brace_count <= 0:
                        in_function = False
                elif ";" in stripped:
                    # Forward declaration, skip
                    pass
                else:
                    # Brace might be on next line
                    pass
        else:
            # Look for opening brace
            if "{" in stripped:
                brace_count += stripped.count("{")
                brace_count -= stripped.count("}")
                if brace_count <= 0:
                    line_count = lineno - func_start + 1
                    if line_count > FUNC_THRESHOLD:
                        functions.append((func_name, func_start, line_count))
                    in_function = False
            elif "}" in stripped:
                # Must have been a single-line function with brace on same line as signature
                pass

        # Handle case where brace is on next line after signature
        if not in_function and func_start > 0 and lineno == func_start + 1:
            if "{" in stripped:
                in_function = True
                brace_count = 1
                if "}" in stripped:
                    brace_count -= stripped.count("}")
                    if brace_count <= 0:
                        line_count = lineno - func_start + 1
                        if line_count > FUNC_THRESHOLD:
                            functions.append((func_name, func_start, line_count))
                        in_function = False
                        func_start = -1
                        func_name = ""

        # Track braces when in function
        if in_function and lineno > func_start:
            brace_count += stripped.count("{")
            brace_count -= stripped.count("}")
            if brace_count <= 0:
                line_count = lineno - func_start + 1
                if line_count > FUNC_THRESHOLD:
                    functions.append((func_name, func_start, line_count))
                in_function = False

    return functions


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Report large functions and files")
    parser.add_argument("--threshold-func", type=int, default=FUNC_THRESHOLD, help=f"Function line threshold (default: {FUNC_THRESHOLD})")
    parser.add_argument("--threshold-file", type=int, default=FILE_THRESHOLD, help=f"File line threshold (default: {FILE_THRESHOLD})")
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

    func_threshold = args.threshold_func
    file_threshold = args.threshold_file

    large_files = []
    all_large_funcs = []

    for src in sources:
        rel = os.path.relpath(src, PROJECT_DIR)
        with open(src, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()

        line_count = len(lines)
        if line_count > file_threshold:
            large_files.append((rel, line_count))

        funcs = extract_functions(src)
        if funcs:
            all_large_funcs.extend((rel, name, start, count) for name, start, count in funcs)

    # Report
    any_issues = False

    if large_files:
        any_issues = True
        large_files.sort(key=lambda x: -x[1])
        print(f"\n{'='*70}")
        print(f"  LARGE FILES (>={file_threshold} lines): {len(large_files)}")
        print(f"{'='*70}\n")
        for path, count in large_files:
            print(f"  {path:70s} {count:5d} lines")

    if all_large_funcs:
        any_issues = True
        all_large_funcs.sort(key=lambda x: -x[3])
        print(f"\n{'='*70}")
        print(f"  LARGE FUNCTIONS (>={func_threshold} lines): {len(all_large_funcs)}")
        print(f"{'='*70}\n")
        for path, name, start, count in all_large_funcs:
            print(f"  {path}:{start:<5d}  {name:<40s} {count:5d} lines")

    if not any_issues:
        print(f"\n  size_report.py: OK — no files >={file_threshold} lines, no functions >={func_threshold} lines.")
    else:
        print(f"\n{'='*70}")
        print(f"  Total: {len(large_files)} large files, {len(all_large_funcs)} large functions.")
        print(f"{'='*70}\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
