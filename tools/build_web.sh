#!/usr/bin/env bash
# Build the optional browser version. Assets are deliberately supplied locally and never copied into git.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/build/assets"
OUT="$ROOT/build-web"
NOINTRO=OFF

usage() {
    echo "usage: tools/build_web.sh [--assets DIR] [--out DIR] [--nointro]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --assets) [ "$#" -ge 2 ] || usage; ASSETS="$2"; shift 2 ;;
        --out) [ "$#" -ge 2 ] || usage; OUT="$2"; shift 2 ;;
        --nointro) NOINTRO=ON; shift ;;
        *) usage ;;
    esac
done

command -v emcmake >/dev/null || { echo "Emscripten is required: activate its environment so emcmake is on PATH" >&2; exit 1; }

# EM_CACHE (below) and CMake's -B both require an absolute path; --assets/--out may be given relative
# to the caller's cwd, so resolve them here rather than letting emcmake fail deep inside its own checks.
case "$ASSETS" in
    /*) ;;
    *) ASSETS="$(cd "$ASSETS" 2>/dev/null && pwd)" || { echo "no such directory: $ASSETS" >&2; exit 1; } ;;
esac
case "$OUT" in
    /*) ;;
    *) mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)" ;;
esac

[ -f "$ASSETS/manifest.json" ] || { echo "no imported asset tree at $ASSETS (run tools/import_assets.py first)" >&2; exit 1; }

# Homebrew installs Emscripten read-only, while emcc's default cache is below that installation.
# Keep generated compiler artefacts with this disposable build instead; callers may still override EM_CACHE.
export EM_CACHE="${EM_CACHE:-$OUT/emscripten-cache}"
mkdir -p "$EM_CACHE"

# emcmake hands CMake the toolchain by its resolved, versioned path (Homebrew: Cellar/emscripten/<version>), and
# CMake pins the compiler from it on the first configure; after an Emscripten upgrade that path is gone and the
# configure fails. Remember the installation the tree was configured with and drop CMake's cache when it changes.
EM_ROOT="$(em-config EMSCRIPTEN_ROOT)"
EM_STAMP="$OUT/.emscripten-root"
if [ -f "$OUT/CMakeCache.txt" ] && [ "$(cat "$EM_STAMP" 2>/dev/null)" != "$EM_ROOT" ]; then
    echo "Emscripten changed to $EM_ROOT: reconfiguring $OUT from scratch" >&2
    rm -rf "$OUT/CMakeCache.txt" "$OUT/CMakeFiles"
fi
mkdir -p "$OUT"
printf '%s\n' "$EM_ROOT" > "$EM_STAMP"

cmake_args=(
    -DAA_BUILD_WEB=ON
    -DAA_BUILD_TESTS=OFF
    -DAA_NOINTRO="$NOINTRO"
    -DAA_WEB_ASSETS="$ASSETS"
)

# import_assets.py normalises the best launcher artwork in the source package to one canonical PNG.
FAVICON="$ASSETS/branding/icon.png"
if [ -f "$FAVICON" ]; then
    cmake_args+=( -DAA_WEB_FAVICON="$FAVICON" )
else
    echo "web favicon: no imported artwork at $FAVICON" >&2
fi

emcmake cmake -S "$ROOT" -B "$OUT" -G Ninja \
    "${cmake_args[@]}"
cmake --build "$OUT" --target amazing_alex
echo "web build: $OUT/app/index.html"
