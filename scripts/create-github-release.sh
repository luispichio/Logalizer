#!/usr/bin/env bash
# Publish only artifacts produced from the exact tagged, clean checkout.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="${DIST_DIR:-$SOURCE_DIR/dist}"
TAG="${1:-}"
if [[ ! "$TAG" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "Usage: $0 vMAJOR.MINOR.PATCH" >&2
    exit 1
fi
cd "$SOURCE_DIR"
for command in git gh sha256sum; do
    command -v "$command" >/dev/null || { echo "Missing required command: $command" >&2; exit 1; }
done
gh auth status >/dev/null 2>&1 || { echo "Run gh auth login first." >&2; exit 1; }
source "$SCRIPT_DIR/release-checks.sh"
check_release_checkout
COMMIT="$(git rev-parse HEAD)"
[[ "$(git rev-parse --verify "refs/tags/$TAG^{commit}")" == "$COMMIT" ]] || { echo "Tag must point to HEAD." >&2; exit 1; }
[[ -f "$DIST_DIR/RELEASE-METADATA" && -f "$DIST_DIR/SHA256SUMS" ]] || { echo "Missing release metadata or checksums." >&2; exit 1; }
find "$DIST_DIR" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.rpm' -o -name '*.tar.gz' -o -name '*.AppImage' \) -print -quit | grep -q . || { echo "No release packages found." >&2; exit 1; }
[[ "$(cat "$DIST_DIR/RELEASE-METADATA")" == "$(printf 'commit=%s\ntag=%s\nclean=1' "$COMMIT" "$TAG")" ]] || { echo "Artifacts do not match a clean $TAG at $COMMIT." >&2; exit 1; }
(
    cd "$DIST_DIR"
    sha256sum --check --strict SHA256SUMS
    # Reject extra files too: every uploaded artifact must be checksummed.
    while IFS= read -r -d '' file; do
        [[ "$file" == './SHA256SUMS' ]] && continue
        grep -Fqx -- "$(sha256sum "$file")" SHA256SUMS || { echo "Unchecked artifact: $file" >&2; exit 1; }
    done < <(find . -maxdepth 1 -type f -print0)
)
if gh release view "$TAG" >/dev/null 2>&1; then
    echo "Release $TAG already exists; refusing to overwrite it." >&2
    exit 1
fi
git push origin "refs/tags/$TAG:refs/tags/$TAG"
gh release create "$TAG" "$DIST_DIR"/* --title "$TAG" --verify-tag --generate-notes
echo "GitHub release ready: $TAG"
