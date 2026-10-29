#!/usr/bin/env python3
"""release_notes.py — generate per-variant release notes from git history.

Reads `.github/release_targets.json`, walks the commits since the previous
`release/<variant-lower>/v*` tag (or --since) and keeps only the ones that
belong to the variant: either the commit touches one of the variant's `paths`,
or its conventional-commit scope matches one of the variant's `scopes`.

The result is grouped by conventional-commit type (Features / Fixes / ...),
ready to be passed to `gh release create --notes-file`.

Usage:
  python chatgpt/scripts/release_notes.py --variant CAM_S3
  python chatgpt/scripts/release_notes.py --variant CAM_S3 --since HEAD~20 --out notes.md
  python chatgpt/scripts/release_notes.py --variant CAM_P4 --version v1.120
"""

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys
from datetime import date

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MANIFEST = os.path.join(PROJECT_ROOT, ".github", "release_targets.json")
SMTP_IMPL = os.path.join(PROJECT_ROOT, "chatgpt", "src", "smtp_core_impl.h")

# conventional-commit type -> section title (order matters)
SECTIONS = [
    ("feat", "Features"),
    ("fix", "Fixes"),
    ("perf", "Performance"),
    ("refactor", "Refactoring"),
    ("docs", "Documentation"),
    ("build", "Build & CI"),
    ("test", "Tests"),
    ("revert", "Reverts"),
    ("chore", "Maintenance"),
]
SECTION_TITLES = dict(SECTIONS)
OTHER = "Other changes"

CONVENTIONAL = re.compile(r"^(?P<type>[a-zA-Z]+)(?:\((?P<scope>[^)]*)\))?!?:\s*(?P<text>.+)$")


def _git(*args):
    return subprocess.run(
        ["git", *args], cwd=PROJECT_ROOT, capture_output=True, text=True, check=False
    ).stdout


def read_version():
    try:
        with open(SMTP_IMPL, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                m = re.search(r'#define\s+FIRMWARE_VERSION_STR\s+"([^"]+)"', line)
                if m:
                    return m.group(1).strip()
    except OSError:
        pass
    return None


def load_variant(key):
    with open(MANIFEST, "r", encoding="utf-8") as f:
        data = json.load(f)
    variants = data.get("variants", data)
    if key not in variants:
        raise SystemExit(f"unknown variant {key!r} (known: {', '.join(sorted(variants))})")
    return variants[key]


def last_release_tag(key):
    prefix = f"release/{key.lower()}/v"
    tags = [t for t in _git("tag", "--list", f"{prefix}*", "--sort=-v:refname").splitlines() if t]
    return tags[0] if tags else None


def collect_commits(rev_range):
    """Return [(sha, subject, [files])] for non-merge commits in rev_range."""
    args = ["log", "--no-merges", "--format=@@%H%x1f%s", "--name-only"]
    if rev_range == "HEAD":
        # No base tag yet: bound the walk instead of scanning the whole history.
        args += ["--max-count=300"]
    raw = _git(*args, rev_range)
    commits = []
    cur = None
    for line in raw.splitlines():
        if line.startswith("@@"):
            sha, _, subject = line[2:].partition("\x1f")
            cur = [sha, subject, []]
            commits.append(cur)
        elif line.strip() and cur is not None:
            cur[2].append(line.strip())
    return commits


def belongs(subject, files, variant):
    scopes = {s.lower() for s in variant.get("scopes", [])}
    paths = variant.get("paths", [])

    m = CONVENTIONAL.match(subject)
    if m and m.group("scope"):
        commit_scopes = {s.strip().lower() for s in m.group("scope").split(",")}
        if commit_scopes & scopes:
            return True

    return any(fnmatch.fnmatch(f, pat) for f in files for pat in paths)


def parse(subject):
    m = CONVENTIONAL.match(subject)
    if not m:
        return None, subject.strip()
    return m.group("type").lower(), m.group("text").strip()


def render(key, variant, version, since, extra_notes):
    rev_range = f"{since}..HEAD" if since else "HEAD"
    commits = collect_commits(rev_range)

    grouped = {}
    seen = set()
    for _sha, subject, files in commits:
        if not belongs(subject, files, variant):
            continue
        ctype, text = parse(subject)
        section = SECTION_TITLES.get(ctype, OTHER)
        dedupe = (section, text.lower())
        if dedupe in seen:
            continue
        seen.add(dedupe)
        grouped.setdefault(section, []).append(text)

    head_sha = _git("rev-parse", "--short", "HEAD").strip() or "?"
    version = version or read_version() or "unreleased"
    title = f"{variant['product']} **{version}** — {variant['variant']}"

    lines = [title, ""]
    if not grouped:
        lines.append("_No pack-specific changes since the previous release._")
    else:
        for ctype, section_title in SECTIONS:
            items = grouped.get(section_title)
            if not items:
                continue
            lines.append(f"### {section_title}")
            lines.extend(f"- {item}" for item in items)
            lines.append("")
        items = grouped.get(OTHER)
        if items:
            lines.append(f"### {OTHER}")
            lines.extend(f"- {item}" for item in items)
            lines.append("")

    if extra_notes:
        lines.extend([extra_notes.strip(), ""])

    base = since or last_release_tag(key) or "(start of history)"
    lines.append("---")
    lines.append(
        f"_Pack `{variant['pack']}` · {variant['variant']} · built from `{head_sha}` "
        f"on {date.today().isoformat()} · changes since {base}._"
    )
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description="generate per-variant release notes")
    ap.add_argument("--variant", required=True, help="variant key from .github/release_targets.json")
    ap.add_argument("--version", default="", help="version label (default: FIRMWARE_VERSION_STR)")
    ap.add_argument("--since", default="", help="base ref (default: last release/<variant>/v* tag)")
    ap.add_argument("--extra-notes", default="", help="extra markdown appended to the notes")
    ap.add_argument("--out", default="", help="write to this file instead of stdout")
    args = ap.parse_args()

    variant = load_variant(args.variant)
    since = args.since or last_release_tag(args.variant) or ""
    notes = render(args.variant, variant, args.version, since, args.extra_notes)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(notes)
        print(f">>> release notes written to {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(notes)


if __name__ == "__main__":
    main()
