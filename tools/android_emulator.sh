#!/usr/bin/env bash
# Drives the Android build on the emulator (docs/10 §8, M7): boots the AVD headless when no device is
# attached, installs the APK, runs the headless campaign walk and reads its verdict from logcat, then
# optionally the scene screenshots (pulled through run-as) and a single-finger touch session with
# screencaps. `adb shell input tap` is a down + up burst that raylib's per-frame touch poll misses, so
# taps are 150 ms swipes in place.
#
#   tools/android_emulator.sh [--avd medium_phone] [--apk build/android/amazing_alex.apk] [--out build/android/run]
#                             [--no-boot] [--screenshots] [--touch] [--keep]
#
# Exit status: 0 when every requested step reported success.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}"
ADB="$SDK/platform-tools/adb"
EMULATOR="$SDK/emulator/emulator"
AVD="medium_phone"
APK="$ROOT/build/android/amazing_alex.apk"
OUT="$ROOT/build/android/run"
PKG="org.amazingalex.remake"
ACTIVITY="$PKG/android.app.NativeActivity"
PROP="debug.amazingalex.args"
BOOT=1
SCREENSHOTS=0
TOUCH=0
KEEP=0

while [ $# -gt 0 ]; do
    case "$1" in
        --avd) AVD="$2"; shift 2 ;;
        --apk) APK="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --no-boot) BOOT=0; shift ;;
        --screenshots) SCREENSHOTS=1; shift ;;
        --touch) TOUCH=1; shift ;;
        --keep) KEEP=1; shift ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
[ -f "$APK" ] || { echo "no APK at $APK (tools/build_android.sh)" >&2; exit 1; }
mkdir -p "$OUT"

STARTED_EMULATOR=0
if ! "$ADB" get-state > /dev/null 2>&1; then
    if [ "$BOOT" = 1 ]; then
        echo "== booting $AVD headless"
        nohup "$EMULATOR" -avd "$AVD" -no-window -gpu swiftshader_indirect -no-audio -no-boot-anim -no-snapshot > "$OUT/emulator.log" 2>&1 &
        STARTED_EMULATOR=1
        "$ADB" wait-for-device
        for _ in $(seq 1 120); do
            [ "$("$ADB" shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ] && break
            sleep 2
        done
    else
        echo "no device attached" >&2
        exit 1
    fi
fi
"$ADB" shell getprop sys.boot_completed | grep -q 1 || { echo "the device did not finish booting" >&2; exit 1; }
echo "== device: $("$ADB" shell getprop ro.product.model | tr -d '\r'), $("$ADB" shell wm size | tail -1 | tr -d '\r'), page size $("$ADB" shell getconf PAGE_SIZE | tr -d '\r')"

echo "== installing"
"$ADB" shell am force-stop "$PKG" > /dev/null 2>&1 || true
# A plain reinstall keeps the old copy until the new one is staged (a 32 MB APK twice); a nearly full
# emulator /data then fails with INSTALL_FAILED_INSUFFICIENT_STORAGE — uninstall first (the walk starts
# from a fresh install anyway; --keep re-runs on the save directory of this run).
"$ADB" uninstall "$PKG" > /dev/null 2>&1 || true
"$ADB" install --no-incremental "$APK" | tail -1

# run <args> <seconds>: launches the activity with the arguments in the property, waits, stops it.
run() {
    "$ADB" shell "setprop $PROP '$1'"
    "$ADB" logcat -c
    "$ADB" shell am start -n "$ACTIVITY" > /dev/null
    sleep "$2"
}
verdict() {
    "$ADB" logcat -d -s "amazing_alex:*" "AndroidRuntime:E" "DEBUG:*" | grep -v "^---" | grep -v glBindAttribLocation > "$OUT/$1.log" || true
    grep -q "$2" "$OUT/$1.log"
}

STATUS=0
echo "== headless walk (fresh install)"
run "--headless" 20
if verdict headless_1 "headless: campaign + sandbox walk ok"; then echo "   ok"; else echo "   FAILED (see $OUT/headless_1.log)"; STATUS=1; fi
"$ADB" shell am force-stop "$PKG"
echo "== headless walk on a kept save directory, twice"
run "--headless --save-dir walk2" 12
"$ADB" shell am force-stop "$PKG"
run "--headless --save-dir walk2" 12
if verdict headless_2 "headless: campaign + sandbox walk ok"; then echo "   ok"; else echo "   FAILED (see $OUT/headless_2.log)"; STATUS=1; fi
"$ADB" shell am force-stop "$PKG"

if [ "$SCREENSHOTS" = 1 ]; then
    echo "== scene screenshots (SwiftShader: minutes)"
    "$ADB" shell "run-as $PKG rm -rf files/shots" > /dev/null 2>&1 || true
    run "--ui-screenshots shots --no-audio" 5
    for _ in $(seq 1 180); do
        "$ADB" shell pidof "$PKG" > /dev/null 2>&1 || break
        sleep 5
    done
    "$ADB" logcat -d -s "amazing_alex:*" | grep -v "^---" > "$OUT/screenshots.log" || true
    mkdir -p "$OUT/shots"
    for f in $("$ADB" shell "run-as $PKG ls files/shots" | tr -d '\r'); do
        "$ADB" shell "run-as $PKG cat files/shots/$f" > "$OUT/shots/$f"
    done
    N="$(ls "$OUT/shots" | wc -l | tr -d ' ')"
    if [ "$N" -ge 30 ] && ! grep -q "walk failed" "$OUT/screenshots.log"; then echo "   $N screenshots in $OUT/shots"; else echo "   FAILED: $N screenshots (see $OUT/screenshots.log)"; STATUS=1; fi
fi

if [ "$TOUCH" = 1 ]; then
    echo "== single-finger session (main menu → play → comic → level → a drag → play), screencaps in $OUT/touch"
    mkdir -p "$OUT/touch"
    "$ADB" shell "run-as $PKG rm -rf files/saves" > /dev/null 2>&1 || true
    run "" 15
    SIZE="$("$ADB" shell wm size | tail -1 | sed 's/.*: //' | tr -d '\r')"
    W="${SIZE%x*}"; H="${SIZE#*x}"
    if [ "$W" -lt "$H" ]; then T="$W"; W="$H"; H="$T"; fi
    px() { echo $(( $1 * W / 2400 )); }
    py() { echo $(( $1 * H / 1080 )); }
    tap() { "$ADB" shell input swipe "$1" "$2" "$1" "$2" 150; sleep "${3:-3}"; }
    shot() { "$ADB" exec-out screencap -p > "$OUT/touch/$1.png"; }
    shot 01_main_menu
    tap "$(px 1200)" "$(py 840)" 4                 # the play button
    shot 02_after_play
    for _ in 1 2 3 4 5; do tap "$(px 1200)" "$(py 540)" 2; done   # the begin comic's frames
    tap "$(px 2208)" "$(py 948)" 8                 # the comic's next button
    shot 03_level
    "$ADB" shell input swipe "$(px 798)" "$(py 198)" "$(px 1300)" "$(py 300)" 900   # a drag on the ball
    sleep 3
    shot 04_after_drag
    tap "$(px 2316)" "$(py 108)" 4                 # play
    shot 05_running
    "$ADB" shell input keyevent HOME
    sleep 4
    "$ADB" shell am start -n "$ACTIVITY" > /dev/null 2>&1
    sleep 6
    shot 06_resumed
    echo "   done (review $OUT/touch by eye)"
fi

if [ "$KEEP" = 0 ]; then
    "$ADB" shell am force-stop "$PKG" > /dev/null 2>&1 || true
    "$ADB" shell "setprop $PROP ''" > /dev/null 2>&1 || true
    if [ "$STARTED_EMULATOR" = 1 ]; then "$ADB" emu kill > /dev/null 2>&1 || true; fi
fi
exit $STATUS
