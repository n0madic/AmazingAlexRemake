# Physics & simulation

All statements **[verified]** from the decompiled `st::GamePhysicsUtils`, `st::GameScreen::UpdateSimulation`,
`st::*Utils::CreatePhysics` and Box2D symbols unless tagged otherwise.

## 1. Box2D version — hard constraint for the remake

The binary statically links **Box2D from SVN trunk, spring 2011, i.e. between v2.1.2 and v2.2.0**:

* present: `b2EPCollider`, `b2EdgeShape` with `ComputeMass/RayCast(childIndex)`, `b2RopeJoint`, `b2FrictionJoint`,
  `b2WeldJoint`, `b2Sweep::alpha0` (body layout below), `b2Body::SetMassData`;
* still using pre-2.2 names: `b2LoopShape` (renamed `b2ChainShape` in 2.2.0), `b2LineJoint` (→ `b2WheelJoint`; it is
  already the wheel joint in everything but the name — def = anchors, axis, `enableMotor`, `maxMotorTorque`,
  `motorSpeed`, `frequencyHz`, `dampingRatio`, **no translation limits**),
  `b2DebugDraw` (→ `b2Draw`), `b2BodyDef::inertiaScale`, contact filter data as `b2Filter{categoryBits, maskBits, groupIndex}`;
* build path string: `AmazingAlex/External/Box2D/...` (unmodified layout).

* `b2_version` (exported data symbol, read from `.data` of the `.so` at `0x2711e4`) = **{2, 2, 0}** [verified].
* Solver internals are still the pre-release ones: joints take `b2TimeStep const&` (not `b2SolverData`),
  `b2Island::Solve(b2TimeStep, gravity, allowSleep)` without `b2Profile`, `b2Island::SolveTOI(step, bodyA, bodyB)`,
  `b2ContactSolver::SolveTOIPositionConstraints(baumgarte, bodyA, bodyB)`, `b2Island::Report(b2ContactConstraint)` —
  i.e. before the position/velocity-array (`b2Position`/`b2Velocity`) refactor that shipped in the 2.2.0 release.
  The shipped code therefore sits between the `b2LineJoint`→wheel rework (early 2011) and the `b2Rot`/solver-data
  refactor (mid 2011). Only circles and polygons are used as shapes; joints used: revolute, prismatic, distance, line/wheel.
  Trunk carried that number for months before the 2.2.0 release, so it dates the build to the pre-release
  2.2 line rather than a 2.1.x solver.
* `b2Transform` is still `{b2Vec2 position; b2Mat22 R}` (24 bytes: `SetTransform` writes `+0x14/+0x18/+0x1C/+0x20`
  as `c, s, −s, c`) — the `b2Rot` change (June 2011) is **not** in; `b2Sweep` already has `alpha0`.

**Build-time constants (`b2Settings.h`) of the shipped binary** [verified from the inlined literals of
`b2ContactSolver::SolvePositionConstraints`, `b2TimeOfImpact`, `b2Island::Solve`, the joint solvers and
`b2DynamicTree`]: `b2_linearSlop = 0.0025` (**half the stock 0.005**; hence `b2_polygonRadius = 0.005`, TOI target
`max(slop, r − 3·slop)`, TOI tolerance `0.25·slop`), `b2_angularSlop` 2°, `b2_maxLinearCorrection` 0.2,
`b2_maxAngularCorrection` 8°, `b2_baumgarte` 0.2 (passed as the joint `SolvePositionConstraints` argument),
`b2_velocityThreshold` 1.0, `b2_maxTranslation` 2, `b2_maxRotation` π/2, `b2_timeToSleep` 0.5,
`b2_linearSleepTolerance` 0.01, `b2_angularSleepTolerance` 2°/s, `b2_aabbExtension` 0.1, `b2_maxPolygonVertices` 8;
the contact position solver early-outs at `minSeparation ≥ −1.5·slop` (the 2.2.1 release uses −3.0).

The remake vendors Box2D **2.2.1** with these settings and with every behavioural difference of the trunk
snapshot back-ported (`core/third_party/Box2D`, its README lists each change); the trajectory harness of
[09-physics-regression.md](09-physics-regression.md) shows it **bit-identical** to the emulated original on all
55 scenarios (39 item drops, 16 levels with attachments). The trunk deviations from 2.2.1 that were found
[verified by the harness — each one changed results until it was ported]:

* `b2Mul(b2Transform, v)` associates as `p + R·col1·x + R·col2·y` (2.1.2 form); `b2PolygonShape::ComputeMass` is
  the 2.1.2 algorithm; both matter for every rotated body.
* Contact position solvers (regular and TOI) use `mass·invMass` and `mass·invI` as the body factors (2.1.2 form —
  the inertia term is *not* 1/I); the TOI position solver moves **all** bodies of the TOI island; the "leap of
  faith" in `b2Island::SolveTOI` resets `c0/a0` of all island bodies; effective masses associate as
  `(rn·rn)·invI`; block-solver `k_maxConditionNumber = 100` and a rejected block reduces the *position* point
  count too; `b2Island::Solve` partitions the island's contacts so dynamic–dynamic pairs are solved first;
  `c0/a0` are stored only for non-static bodies (a static body's sweep start keeps what the TOI loop left);
  the regular step's velocity-constraint set-up reads the bodies' stored transforms (`b2Body::m_xf`) instead of
  re-deriving them from the sweep — the two differ by a rounding right after `SetTransform` on a body with a
  non-zero mass centre (the dart of `02_Room/LaunchingRamp`, 09 §5); the TOI set-up keeps the derivation, the
  trunk's TOI position solver writes `m_xf` in place before it [verified: `SolveTOIPositionConstraints`].
* `b2ContactManager::AddPair` does not wake bodies; `Collide` only checks the awake flags (an awake *static*
  body keeps its sleeping neighbour's contact updated).
* Joints: `b2RevoluteJoint` and `b2PrismaticJoint` are the 2.1.2 solvers (3×3 effective mass, "large detachment"
  particle pre-step with `k_allowedStretch = 10·slop`; prismatic with the trunk's `k22 == 0 → 1` guard and **no
  axis normalisation** — the glove passes `(1, −0.15)`); `b2LineJoint` is bit-identical to 2.2.1's
  `b2WheelJoint`; `b2DistanceJointDef` carries an extra trailing bool (our name `resistCompression`, the game
  always passes true; false makes the joint one-sided/rope-like).
* Trig/sqrt are the double libm functions rounded to float (the Box2D code paths use only `sin`/`cos`/`sqrt`/
  `atan2`; the binary as a whole imports 21 libm functions, listed in `core/third_party/aa_libm/README.md`); the
  vendored build does the same (`B2_DOUBLE_LIBM`, on by default) — two scenarios only match with it. The double
  `sin`/`cos`/`atan2` (and every other transcendental the game imports) are the device's own code: Bionic's
  FreeBSD-msun sources, vendored as `core/third_party/aa_libm` (its README has the provenance and the known-answer
  self-test to run on each new platform), so the numbers do not depend on the host libm.
Recovered `b2Body` layout: `+0xC xf.position, +0x14 xf.R (b2Mat22), +0x24 sweep (localCenter,c0 +0x2C,c +0x34,
a0 +0x3C,a +0x40,alpha0 +0x44), +0x48 linearVelocity, +0x50 angularVelocity, +0x54 force, +0x5C torque, +0x60 world,
+0x7C mass, +0x94 sleepTime, +0x98 userData (st::PhysicsObject*)`; `b2Fixture +0x20 filter.categoryBits,
+0x24 filter.groupIndex`.

## 2. World

| Parameter | Value | Source |
|-----------|-------|--------|
| Gravity | (0, −9.8) m/s² | `GamePhysicsUtils::CreateWorld` |
| `doSleep` | true | ctor arg |
| Auto clear forces | **off** (`flags &= ~e_clearForces`); `ClearForces()` is called manually after each `Step` | |
| Contact listener | `WorldContactListener` in simulation mode, `WorldContactListenerSetUp` in set-up mode (PreSolve only) | |
| Units | metres; 1 px (1024-wide reference) = 3.41/1024 m; world 3.41 × 2.12459 m | `WorldStateUtils::GetPixelToMetersFactor` |
| Physics modes | `PhysicsMode::Enum`: **0 = SetUp** (editing), **1 = Simulation** | |

In set-up mode every body is created as **`b2_dynamicBody`** (type 2 — verified by running the original code, see
08; Box2D then assigns mass 1 to the density-0 shelves/pipes, which is harmless because the world is never stepped
in set-up) and extra *selection* sensor fixtures (filter `Selection`) are added so the player can pick items with a
finger. Only `WorldBound` and the `SelectionArea` helper are static in set-up. Simulation-mode body types per item are
listed in 03 §2 (intro) and in 08; Scissors and the Spring base are the only **kinematic** bodies. The world is destroyed and
re-created on every transition (`GamePhysicsUtils::DestroyWorld/CreateWorld`, `CreateDynamicPhysics`,
`CreateAttachments`).

## 3. Time step and interpolation — `GameScreen::UpdateSimulation`

```
accumulator += frameDt * gameState.timeScale(=1.0) * 0.8      // GameScreenController::doFrame
acc0 = accumulator                                            // kept in s18 for the trailing updates
while (accumulator >= 1/120):
    prevState = worldState                                     // memcpy of the whole WorldState
    Balloon.Update(dt); BoxingGlove.Update; RadioController.Update; Helicopter.Update; Slingshot.Update;
    Magnet.Update; PaperPlane.Update; Spring.Update; Bumper.Update; TrapdoorLever.Update; Seesaw.Update
    world.Step(dt = 1/120, velocityIterations = 10, positionIterations = 10)
    world.ClearForces()
    GamePhysicsUtils::GetStateFromPhysics()                    // copy body transforms into WorldState
    accumulator -= 1/120
    Scissors.Update(dt); GoalStar.Update(dt)
    actionProcessor.Process()                                  // GameScreenController::processActions (vtable +8)
PiggyBank.Update(acc0); Dart.UpdateSetUpMode(acc0)
if (!goalReached) { GoalStateUtils::Update(acc0); if (IsGoalComplete) queue Action(11 = GoalComplete) }
renderState = worldState; LerpState(renderState, prevState, worldState, accumulator * 120)   // dynamic objects only
return accumulator
```

* Fixed step **1/120 s** (`0x3C088889`), `Step(dt, 10, 10)`. [verified: decompile + disassembly, M4]
* **The trailing updates receive the accumulator as it was *before* the substep loop** (`acc0`, register `s18`),
  not the frame dt: PiggyBank's piece fade, Dart's set-up-mode wobble and the goal timers (type 7's 0.3 s sensor
  accumulation) advance by 1/120 + remainder on a normal frame, i.e. by a value that oscillates between
  `frameDt·0.8` and `1/120 + frameDt·0.8`. The lerp and the return value use the remainder (`s16`) [verified:
  disassembly of `UpdateSimulation`; reproduced by the remake — the G4 gate compares the goal timers bit-exactly].
* The simulation runs at **0.8 × real time** [verified]: `doFrame` does `accumulator += frameDt * 0.8` whenever the
  `paused` flag (`GameScreenController+0xC996C`) is clear. That flag is the ordinary play switch — set to 1 in the
  constructor/`playNewLevel`/`settle`/`setCompletedState`, written by `setPaused(bool)`, and cleared by
  `toggleSimulation` when the player presses Play (set again on Stop) — so 0.8 applies to every normal run, not to a
  special mode. The game is deliberately slightly slow-motion: `frameDt` is the real wall-clock delta
  (`nativeUpdate`: `currentTimeMicros` difference, clamped to [0, 0.1] s → `GameApp::update` → `SceneManager::Update`
  → `GameView::Update` stores it at `+0x3B34` → `doFrame`), so the world advances 0.8 s per real second.
  `gameState.timeScale` (`GameState+4`) is written only by the `GameState` constructor (1.0) [verified]; the
  slow-motion drag of `GameSimulationTouchHandler` (touch state 2) that would scale it is dead code — nothing
  enters that state [verified, M4].
* `GameState+0x22774` (= `WorldState+0x2015C`, the `PhysicsObjectCollection`'s first byte) is set to 1 while
  `UpdateSimulation` runs and to 0 after; none of the 58 decompiled functions that take the collection compares
  that byte (`PhysicsObjectsUtils::Remove` and the item updates read `+4` count / `+8` objects only) — the flag
  has no effect on the simulation and is not ported [verified: grep of the decompile, M4].
* `GetStateFromPhysics` copies body 0's position / angle into every object that has a body and every body's
  transform into the per-body arrays (`+0x28`, `+0x34`); `LerpState` interpolates position and angle of the objects
  with the dynamic flag (bit1) only, angle without wrapping, with the unfused `a + (b − a)·t` of the VFP `vmla`
  [verified: disassembly].
* Item update order matters for determinism (forces applied before `Step`); Scissors/stars are evaluated after.
* The rest of `doFrame`'s simulation branch, in order [verified]: `GameSimulationTouchHandler::Process` →
  `processActions` (a state change recurses into `doFrame` for the same dt) → the accumulator / `UpdateSimulation`
  above (a paused run copies the world state into the render copy instead) → `SoundRenderer::Render` (looping
  sounds; it writes the clip handles into the item blocks — audio only) → the three-stars rule (05 §3) → the play
  time `GameState+0` += dt, or the completion countdown → `UpdateSparkles` → the common tail: effects,
  transitions, `renderFrame`, **`StopRunawayObjects`** (state 4 only: a moving body outside |x| > 6.82 m or
  y < −4.249 m has its velocities zeroed), `RemoveInvalidItems`, the prev copy (`+0x29C0` ← world state),
  `saveUndoState` when edited, and the 5 s auto-stop: when `HasMovingObjects` (a body with |v| > 0.001, the rope
  root body skipped) is false for more than 5 s of frame time, `toggleSimulation` returns to set-up.

## 4. Object model

`st::PhysicsObject` (216 bytes = `0xD8`, copied from `PhysicsObjectTemplates[type]` in `PhysicsObjectsUtils::Add`):

| Offset | Field |
|--------|-------|
| +0 | `ItemType` |
| +4 | index in `PhysicsObjectCollection` (`PhysicsObjectsUtils::Add` writes it; collection = `{…, +4 count, +8 objects[]}`) |
| +8 | handle (copied from the `GameItem` in `WorldStateUtils::AddItemWithHandle`) |
| +0xC | **flags byte** (from the template, then modified at runtime): bit0 always set (cleared by `WorldStateUtils::InvalidateItem` until `RemoveInvalidItems` drops the object); **bit1 = dynamic** (falls, interpolated by `LerpState`); **bit2 = fixed** (set by `LevelLayoutUtils::Apply` from level flag bit 0); **bit3 = flippable** (has a flip button: scissors, glove, doll, skateboard, magnet, plane, dart, slingshot, truck, helicopter — `GameTouchHandler::Process` tests it before the flip rectangle) [verified]; bit4 = breakable; **bit5 = magnetic** (Magnet attracts it); **bit6 = stabbable** (Dart sticks into it); **bit7 = returns to the toolbox when removed** (every template but RCController and TrapdoorLever, whose truck / trapdoor returns instead — `FUN_000c3478`) [verified] |
| +0xD | **state byte**: bit0 = activated (balloon popped in `BalloonUtils::Update`, piggy broken in `GameItemUtils::Break`; read by goal 5); bit1 = ghost tint (the held item while colliding, 05 §5); bit2 = is a ghost copy |
| +0x10, +0x14 | position (m) |
| +0x18 | angle (rad) |
| +0x1C, +0x20 | scale x, y (x = −1 when flipped) |
| +0x24 | half-size / radius — the "template size" derived from the sprite frame |
| +0x28 | attachment count, +0x2C.. attachment points (48 bytes each, see §7) |
| +0x90..+0x92 | per-body flag bytes: bit0 selectable (the pick query; cleared for an attached rope end), bit1 shows gizmos (cleared by the templates for FishBowl body 0, Rope 0–2, GoalStar 0, Slingshot 1, ZipLine 0/1) [verified] |
| +0x94 | body count, +0x98.. `b2Body*[…]` (up to ~13 bodies; index 0 = main body; `b2Body::userData` = the `PhysicsObject*`) |

Template sizes are computed once from the atlas (`PhysicsObjectsUtils::InitializePhysicsObjectTemplates`):

```
halfSize = trunc(|frame.x1 − frame.x0| − 2) * 0.5 * (3.41 / 1024) * tweak
```
`tweak` per type: Shelf 0.98, TennisBall 0.98, SoccerBall 1.05, Bucket 0.9, CardboardBox 0.9, FishBowl 0.95,
PiggyBank 0.9, Pipe/Pipe90 0.97, GoalStar 0.91, others 1.0; fixed radii: EightBall 0.05661, Pinball 0.04829,
BouncyBall 0.04, BoxingGlove 0.2, Billboard 0.2, Doll 0.15, Helicopter 0.225, Rope link 0.03, ZipLine 1.0,
Seesaw/Trapdoor = full width (`2*half`), RCTruck = half + 0.03996.

## 5. Collision filters — `st::CollisionFiltersUtils::Create`

| Name | category | mask | group | Used by |
|------|---------:|-----:|------:|---------|
| `Static` | 0x001 | 0xFFFF | 0 | shelves, pipes, hooks, magnets, walls, floor, pulleys, trapdoor frames |
| `Dynamic` | 0x002 | 0x0103 | 0 | most dynamic bodies (collide with Static, Dynamic, ReturnAreaBound) |
| `Rope` | 0x004 | 0x0101 | −1 | rope links (never collide with each other or dynamic items; only static + return bound) |
| `Debris` | 0x008 | 0x0001 | −6 | broken piggy-bank pieces (static only) |
| `Selection` | 0x010 | 0x0000 | 0 | set-up-mode pick sensors (no collision at all; found by AABB/point queries) |
| `PipeFilling` | 0x040 | 0x0141 | 0 | inner fill of pipes (set-up mode; keeps other items from being placed inside) |
| `Topping` | 0x080 | 0x0080 | 0 | solid (non-sensor) boxes across the *opening* of Bucket, LaundryBasket, HangingLamp, the pipe mouths and (set-up only) the boxing-glove head; category = mask = 0x80, and the Topping mask accepts no other category, so they collide **only with each other** — a container cannot be pushed mouth-first into another container/pipe, in either mode. Their density (50–70) enters the automatic mass of LaundryBasket and HangingLamp (no `SetMassData`); Bucket overrides its mass to 0.5 kg anyway (08) [verified: `CollisionFiltersUtils::Create`, 08] |
| `ReturnAreaBound` | 0x100 | 0x0100 | 0 | toolbox return area |
| `NonCollidable` | 0 | 0 | 0 | purely visual fixtures |

Category bit `0x10` is OR-ed into the category of most dynamic fixtures (`Dynamic|0x10`) and `0x100` into the
mask of boxes so they collide with the return-area bound. Group indices with special meaning:
**−2, −8 = sharp** (pops balloons in `BeginContact`; −8 = dart tip which also *stabs* stabbable items),
**−4 = boxing-glove trigger button** (contact → punch), **−5 = glove stand plate/head box/wrist** (`0xFFFB`, mutually non-colliding), **−6 = debris**, **−7 = goal sensor** (container
interior used by goal type 7).

## 6. World bounds — `st::WorldBoundUtils::CreatePhysics` (item type 31, one per level)

Static bodies with `SetAsBox(hx, hy)` at (cx, cy), filter `Static`, friction 0.2:

| Body | centre | half-size | Notes |
|------|--------|-----------|-------|
| floor | (1.705, −0.5) | (2.705, 0.5) | top edge at y = 0, spans x ∈ [−1, 4.41] |
| ceiling | (1.705, 30.5) | (1.705, 0.5) | 30 m up |
| right wall | (3.91, 15) | (0.5, 15) | inner face x = 3.41 |
| left wall | (−0.5, 15) | (0.5, 15) | inner face x = 0 |

`backgroundIndex == 3` (Treehouse, `WorldBoundUtils::GetTypeForBackgroundIndex`) replaces the floor by two
pieces: centres (−0.36066, −0.5) and (3.77074, −0.5), half-size (1.639375, 0.5) → a **hole in the floor for
x ∈ [1.27875, 2.13136]** (objects falling through it are "delivered" — used by the hatch levels).

Off-screen handling (`GamePhysicsUtils::StopRunawayObjects`, per frame) [verified]: for every non-static body that
is moving (|vx| > 0.001 or |vy| > 0.001), if **|x| > 6.82** (= 2 × 3.41, either side) or **y < −4.24918**
(= 2 × 2.12459) its linear and angular velocity are zeroed — the body is *not* removed; because the reset happens
once per frame it keeps creeping under gravity by one frame's worth of velocity, but never comes back.
`HasMovingObjects`: any body with |v| > 0.001 m/s or |ω| > 0.001 rad/s (rope root bodies skipped) — used to
decide when the contraption has come to rest.

## 7. Attachments (snap points) — `st::AttachmentUtils`

Attachment point record (48 bytes at `PhysicsObject+0x2C + i*0x30`):

| Offset | Field |
|--------|-------|
| +0, +4 | local position (multiplied by object scale → flips with the item) |
| +8, +0xC | direction vector (local) — used for alignment |
| +0x10 | **kind**: 1 = hook (holds things), 2 = hangable (balloon knot, bucket handle, doll, lamp, truck hitch, helicopter hook, zip-line trolley), 4 = rope end, 8 = pipe end |
| +0x14 | **mask** of kinds it can connect to: rope end → 3 (hook or hangable); hangable/hook → 4 (rope); pipe → 8 (pipe) |
| +0x18 | index of the body owning the point |
| +0x1C | **position-only snap** flag [verified in `CalculateSnap`]: non-zero → the point snaps by distance alone; 0 → additionally requires the two direction vectors to be within **45°** (`acosf(d1·d2) < 0.785398`) and the snap result carries the rotation delta so the item is turned to align. Template values: 1 for every hook/hangable/rope-end point, **0 for pipe ends** (pipes must be aligned, ropes/hooks need not) |
| +0x20 | state 0/1/2 (free / snapped / attached), +0x24 other object index, +0x28 other point index, +0x2C `b2Joint*` |

Per-type attachment points (template values, metres, before scaling):

| Type | Points |
|------|--------|
| Balloon | (0, −0.25) dir (0,−1), kind 2 |
| Bucket | (0, 0.26) dir (0,1), kind 2 |
| Hook | (0, −0.03) dir (0,−1), kind 1 |
| Rope | two ends (0,0) dir (1,0) kind 4 mask 3 (positions come from the rope bodies) |
| Pipe | (±(half+0.0051), 0) dir (±1,0), kind 8 |
| Pipe90 | (0.44·h, 0.967·h) dir (0,1) and (−0.985·h, −0.42·h) dir (−1,0), kind 8 |
| Doll | (−0.06, −0.01) dir (1,0), kind 2 |
| HangingLamp | (0, 0.2) dir (1,0), kind 2 |
| RCTruck | (−hitchX, −0.02), kind 2 (hitch) |
| Helicopter | (−0.09, −0.084), kind 2 (hook) |
| ZipLine | (0, −0.17), kind 2 (trolley hook), body 2 |

Snapping (`CalculateSnap(result, obj, position, queryCenter, radius)`, called while dragging in set-up mode)
[verified: decompile + disassembly + the G5a oracle]: one `b2World::QueryAABB` of half-size `radius` around
`queryCenter` (`UpdatePos` passes **1.1 · halfSize**; `RopeUtils::UpdatePos` 1.1 · the rope's end vector length)
collects up to 32 candidate objects — not the dragged one, with attachment points, and **not fully attached**
(every record in state 2). Then, for each not-attached point of the dragged object in order, where that point would
be with the object at `position` is tested against the candidates' points (object order, point order): the kind
must be in the mask, the candidate point must not be attached, it must be **within 0.08 m** (`FUN_000a7080`, a
literal passed in `s0`, not the query radius), and if it is snapped it must be snapped to exactly this point; the
first hit wins, unless the candidate is a ghost copy. A position-only point moves the item so the points coincide;
an aligned kind (pipes) additionally needs the direction vectors within **45°** (`acosf(d1·d2) < 0.785398`, the
game's Pi × 0.25) and the result carries the rotation delta so the item is turned. `Snap` sets state 1 on both
records (`Unsnap` first when either was snapped elsewhere); `UnsnapAllNotAttached` clears the snapped records of
an object that found nothing. Releasing the item calls `AttachToNearbyItems` (AABB 0.07 m around each not-attached
position-only point, point radius 0.03 m, one attachment per call) → `Attach` (state 2) → `CreateJoint`: a
**`b2RevoluteJoint`** at the point with `enableMotor = true, motorSpeed = 0, maxMotorTorque = 0.01` (tiny rotational
damping), no limits, `collideConnected = false`; then `GameItemUtils::AttachmentChanged` for both items — a rope
attached at both ends gets an end-to-end distance joint (`FUN_000e2b48`, length 1.04 · |end|, `collideConnected`,
anchored at record 0 of both neighbours) and its selectable bytes cleared [verified: M2's oracle skipped this
hook; G3 was re-run with it]. Pipes connect end-to-end the same way (rigid enough at 1/120 s). `Detach` destroys
the joint and frees both records; `RemoveAllAttachments` (detach + unsnap everything) runs before a flip and when
an item is destroyed (balloon popped, piggy broken, returned to the toolbox).

## 8. Ropes — `st::RopeUtils`, `(anon)::CreateRopeBodies / CreateRopeJoints`

* Link count `N = clamp(int(length / 0.0671 + 1), 2, 15)` where `length` = distance between the two rope ends
  (`center` ↔ `ropeEndPos`).
* Each link is a dynamic body with one circle fixture, radius **0.03355**, filter `Rope`
  (`CreateRopeBodies`); every link gets `SetMassData(mass = 0.01, centre = 0, I = 0.1)`.
* Consecutive links are connected by **`b2DistanceJoint`s** with `length = ropeLength / (N−1)`,
  `frequencyHz = 60`, `dampingRatio = 0.95` (`CreateRopeJoints`, body pairs from
  `RopeRenderUtils::CalculateBodyIndices`).
* The two extreme bodies (`+0x9C`, `+0xA0`) carry the attachment points (kind 4) and, in set-up mode,
  selection sensors (circle r = 0.12); the rope root body (`+0x98`) gets a 0.01×0.01 selection box.
* `RopeUtils::Cut(rope, obj, i, j)` (scissors, via `ScissorsCutCallback` AABB query; the link pair `(i, j)` is
  chosen as described in 03 §6): destroys the distance joint between links `i` and `j`, nudges the links to wake
  them and sets the rope's *activated* bit, so the rope separates into two hanging halves [verified, 03 §6].
* `UpdateLinkPositionsFromExtremes` lays the links on the straight segment between the ends in set-up mode;
  `SetEndPosition` moves the free end while dragging; `GetConstrainedPos` limits stretching;
  `UpdatePosFromAttachedObjects` follows attached items.
* Rendering: `RopeRenderUtils` builds a triangle strip along the links (`RopeSegment.png`, `RopeKnot.png`).

## 9. Ball/box helpers used by simple items

`CreateBallPhysics(obj, world, mass, inertia, friction, restitution, bullet)`: dynamic body, `angularDamping 0.2`,
circle fixture radius `r*0.9` (density 1, filter `Dynamic|0x10`), then `SetMassData(mass, centre 0, I)`;
in set-up mode a selection circle of `GameParams::MinSelectionRadius` (= **0.12 m**) is added if the ball is smaller.

`CreateBoxPhysics(obj, world, shrink, density)`: dynamic body, `SetAsBox(hw*shrink, hh*shrink)`, density as given,
friction 0.6, restitution 0, filter `Dynamic|0x10`, mask |0x100.

Item-specific bodies are listed in 03-game-items.md.
