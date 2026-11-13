#!/usr/bin/env bash
set -euo pipefail
SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT
mkdir -p "$TEMP_DIR/repo/cmake"
cp "$SOURCE_DIR"/cmake/{Version,GenerateVersion}.cmake "$TEMP_DIR/repo/cmake/"
cp "$SOURCE_DIR/version.h.in" "$TEMP_DIR/repo/"
cd "$TEMP_DIR/repo"
generate() {
    cmake -DSOURCE_DIR="$PWD" -DOUTPUT_FILE="$TEMP_DIR/version.h" -P cmake/GenerateVersion.cmake
}
expect() { grep -Fq -- "$1" "$TEMP_DIR/version.h" || { cat "$TEMP_DIR/version.h"; exit 1; }; }
generate
expect '"0.0.0-dev"'
git init -q
git -c user.name=Test -c user.email=test@example.com commit -q --allow-empty -m initial
git add cmake version.h.in
git -c user.name=Test -c user.email=test@example.com commit -q -m files
generate
expect '"0.0.0-dev+'
git tag v1.2.3
generate
expect '"1.2.3"'
expect '#define APP_VERSION_PATCH 3'
git -c user.name=Test -c user.email=test@example.com commit -q --allow-empty -m next
generate
expect '"1.2.3-dev.1+'
touch untracked
generate
expect ' (dirty)"'
rm untracked
git tag v1.2.4
generate
expect '"1.2.4"'
echo "Version tests passed."
