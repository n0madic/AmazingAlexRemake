#!/usr/bin/env bash
# Cross-builds the remake for Windows with mingw-w64 (docs/10 §6, M7) and runs the gates under Wine:
# G1 (aa_libm_selftest.exe), aa_tests.exe with the imported assets, and the --headless walk of
# amazing_alex.exe. Wine has no OpenGL on this machine, so the windowed app is not started here; without
# Wine the script stops after the build (--no-run skips the gates on purpose).
#
#   tools/build_windows_mingw.sh [--assets build/assets] [--build build-mingw] [--icon PNG] [--nointro] [--no-run]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="$ROOT/build/assets"
BUILD="$ROOT/build-mingw"
ICON=""
NOINTRO=OFF
RUN=1
while [ $# -gt 0 ]; do
    case "$1" in
        --assets) ASSETS="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --icon) ICON="$2"; shift 2 ;;
        --nointro) NOINTRO=ON; shift ;;
        --no-run) RUN=0; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
command -v x86_64-w64-mingw32-g++ > /dev/null || { echo "x86_64-w64-mingw32-g++ not found (brew install mingw-w64)" >&2; exit 1; }
[ -f "$ASSETS/manifest.json" ] || { echo "assets not imported: $ASSETS (run tools/import_assets.py first)" >&2; exit 1; }
[ -n "$ICON" ] || ICON="$ASSETS/branding/icon.png"
# The launcher icon is optional: a source package without artwork (a bare decrypted Data tree) imports none.
WINDOWS_ICON=""
if [ -f "$ICON" ]; then
    WINDOWS_ICON="$BUILD/app/AppIcon.ico"
    python3 "$ROOT/tools/windows_icon.py" "$ICON" "$WINDOWS_ICON"
else
    echo "icon: no original at $ICON (--icon <png>), the .exe keeps the generic icon" >&2
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/mingw-w64.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DAA_WINDOWS_ICON="$WINDOWS_ICON" -DAA_NOINTRO="$NOINTRO"
cmake --build "$BUILD"
[ "$RUN" = 1 ] || exit 0
command -v wine > /dev/null || { echo "wine not found (brew install --cask wine-stable); built only" >&2; exit 0; }
export WINEDEBUG=-all
# Wine sees the Unix tree as drive Z:.
WIN_ASSETS="Z:$ASSETS"
echo "== G1 under Wine"
wine "$BUILD/aa_libm/aa_libm_selftest.exe" | grep "aa_libm self-test"
echo "== aa_tests under Wine (AA_ASSETS=$WIN_ASSETS, AA_SOURCE_DIR=Z:$ROOT)"
AA_ASSETS="$WIN_ASSETS" AA_SOURCE_DIR="Z:$ROOT" wine "$BUILD/tests/aa_tests.exe" | grep -E "test cases|Status"
echo "== headless walk under Wine"
wine "$BUILD/app/amazing_alex.exe" --assets "$WIN_ASSETS" --headless | grep "headless:"
