#!/usr/bin/env bash
# Builds and checks the remake on Linux inside Docker (docs/10 §6, M7): ubuntu:24.04 with gcc 13, the
# X11 / OpenGL development packages raylib's GLFW needs, then G1 (aa_libm_selftest), aa_tests with the
# imported assets and the --headless walk of amazing_alex. The window itself is not opened (no display).
#
#   tools/build_linux_docker.sh [--assets build/assets] [--image ubuntu:24.04] [--build build-linux-docker]
#                               [--icon PNG] [--nointro]
# The Docker daemon must be running. The project tree is mounted read-only at /src; the build directory
# is mounted read-write at /build (a host directory, kept between runs).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="$ROOT/build/assets"
IMAGE="ubuntu:24.04"
BUILD="$ROOT/build-linux-docker"
ICON=""
NOINTRO=OFF
while [ $# -gt 0 ]; do
    case "$1" in
        --assets) ASSETS="$2"; shift 2 ;;
        --image) IMAGE="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        --icon) ICON="$2"; shift 2 ;;
        --nointro) NOINTRO=ON; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
[ -f "$ASSETS/manifest.json" ] || { echo "assets not imported: $ASSETS (run tools/import_assets.py first)" >&2; exit 1; }
[ -n "$ICON" ] || ICON="$ASSETS/branding/icon.png"
docker info > /dev/null 2>&1 || { echo "the Docker daemon is not running" >&2; exit 1; }
mkdir -p "$BUILD"
if [ -f "$ICON" ]; then
    cp "$ICON" "$BUILD/amazing_alex.png"
else
    echo "icon: no original at $ICON (--icon <png>), no amazing_alex.png beside the build" >&2
fi
# Ubuntu 24.04's cmake is 3.28 (the raylib GLFW policy floor is fine there); ninja for the build.
docker run --rm \
    -v "$ROOT:/src:ro" -v "$BUILD:/build" -v "$ASSETS:/assets:ro" \
    -e DEBIAN_FRONTEND=noninteractive -e AA_NOINTRO="$NOINTRO" "$IMAGE" bash -euo pipefail -c '
        apt-get update -qq > /dev/null
        apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config \
            libgl1-mesa-dev libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libxkbcommon-dev \
            libwayland-dev wayland-protocols libasound2-dev > /dev/null
        echo "== $(gcc --version | head -1), $(cmake --version | head -1)"
        cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAA_ASSETS=/assets -DAA_NOINTRO="$AA_NOINTRO" > /build/configure.log
        cmake --build /build > /build/build.log 2>&1 || { grep -B 20 -E "error|FAILED" /build/build.log | tail -60; exit 1; }
        grep -E "warning:" /build/build.log | grep -v third_party || true
        echo "== G1"
        /build/aa_libm/aa_libm_selftest | grep "aa_libm self-test"
        echo "== aa_tests"
        AA_SOURCE_DIR=/src /build/tests/aa_tests | grep -E "test cases|Status"
        echo "== headless walk"
        /build/app/amazing_alex --assets /assets --headless --save-dir /build/walk_saves | grep "headless:"
    '
