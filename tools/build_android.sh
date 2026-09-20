#!/usr/bin/env bash
# Builds the Android APK without Gradle (docs/10 §6, M7): CMake with the NDK toolchain →
# libamazing_alex.so, the imported asset tree bundled under assets/aa/ (read in place from the APK
# through AssetRoot), aapt2 compile / link, the library added, zipalign, apksigner with a generated debug key.
#
#   tools/build_android.sh [--assets build/assets] [--out build/android] [--abi arm64-v8a]
#                          [--ndk <dir>] [--sdk <dir>] [--build-tools <version>] [--platform <api>]
#                          [--icon <png>] [--nointro] [--release]   (Release CMake build type is the default; --debug for Debug)
#
# The launcher icon is the imported canonical branding/icon.png (or --icon), resized into the
# mipmap densities at packaging time — like the assets, it is the player's own copy and never sits in
# platform/android/res, whose ic_launcher.png is a generated stand-in used only when no original is found.
#
# Output: <out>/amazing_alex.apk (installed with `adb install -r`); tools/android_emulator.sh drives it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}"
NDK=""
BUILD_TOOLS=""
PLATFORM_API=36
MIN_SDK=24
ABI="arm64-v8a"
ASSETS="$ROOT/build/assets"
OUT="$ROOT/build/android"
BUILD_TYPE="Release"
ICON=""
NOINTRO=OFF
PYTHON="${PYTHON:-$ROOT/.venv/bin/python}"
[ -x "$PYTHON" ] || PYTHON="python3"

while [ $# -gt 0 ]; do
    case "$1" in
        --assets) ASSETS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --abi) ABI="$2"; shift 2 ;;
        --ndk) NDK="$2"; shift 2 ;;
        --sdk) SDK="$2"; shift 2 ;;
        --build-tools) BUILD_TOOLS="$2"; shift 2 ;;
        --platform) PLATFORM_API="$2"; shift 2 ;;
        --icon) ICON="$2"; shift 2 ;;
        --nointro) NOINTRO=ON; shift ;;
        --debug) BUILD_TYPE="Debug"; shift ;;
        --release) BUILD_TYPE="Release"; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done

# Keep output paths absolute: packaging temporarily changes directory to the staged
# tree before adding the native library, so a relative OUT would otherwise be
# resolved as OUT/stage/OUT and zip would fail to create the APK.
case "$OUT" in
    /*) ;;
    *) OUT="$ROOT/$OUT" ;;
esac

[ -f "$ASSETS/manifest.json" ] || { echo "no imported asset tree at $ASSETS (tools/import_assets.py ... --border-profile 1024X768)" >&2; exit 1; }
[ -n "$ICON" ] || ICON="$ASSETS/branding/icon.png"
if [ -z "$NDK" ]; then
    # `|| true`: with no ndk/ directory the pipeline would end the script (set -e -o pipefail) before the
    # diagnostic below.
    NDK="$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1 || true)"
fi
[ -f "$NDK/build/cmake/android.toolchain.cmake" ] || { echo "no NDK toolchain under ${NDK:-$SDK/ndk} (--ndk <dir>)" >&2; exit 1; }
if [ -z "$BUILD_TOOLS" ]; then
    BUILD_TOOLS="$(ls "$SDK/build-tools" 2>/dev/null | sort -V | tail -1 || true)"
fi
BT="$SDK/build-tools/$BUILD_TOOLS"
ANDROID_JAR="$SDK/platforms/android-$PLATFORM_API/android.jar"
for tool in "$BT/aapt2" "$BT/zipalign" "$BT/apksigner" "$ANDROID_JAR"; do
    [ -e "$tool" ] || { echo "missing $tool" >&2; exit 1; }
done
KEYTOOL="$(command -v keytool || true)"
[ -n "$KEYTOOL" ] || { echo "keytool (a JDK) is needed for the debug keystore" >&2; exit 1; }

echo "== NDK $NDK, build-tools $BUILD_TOOLS, platform $PLATFORM_API, ABI $ABI, $BUILD_TYPE"
NATIVE="$OUT/native-$ABI"
mkdir -p "$NATIVE"
cmake -S "$ROOT" -B "$NATIVE" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$MIN_SDK" -DANDROID_STL=c++_static \
    -DAA_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DAA_NOINTRO="$NOINTRO" > "$NATIVE/configure.log"
cmake --build "$NATIVE" --target amazing_alex
SO="$NATIVE/app/libamazing_alex.so"
[ -f "$SO" ] || { echo "no $SO" >&2; exit 1; }

echo "== staging the assets"
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/assets/aa" "$STAGE/lib/$ABI"
cp -R "$ASSETS"/. "$STAGE/assets/aa/"
cp "$SO" "$STAGE/lib/$ABI/"
FILE_COUNT="$(find "$STAGE/assets/aa" -type f | wc -l | tr -d ' ')"
echo "   $FILE_COUNT files"

echo "== aapt2"
mkdir -p "$OUT/res"
# The resources: platform/android/res (the manifest's strings, the stand-in icon) with the original's launcher
# icon laid over its mipmap-* when one is at hand.
RES_SRC="$OUT/res/src"
rm -rf "$RES_SRC"
cp -R "$ROOT/platform/android/res" "$RES_SRC"
if [ -f "$ICON" ]; then
    rm -rf "$RES_SRC"/mipmap-*
    if "$PYTHON" "$ROOT/tools/android_icon.py" "$ICON" "$RES_SRC" > /dev/null; then
        echo "   launcher icon from $ICON"
    else
        cp -R "$ROOT/platform/android/res"/mipmap-* "$RES_SRC/"
        echo "   launcher icon: the resize failed, keeping platform/android/res's stand-in" >&2
    fi
else
    echo "   launcher icon: no original at $ICON (--icon <png>), keeping platform/android/res's stand-in"
fi
"$BT/aapt2" compile --dir "$RES_SRC" -o "$OUT/res/compiled.zip"
# ${A[@]+"${A[@]}"} below: an empty array is "unbound" to bash 3.2's set -u (stock macOS /bin/bash).
AAPT_LINK_FLAGS=()
if [ "$BUILD_TYPE" = "Debug" ]; then
    AAPT_LINK_FLAGS+=(--debug-mode)
fi
"$BT/aapt2" link -o "$OUT/unaligned.apk" -I "$ANDROID_JAR" \
    --manifest "$ROOT/platform/android/AndroidManifest.xml" -A "$STAGE/assets" \
    --min-sdk-version "$MIN_SDK" --target-sdk-version 34 ${AAPT_LINK_FLAGS[@]+"${AAPT_LINK_FLAGS[@]}"} \
    --auto-add-overlay "$OUT/res/compiled.zip"
(cd "$STAGE" && zip -q -r "$OUT/unaligned.apk" lib)

echo "== zipalign + apksigner"
KEYSTORE="$OUT/debug.keystore"
if [ ! -f "$KEYSTORE" ]; then
    "$KEYTOOL" -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android -alias androiddebugkey \
        -dname "CN=Android Debug,O=Android,C=US" -keyalg RSA -keysize 2048 -validity 10000 > /dev/null 2>&1
fi
"$BT/zipalign" -p -f 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey \
    --out "$OUT/amazing_alex.apk" "$OUT/aligned.apk"
rm -f "$OUT/unaligned.apk" "$OUT/aligned.apk"
ls -la "$OUT/amazing_alex.apk"
