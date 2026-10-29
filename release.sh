#!/usr/bin/env bash
# =============================================================================
# release.sh — dispatch the "Release firmware" workflow and follow it.
#
# Usage:
#   ./release.sh CAM_S3                      # build + publish
#   ./release.sh CAM_S3 --dry-run            # build + package, do not publish
#   ./release.sh CAM_S3 --wait               # dispatch and watch the run
#   ./release.sh CAM_S3 --version v1.121 --since v1.120 --notes "hotfix"
#   ./release.sh CAM_S3 --overwrite          # replace an existing release tag
#
# The variant -> repo mapping is .github/release_targets.json. Secrets are read
# by GitHub Actions, never by this script; it only checks that they exist.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO="${RELEASE_REPO:-nasp2000/Node32-HUB}"
WORKFLOW="release.yml"

VARIANT=""
DRY_RUN=0
WAIT=0
OVERWRITE=0
VERSION=""
SINCE=""
NOTES=""

usage() { sed -n '2,14p' "$0"; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run)   DRY_RUN=1 ;;
    --wait)      WAIT=1 ;;
    --overwrite) OVERWRITE=1 ;;
    --version)   shift; VERSION="${1:-}" ;;
    --since)     shift; SINCE="${1:-}" ;;
    --notes)     shift; NOTES="${1:-}" ;;
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
TARGET_REPO=$(python3 - "$VARIANT" <<'PY'
import json, sys
with open(".github/release_targets.json", encoding="utf-8") as f:
    variants = json.load(f)["variants"]
key = sys.argv[1]
if key not in variants:
    sys.exit(f"unknown variant {key!r} (known: {', '.join(sorted(variants))})")
print(variants[key]["repo"])
PY
)
echo ">>> variant $VARIANT -> $TARGET_REPO"

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
