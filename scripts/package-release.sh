#!/usr/bin/env bash
# Build release artifacts and collect them under dist/.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build-release}"
DIST_DIR="${DIST_DIR:-$SOURCE_DIR/dist}"
WITH_APPIMAGE="${WITH_APPIMAGE:-1}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

collect_artifacts() {
    local pattern="$1"

    while IFS= read -r -d '' file; do
        cp -f "$file" "$DIST_DIR/"
    done < <(find "$BUILD_DIR" -maxdepth 1 -type f -name "$pattern" -print0)
}

require_command cmake
require_command cpack

"$SCRIPT_DIR/build-release.sh"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
find "$BUILD_DIR" -maxdepth 1 -type f \( \
    -name "*.deb" -o \
    -name "*.rpm" -o \
    -name "*.tar.gz" -o \
    -name "*.AppImage" \
\) -delete

(
    cd "$BUILD_DIR"
    cpack -G DEB
    cpack -G RPM
    cpack -G TGZ
)

if [[ "$WITH_APPIMAGE" == "1" ]]; then
    cmake --build "$BUILD_DIR" --target package-appimage --parallel
fi

collect_artifacts "*.deb"
collect_artifacts "*.rpm"
collect_artifacts "*.tar.gz"
collect_artifacts "*.AppImage"

if ! find "$DIST_DIR" -maxdepth 1 -type f | grep -q .; then
    echo "No release artifacts were generated." >&2
    exit 1
fi

(
    cd "$DIST_DIR"
    sha256sum * > SHA256SUMS
)

echo "Release artifacts: $DIST_DIR"
ls -lh "$DIST_DIR"
