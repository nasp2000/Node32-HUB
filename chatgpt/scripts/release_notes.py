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
    res = subprocess.run(
        ["git", *args], cwd=PROJECT_ROOT, capture_output=True, text=True, check=False
    )
    if res.returncode != 0:
        head = (res.stderr or "").strip().splitlines()[:1]
        print(f"WARNING: git {' '.join(args)} failed: {head[0] if head else '?'}",
              file=sys.stderr)
    return res.stdout


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


def _version_tuple(text):
    m = re.fullmatch(r"v(\d+)\.(\d+)", (text or "").strip())
    return (int(m.group(1)), int(m.group(2))) if m else None


def target_repo_last_tag(variant, current=""):
    """Highest release tag already published in the variant's target repo.

    Using the target repo as the base keeps the notes correct even if a release
    was deleted or the provenance tags were lost; only tags carrying this
    variant's suffix are considered (Cam-M: v1.120 vs v1.120-p4).
    """
    repo = variant.get("repo")
    suffix = variant.get("tag_suffix", "") or ""
    if not repo:
        return None
    res = subprocess.run(
        ["gh", "release", "list", "--repo", repo, "--limit", "60", "--json", "tagName"],
        cwd=PROJECT_ROOT, capture_output=True, text=True, check=False,
    )
    if res.returncode != 0:
        return None
    try:
        tags = [t.get("tagName", "") for t in json.loads(res.stdout or "[]")]
    except json.JSONDecodeError:
        return None

    ceiling = _version_tuple(current)
    scored = []
    for t in tags:
        if suffix:
            if not t.endswith(suffix):
                continue
            base = t[: -len(suffix)]
        else:
            base = t
        v = _version_tuple(base)
        if not v:
            continue
        if ceiling and v >= ceiling:
            continue
        scored.append((v, t))
    if not scored:
        return None
    scored.sort()
    return scored[-1][1]


def previous_release_sha(variant, current=""):
    """Commit recorded in the footer of the previous release ("built from <sha>").

    This replaces the old provenance tags: the release body is the anchor, so no
    tag has to exist in the source repo (which is what generated the source-code
    zips on the tags page).
    """
    repo = variant.get("repo")
    tag = target_repo_last_tag(variant, current)
    if not repo or not tag:
        return None
    res = subprocess.run(
        ["gh", "release", "view", tag, "--repo", repo, "--json", "body"],
        cwd=PROJECT_ROOT, capture_output=True, text=True, check=False,
    )
    if res.returncode != 0:
        return None
    try:
        body = json.loads(res.stdout or "{}").get("body", "") or ""
    except json.JSONDecodeError:
        return None
    m = re.search(r"built from `([0-9a-f]{7,40})`", body)
    return m.group(1) if m else None


def _tag_exists(ref):
    if not ref:
        return False
    r = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"refs/tags/{ref}"],
        cwd=PROJECT_ROOT, capture_output=True, text=True, check=False,
    )
    return r.returncode == 0


def _rev_exists(ref):
    r = subprocess.run(
        ["git", "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}"],
        cwd=PROJECT_ROOT, capture_output=True, text=True, check=False,
    )
    return r.returncode == 0


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


def pick_base(args, variant, target_tag, version=""):
    """Pick a ref that bounds the git log range for this variant.

    Order: explicit --since, the commit recorded by the previous release in the
    target repo, the legacy provenance tag, a local version tag, else ""
    (bounded window).
    """
    if args.since:
        return args.since
    anchor = previous_release_sha(variant, version)
    if anchor and _rev_exists(anchor):
        return anchor
    key = args.variant
    if target_tag:
        prov = f"release/{key.lower()}/{target_tag}"
        if _tag_exists(prov):
            return prov
        if _tag_exists(target_tag):
            return target_tag
    prov = last_release_tag(key)
    if prov:
        return prov
    return ""


def render(key, variant, version, since, extra_notes, include_non_conventional=False):
    rev_range = f"{since}..HEAD" if since else "HEAD"
    commits = collect_commits(rev_range)

    grouped = {}
    seen = set()
    omitted = 0
    for _sha, subject, files in commits:
        if not belongs(subject, files, variant):
            continue
        ctype, text = parse(subject)
        if ctype not in SECTION_TITLES and not include_non_conventional:
            # Release notes are English-only: a commit without a conventional
            # subject (type(scope): text) is not included — legacy free-form
            # subjects are often in another language.
            omitted += 1
            continue
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

    if omitted:
        lines.append(f"_{omitted} commit(s) without a conventional English subject were omitted._")
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
    ap.add_argument("--since", default="", help="base ref (default: commit of the previous release in the target repo)")
    ap.add_argument("--extra-notes", default="", help="extra markdown appended to the notes")
    ap.add_argument("--out", default="", help="write to this file instead of stdout")
    ap.add_argument("--include-non-conventional", action="store_true",
                    help="also list commits whose subject is not `type(scope): text`")
    args = ap.parse_args()

    variant = load_variant(args.variant)
    if args.since and not _tag_exists(args.since) and not _rev_exists(args.since):
        raise SystemExit(f"ERROR: --since {args.since!r} is not a tag/commit in this repository")
    target_tag = target_repo_last_tag(variant, args.version)
    since = pick_base(args, variant, target_tag, args.version)
    if since:
        print(f">>> notes base: {since}", file=sys.stderr)
    elif target_tag:
        print(f">>> notes base: none (bounded window) — could not map the target repo's last "
              f"release ({target_tag}) to a local commit; pass --since to pin the range.",
              file=sys.stderr)
    notes = render(args.variant, variant, args.version, since, args.extra_notes,
                   args.include_non_conventional)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(notes)
        print(f">>> release notes written to {args.out}", file=sys.stderr)
    else:
        sys.stdout.write(notes)


if __name__ == "__main__":
    main()
