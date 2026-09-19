# Game items (`st::ItemType`)

Sources: `st::PhysicsObjectUtils::CreatePhysics` (type switch), `st::*Utils::CreatePhysics/Update/HandleCollision`,
`st::PhysicsObjectsUtils::InitializePhysicsObjectTemplates`, the renderer switch (`FUN_000bd4f0` in the Android
build) and level data. Everything is **[verified]** unless tagged. Notation: `r` = template half-size/radius of
the item (04-physics.md §4), `s` = flip sign (`+1`, or `−1` when `flags & 2`), `dyn` = `b2_dynamicBody`,
`static*` = static in simulation / kinematic in set-up mode, `sel` = selection sensor added only in set-up mode.
Fixture parameters are written as `(friction, restitution, density)`; `Dynamic+` = filter `Dynamic` with category
bit 0x10 added. Masses set through `SetMassData` are noted as `mass/I`.

## 1. Type table

| id | Name (class) | Main sprite(s) (`GameItems.plist` index) | Body | Flags (template) | Behaviour summary |
|---:|--------------|-------------------------------------------|------|------------------|-------------------|
| 1 | Shelf | 112 `ShelfTop.png` (+110 `Shelf.png`, 111 `ShelfBottom.png` decor) | static* box | stabbable | passive platform |
| 2 | TennisBall | 138 `TennisBall.png` | dyn circle | dynamic | bouncy ball |
| 3 | BowlingBall | 12 `BowlingBall.png` | dyn circle | dynamic | heavy ball |
| 4 | SoccerBall | 121 `SoccerBall.png` | dyn circle | dynamic | medium bouncy ball |
| 5 | Balloon | 1 `Balloon.png`, pop anim 2–5 `BalloonPop1..4.png` | dyn circle+tri | dynamic | rises (buoyancy), pops on sharp contact, can carry a rope |
| 6 | Scissors | 107 `ScissorsTop.png`, 106 `ScissorsBottom.png`, 101–105 `Scissors01..05` (cut anim) | static* two polygons | compound | when hit hard, closes and cuts ropes in the blade area |
| 7 | Bucket | 26 `Bucket.png`, 27 `BucketHandle.png` | dyn polygons | dynamic | container (goal 7), hangs on ropes via the handle |
| 8 | Hook | 57 `Hook.png` | static* no fixture | — | anchor point for rope ends |
| 9 | Rope | 98 `RopeSegment.png`, 97 `RopeKnot.png`, 96 `RopeDollFront.png` | chain of dyn circles | dynamic | two ends attach to hooks/hangables; cuttable |
| 10 | CardboardBoxMedium | 30 `CardboardBoxMedium.png` | dyn box | dynamic, stabbable | crate |
| 11 | CardboardBoxSmall | 31 `CardboardBoxSmall.png` | dyn box | dynamic, stabbable | crate |
| 12 | FishBowl | 42 `FishBowl.png`, 41 `Fish.png`, 43 `FishBowlHighlight.png` | dyn box | dynamic | heavy box |
| 13 | PiggyBank | 74 `PiggyBank.png`, 75–77 `PiggyBankPiece1..3.png`, 32 `Coins.png` | dyn 2 boxes | dynamic, breakable | breaks on hard impact (goal 5) |
| 14 | BoxingGlove | 13 `BoxingGlove.png` + 14–25 (attachment, button, support, hinges, plates, plunger, spring, stand) | static* trigger + dyn fist | compound | punches when its trigger plate is hit |
| 15 | Book | 6 `Book.png` / 7 Blue / 8 Green / 9 Red / 10 Yellow (variant = `itemData`) | dyn box | dynamic, stabbable | domino |
| 16 | EightBall | 0 `8ball.png` | dyn circle | dynamic | billiard ball |
| 17 | Pipe | 79 `Pipe.png`, 83 `PipeBack.png`, 84/85 `PipeBracketLeft/Right.png` | static* 2 walls | — | straight tube; snaps to other pipes end-to-end |
| 18 | Pipe90 | 80 `Pipe90.png`, 81 `Pipe90Back.png`, 82 `Pipe90Bracket.png` | static* 5 polygons | — | elbow tube |
| 19 | Doll | 40 Head, 36 Body, 37 Dress, 34/38 arms, 35/39 legs (`Doll*.png`) | ragdoll: 6 dyn bodies, 5 revolute joints | dynamic, stabbable | ragdoll, hangable (kind 2 point) |
| 20 | Skateboard | 113 `Skateboard.png`, 114 `SkateboardWheel.png` | dyn deck + 2 wheels (line joints) | dynamic | rolling platform |
| 21 | Pulley | 88 `PulleyWheel.png`, 86 `PulleyClevis.png`, 87 `PulleyScrew.png` | static* circle | — | rope guide: rope links wrap around it by plain collision (no `b2PulleyJoint`) [verified] |
| 22 | Seesaw | 108 `SeesawArm.png`, 109 `SeesawFulcrum.png` | static fulcrum + dyn arm, revolute | — | lever |
| 23 | GoalStar | 125–136 `Star01..12.png` (spin anim), 137 `StarGlow.png`, 122 `Sparkle03.png` | static* circle sensor | — | collectible star (05) |
| 24 | Billboard | 110 `Shelf.png` or 6 `Book.png` by `itemData & 0xF` (2/3) | static* selection box only | — | placement hint image, no collision |
| 25 | Magnet | 62 `Magnet.png`, 63–68 `Magnet01..06.png` (pulse anim) | static* circle + 2 boxes | compound | attracts magnetic items |
| 26 | Pinball | 78 `Pinball.png` | dyn circle | dynamic, **magnetic** | steel ball |
| 27 | PaperPlane | 73 `PaperPlane.png` | dyn polygon | dynamic, magnetic, stabbable (flags `0x6B`) | glides (lift/drag) |
| 28 | Spring | 123 `Spring.png`, 124 `SpringSeat.png` | 3 bodies, prismatic + distance joints | dynamic | trampoline |
| 29 | Dart | 33 `Dart.png` | dyn polygon | dynamic, magnetic | tip pops balloons, sticks into stabbable items |
| 30 | HangingLamp | 59 `LampShade.png` | dyn polygons | dynamic, stabbable | shade that can catch things; hangable |
| 31 | WorldBound | — | static boxes | — | floor/walls/ceiling, one per level (04 §6) |
| 32 | LaundryBasket | 61 `LaundryBasketFront.png`, 60 `LaundryBasketBack.png` | dyn polygons | dynamic | container (goal 7) |
| 33 | Bumper | 28 `BumperOff.png`, 29 `BumperOn.png` | static* circle | — | pinball bumper: kicks what touches it |
| 34 | Slingshot | 117/118 frame back/front, 115/116 elastic, 119/120 pocket | static* base + pouch | compound | launches the loaded item on start |
| 35 | RCTruck | 94 `RCTruck.png`, 95 `RCTruckWheel.png`, 93 `RCHitch.png`, 89 `RCAntenna.png` | dyn chassis + 2 wheels (line joints) | dynamic | driven while its controller button is pressed |
| 36 | RCController | 90 `RCController.png`, 91 `RCControllerButton.png`, 92 `RCHelicopterController.png` | dyn base + button (prismatic) | dynamic | button pressed by falling objects |
| 37 | Trapdoor | 140/143 `TrapdoorLeft/Right.png`, 144/145 `TrapdoorShelfLeft/Right.png` | static frame + 2 static doors | — | doors become dynamic when unlocked by the lever |
| 38 | TrapdoorLever | 141 `TrapdoorLever.png`, 142 `TrapdoorLeverBase.png` | dyn base + lever (revolute, ±36°) | — | unlocks its trapdoor when tilted > 18° |
| 39 | Helicopter | 45 `Helicopter.png`, 46–55 rotor anim, 56 tail rotor | dyn polygons | dynamic | flies when turned on by its controller |
| 40 | SelectionArea (runtime) / `RCHelicopterController` (legacy level files ≤ v6) | none | set-up only: `CreatePhysics` case 40 makes a static 2.705×0.5 `ReturnAreaBound` box at (1.705, −0.5) [verified: G3] | — | in level files it is converted to type 36 on load (02 §5) [verified] |
| 41 | BouncyBall | 11 `BouncyBall.png` | dyn circle | dynamic | very bouncy light ball |
| 42 | ZipLine | 99 `RopeZipLine.png`, 148 `ZipLineSegment.png`, 149 `ZipLineTrolley.png`, 147 `ZipLineAttachment.png` | 2 static anchors + dyn trolley on prismatic joint | — | trolley slides along the line; hangable |

Types 5, 6, 7, 9, 12–15, 19–25, 28–30, 32–40, 42 use multi-sprite renderers (`RenderScissors`, `RenderSlingshot`,
`RenderTruck`, `RenderZipLine`, `RenderPulley`, `RenderRopeSegments`, `RenderKnots` …). `RotationGizmo_iPhone.png`,
`FlipGizmo.png`, `TranslationGizmoBig.png`, `InvalidSelection.png`, `POW1..4.png`, `Wave.png` are UI/effect
frames in the same atlas.

## 2. Item details

Body definitions below list fixtures in creation order. Positions are body-local metres unless noted.

**Initial item state** [verified 2026-09-13]: `GameItemCollectionUtils::InsertWithHandle` copies the per-type
default block of `st::ItemInfos` (`.bss` 0x27e7c8, 0x18 bytes per type: `+8` block size, `+0xC` default block)
into the new item, then `GameItemUtils::SetInitialState` runs (Scissors, Book, Dart, Truck, Trapdoor, Helicopter).
Non-zero defaults: every type has `+4` (object index) = −1; Scissors `+0xC` cut angle = 0.2618 (15°), `+0x10` =
−1, `+0x14` = 0.5, `+0x24` = 1.0; Rope end `+8/+0xC` = (0, −0.6); BoxingGlove interpolator `+0x14` = 36, `+0x18`
= 1.0, `+0x20` = 36 (button height px); Skateboard `+8` = −1; Billboard `+8` = 1, `+0xC` = 1.0; Magnet `+0xC` =
10000, `+0x10` = −1; Dart `+0x14` = 1.0, `+0x18` = 0.5; Slingshot pouch `+0xC/+0x10` = (−0.11, 0.03);
RCController `+0x14` = −1; ZipLine end `+8/+0xC` = (1.0, −0.2), `+0x10` = −1. The remake mirrors this in
`GameItem::defaults`; the harness (`uc_trace.py`, `uc_dump_physics.py`) copies the same block, so the scissors
drop scenes of 08/G3 open the blades by 15°.

**M4 porting notes (2026-09-14)** — every `…Utils::Update` / `HandleCollision` below was ported from the
decompile *and* the disassembly and is gated bit-exactly against the emulated original (10 §8, G4/G6); the
corrections found on the way: the **Helicopter** fixture at `+0x30` is the *third* fixture (the tail rotor at the
nose end, not the skid), `TurnOn` makes the main rotor (`+0x2C`, 5th) and the tail rotor sharp (group −2), `TurnOff`
restores only the main rotor; `HelicopterUtils::HandleCollision` fires only while the helicopter is on and only
for those two fixtures, splitting 80 N by mass (a massless other body counts 100 kg), sound `0x38` at volume 0.1.
`BumperUtils::Update` walks the bumper's contact list every substep and calls `HandleCollision` with the manifold
normal as it is, while `WorldContactListener::PreSolve` negates it when the bumper is fixture B; the "on" state
blocks a second impulse for 0.18 s. `BoxingGloveUtils::HandleCollision` on the B side of a contact receives
object A *twice* (the sound `0x24` plays at the other object, not at the glove). `BalloonUtils::Pop` has no
"already popped" test (a second sharp contact restarts the 0.15 s timer and queues the sound and the radial
force again; `Update`'s popped branch destroys the physics and the attachments on its next substep, sets the
activated bit, counts the timer down and queues action 7 when it runs out). `SlingshotUtils::ShouldCollide` lets everything collide until the
first tick of the run; `Update`'s first tick always queues the fire sound, picks the object at the pouch with
`GetNearestIntersectingObjectWithFilter(…, 0xFF)` and applies `(0.6·log2(1 + m))·2500·(rest − pouch)` at the pouch
with `ApplyForce` (not an impulse), then the pouch springs back explicitly (`v += dt·(2000·d − 20·v)`). The
**Scissors** `Scissors01..05` sprites are the idle snip animation (11 §5), shared by `Update` and `UpdateSetUpMode`,
with the idle timers of scissors and darts seeded from a `Random` built on the item's *address* in the original
(the remake seeds them with the handle — 10 §11 item 12). The **Spring**'s fixtures carry user data 1 / 2 / 1 so
`PreSolve` can disable the base box's contacts with the spring's own plate and seat. `RopeUtils::Cut` and the
dart's `AttachSharpObject` are as described (§6, §29 — a revolute joint with a zero-speed motor, not a weld);
the removal of popped balloons / collected stars goes through action 7 → `InvalidateItem` → the frame tail's
`RemoveInvalidItems` (05 §7); a cut rope keeps its item, only its joints go.

> **Exact numbers:** the formulas here were read from decompiled code and some arguments were lost by the
> decompiler. The authoritative, fully numeric set-up (every body/fixture/joint per item and mode, evaluated by the
> original code itself under emulation) is in [08-physics-dump.md](08-physics-dump.md); use it as the source for an
> implementation and this section for the *why*. Set-up-mode bodies are all `b2_dynamicBody` (the world is never
> stepped in set-up). In simulation mode (derived from 08) the main body is **static** for Shelf, Hook, BoxingGlove
> (stand), Pipe, Pipe90, Pulley, Seesaw (fulcrum), GoalStar, Billboard, Magnet, WorldBound, Bumper, Slingshot,
> Trapdoor (frame + both doors until unlocked), TrapdoorLever (base) and ZipLine (both anchors); **kinematic** for
> Scissors (both halves) and the Spring base; **dynamic** for everything else.

### 1 Shelf
* `static*`; `SetAsBox(r, r·0.05)` → thickness = 10 % of width; `(0.2, 0, —)` `Static`. `r = (w−2)/2·px·0.98`, `w` = 218 px → r ≈ 0.352 m.
* sel: `SetAsBox(r, r·0.286)` → (0.352, 0.1007) [verified by 08].

### 2 TennisBall · 3 BowlingBall · 4 SoccerBall · 16 EightBall · 26 Pinball · 41 BouncyBall
`CreateBallPhysics` (04 §9): circle `r·0.9`, `angularDamping 0.2`; `GameParams::MinSelectionRadius = 0.12` m.

| Type | r (m) | mass | I | friction | restitution | bullet |
|------|-------|------|---|----------|-------------|--------|
| TennisBall | 19·px·0.98 ≈ 0.0620 | 0.057 | 6e-5 | 0.6 | 0.72 | yes |
| BowlingBall | 36·px ≈ 0.1199 | 7.0 | ½·7·r² | 0.5 | 0.2 | no |
| SoccerBall | 41·px·1.05 ≈ 0.1434 | 0.41 | 0.004 | 0.5 | 0.78 | no |
| EightBall | 0.05661 | 0.2 | ½·0.2·r² | 0.5 | 0.5 | yes |
| Pinball | 0.04829 | 0.1 | ½·0.1·r² | 0.5 | 0.3 | yes |
| BouncyBall | 0.04 | 0.03 | 2e-5 | 0.9 | 0.99 | yes |

(`px` = 3.41/1024.) Pinball, Dart and PaperPlane carry the *magnetic* flag (0x20).

### 5 Balloon
* `dyn`; circle `r` `(0.3, 0.01, 0.2)` `Dynamic+`; triangle knot `(0,−0.25) (0.1,−0.1) (−0.1,−0.1)`; `SetMassData(0.1 / 0.01)`.
* Attachment: (0, −0.25) kind 2 (rope may hang from it).
* `BalloonUtils::Update` every substep, while not popped (`m_force` += dt·F, applied at the knot point, giving torque):
  ```
  drag_x = sign(vx)·min(400·vx², 5000),  drag_y = sign(vy)·min(400·vy², 5000)
  h = clamp((3 − y)/3, 0, 1)                            // altitude factor
  mLoad = mass of the object attached through a rope (0 if none)
  lift = 350 · (1 + 3.2 · log2(1 + mLoad) · (0.8 + 0.2·h))
  F = (−drag_x, lift − drag_y) · dt
  ```
  Constants 350, 0.2, 400, 3.2, 5000 from `.rodata` (`DAT_00281144..54`).
* Pops (`BalloonUtils::Pop`) when touched by a fixture with group index −2 or −8 (dart tip, scissors blades) —
  `WorldContactListener::BeginContact`. Popping: physics destroyed, attachments removed, state byte (`+0xD`) bit 0 set (goal 5),
  4-frame pop animation over 0.15 s, sound action.

### 6 Scissors
* Two `static*` bodies (top/bottom halves), each with two polygons (vertex lists in pixel units divided by
  `115/(2r)` — 115 px = `ScissorsTop.png` width), `(0.2, —, —)` `Dynamic`; in simulation an extra fixture with
  restitution 0.01 at the blade tip (`r·0.95`, y = 0.01 / −0.05); sel box on the main body. `UpdateAngle` places
  the halves at `pos ∓ Rotate(angle, (0, 0.12r))` with angles `angle ± cutAngle` (`Scissors+0xC`, 15° from the
  default block above, so the blades are open at rest) [verified: G3].
* `ScissorsUtils::HandleCollision`: when closed == 0 and `|v_n · m_other| > 0.01` → closing animation starts
  (`Scissors01..05`), sound; when the closing timer (decremented by 2·dt in `ScissorsUtils::Update`) reaches 0 the
  blade tip point (item offset `+0x2C`, rotated by the item angle) is used for a `b2World::QueryAABB`; for every rope
  link fixture found [verified]: `i` = body index of the link (never 0), partner `j = i−1` if `i` is the last link,
  otherwise `j = i+1` unless link `i−1` is at least as close to the tip as link `i+1`; then `RopeUtils::Cut(rope, obj, i, j)`
  destroys the distance joint between links `i` and `j`, destroys the rope's overall length joint (`Rope+0x10`),
  nudges every link body by x ±0.001 (alternating) to wake them, and sets the rope's *activated* bit → **cuts ropes**. Blade fixtures have group −2 (balloon popping). `UpdateAngle`/`UpdateSetUpMode` keep the
  halves aligned while editing.

### 7 Bucket
* `dyn`; walls: three polygons (left/right/bottom, built from `r`, `r/1.3·0.9`, `r·0.75`); solid `Topping` box across the top (collides only with other Topping fixtures, 04 §5);
  simulation: interior goal-sensor polygon with `isSensor = true` and `groupIndex = −7` (goal 7) [verified: `mvn r3,#6` → `strh` into the fixture filter right before `CreateFixture`]; set-up: interior `NonCollidable`
  polygon, sel box and circle `r·0.75`; `SetMassData(0.5 / 0.02)` at centre (0, −0.3·r/1.3·0.9).
* Attachment: (0, 0.26) kind 2 (handle) — hangs from a rope end.

### 8 Hook
* `static*` body without fixtures (rope ends attach to it); attachment (0, −0.03) kind 1.

### 9 Rope — see 04 §8. Ends attach to kinds 1|2. In level files `center` = end A, `ropeEndPos` = end B.

### 10/11 CardboardBoxMedium/Small · 12 FishBowl
`CreateBoxPhysics`: box `hw·k × hh·k`, friction 0.6, restitution 0: crates `k = 0.96, density 20`; fish bowl
`k = 0.88, density 100`. Sizes: medium 144×138 px, small 92×88 px, bowl 93×85 px (×0.9 / 0.95 template tweak).

### 13 PiggyBank
* `dyn`; body box `SetAsBox(r·0.9, r·0.6, centre (−0.02·s, 0.02))` `(0.6, —, —)` `Dynamic+`; feet box
  `SetAsBox(r·0.6, r·0.1, centre (0.02·s, −0.09))`; `SetMassData(1.0 / 0.001)`.
* `PiggyBankUtils::Break` (called from `GameItemUtils::Break` via Action 12, queued in `WorldContactListener::PostSolve`
  [verified]: for a *breakable* (flags bit 4), not yet *activated* object, when the **sum of normal impulses over the
  manifold points > 3.5 N·s** in one contact): body set non-collidable, spawns `Debris` pieces (a 0.5r×0.2r box + up to three
  polygon shards with velocities from a table `(-1,1,0.1,2,0.8,1.3)`), coins effect, state byte (`+0xD`) bit 0 set (goal 5);
  pieces fade in `PiggyBankUtils::Update` after 0.2 s.

### 14 BoxingGlove
(All numbers from 08, `s` = scale.x sign.)
* Body A (`static*`, the stand): **trigger button** circle r 0.05 at (−0.2·s, 0.22813 = `0x3e699ae2`) `(μ 0.6)` `Static`, **group −4** —
  the fixture pointer is kept at glove `+0x2C`; plate box `0.0999 × 0.4163` (half 0.04995 × 0.20813) at (−0.2·s, 0.01)
  **group −5**; head box 0.2×0.2 (half 0.1) at the origin **group −5** (this one is re-shaped every substep while
  punching, see below). Set-up adds a `Topping` box 0.18×0.18 at (−0.06·s, 0) and a sel box `0.0833 × 0.2498`.
* Body B (`dyn`, the fist): circle r 0.1 `(0.7, 0.3, 0.01)` `Dynamic` at offset **(0.28·s, 0.01)** rotated by the item
  angle (the `.bss` constant `DAT_00282f58` (ELF 0x272f58) is `0x3e8f5c28`, one ulp below 0.28f — the ulp shows in level scenes); wrist box 0.12×0.1 (half 0.06×0.05) at (−0.13·s, 0) **group −5**; `SetMassData(10 / 0.01)`.
  Group −5 makes the plate, head box and wrist mutually non-colliding, so the fist can slide through its own stand.
* Simulation: `b2PrismaticJoint` A→B, anchor B (−0.28, −0.01), axis `rotate(angle, (s, −0.15))`, no limit, **motor on**
  with speed 0 and `maxMotorForce 10000` — this holds the fist in place until the punch.
* `BoxingGloveUtils::HandleCollision` [verified] — called from `WorldContactListener::PreSolve` when one fixture of the
  contact has **group −4** (the trigger button): if the glove state (`+8`) is 0 (armed) and `|v_n · m_other| ≥ 0.2`
  (normal relative velocity × mass of the other body — momentum along the normal ≥ 0.2 kg·m/s): state ← 1 (punching);
  the prismatic joint's motor is disabled; the trigger button fixture (`+0x2C`) becomes `NonCollidable`;
  `CubicInterpolator::Start(36 → 8, 0.06 s)` is started: `Update` calls `CubicInterpolator::Update` (its `r0`
  return value is unused, `ldm r1,{r0,r1}` at `0x9A024`), and the interpolated value at glove `+0x20` **is the
  button sprite height in px** that the renderer stretches `BoxingGloveButton` to (11 §4: 36 px at rest, 8 when
  pressed — corrected 2026-09-13, this was documented as dead code); `UpdateArmGeometry`, `HandleCollisionSounds`
  and `Retract` never touch it; sound `BoxingGloveTriggered` at volume 0.4.
  `Retract()` (state 2) is never called — after a punch the fist stays out for the rest of the run.
* `BoxingGloveUtils::Update` (every substep while state ≠ 0) [verified]:
  ```
  anchor = basePos + Rotate(θ, (−0.2·r, 0))          // r = 0.2 → 0.04 behind the stand origin
  d = fistPos − anchor;  L = |d|;  u = d / L (or (1,0) if L ≤ 1e-4)
  (k, rest) = (−5850, 0.75) if state == 1 else (−1170, 0.6)   // state 2 never occurs
  F = (L − rest)·k·u − 40·v_fist                       // stiff spring to 0.75 m from the anchor + linear damping
  fist.force += F;  fist.SetAwake(true)                // applied at the centre (no torque)
  UpdateArmGeometry()                                  // FUN_000a9578
  ```
  `UpdateArmGeometry`: with `L = |fist − base| + 0.2`, the scissor-lattice hinge angle
  `φ = asin(clamp(2·(L − 0.21)/5 / 0.2973, −1, 1))` is stored at glove `+0x24` for the renderer, and the head box of
  the stand is **re-shaped** to `SetAsBox((L−0.2)/2, cos φ·0.14865, centre (((L−0.2)/2 − 0.2)·s, cos φ·0.14865 − 0.14))`,
  i.e. a collision box spanning from the stand to the fist that shrinks in height as the lattice extends — the
  extended arm pushes things. `CreatePhysics` runs it once too, so from the start the head fixture is the
  `0.28 × 0.277` arm box centred at `(−0.06·s, −0.0015)` (`SetAsBox` is applied to the fixture's own shape — the
  **head of the stand's fixture list = the last fixture created**, so in set-up mode it is the selection box that
  gets re-shaped, not the head box [verified: G3 `polygonfix` lines]; in
  place: the stand is static, so its broad-phase AABB and mass data stay those of the original 0.2 × 0.2 box —
  reproduced by the `polygonfix` scene line of the regression harness).

### 15 Book
* `dyn`; `SetAsBox(hw, hh)` with `hw = w·px·0.6·0.5`, `hh = h·px·0.95·0.5`, `px = 3.41/1024 = 0.00333`, where
  `(w, h)` comes from a **hard-coded** 4-entry table (`.bss` `0x272EF4`, filled by a static initializer — stores at `0x8797C`–`0x879B0`,
  *not* from the sprite frames) indexed by the book's colour state (`itemData` 0–3) [verified by 08 and the
  initializer]: 0 → 26×108, 1 → 20×102, 2 → 18×88, 3 → 20×97 px. The renderer draws frame `7 + colour`
  (BookBlue 26×108, BookGreen 20×97, BookRed 18×88, BookYellow 20×102), so colours 1 and 3 use each other's
  height (a 5 px / 1.7 cm mismatch in the original — reproduce it as is). Levels only use 0–3; index 4 would read
  the neighbouring constants (π/4, 0.1). `(0.6, —, 25)` `Dynamic+`; a `Selection` box is added when either
  half-size is < 0.12 m.

### 17 Pipe · 18 Pipe90
* `static*`. Pipe: two wall boxes `r × 0.005` at y = ±(0.236·r + 0.0), i.e. for r = 0.3101: `[-0.3101, 0.0682]…[0.3101, 0.0782]` and its mirror [verified by 08] `(0.2, —, —)` `Static`, a `Topping`
  box `(0.7, 0.4, 50)`, set-up: `PipeFilling` box and sel box (r·1.3 × …).
* Pipe90: five wall polygons with explicit vertex lists in pixel units scaled by `107/(2r)` (107 px = sprite width),
  `Static`; two `Topping` polygons; set-up: sel box + two `PipeFilling` circles r 0.07 at (±0.06, ∓0.09).
* Attachments: Pipe `(±(r+0.0051), 0)` dir `(±1,0)`, kind 8 mask 8; Pipe90 `(0.44r, 0.967r)` dir (0,1) and
  `(−0.985r, −0.42r)` dir (−1,0). Pipes snap to each other only.

### 19 Doll (ragdoll)
* Six `dyn` bodies (`angularDamping 0.1`): torso box `(±0.06, −0.095…+0.065)`, head circle r 0.07 at
  (0, 0.13), two legs and two arms (boxes) at ±0.06/±0.04/±0.05 offsets rotated ±22.5°/±30°/−18°;
  `Dynamic+` `(0.8, 0.3, 9)`. Five `b2RevoluteJoint`s from the body, all with `maxMotorTorque 0.001`: head (limits
  ±45°, motor **off**), arms (−22.5°…+120°, mirrored by `s`, motor on), legs (±45°, motor off) [verified: G3]. Attachment (−0.06, −0.01) kind 2.
* Sounds: `DollUtils::HandleCollisionSounds` (`DollImpact1..`).

### 20 Skateboard
* Deck `dyn`: three boxes — centre `SetAsBox(r·0.67, 0.014)` at (−0.02·s, 0), nose `SetAsBox(r·0.15, 0.014)` at
  (−0.81r·s, 0.025) angle −0.5712·s, tail `SetAsBox(r·0.19, 0.014)` at (0.78r·s, 0.025) angle `π/8.5·s`;
  `(0.6, —, 40)` `Dynamic+`; sel box `r × 0.28r` at (0, −0.02).
* Two wheels `dyn` circles r 0.04 `(0.6, 0.4?, 40)` at (∓0.52r·s / 0.56r·s, −0.08) (rotated with the deck),
  joined by `b2LineJoint`s (= wheel joints, 04 §1: axis (0,−1), `frequencyHz 15`, `dampingRatio 0.8`, motor **disabled**, `maxMotorTorque 0`) [verified by 08].

### 21 Pulley
* `static*` circle r `r` `(0.5, 0.4, 5)` `Static`. No joint of any kind: a rope passing over it is simply a chain of
  links (filter `Rope` 0x4/0x101) colliding with the wheel (`Static` 0x1/0xFFFF), so it wraps and slides with
  μ 0.4 / e 0.5 [verified: `PulleyUtils::CreatePhysics`, 04 §5; `b2PulleyJoint` is only present as unused library
  code].

### 22 Seesaw
* Fulcrum `static*` polygon (triangle: `(0.2r, 0)`, `(0.4r·0.55…)` etc.) `(0.9, —, 2000)` `Dynamic`; arm `dyn`
  `SetAsBox(r·0.98, r·0.05, centre (0, 0.024))` `(0.9, —, 20)`; `b2RevoluteJoint` at the pivot, limits **±0.698 rad (±40°)**,
  `maxMotorTorque 0.02`; sel box `r × 0.3r`.
* `SeesawUtils::Update`: plays the seesaw sound when joint speed exceeds 4 rad/s and the direction changes.

### 23 GoalStar
* `static*` circle `r·1.1` `(0.2, —, —)` `Dynamic` (contact sensor); sel circle 0.12 in set-up mode.
* `GoalStarUtils::Update`: on first `IsColliding` → collected (sparkle, sound 0xD, `GoalState.stars++`), then a 0.4 s
  shrink animation (`CurveUtils::GetValueAt` over 6 points) and the item is removed (Action 7).

### 24 Billboard
* `static*` with only a selection box (`SetAsBox(r, r)`, r = 0.2) — no collision. It is a **placement hint**: the
  renderer draws, at the billboard's transform, the sprite selected by `itemData & 0xF` [verified: item renderer
  `FUN_000bd4f0`, case 0x18]: **2 → frame 110 `Shelf.png`, 3 → frame 6 `Book.png`**, anything else → nothing. Shipped
  levels use `itemData` 0x12/0x22 (shelf) and 0x13/0x23 (book) in seven Classroom levels; the free edition's
  levels use plain 2/3. The high nibble is 1 in every single-billboard level and 1/2 where two billboards exist
  (0x12 + 0x22 / 0x13 + 0x22 / 0x12 + 0x23) — consistent with an authoring ordinal — and is read by nothing in the game
  [verified: the renderer masks `& 0xF`, `BillboardUtils::CreatePhysics` ignores the state, the Classroom tutorials
  (`st::tutorial_chap0_level*`) carry their target positions as hard-coded constants, e.g. KlassRoom
  (1.149, 1.077) / (1.329, 0.335) = the two billboards + 0.05 m]. Billboards are excluded from touch selection in
  campaign mode (`GetNearestIntersectingObjectMinusBillboards`).

### 25 Magnet
* `static*`: circle `r·0.85` at (−0.015·s, 0) `(0.3,…)` `Static`; two boxes 0.02×0.044 at (0.8r·s, ±0.43r/−0.45r)
  (the poles); sel circle `r·1.3`.
* `MagnetUtils::Update`: for every object with the magnetic flag within 1 m (squared distance ≤ 1) and inside the cone
  (dot of unit vector to the object with the magnet's facing direction ≥ `DAT_0028ee20` = **0.17365 = cos 80°**, i.e. a
  160° wide cone; value read from the initialised `.bss` under emulation) applies a force toward the pole:
  `F = −(300 − 300·d²) · (m / 0.1) · dt` along the direction, at `GameItemUtils::GetMagneticCenter` (object-specific
  point, e.g. dart tip). Pulse animation frame advances every 1/30 s (6 frames) while something is attracted.

### 27 PaperPlane
* `dyn`; polygon from the sprite (5 vertices: `(r,0)`, `(0.9·0.45r, −0.72r)…`, mirrored by `s`) `(0.8, 0.5, —)` `Dynamic`;
  sel box `r × 0.3r`.
* `PaperPlaneUtils::Update` [verified, full formula], every substep for each plane (`s` = scale.x, `θ` = body angle,
  `v` = linear velocity, `ω` = angular velocity, `dt` = 1/120):
  ```
  c = cos θ;  if c < 0: c = −0.2·c            // flying backwards: 20 % effect, sign restored
  (k, k1) = (−5, −10) if vy ≥ 0 else (5, 10)   // rising → push down/back, falling → lift
  Fx = s · k  · c · vy²                        // "drag" along x
  Fy = 2·vx² + k1 · c · vy²                    // lift from forward speed, damped by vertical speed
  body.force  += (Fx, Fy) · dt                 // applied at the centre of mass (no torque from it)
  body.torque += −ω · dt                       // angular damping
  body.SetAwake(true)
  ```
  Magnetic centre = nose.

### 28 Spring (trampoline)
* Base `static*`: thin box `SetAsBox(r·0.6, 0.001)` + box `r·0.6 × r·0.5` `Dynamic+`; sel box `0.8r × 1.3r`.
* Plate and seat bodies (`dyn`), each `SetAsBox(r·0.9, 0.03)` `(0.6, —, 10)`, placed at ±(r − 0.007) along the spring
  axis; simulation: `b2PrismaticJoint` base→plate along the axis with limits ±0.16 and a `b2DistanceJoint`
  `frequencyHz = DAT_002811ac = 12, dampingRatio 0.1`.
* `SpringUtils::Update`: when compressed (length < rest − 0.01) and something is pushing, the joint frequency is
  raised linearly from 12 to **90 Hz** and damping set to 0.1 for the release (`DAT_002811b0 = 90`), producing the
  bounce; the base box is re-sized to the current compression; sound `Spring` (id 0x21, volume ∝ compression).

### 29 Dart
* `dyn`, `linearDamping 0.01, angularDamping 0.01`; polygon body `(±0.95r, ±0.005)…(0.2r)` `(0.5, —, 5)` `Dynamic`;
  simulation: tip circle r 0.001 at `(s·r, 0)` `Dynamic` **group −8**; set-up: sel box `1.1r × 0.7r`;
  `SetMassData(0.05 / 0.001)` centre (0.2·s·r, 0).
* `DartUtils::HandleStabCollision` (from `PreSolve` when the −8 tip hits a *stabbable* object with
  `dot(tip dir, normal) ≥ 0.65`): the tip fixture becomes `NonCollidable`, a revolute joint (`collideConnected`, motor speed 0, max torque 4·v² — verified in M4) is created (Action 0x11
  "attach sharp object" → `GameItemUtils::AttachSharpObject`) so the dart sticks. Tip pops balloons.

### 30 HangingLamp
* `dyn`; shade = trapezoid polygon (0.35 h wide top, 0.2 h) `(0.7, 0.4, 100)` `Dynamic`; 4 thin side boxes 0.01 wide
  at ±0.4h/±0.72h rotated ±20°/±60° `(…, 5)`; `Topping` box across the opening `(0.6, 0.4, 70)`; set-up: sel box
  and an inner polygon. Attachment (0, 0.2) kind 2 (hangs from a rope).

### 32 LaundryBasket
* `dyn`; bottom box `SetAsBox(w, 0.009)`, two side polygons (0.95·r), `(0.7, 0.4, 50)` `Dynamic`; `Topping` box
  `(0.7, 0.4, 50)`; simulation: interior goal-sensor polygon, `isSensor = true`, `groupIndex = −7` (goal 7) [verified]; set-up: sel box `r × r/2.123`.

### 33 Bumper
* `static*` circle `r·0.95` (simulation) / `r·1.1` (set-up) `(0.2, 0.4, 50)` `Dynamic+`.
* `BumperUtils::Update/HandleCollision` [verified, M4]: on a touching, non-sensor contact → Action 0x12
  (`ApplyForce`) of magnitude **300 · log2(1 + m)** along the manifold normal at the other body's position, the
  "on" state for **0.18 s** (no second impulse meanwhile; `Update` counts the timer down), sound `BumperImpact`
  (0x17) at the bumper. `Update` scans the contact list every substep (normal as is); the listener's `PreSolve`
  path negates the normal when the bumper is fixture B.

### 34 Slingshot
* Base `static*`: box 0.02×0.06 at (0.05·s, −0.05) `(0.7, 0.4)`; set-up: sel box 0.07×0.12 and a separate pouch body with
  sel circle 0.12 at `ropeEndPos` (the player drags the pouch to stretch).
* `SlingshotUtils::ShouldCollide` lets the loaded item pass through the frame once fired (everything collides
  before). On simulation start (`Update`, first tick) [verified, M4]: the launch sound `SlingshotFire` (0x26)
  always; the object at the pouch (`GetNearestIntersectingObjectWithFilter(…, 0xFF)`, the set-up pick query), if
  its body is dynamic, receives `ApplyForce((0.6·log2(1+m)) · 2500 · (rest − pouch), pouch)` **once** (rest =
  (−0.01, 0.076) item-local, rotated); then the pouch vector springs back explicitly every substep
  (`v += dt·(2000·|d|·d̂ − 20·v)`, `pouch += dt·v`) for the elastic sprites.

### 35 RCTruck · 36 RCController
* Truck chassis `dyn`: two boxes `0.47w × 0.2h` / `0.47w × 0.4h` and a top polygon `(0.7, 0.4, 50)` `Dynamic`; two wheels
  `dyn` circles r 0.085 `(0.4, —, 8)` at (∓0.6w·s, −0.1)/(0.58w·s, −0.1) joined by `b2LineJoint`s (axis (0,−1),
  `frequencyHz 25, dampingRatio 0.8`, motor enabled with `maxMotorTorque 5`, speed 0 until driven) [verified by 08]; `w = 0.85r`, `h = 0.9r·116/220`.
  Attachment hitch (−hitchX, −0.02) kind 2. Set-up: the controller body (box 0.1×0.04 + button 0.05×0.038).
* Controller `dyn`: base box 0.1×0.04 `(0.7, 0.4, 12)`, button box 0.05×0.038 `(…, 8)` on a `b2PrismaticJoint`
  (vertical, limits −0.04…0, `maxMotorForce 2`).
* `RadioControllerUtils::Update`: if `translation < −0.03` (button pushed) and not yet pressed → sound `RCButtonClick` (0x2C) and
  `SetMotorSpeed(±15 rad/s)` on both wheel joints (sign from the truck's flip) for the truck, or
  `HelicopterUtils::TurnOn` for a helicopter (`itemData` type 0x27); releasing sets speed 0 / `TurnOff`.
* `TruckUtils::UpdateAnimation`: exhaust puffs (0.33 s interval, life 1.1 s) while driving.

### 37 Trapdoor · 38 TrapdoorLever
* Trapdoor frame `static*`: two shelf boxes `0.16w × 0.12h` at (±0.8w, 0.01) `(0.5, 0.3, 50)` `Static`; sel box.
  Two door bodies (static, `SetType(dynamic)` on unlock): `SetAsBox(0.3w, 0.04h)` at ±(0.6w+…), each on a
  `b2RevoluteJoint` to the frame with limits **0 … ±1.884955 rad (108° = 0.6π)** opening downward (left door
  −108°…0, right door 0…+108°), `maxMotorTorque 0.005` [verified by 08: each door is `SetAsBox(0.0959, 0.0128)` centred
  0.0959 from its hinge edge, i.e. the polygon spans x = 0…0.1918 from the hinge; hinge at (±0.2168, −0.03) from the
  frame centre]. Set-up: the lever body is created next to it.
* Lever: base box **0.16 × 0.08** (half 0.08 × 0.04) + lever box **0.04 × 0.28** (half 0.02, from 0 to 0.28 up) `(0.7, 0.4, 8)`
  on a `b2RevoluteJoint` at the base centre with limits **±0.628 rad (±36°)**, `maxMotorTorque 0.2` (`DAT_00293ac0..`
  = 0.08, 0.04, 0.02, 0.14 read under emulation); sel box `0.12 × 1.2r`.
* `TrapdoorLeverUtils::Update`: when `|jointAngle| > π/10` (18°) → `TrapdoorUtils::Unlock`: both doors become dynamic
  (they swing open under load), sound `TrapdoorOpen` (0x2F) volume 0.5. A lever sound plays when the joint moves (speed threshold).

### 39 Helicopter (controller = RCController 36; legacy type 40 is converted on load)
* `dyn`: fuselage `SetAsBox(0.65r, h)` at (−0.4r·s, 0) `(0.6, —, 150)` `Dynamic`; cabin `0.32r × 0.3h` at (0.57r·s, 0.28h)
  `(…, 5)`; mast `0.12r × 1.2h`; tail `0.53r × 0.4` at (−0.43r·s, −1.4h); rotor box `1.1r × 0.2·…` at (−0.36r·s, 1.9h)
  (exact fixtures in 08). The tail-rotor fixture (`+0x30`, the 3rd — at the nose end in 08, x ≈ r; corrected in
  M4, this was documented as the skid) and the main-rotor fixture (`+0x2C`, 5th) are created with the normal
  `Dynamic` filter; **`TurnOn` sets both to `Dynamic` with `groupIndex = −2`** (the "sharp" group shared with
  scissors blades — a spinning rotor pops balloons and never collides with other −2 fixtures), `TurnOff` restores
  group 0 on the main rotor only [verified: disassembly, M4]. `h = 0.25r`, `r = 0.225`. Set-up: sel box `1.2r × 2h` and the controller body.
  Attachment hook (−0.09, −0.084) kind 2.
* `HelicopterUtils::Update` [verified, full control law]. State per helicopter (stride 0x34): `+0x10 on`,
  `+0x14 throttle` (starts 0, only grows while on), `+0x1C rotorRpm`, `+0x20 rotorPhase`, `+0x24 tailRpm`,
  `+0x28 tailPhase`. Every substep (`dt` = 1/120), with `s = sign(scale.x)` (±1, flipped helicopter), `θ` = body angle,
  `a = s·θ` wrapped into [−2π, 2π], `vy` = body velocity y, `ω' = s·ω` (angular velocity in the un-flipped frame),
  constants `A50 = 50°, A10 = −10°, A60 = ±60°` (`.bss`, `DegToRad` products):
  ```
  if not on:
      rotorRpm = max(rotorRpm − dt·10, 0);  tailRpm = max(tailRpm − dt·20, 0)      // spin down
  else:
      if vy ≥ 0.04:   tiltTarget = 10°                                            // rising: level out
      else:           throttle = clamp(throttle + dt·10, 50, 100); tiltTarget = 1.1·A50 = 55°
      # attitude controller (torque, in the s frame → multiplied by s):
      if a ∈ [−60°, −10°):                       # nose too high → push it back to −10°
          gain = (ω' ≤ 0) ? (1 − ω')·10 : 1
          torque += gain · dt · s · 100 · (A10 − a)
      elif tiltTarget < a ≤ 60°:                 # tilted past the target → push back to target
          gain = (ω' ≥ 0) ? (ω' + 1)·10 : 1
          torque += −gain · dt · s · 100 · (a − tiltTarget)
      if vy ≥ 0.028 and a ≤ 50°: torque += dt · s · 0.8           # slow nose-down while climbing
      force += throttle · Rotate(s·a, (0, 1))   # = Rotate(θ, (0,1)): thrust along the body's up axis (also for flipped s = −1)
      body.SetAwake(true)
      rotorRpm = min(rotorRpm + dt·10, 15);  tailRpm = min(tailRpm + dt·60, 100)
  rotorPhase += dt·rotorRpm;  tailPhase += dt·tailRpm                                // sprite animation only
  ```
  Note the thrust is `throttle` newtons per substep of accumulated `m_force` (Box2D integrates `force·dt`), the
  torque terms are already multiplied by `dt`; outside the two angle bands no corrective torque is applied.
* `HandleCollision` [verified, M4]: only while on and only when the contact's fixture on the helicopter is the
  main or the tail rotor: two Action 0x12 forces of 80 N split by mass (`80·m_heli/(m_other+m_heli)` on the other
  body along the normal, the rest back on the helicopter; a massless other counts 100 kg) and the thud sound 0x38
  (volume 0.1).
* In legacy level files (version ≤ 6) type 40 was the `RCHelicopterController` and the helicopter's `itemData`
  held its handle; `LoadPlist` converts both to type 36 (02 §5) [verified]. At runtime type 40 is the
  `SelectionArea` helper created by `prepareForNewLevel` → `CreateSelectionAreaObject`: in set-up mode only, a
  static 5.41 × 1.0 m box centred (1.705, −0.5) — the strip *below* the floor — with the `ReturnAreaBound` filter
  (0x100/0xFFFF, 08 §40), i.e. the physical toolbox return area that dynamic items (mask 0x103) collide with; it
  renders nothing and is never saved [verified].

### 42 ZipLine
* Two anchor bodies (`static*`, `NonCollidable` fixtures) at `center` and `ropeEndPos`; trolley `dyn`: polygon
  `(±0.15, ±0.04) (±0.13, −0.13)` `(0.3, —, 100)` `Dynamic` (no damping set in `CreatePhysics` [verified: G3]), on a
  `b2PrismaticJoint` along the line
  (`maxMotorForce 2`, limits = line length); set-up: sel circles 0.12 at (±0.06/0.15) on the anchors and the trolley,
  plus a thin `Rope`-filter box along the line so items cannot be placed across it.
  Attachment (0, −0.17) kind 2 on the trolley (body index 2).
* `ZipLineUtils::UpdatePos/ManipulationEnded`: dragging either anchor re-lays the line (angle flips by π when
  the second anchor is left of the first).

## 3. Generic item logic (`st::GameItemUtils`)

* `SetInitialState(type)` — per-type reset at simulation start (`BookUtils`, `DartUtils`, `HelicopterUtils`,
  `ScissorsUtils`, `TrapdoorUtils`, `TruckUtils`).
* `Flip` — toggles `scale.x`, mirrors attachment points, re-creates physics.
* `Break` — piggy bank (only breakable type).
* `AttachSharpObject` — dart sticking (creates a joint between dart and target).
* `GetMagneticCenter` — magnet target point (Dart: tip; PaperPlane: nose; balls: centre).
* `HandleCollisionSounds` — impact sound selection when |normal relative velocity| > 0.5 m/s, volume scaled by
  impact strength; per-type overrides for doll/glove.
* `ManipulationStarted/Ended`, `UpdatePos`, `UpdateAngle`, `SetPos` — set-up-mode drag/rotate with snapping
  (`AttachmentUtils`), constrained positions (`GetConstrainedPos` for ropes/slingshot/zip-line), ghost validation (05).
* `RemoveRelatedItems` — deleting a truck/helicopter/trapdoor also deletes its paired controller/lever
  (`GetRelatedItem` via `itemData`).

## 4. `GameItems.plist` frame index (for importers)

```
0 8ball 1 Balloon 2-5 BalloonPop1-4 6 Book 7 BookBlue 8 BookGreen 9 BookRed 10 BookYellow 11 BouncyBall
12 BowlingBall 13 BoxingGlove 14 BoxingGloveAttachment 15 BoxingGloveButton 16 BoxingGloveButtonSupport
17 BoxingGloveHingeBig 18 BoxingGloveHingeSmall 19-22 BoxingGlovePlateNE/NW/SE/SW 23 BoxingGlovePlunger
24 BoxingGloveSpring 25 BoxingGloveStand 26 Bucket 27 BucketHandle 28 BumperOff 29 BumperOn
30 CardboardBoxMedium 31 CardboardBoxSmall 32 Coins 33 Dart 34 DollBackArm 35 DollBackLeg 36 DollBody
37 DollDress 38 DollFrontArm 39 DollFrontLeg 40 DollHead 41 Fish 42 FishBowl 43 FishBowlHighlight 44 FlipGizmo
45 Helicopter 46-55 HelicopterRotor01-10 56 HelicopterTailRotor 57 Hook 58 InvalidSelection 59 LampShade
60 LaundryBasketBack 61 LaundryBasketFront 62 Magnet 63-68 Magnet01-06 69-72 POW1-4 73 PaperPlane
74 PiggyBank 75-77 PiggyBankPiece1-3 78 Pinball 79 Pipe 80 Pipe90 81 Pipe90Back 82 Pipe90Bracket 83 PipeBack
84 PipeBracketLeft 85 PipeBracketRight 86 PulleyClevis 87 PulleyScrew 88 PulleyWheel 89 RCAntenna
90 RCController 91 RCControllerButton 92 RCHelicopterController 93 RCHitch 94 RCTruck 95 RCTruckWheel
96 RopeDollFront 97 RopeKnot 98 RopeSegment 99 RopeZipLine 100 RotationGizmo_iPhone 101-105 Scissors01-05
106 ScissorsBottom 107 ScissorsTop 108 SeesawArm 109 SeesawFulcrum 110 Shelf 111 ShelfBottom 112 ShelfTop
113 Skateboard 114 SkateboardWheel 115 SlingshotElasticBack 116 SlingshotElasticFront 117 SlingshotFrameBack
118 SlingshotFrameFront 119 SlingshotPocketBack 120 SlingshotPocketFront 121 SoccerBall 122 Sparkle03
123 Spring 124 SpringSeat 125-136 Star01-12 137 StarGlow 138 TennisBall 139 TranslationGizmoBig
140 TrapdoorLeft 141 TrapdoorLever 142 TrapdoorLeverBase 143 TrapdoorRight 144 TrapdoorShelfLeft
145 TrapdoorShelfRight 146 Wave 147 ZipLineAttachment 148 ZipLineSegment 149 ZipLineTrolley
```
