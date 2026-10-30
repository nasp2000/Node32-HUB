#!/usr/bin/env bash
# =============================================================================
# release.sh — dispatch the "Release firmware" workflow and follow it.
#
# Usage:
#   ./release.sh CAM_S3                      # build + publish
#   ./release.sh CAM_S3 --dry-run            # build + package, do not publish
#   ./release.sh CAM_S3 --wait               # dispatch and watch the run
#   ./release.sh CAM_S3 --bump 1.121         # bump version (both files), commit, push, release
#   ./release.sh CAM_S3 --version v1.121 --since v1.120 --notes "hotfix"
#   ./release.sh CAM_S3 --overwrite          # replace an existing release tag
#
# Preflights (all fail before any CI minutes are spent):
#   - gh authenticated; secrets present (skipped on --dry-run)
#   - working tree clean and HEAD pushed (the workflow runs the pushed commit)
#   - target repo already has no release tag with this version
#
# The variant -> repo mapping is .github/release_targets.json. Secrets are read
# by GitHub Actions, never by this script; it only checks that they exist.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="${RELEASE_REPO:-nasp2000/Node32-HUB}"
WORKFLOW="release.yml"
SMTP_IMPL="chatgpt/src/smtp_core_impl.h"
MODBUS_SLAVE="chatgpt/src/modbus_slave.cpp"

VARIANT=""
DRY_RUN=0
WAIT=0
OVERWRITE=0
VERSION=""
SINCE=""
NOTES=""
BUMP=""

usage() { sed -n '2,21p' "$0"; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run)   DRY_RUN=1 ;;
    --wait)      WAIT=1 ;;
    --overwrite) OVERWRITE=1 ;;
    --version)   shift; VERSION="${1:-}" ;;
    --since)     shift; SINCE="${1:-}" ;;
    --notes)     shift; NOTES="${1:-}" ;;
    --bump)      shift; BUMP="${1:-}" ;;
    -h|--help)   usage 0 ;;
    -*)          echo "unknown option: $1" >&2; usage 2 ;;
    *)           [ -z "$VARIANT" ] && VARIANT="$1" || { echo "unexpected arg: $1" >&2; usage 2; } ;;
  esac
  shift
done

[ -n "$VARIANT" ] || usage 2

cd "$ROOT"

# --- preflight: gh auth -------------------------------------------------------
if ! gh auth status >/dev/null 2>&1; then
  echo "ERROR: gh is not authenticated. Run: gh auth login" >&2
  exit 1
fi

# --- preflight: variant exists in the manifest --------------------------------
read -r TARGET_REPO TAG_SUFFIX <<EOF
$(python3 - "$VARIANT" <<'PY'
import json, sys
with open(".github/release_targets.json", encoding="utf-8") as f:
    variants = json.load(f)["variants"]
key = sys.argv[1]
if key not in variants:
    sys.exit(f"unknown variant {key!r} (known: {', '.join(sorted(variants))})")
v = variants[key]
print(v["repo"], v.get("tag_suffix", "") or "-")
PY
)
EOF
[ "$TAG_SUFFIX" = "-" ] && TAG_SUFFIX=""
echo ">>> variant $VARIANT -> $TARGET_REPO"

# --- preflight: the workflow runs the PUSHED commit ---------------------------
if [ -n "$(git status --porcelain)" ]; then
  echo "ERROR: working tree has uncommitted changes:" >&2
  git status --short >&2
  echo "  The workflow always builds the pushed commit — commit or stash first." >&2
  exit 1
fi

# --- optional: bump the firmware version (both sources + commit + push) --------
if [ -n "$BUMP" ]; then
  NEW=$(python3 - "$BUMP" <<'PY'
import re, sys
raw = sys.argv[1].strip()
m = re.fullmatch(r"v?(\d+)\.(\d+)", raw)
if not m:
    sys.exit(f"ERROR: invalid version {raw!r} (expected X.YYY, e.g. 1.121)")
print(f"v{m.group(1)}.{m.group(2)}")
PY
)
  NUM=$(python3 -c "import sys,re; v=sys.argv[1].lstrip('v'); a,b=v.split('.'); print(int(a+b))" "$NEW")
  python3 - "$SMTP_IMPL" "$MODBUS_SLAVE" "$NEW" "$NUM" <<'PY'
import re, sys
smtp, modbus, new, num = sys.argv[1:5]
s = open(smtp, encoding="utf-8").read()
s2, n = re.subn(r'(#define\s+FIRMWARE_VERSION_STR\s+")v?\d+\.\d+(")', r"\g<1>%s\g<2>" % new, s, count=1)
if not n:
    sys.exit("ERROR: FIRMWARE_VERSION_STR not found")
open(smtp, "w", encoding="utf-8").write(s2)

m = open(modbus, encoding="utf-8").read()
m2, n = re.subn(r'(_hreg\[HR_FW_VERSION\]\s*=\s*)\d+(;.*)$',
                r"\g<1>%s\g<2>" % num, m, count=1, flags=re.M)
if not n:
    sys.exit("ERROR: _hreg[HR_FW_VERSION] not found")
# keep the trailing "// vX.YYY" comment honest
m2 = re.sub(r'(HR_FW_VERSION\]\s*=\s*\d+;\s*//\s*)v?\d+\.\d+', r"\g<1>%s" % new, m2, count=1)
open(modbus, "w", encoding="utf-8").write(m2)
print(f">>> bumped {smtp} + {modbus} to {new} ({num})")
PY
  python3 chatgpt/scripts/validate_version.py
  git add "$SMTP_IMPL" "$MODBUS_SLAVE"
  git commit -q -m "chore: bump $NEW (no fw change)" \
    --author="nasp2000 <nasp2000@users.noreply.github.com>"
  git push origin HEAD 2>&1 | tail -1
  VERSION="$NEW"
  echo ">>> version bumped to $NEW and pushed"
fi

# --- preflight: HEAD is exactly what origin will build ------------------------
git fetch -q origin
if [ "$(git rev-parse HEAD)" != "$(git rev-parse '@{u}')" ]; then
  echo "ERROR: local HEAD is not pushed to $(git rev-parse --abbrev-ref '@{u}')." >&2
  echo "  The workflow builds the remote commit — push first." >&2
  exit 1
fi

# --- resolve version + target tag --------------------------------------------
if [ -z "$VERSION" ]; then
  VERSION=$(python3 - "$SMTP_IMPL" <<'PY'
import re, sys
for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    m = re.search(r'#define\s+FIRMWARE_VERSION_STR\s+"([^"]+)"', line)
    if m:
        print(m.group(1).strip())
        break
PY
)
fi
TAG="${VERSION}${TAG_SUFFIX}"
echo ">>> version $VERSION -> tag $TAG"

# --- preflight: tag not already used in the target repo ----------------------
if [ "$OVERWRITE" = "0" ] && gh api "repos/$TARGET_REPO/git/refs/tags/$TAG" >/dev/null 2>&1; then
  echo "ERROR: $TARGET_REPO already has a tag $TAG." >&2
  echo "  Bump the version (--bump X.YYY) or re-run with --overwrite." >&2
  exit 1
fi

# --- preflight: secrets exist -------------------------------------------------
if [ "$DRY_RUN" = "1" ]; then
  echo ">>> dry-run: skipping secret checks (no publish, no OTA signing)"
else
  secrets=$(gh secret list --repo "$REPO" 2>/dev/null | awk '{print $1}')
  missing=()
  echo "$secrets" | grep -qx "RELEASE_TOKEN" || missing+=("RELEASE_TOKEN")
  echo "$secrets" | grep -qx "OTA_PRIVATE_PEM" || missing+=("OTA_PRIVATE_PEM")
  if [ ${#missing[@]} -gt 0 ]; then
    echo "ERROR: missing repo secret(s) on $REPO: ${missing[*]}" >&2
    for s in "${missing[@]}"; do
      case "$s" in
        RELEASE_TOKEN) echo "  gh secret set RELEASE_TOKEN --repo $REPO" >&2 ;;
        OTA_PRIVATE_PEM) echo "  gh secret set OTA_PRIVATE_PEM --repo $REPO < chatgpt/ota_private.pem" >&2 ;;
      esac
    done
    exit 1
  fi
fi

# --- dispatch -----------------------------------------------------------------
args=(-f "variant=$VARIANT")
[ -n "$VERSION" ] && args+=(-f "version=$VERSION")
[ -n "$SINCE" ] && args+=(-f "since=$SINCE")
[ -n "$NOTES" ] && args+=(-f "notes=$NOTES")
[ "$OVERWRITE" = "1" ] && args+=(-f "overwrite=true")
[ "$DRY_RUN" = "1" ] && args+=(-f "dry_run=true")

before=$(gh run list --repo "$REPO" --workflow "$WORKFLOW" --limit 1 \
  --json databaseId --jq '.[0].databaseId // empty' 2>/dev/null || true)

label="$VARIANT"
[ "$DRY_RUN" = "1" ] && label="$VARIANT, dry-run"
echo ">>> dispatching $WORKFLOW ($label)"
gh workflow run "$WORKFLOW" --repo "$REPO" "${args[@]}"

run_id=""
for _ in $(seq 1 30); do
  sleep 2
  run_id=$(gh run list --repo "$REPO" --workflow "$WORKFLOW" --limit 1 \
    --json databaseId --jq '.[0].databaseId // empty' 2>/dev/null || true)
  [ -n "$run_id" ] && [ "$run_id" != "$before" ] && break
  run_id=""
done

if [ -z "$run_id" ]; then
  echo "WARNING: could not find the new run; check: gh run list --repo $REPO --workflow $WORKFLOW" >&2
  exit 0
fi

url=$(gh run view "$run_id" --repo "$REPO" --json url --jq '.url')
echo ">>> run: $url"

if [ "$WAIT" = "1" ]; then
  gh run watch "$run_id" --repo "$REPO" --exit-status
fi
