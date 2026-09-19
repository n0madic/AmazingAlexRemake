# Physics regression harness — original binary vs vendored Box2D 2.2.1

Goal: decide, with numbers instead of guesses, how far the remake's physics (vendored Box2D 2.2.1,
`core/third_party/Box2D`) is from the trunk snapshot linked into the shipped game, and keep that distance from
growing. The original code is *executed* (Unicorn, `tools/uc_harness.py`), so the reference is the real thing.

## 1. Pipeline

```
tools/uc_trace.py          original .so under Unicorn: builds a scene with the game's own CreatePhysics
                           (simulation mode) in an emulated b2World, records every Box2D construction call
                           → <name>.scene (engine-agnostic, exact float32 bits), steps Step(1/120,10,10) +
                           ClearForces() (auto-clear off, exactly like GamePhysicsUtils/GameScreen) and writes
                           per-step body states → <name>.uc.traj
tools/trace_native/        C++ replayer: same scene on the vendored 2.2.1 → <name>.native.traj
tools/trace_perturb.py     scene with one body nudged by 1 ulp → chaos-floor calibration (<name>.pert.traj)
tools/trace_compare.py     pairwise diff → first step above 1e-6/1e-4/1e-3/1e-2 m and the body/item where it
                           happens, max/final Δ, outcome
tools/run_physics_regression.sh   all of the above: build, 39 item drops + 16 levels, report.md + noise.md
```

Scene lines replay the *same* construction calls (`SetAsBox` with the recorded arguments rather than raw
vertices, `SetMassData`, `SetTransform`, joint defs field by field; the trunk's `b2LineJoint` → `b2WheelJoint`),
in creation order, so body/proxy/contact ordering is identical on both sides; `# item <type> <name> bodies a..b`
comments map bodies back to items. Compiler flags:
`-ffp-contract=off -fno-fast-math` (the original ran on VFPv3 with unfused multiply-add).

Scenarios: every item type dropped from (1.7, 1.0) onto the world-bound floor (`--drop all`, 600 steps = 5 s) and
16 shipped levels with all pre-placed items at their positions/flips **and their attachments** (`--level`):
KlassRoom, ChainReaction, Sliders, SlamDunk, DangerRoad, Helipad, FreeThemAll, BallastOverboard, BalloonCatching,
Cannibals, GreatEscape, DominoEffect, BumperFun, DrPig, Cannonball, StealthOps. Run time ≈ 40 s in total.

Level attachments are re-created the way `GamePhysicsUtils::CreateAttachments` does on load
(`AttachmentUtils::CreateJoint` for every record in state 2, then `RopeUtils::UpdatePosFromAttachedObjects`,
which moves the rope onto the attached objects and rebuilds its links when the count changes — hence the
`destroybody`/`destroyjoint` scene lines). Fixtures the game edits in place after creation (the boxing-glove arm)
are captured as `polygonfix` lines.

Stage-2 corrections to the harness (2026-09-13, `uc_trace.py`/`uc_dump_physics.py`): the level loader wrote
`ropeEndPos − center` into the rope/zip-line end vector although the file stores the offset itself (02 §3.4),
the slingshot pouch was written to the rope offset `+0x8` instead of `+0xC/+0x10`, and only state-2 attachment
records were copied although `LevelLayoutUtils::Apply` copies all of them; the item block handed to
`CreatePhysics` was zero-filled although the game starts from the `st::ItemInfos` default block (03 §2 — the
scissors open by 15°). The 55 scenarios were re-traced with
the fix; the result table below is unchanged (the comparison is harness vs native on the same scene). Drop
scenes and the 16 levels of the level set are therefore the corrected geometry.

## 2. Result (2026-09-13)

**55 of 55 scenarios bit-identical** for 600 steps (`build/physics_regression/report.md`): every body position,
angle, velocity and awake flag of the vendored Box2D 2.2.1 equals the emulated original to the last bit. The
1-ulp perturbation runs (`noise.md`) show how chaotic some scenes are (ChainReaction changes outcome from one
ulp; the Doll drifts 7e-5), which is why anything short of bit identity would be hard to interpret.

Getting there took two rounds. Round 1 (harness build-up) found that `memmove` was not stubbed in
`uc_harness.py` (broadphase pair sort was a no-op in the emulator — fixed; the 08 dump regenerated afterwards is
byte-identical, so no documented number depended on it) and that the shipped build uses `b2_linearSlop = 0.0025`.
Round 2 (this document's tables) bisected the remaining differences scenario by scenario, each time instrumenting
both sides at the first diverging step. Everything found is now in `core/third_party/Box2D` (its README is the
authoritative list; 04 §1 summarises). The ones that mattered most, in the order they were found:

| # | trunk behaviour ported | first seen in | effect before the port |
|---|---|---|---|
| 1 | 2.1.2 revolute solvers (3×3 mass, detachment pre-step) | Doll drop | 1.08 m |
| 2 | `b2Mul(xf, v)` association, 2.1.2 polygon `ComputeMass` | every rotated polygon (13 drops) | ≈ 1e-7 |
| 3 | contact position solver: `mass·invMass`, `mass·invI` factors; TOI solver moves all island bodies | basket, lamp, dart … | ≈ 1e-8 |
| 4 | `AddPair` does not wake bodies | KlassRoom awake mismatch | wrong sleep state |
| 5 | `(rn·rn)·invI` effective-mass association | PaperPlane, RCController … | ≈ 1e-7 |
| 6 | 2.1.2 prismatic solvers, `k22` guard, no axis normalisation | glove, RC controller, DangerRoad | 1.6e-3 |
| 7 | dynamic–dynamic contacts solved first (island partition) | Skateboard, Doll, ChainReaction | 5 cm / 2.6 cm / different outcome |
| 8 | block-solver condition number 100 | SlamDunk bucket impact (1 vs 2 manifold points) | 0.10 m |
| 9 | reduced block count applies to the position solver | Bucket drop | 1.6e-4 |
| 10 | `Collide` awake check without the static condition | SlamDunk (ball wakes bucket on floor) | 2.4e-7 |
| 11 | static bodies keep their sweep start (`c0`) | BallastOverboard | floor moved by 1 ulp |
| 12 | double libm (`B2_DOUBLE_LIBM` default on) | PaperPlane at rest, RCTruck | ≈ 1e-8 |
| 13 | glove arm fixture edited in place after creation (harness `polygonfix`) | Cannonball | missed TOI, 1 m |

Negative results worth keeping: the trunk's `b2LineJoint` *is* 2.2.1's `b2WheelJoint` bit for bit (a
"decompile-guided" port of it made things worse and was reverted); the sleep-rule macro and the circle-shape
inertia association made no measurable difference. Lesson from the round: decompiler output does **not**
preserve the association of same-precedence float chains (it printed `((d·π)·r)·r` as `r·d·π·r`), so every
expression-shape hypothesis was tested numerically against the emulator (float32 re-implementation in Python fed
with hooked inputs) rather than trusted from the listing.

## 3. Interpretation

The remake's physics core uses the vendored library as is: contacts, TOI, sleeping, all four joint types the game
creates (revolute incl. the motorised attachment joints, prismatic, distance ropes, wheel) and the level set-up
path (attachments, rope rebuild) reproduce the original exactly. This 55-scenario gate isolates Box2D itself;
game logic on top of Box2D is covered separately by G3 construction conformance, G5a set-up manipulation, G4
scripted simulation runs and G6 whole-catalogue runs (10 §8). That includes item `Update` functions, the original
`WorldContactListener` behaviour and toolbox manipulation, none of which should be inferred from G2 alone.

## 4. Keeping it green

* Run `tools/run_physics_regression.sh` after any change under `core/third_party/Box2D` or to the physics
  interface of the core; the acceptance line is `55 scenarios … 55 bit-identical`.
* To investigate a regression: `tools/trace_compare.py` names the first diverging body and item; then hook the
  emulator (`Emu.hook_function`, see `tools/uc_trace.py` for the idiom) and add a temporary `fprintf` on the
  native side at that step — the two traces are directly comparable because the scene file replays the same
  construction calls in the same order.
* Adding a level: append it to `LEVEL_SET` in the script; ropes that the game rebuilds on load and in-place
  fixture edits are handled automatically.
* Box2D on another platform/compiler (no Unicorn needed): copy `build/physics_regression/*.scene` and `*.uc.traj`
  from a macOS run (21 MB for all 55; a handful of levels is enough for a smoke test), build `tools/trace_native`
  there, replay every scene and run `trace_compare.py` — the `.uc.traj` files are the known answers. Run
  `aa_libm_selftest` first so a libm difference is not mistaken for a solver one.

## 5. Limitations

* Passive physics only (no item `Update` logic, no contact listener, no toolbox items) — see §3. The M4 gates
  cover the rest: **G4** `tools/sim_run_conformance.py` runs the real `st::GameState` under Unicorn
  (`tools/uc_sim_oracle.py`: the play path `LevelLayoutUtils::Apply` → `CreateWorld(1)` with the original
  `WorldContactListener`, every `…Utils::Update` called from a Python transcription of `UpdateSimulation`) against
  `tests/sim_run_dump` and compares the scene, every substep's trajectory and a per-frame state trace (accumulator,
  play time, goal state, the `Random` seed, object flags / states, the lerped render copy, the animation fields of
  the item blocks, removals, every queued action with its payload) bit-exactly — 39 scripts (`tests/sim_scripts`,
  300 frames each, `playtime` 720 frames through the completion) identical; **G6** the same for every level of the
  catalogue for 5 s of play: 115 of 116 identical, one known divergence (below).
* **`02_Room/LaunchingRamp` — closed (2026-09-14, M5 follow-up).** With the pre-placed layout the dart (body 17)
  rests on a shelf from the first step, and its velocity after step 1 differed by ~1e-8. `tools/uc_manifold_probe.py`
  (hooks on `b2CollidePolygons`, `b2ContactSolver::InitializeVelocityConstraints` / `WarmStart` /
  `SolveVelocityConstraints` / `StoreImpulses`, the trunk's 0xb4-byte `b2ContactConstraint` layout) against
  temporary `fprintf`s in the vendored solver showed identical manifolds and integrated velocities but a different
  `rB` (`−0.167968363` vs `−0.167968303`) and `normalMass` after the constraint set-up. Root cause: 2.2.1's
  `InitializeVelocityConstraints` re-derives each body's transform from the sweep (`q.Set(a)`,
  `p = c − q·localCenter`), the shipped trunk build reads the stored `b2Body::m_xf`; the two differ by a rounding
  right after `SetTransform` on a body whose local centre is not zero (the dart's mass centre is 0.0296 m off its
  origin) — i.e. only in the first step of a level whose dart is already in contact, which is why a raised or
  flat dart matched. Fixed by `b2ContactSolverDef::useBodyTransforms` (the regular step uses the stored
  transforms, the TOI step keeps the derivation — the trunk's `SolveTOIPositionConstraints` writes `m_xf` in place
  before that set-up [verified]): the 55 scenarios stay
  bit-identical, G6 is **116 of 116**, `tests/sim_known_divergences.txt` is empty.
* The single-precision libm names the binary imports (`logf`, `atan2f`, `asinf`, `powf`, like `sinf`/`cosf`) are
  local wrappers over the double libm — `(float)log((double)x)` etc. [verified: disassembly of the call sites and
  of the wrappers, M4]; the remake's `math_utils.h` (`logF`, `powF`, `atan2F`, `asinF`) does the same through
  `aa_libm`, so no msun float sources are needed and the G4 identity of Balloon / Magnet / Bumper / Slingshot /
  PaperPlane forces rests on the same double libm identity as `sin`/`cos`.
* Level items get only the state the physics needs (book colour, rope/slingshot/zip-line end vectors, Treehouse
  floor hole, attachment records); everything else in `GameItem` state is zero.
* `GameItemUtils::AttachmentChanged` is skipped when attachments are re-created (GameItem bookkeeping only).
* Branches the 55 scenarios never exercise: the distance-joint one-sided mode (`resistCompression = false`; the
  game always passes true), revolute/prismatic `equalLimits`, the TOI translation/rotation clamps (unreachable at
  game speeds), the circle-shape inertia association (left as in 2.2.1 — no scenario discriminates).
* libm: both sides use the device's own transcendentals (Bionic/msun, vendored as `core/third_party/aa_libm`;
  the emulator's libm stubs call the same code through ctypes). Measured on the 55 scenarios: 3.3 M `sin`/`cos`
  calls, 38 500 of them (1.2 %) differ from Apple's libm in the double result, **none** after rounding to float —
  so the traces, the 08 dump and a `BOX2D_PLATFORM_LIBM=ON` build are all still bit-identical. Bit identity with
  the device libm is therefore *consistent with* these scenarios, not proven by them; the proof that a build
  computes the same bits is `aa_libm_selftest` (3405 known answers recorded on macOS arm64), which every new
  platform/compiler must pass.
* Compiler flags remain a per-target obligation: `-ffp-contract=off -fno-fast-math` (`PUBLIC` on the Box2D
  target), SSE2 rather than x87 on 32-bit x86, `/fp:precise` on MSVC (untested; the self-tests are the arbiter,
  not the flag). `aa_libm_selftest` catches a fused **aa_libm** build (9 failures with `-ffp-contract=fast` on
  arm64) but says nothing about Box2D's own float arithmetic, which is checked only by the harness — and the
  harness (Unicorn) runs only on macOS. For any other platform use the Box2D known-answer recipe in §4.
* The harness proves the two engines agree under the scene-construction order the harness uses (which follows
  `CreatePhysics`/`CreateAttachments`); that this order equals the game's complete load sequence is a 02/05 matter.
