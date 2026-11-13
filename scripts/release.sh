#!/usr/bin/env bash
# Create a version tag, build its artifacts, and publish the GitHub release.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
KIND="${1:-}"
case "$KIND" in major|minor|patch) ;; *) echo "Usage: $0 {major|minor|patch}" >&2; exit 1 ;; esac
cd "$SOURCE_DIR"
for command in git gh cmake cpack sha256sum; do
    command -v "$command" >/dev/null || { echo "Missing required command: $command" >&2; exit 1; }
done
gh auth status >/dev/null 2>&1 || { echo "Run gh auth login first." >&2; exit 1; }
source "$SCRIPT_DIR/release-checks.sh"
check_release_checkout
if git tag --points-at HEAD --list 'v*' | grep -Eq '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'; then
    echo "HEAD already has a version tag; release a new commit instead." >&2
    exit 1
fi
LATEST="$(git tag --merged HEAD --list 'v*' | grep -E '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' | sort -V | tail -n 1 || true)"
[[ -n "$LATEST" ]] || { echo "No reachable version tag found; create an initial vMAJOR.MINOR.PATCH tag first." >&2; exit 1; }
IFS=. read -r major minor patch <<< "${LATEST#v}"
case "$KIND" in
    major) major=$((10#$major + 1)); minor=0; patch=0 ;;
    minor) minor=$((10#$minor + 1)); patch=0 ;;
    patch) patch=$((10#$patch + 1)) ;;
esac
TAG="v$major.$minor.$patch"
if git show-ref --verify --quiet "refs/tags/$TAG"; then
    echo "Tag $TAG already exists; refusing to overwrite it." >&2
    exit 1
fi
echo "Creating $TAG at $(git rev-parse --short HEAD) (previous: $LATEST)"
git tag -a "$TAG" -m "Release $TAG"
echo "If a later step fails, tag $TAG is retained for inspection and retry."
# A fresh, ignored directory avoids mixing old builds or deleting user artifacts.
mkdir -p "$SOURCE_DIR/build-release"
BUILD_DIR="$(mktemp -d "$SOURCE_DIR/build-release/release.XXXXXXXX")"
export BUILD_DIR
export DIST_DIR="$BUILD_DIR/dist"
"$SCRIPT_DIR/package-release.sh"
"$SCRIPT_DIR/create-github-release.sh" "$TAG"
