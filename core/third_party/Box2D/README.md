# Box2D 2.2.1 (vendored, frozen, patched to match the shipped game)

Source: `Box2D_v2.2.1.zip` from the Box2D Google Code download archive
(`https://storage.googleapis.com/google-code-archive-downloads/v2/code.google.com/box2d/Box2D_v2.2.1.zip`,
files dated 2011-09-17), directory `Box2D/` plus `License.txt` → `LICENSE.txt` (zlib). Line endings converted
to LF.

Why this version: the original game links a pre-release 2.2 trunk snapshot (`docs/04-physics.md` §1);
2.2.1 is the last release of that line with the same algorithms and, for everything the game uses, the
same definitions (`b2LineJointDef` → `b2WheelJointDef`, `localAxis` → `localAxisA`).

Every deviation of the shipped trunk snapshot from 2.2.1 that `tools/run_physics_regression.sh` could detect
has been back-ported, plus the one the whole-catalogue gate (G6, `tools/sim_run_conformance.py --all-levels`)
found afterwards; the harness reports all 55 scenarios **bit-identical** to the emulated original
(`docs/09-physics-regression.md`) and G6 all 116 levels. Each change is marked `AMAZING ALEX LOCAL CHANGE` in
the source.

Local changes (keep this list complete):

* `CMakeLists.txt` and `Box2DConfig.cmake` replaced by a minimal modern CMake file (the original required
  CMake < 3.5 syntax). Options: `BOX2D_DOUBLE_LIBM` (**default ON**, see below), `BOX2D_PLATFORM_LIBM`
  (diagnostic, OFF), `BOX2D_RELEASE_EARLY_OUT`, `BOX2D_RELEASE_SLEEP` (both OFF = trunk behaviour).
* `Common/b2Math.h`
  * `b2Sqrt`/`b2Atan2`/`b2Sin`/`b2Cos` macros; with `B2_DOUBLE_LIBM` they evaluate in double and round to float,
    which is what the original Android build does (it imports only the double libm entry points). This is on by
    default: it makes the results independent of the platform's `sinf`/`cosf` implementation and two scenarios
    (paper plane at rest, RC truck) only match with it. `sin`/`cos`/`atan2` come from the vendored device libm
    (`../aa_libm`, Bionic's msun — the Box2D target links `aa_libm`); `BOX2D_PLATFORM_LIBM` is a diagnostic
    switch back to the host's double libm. `sqrt` is IEEE-exact and stays on the host.
  * `b2Mul(const b2Transform&, const b2Vec2&)` uses the 2.1.2 / trunk association `p + col1 * x + col2 * y`
    (2.2.1: `(q * v) + p`); every rotated body depends on it.
* `Common/b2Settings.h`: `b2_linearSlop` **0.0025** instead of 0.005 (the value the shipped game was built with;
  `b2_polygonRadius` = 0.005 and the TOI target follow); macro `b2_positionEarlyOutFactor` (default 1.5 = trunk,
  2.2.1 release 3.0); macro `b2_sleepRequiresPositionSolved` (default 0 = trunk: an island sleeps as soon as its
  sleep timer reaches `b2_timeToSleep`).
* `Collision/Shapes/b2PolygonShape.cpp`: `ComputeMass` is the 2.1.2 / trunk algorithm (triangles fanned from the
  local origin, explicit reference-point terms); 2.2.1 fans from the vertex average and differs at rounding level.
* `Dynamics/b2Island.cpp`
  * `Solve`: dynamic-vs-dynamic contacts are moved to the front of the island's contact array before solving
    (the trunk's swap-based partition); the sweep start (`c0`, `a0`) is stored only for non-static bodies;
    sleep rule macro (above).
  * `SolveTOI`: the "leap of faith" resets `c0`/`a0` of every island body (not only the TOI pair); the
    translation/rotation clamps use the trunk's normalise-and-rescale form.
* `Dynamics/Contacts/b2ContactSolver.cpp`
  * position solvers (regular and TOI) scale inverse mass/inertia by the body mass (`mass * invMass`,
    `mass * invI`, the 2.1.2 form — note the inertia term is not `1 / I`); the TOI variant moves every body of
    the TOI island (it ignores the TOI pair), like the trunk.
  * effective masses `kNormal`/`kTangent` associate as `(rn * rn) * invI`; block solver
    `k_maxConditionNumber` = **100** (2.2.1: 1000); when the block solver is rejected the reduced point count is
    also applied to the position constraint (the trunk has a single count per contact); the circles position
    manifold keeps a `(1, 0)` normal for coincident points; early-out factor macro (above).
* `Dynamics/Contacts/b2ContactSolver.h/.cpp`, `Dynamics/b2Island.cpp`: `b2ContactSolverDef::useBodyTransforms` —
  the regular step's `InitializeVelocityConstraints` reads the bodies' stored transforms (`b2Body::m_xf`) like the
  trunk's solver did; 2.2.1 re-derives them from the sweep, which differs by a rounding right after
  `SetTransform` on a body with a non-zero local centre (docs/09 §5, `02_Room/LaunchingRamp`). The TOI step keeps
  the derivation: the trunk's `SolveTOIPositionConstraints` writes every constrained body's `m_xf` in place
  before that set-up [verified], so the derived and the stored transforms agree there.
* `Dynamics/b2ContactManager.cpp`: `AddPair` does not wake the bodies (2.2.1 added that); `Collide` skips a
  contact only when neither body has the awake flag (2.2.1 also requires a non-static body), so contacts of a
  sleeping body with an awake static body keep being updated.
* `Dynamics/Joints/b2RevoluteJoint.cpp`: the 2.1.2 / trunk solvers (3×3 effective mass, "large detachment"
  particle pre-step with `k_allowedStretch = 10 · b2_linearSlop`) on 2.2.1's `b2SolverData`.
* `Dynamics/Joints/b2PrismaticJoint.cpp`: the 2.1.2 / trunk solvers (old K layout, 3×3 block solve) plus the
  trunk's `k22 == 0 → 1` guard; the local axis is **not** normalised (the game passes non-unit axes).
* `Dynamics/Joints/b2DistanceJoint.h/.cpp`: trunk-only def field `resistCompression` (our name; default true).
  False makes the joint one-sided (rope-like: no impulse until the position solver has seen `C >= 0`, and no
  correction pushing the anchors apart). The game always passes true.
* `Dynamics/Joints/b2WheelJoint.cpp` is **unchanged**: the trunk's `b2LineJoint` is bit-identical to it.

Nothing else is modified. Do not "upgrade" this copy: `tools/run_physics_regression.sh` measures it against the
original binary and must stay at 100 % bit-identical.
