#!/usr/bin/env bash
# Create or update a GitHub release using artifacts from dist/.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="${DIST_DIR:-$SOURCE_DIR/dist}"
TAG="${1:-}"

if [[ -z "$TAG" ]]; then
    echo "Usage: $0 <tag>" >&2
    echo "Example: $0 v0.2.47" >&2
    exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
    echo "Missing required command: gh" >&2
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "GitHub CLI is not authenticated. Run: gh auth login" >&2
    exit 1
fi

if [[ ! -d "$DIST_DIR" ]] || ! find "$DIST_DIR" -maxdepth 1 -type f | grep -q .; then
    echo "No artifacts found in $DIST_DIR. Run scripts/package-release.sh first." >&2
    exit 1
fi

cd "$SOURCE_DIR"

if ! git rev-parse "$TAG" >/dev/null 2>&1; then
    git tag "$TAG"
fi

TARGET_COMMIT="$(git rev-list -n 1 "$TAG")"

if gh release view "$TAG" >/dev/null 2>&1; then
    gh release upload "$TAG" "$DIST_DIR"/* --clobber
else
    gh release create "$TAG" "$DIST_DIR"/* --title "$TAG" --target "$TARGET_COMMIT" --generate-notes
fi

echo "GitHub release ready: $TAG"
