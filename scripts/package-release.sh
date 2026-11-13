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

cd "$SOURCE_DIR"
SOURCE_COMMIT="$(git rev-parse HEAD)"
SOURCE_TAG="$(git describe --tags --exact-match --match 'v[0-9]*' HEAD 2>/dev/null || true)"
SOURCE_CLEAN=1
[[ -z "$(git status --porcelain --untracked-files=normal)" ]] || SOURCE_CLEAN=0
"$SCRIPT_DIR/build-release.sh"
if [[ "$(git rev-parse HEAD)" != "$SOURCE_COMMIT" ]]; then
    echo "HEAD changed during the build; refusing to package." >&2
    exit 1
fi
[[ -z "$(git status --porcelain --untracked-files=normal)" ]] || SOURCE_CLEAN=0

mkdir -p "$DIST_DIR"
if find "$DIST_DIR" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
    echo "Artifact directory is not empty: $DIST_DIR. Choose an empty DIST_DIR." >&2
    exit 1
fi
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

if [[ "$(git rev-parse HEAD)" != "$SOURCE_COMMIT" ]]; then
    echo "HEAD changed during packaging; refusing to write release metadata." >&2
    exit 1
fi
[[ -z "$(git status --porcelain --untracked-files=normal)" ]] || SOURCE_CLEAN=0
if [[ -n "$SOURCE_TAG" && "$SOURCE_CLEAN" == 1 ]]; then
    VERSION="${SOURCE_TAG#v}"
    if ! grep -Fq "\"$VERSION\"" "$BUILD_DIR/generated/version.h" ||
       ! grep -Fq "set(CPACK_PACKAGE_VERSION \"$VERSION\")" "$BUILD_DIR/CPackConfig.cmake"; then
        echo "Built application and package version do not match $SOURCE_TAG." >&2
        exit 1
    fi
fi
cat > "$DIST_DIR/RELEASE-METADATA" <<EOF
commit=$SOURCE_COMMIT
tag=$SOURCE_TAG
clean=$SOURCE_CLEAN
EOF
(
    cd "$DIST_DIR"
    sha256sum ./* > SHA256SUMS
)

echo "Release artifacts: $DIST_DIR"
ls -lh "$DIST_DIR"
