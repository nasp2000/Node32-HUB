"""
Scan for hardcoded secrets (passwords, tokens, API keys, SSIDs) in source files.

Usage:
    python scripts/scan_secrets.py                          # scan staged files (git diff --cached)
    python scripts/scan_secrets.py --all                     # scan all tracked files
    python scripts/scan_secrets.py --path src/               # scan a specific path
    python scripts/scan_secrets.py --list-patterns           # show all patterns
"""

import re
import subprocess
import sys
import os

PROJECT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
EXTENSIONS = (".h", ".cpp", ".ino", ".py", ".json", ".yml", ".yaml", ".cfg", ".conf")

# Patterns to flag — ordered by severity
SECRET_PATTERNS = [
    # High confidence
    (r'(?i)password\s*=\s*["\'][^"\'\s]{3,}["\']', "hardcoded password"),
    (r'(?i)passwd\s*=\s*["\'][^"\'\s]{3,}["\']', "hardcoded passwd"),
    (r'(?i)(?:api[_-]?key|apikey)\s*=\s*["\'][^"\'\s]{4,}["\']', "hardcoded API key"),
    (r'(?i)(?:api[_-]?secret|apisecret)\s*=\s*["\'][^"\'\s]{4,}["\']', "hardcoded API secret"),
    (r'(?i)(?:access[_-]?token|accesstoken)\s*=\s*["\'][^"\'\s]{8,}["\']', "hardcoded access token"),
    (r'(?i)(?:secret|token)\s*=\s*["\'][A-Za-z0-9_\-]{16,}["\']', "potential secret/token"),
    (r'(?i)(?:wifi|ssid)\s*["\']?\s*=\s*["\'][^"\'\s]{2,}["\']', "hardcoded SSID"),
    # Medium confidence
    (r'(?i)bearer\s+[A-Za-z0-9_\-\.]{20,}', "Bearer token in code"),
    (r'(?i)auth\s*=\s*["\'][^"\'\s]{8,}["\']', "potential auth credential"),
    (r'(?i)psk\s*=\s*["\'][A-Za-z0-9_\-\.\/]{8,}["\']', "hardcoded PSK"),
    # Low confidence — flag only in known config/credential files
    (r'(?i)secret\s*=\s*["\'][^"\'\s]{4,}["\']', "secret assignment"),
]

# Directories/files to skip entirely
SKIP_DIRS = {".pio", "build", "managed_components", "languages", "__pycache__", ".git", "Docs", "Others", "util"}
SKIP_FILES = {
    "scan_secrets.py",  # itself
    "lang_pt_ui.h", "lang_en_ui.h",  # large UI JSON, no secrets
    "ota_private.pem",  # private key — expected to be present locally
    "ota_pubkey.h",     # public key — committed by design
    "feature_flags.h",  # config flags, not secrets
}


def get_staged_files() -> list[str]:
    try:
        result = subprocess.run(
            ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
            capture_output=True, text=True, cwd=PROJECT_DIR, check=True,
        )
        files = [f.strip() for f in result.stdout.split("\n") if f.strip()]
        return [f for f in files if any(f.endswith(ext) for ext in EXTENSIONS)]
    except subprocess.CalledProcessError:
        return []


def get_all_tracked_files() -> list[str]:
    try:
        result = subprocess.run(
            ["git", "ls-files"],
            capture_output=True, text=True, cwd=PROJECT_DIR, check=True,
        )
        files = [f.strip() for f in result.stdout.split("\n") if f.strip()]
        return [f for f in files if any(f.endswith(ext) for ext in EXTENSIONS)]
    except subprocess.CalledProcessError:
        return []


def should_skip(filepath: str) -> bool:
    parts = filepath.replace("\\", "/").split("/")
    if any(d in parts for d in SKIP_DIRS):
        return True
    basename = os.path.basename(filepath)
    if basename in SKIP_FILES:
        return True
    # Skip generated files
    if "managed_components" in parts:
        return True
    return False


def scan_file(filepath: str) -> list[tuple[int, str, str]]:
    """Return list of (line_number, pattern_desc, snippet)."""
    full_path = os.path.join(PROJECT_DIR, filepath) if not os.path.isabs(filepath) else filepath
    if not os.path.isfile(full_path):
        return []
    if should_skip(filepath):
        return []

    findings = []
    try:
        with open(full_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except Exception:
        return []

    for lineno, line in enumerate(lines, 1):
        stripped = line.strip()
        # Skip comments
        if stripped.startswith("//") or stripped.startswith("#") or stripped.startswith("/*"):
            continue
        # Skip NVS key names (not actual secrets)
        if re.search(r'NVS_KEY_\w+\s*=\s*"[^"]+"', stripped):
            continue
        for pattern, desc in SECRET_PATTERNS:
            m = re.search(pattern, stripped)
            if m:
                snippet = stripped[:120].strip()
                findings.append((lineno, desc, snippet))
                break  # one flag per line

    return findings


def list_patterns():
    print(f"\n{'='*70}")
    print(f"  Secret scan patterns")
    print(f"{'='*70}\n")
    for i, (pattern, desc) in enumerate(SECRET_PATTERNS, 1):
        print(f"  {i:2d}. [{desc}]")
        print(f"      {pattern}")
    print()


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Scan for hardcoded secrets")
    parser.add_argument("--all", action="store_true", help="Scan all tracked files (default: staged only)")
    parser.add_argument("--path", help="Scan a specific file or directory")
    parser.add_argument("--list-patterns", action="store_true", help="Show all patterns and exit")
    args = parser.parse_args()

    if args.list_patterns:
        list_patterns()
        return 0

    if args.path:
        target_path = os.path.abspath(args.path)
        rel_path = os.path.relpath(target_path, PROJECT_DIR)
        if os.path.isfile(target_path):
            files = [rel_path]
        elif os.path.isdir(target_path):
            files = []
            for root, dirs, fnames in os.walk(target_path):
                dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
                for fname in fnames:
                    if fname.endswith(EXTENSIONS) and fname not in SKIP_FILES:
                        full = os.path.join(root, fname)
                        files.append(os.path.relpath(full, PROJECT_DIR))
        else:
            print(f"ERROR: path not found: {args.path}", file=sys.stderr)
            return 1
    elif args.all:
        files = get_all_tracked_files()
    else:
        files = get_staged_files()

    if not files:
        print("  scan_secrets.py: no files to scan.")
        return 0

    # Filter out skipped files
    files = [f for f in files if not should_skip(f)]

    all_findings = []
    for filepath in sorted(files):
        findings = scan_file(filepath)
        if findings:
            all_findings.append((filepath, findings))

    if all_findings:
        print(f"\n{'='*70}")
        print(f"  SECRET SCAN FAILED: {len(all_findings)} file(s) with potential secrets")
        print(f"{'='*70}\n")
        for filepath, findings in all_findings:
            for lineno, desc, snippet in findings:
                print(f"  {filepath}:{lineno:<5d}  [{desc}]  {snippet}")
        print(f"\n  Review each finding. If legitimate (test fixture, etc.),")
        print(f"  add the file to SKIP_FILES or the line to a comment-ignore pattern.")
        print(f"{'='*70}\n")
        return 1
    else:
        print(f"  scan_secrets.py: OK — scanned {len(files)} file(s), no secrets found.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
