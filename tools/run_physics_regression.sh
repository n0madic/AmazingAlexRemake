#!/usr/bin/env bash
# Physics regression: original binary (Unicorn) vs vendored Box2D 2.2.1 (docs/09-physics-regression.md).
# Usage: tools/run_physics_regression.sh [OUT_DIR] [extra uc_trace.py args...]
# Produces OUT_DIR/*.scene, *.uc.traj, *.native.traj, *.pert.traj and OUT_DIR/report.md / noise.md.
# Exit code 1 unless EXPECT_IDENTICAL (default 55) scenarios are bit-identical — registered in CTest as
# `physics_regression` (gate G2).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${1:-build/physics_regression}; shift || true
PY=${PY:-.venv/bin/python}
SO=extracted/apk/lib/armeabi-v7a/libamazingalex.so
# The decrypted *original* tree (not the imported one the Makefile's ASSETS names — make exports that
# variable into ctest's environment); the assets are byte-identical on both platforms, so use whichever
# tree carries GameItems.plist (a `Levels` test alone passes for the imported `levels` on APFS).
ORIG_ASSETS=${ORIG_ASSETS:-extracted/ios_dec}
[ -f "$ORIG_ASSETS/Common/Game/GameItems.plist" ] || ORIG_ASSETS=extracted/android_dec
ITEMS=$ORIG_ASSETS/Common/Game/GameItems.plist
LEVELS=$ORIG_ASSETS/Levels
# the last three are attachment-heavy (ropes on hooks/balloons/buckets, attached pipes)
LEVEL_SET=(00_Classroom/KlassRoom 00_Classroom/ChainReaction 01_Backyard/Sliders 01_Backyard/SlamDunk
           02_Room/DangerRoad 03_Treehouse/Helipad 03_Treehouse/FreeThemAll 02_Room/BallastOverboard
           02_Room/BalloonCatching 03_Treehouse/Cannibals 03_Treehouse/GreatEscape 02_Room/DominoEffect
           02_Room/BumperFun 03_Treehouse/DrPig 01_Backyard/Cannonball 02_Room/StealthOps)

cmake -S tools/trace_native -B build/trace_native -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/trace_native --config Release >/dev/null

rm -rf "$OUT"; mkdir -p "$OUT"
level_args=()
for l in "${LEVEL_SET[@]}"; do level_args+=(--level "$LEVELS/$l.plist"); done
"$PY" tools/uc_trace.py "$SO" "$ITEMS" --out "$OUT" --drop all "${level_args[@]}" "$@"

for scene in "$OUT"/*.scene; do
    base=${scene%.scene}
    build/trace_native/trace_native "$scene" "$base.native.traj"
    "$PY" tools/trace_perturb.py "$scene" "$base.pert.scene" >/dev/null
    build/trace_native/trace_native "$base.pert.scene" "$base.pert.traj"
done
EXPECT=${EXPECT_IDENTICAL:-55}
"$PY" tools/trace_compare.py "$OUT" --a .native.traj --b .pert.traj --md "$OUT/noise.md" >/dev/null
echo "report: $OUT/report.md   noise floor: $OUT/noise.md"
# the gate: fails by exit code when fewer than EXPECT scenarios are bit-identical (docs/09)
"$PY" tools/trace_compare.py "$OUT" --md "$OUT/report.md" --expect-identical "$EXPECT" >/dev/null
