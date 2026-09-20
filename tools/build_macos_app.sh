#!/usr/bin/env bash
# Builds the macOS .app bundle (docs/10 §6): the amazing_alex_bundle CMake target (main.cpp as a bundle with
# platform/macos/Info.plist.in), the imported asset tree laid into Contents/Resources/assets — the game finds it there
# without --assets (platform_desktop.cpp) — the imported canonical launcher icon (branding/icon.png,
# or --icon PNG) as AppIcon.icns, then an ad-hoc code signature over the whole bundle. The bundle embeds the
# user's own imported assets, so it is not something to redistribute.
#
#   tools/build_macos_app.sh [--assets build/assets] [--build build] [--icon <png>] [--debug | --release]
#                            [--nointro] [--no-run]
#   → <build>/app/Amazing Alex.app; Release is the default and is configured into <build> every time (a Debug
#   tree there gets reconfigured — pass --debug to keep it); --no-run skips the headless walk of the bundle's binary
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="$ROOT/build/assets"
BUILD="$ROOT/build"
ICON=""
BUILD_TYPE="Release"
NOINTRO=OFF
RUN=1
PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
[ -x "$PYTHON" ] || PYTHON="python3"

while [ $# -gt 0 ]; do
    case "$1" in
        --assets) ASSETS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --icon) ICON="$2"; shift 2 ;;
        --debug) BUILD_TYPE="Debug"; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        --nointro) NOINTRO=ON; shift ;;
        --no-run) RUN=0; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
[ "$(uname)" = Darwin ] || { echo "the .app bundle is built on macOS only" >&2; exit 1; }
[ -f "$ASSETS/manifest.json" ] || { echo "no imported asset tree at $ASSETS (tools/import_assets.py ...)" >&2; exit 1; }
[ -n "$ICON" ] || ICON="$ASSETS/branding/icon.png"

echo "== cmake (amazing_alex_bundle, $BUILD_TYPE)"
if [ -f "$BUILD/CMakeCache.txt" ]; then
    CACHED="$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=//p' "$BUILD/CMakeCache.txt")"
    [ "$CACHED" = "$BUILD_TYPE" ] || echo "   $BUILD was configured as '$CACHED': reconfiguring as $BUILD_TYPE (a full rebuild)"
fi
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DAA_NOINTRO="$NOINTRO" > /dev/null
cmake --build "$BUILD" --target amazing_alex_bundle
APP="$BUILD/app/Amazing Alex.app"
RES="$APP/Contents/Resources"
[ -x "$APP/Contents/MacOS/Amazing Alex" ] || { echo "no bundle at $APP" >&2; exit 1; }

echo "== assets → Contents/Resources/assets"
rm -rf "$RES/assets"
mkdir -p "$RES"
ditto "$ASSETS" "$RES/assets"
echo "   $(find "$RES/assets" -type f | wc -l | tr -d ' ') files, $(du -sh "$RES/assets" | cut -f1)"

echo "== icon"
if [ -f "$ICON" ]; then
    if "$PYTHON" "$ROOT/tools/macos_icon.py" "$ICON" "$RES/AppIcon.icns" > /dev/null; then
        echo "   AppIcon.icns from $ICON"
    else
        echo "   icon: the conversion failed (pip install -r tools/requirements.txt), the bundle keeps the generic icon" >&2
    fi
else
    echo "   icon: no original at $ICON (--icon <png>), the bundle keeps the generic icon"
fi

echo "== codesign (ad hoc)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "   $APP"

[ "$RUN" = 1 ] || exit 0
echo "== headless walk of the bundle's binary (no --assets)"
"$APP/Contents/MacOS/Amazing Alex" --headless | grep "headless:"
