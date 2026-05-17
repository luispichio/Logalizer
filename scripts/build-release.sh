#!/usr/bin/env bash
# Configure and build Logalizer in Release mode.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build-release}"
GENERATOR="${GENERATOR:-Ninja}"

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel

echo "Build ready: $BUILD_DIR"
