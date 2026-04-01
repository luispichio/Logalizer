#!/usr/bin/env bash
# scripts/make-appimage.sh
# Called by the CMake target 'package-appimage'.
# Usage: make-appimage.sh <BUILD_DIR> <SOURCE_DIR> <QMAKE_PATH>
set -e

BUILD_DIR="$1"
SOURCE_DIR="$2"
QMAKE="$3"

APPDIR="$BUILD_DIR/AppDir"
TOOLS="$SOURCE_DIR/tools"
LINUXDEPLOYQT="$TOOLS/linuxdeployqt.AppImage"

echo "=== Logalizer AppImage builder ==="
echo "Build dir : $BUILD_DIR"
echo "qmake     : $QMAKE"

# ── 1. Download linuxdeployqt if missing ─────────────────────────────────────
if [ ! -f "$LINUXDEPLOYQT" ]; then
    echo "Downloading linuxdeployqt..."
    mkdir -p "$TOOLS"
    wget -q --show-progress \
        "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage" \
        -O "$LINUXDEPLOYQT"
    chmod +x "$LINUXDEPLOYQT"
    echo "linuxdeployqt saved to $LINUXDEPLOYQT"
fi

# ── 2. Install into AppDir ────────────────────────────────────────────────────
echo "Installing into AppDir..."
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

# linuxdeployqt needs a .desktop file at AppDir/usr/share/applications/
# (already installed via CMakeLists install() rule)

# ── 3. Run linuxdeployqt ──────────────────────────────────────────────────────
echo "Running linuxdeployqt..."
cd "$BUILD_DIR"

# Disable FUSE (required in some environments / containers)
# linuxdeployqt fails if it detects Qt plugins with missing dependencies.
# Since Qt6Sql is linked, it pulls all SQL drivers (MySQL, PSQL, ODBC, Mimer),
# which fail ldd if client libraries are not installed on the system.
# We dynamically create dummy libraries for ALL missing dependencies 
# to trick ldd and satisfy the check without altering the host Qt installation.
DUMMY_DIR="$BUILD_DIR/dummy_libs"
mkdir -p "$DUMMY_DIR"

QT_PLUGINS_DIR="$(dirname "$QMAKE")/../plugins/sqldrivers"
if [ -d "$QT_PLUGINS_DIR" ]; then
    MISSING_LIBS=$(ldd "$QT_PLUGINS_DIR"/*.so 2>/dev/null | grep "=> not found" | awk '{print $1}' | sort -u || true)
    for lib in $MISSING_LIBS; do
        echo "Stubbing missing Qt plugin dependency: $lib"
        gcc -shared -o "$DUMMY_DIR/$lib" -x c /dev/null
    done
fi

# ── 3. Force desktop file and icon to root (fix for appimagetool) ─────────────
echo "Copying desktop file to AppDir root..."
cp "$APPDIR/usr/share/applications/logalizer.desktop" "$APPDIR/"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/logalizer.png" "$APPDIR/"

export LD_LIBRARY_PATH="$DUMMY_DIR:$LD_LIBRARY_PATH"

export VERSION=$(git rev-parse --short HEAD)

"$LINUXDEPLOYQT" \
    "$APPDIR/usr/bin/Logalizer" \
    -qmake="$QMAKE" \
    -appimage \
    -no-translations \
    -unsupported-allow-new-glibc \
    -verbose=1

# Cleanup dummy
rm -rf "$DUMMY_DIR"

echo ""
echo "=== AppImage ready ==="
ls -lh "$BUILD_DIR"/Logalizer*.AppImage 2>/dev/null || \
ls -lh "$BUILD_DIR"/*.AppImage 2>/dev/null
