# Rendering: screen layout, draw order, sprite parts and animations

Sources (Android build): `GameApp::GameApp` (screen maths), `selectAssetProfile`,
`UI::View::UpdateViewAnchors`, `st::GameRenderer::Render/RenderWorld`, the per-item renderer `FUN_000bd4f0` and its
helpers (`FUN_000bfde0` per-type loop, `FUN_000c0064` sorted container pass, `FUN_000bc304` knots, `FUN_000bc12c`
flush, `FUN_000bcbc8` scissors, `FUN_000bc904` slingshot, `FUN_000bd0a8` truck, `FUN_000bc478` zip line,
`FUN_000bd3d0` pulley), `st::VisualWorldStateUtils`, the `*Utils::Update/UpdateAnimation` functions. Everything is
**[verified]** from the decompile; `.bss` constants were read from the emulated binary after its static
initialisers (`tools/uc_harness.py`). Frame numbers refer to the `GameItems.plist` index (03 §4). Data addresses in
the Android decompile are the file address + 0x10000 (`DAT_0028e284` = `.bss` 0x27e284).

## 1. Screen layout — the original already handles wide screens

`GameApp::GameApp` computes the play-field mapping once from the native screen size (`w`, `h` in pixels):

```
s      = (h ≤ 677) ? min(h / 677, w / 1024) : 1        // reference play field: 1024 wide, 638 px above the floor
floor  = clamp(h − ceil(638·s), 39, 130)               // floor strip in native px (130 on 4:3 and on every tall window)
H      = 638 + floor                                   // virtual play-field height
PixelScale            = (h / H > 1) ? h / H : s
WorldScaleWithFloor   = max(1024 / w, H / h)           // virtual px per native px
LetterBox             = aspect > 1024 / H              // true on every window wider than the play field
LetterBoxFrameWidth   = LetterBox ? ceil(|1024 − ceil(w·WorldScaleWithFloor)| / 2) : 0     // side frame, virtual px
LetterBoxViewportYOffset = ceil(H) − ceil(h·WorldScaleWithFloor)
AnchorAspectCorrectionFactor = (1 − (aspect − 4/3)/2,  1 − (1/aspect − 3/4)/2)
```

Test vectors (computed from the formulas in float, as `GameApp::GameApp` does with `ceilf`; the resolutions named by
`selectAssetProfile` plus desktop sizes; `tests/test_screen_layout.cpp`):

| w×h | floor | H | PixelScale | WorldScaleWithFloor | LetterBox | frame | y offset |
|---|--:|--:|--:|--:|:--:|--:|--:|
| 480×320 | 39 | 677 | 0.469 | 2.1333 | no | 0 | −6 |
| 800×480 | 39 | 677 | 0.709 | 1.4104 | yes | 53 | 0 |
| 960×540 | 39 | 677 | 0.798 | 1.2537 | yes | 90 | 0 |
| 1024×600 | 39 | 677 | 0.886 | 1.1283 | yes | 66 | 0 |
| 1024×768 | 130 | 768 | 1.0 | 1.0 | no | 0 | 0 |
| 1136×640 | 39 | 677 | 0.945 | 1.0578 | yes | 89 | 0 |
| 1280×720 | 82 | 720 | 1.0 | 1.0 | yes | 128 | 0 |
| 1280×800 | 130 | 768 | 1.042 | 0.96 | yes | 103 | 0 |
| 1920×1080 | 130 | 768 | 1.406 | 0.7111 | yes | 171 | 0 |
| 2048×1536 | 130 | 768 | 2.0 | 0.5 | no | 0 | 0 |
| 2560×1440 | 130 | 768 | 1.875 | 0.5333 | yes | 171 | −1 (float: 1440·0.53333336 = 768.00004, `ceilf` → 769) |
| 2400×1080 (the 20:9 phone / the arm64 AVD, M7) | 130 | 768 | 1.406 | 0.7111 | yes | 342 | 0 |
| 2340×1080 | 130 | 768 | 1.406 | 0.7111 | yes | 320 | 0 |
| 2556×1179 | 130 | 768 | 1.535 | 0.6514 | yes | 321 | 0 |
| 960×640 (iPhone 4) | 41 | 679 | 0.938 | 1.0667 | no | 0 | −4 |

So the world keeps its full **3.41 m width** on every screen; the vertical room is 638 virtual px above a floor strip
that shrinks from 130 px to 39 px on small wide screens (on tall windows the scale stays 1 and the strip is what
is left below 638 px, e.g. 82 px at 1280×720). On wider screens the sides outside the play field are covered by
the letterbox backdrop, not by the world renderer. In the original: `GameView`'s `BorderLeft`/`BorderRight` image
views (`GameScene.json`, sprites `BORDERIMAGE_LEFT/RIGHT` from `BORDER_BORDER.dat/.png`, 400×800; placed at
`x = PixelScale·LetterBoxFrameWidth − width` and `NativeScreenWidth − PixelScale·LetterBoxFrameWidth − 1`; when the
screen is wider than 1200 px both views are scaled by `ScreenHeight / imageHeight` about their top-left pivot and
the extra size `(int)(scale·w − w)`, `(int)(scale·h − h)` is taken off the position [verified: `GameView::Init`]) —
only the `1024X768` and `800X480` asset profiles ship that file (the sheet: LEFT 198×800 at x 0, RIGHT 200×800 at
x 200, a 400×800 PNG); the `2048X1536` profile the remake imports has none, so M7 imports the `1024X768` sheet
resampled ×2 (`tools/import_assets.py --border-profile 1024X768`, 12 §2) into the imported profile, where every
sheet is drawn at PixelScale / 2. On a 16:9 screen the scaled sprite covers its strip (1280×720: 178 of 128 px); on a
20:9 phone (2400×1080: strips of 481 px, sprites of 267) the outer part of each strip is wider than the sprite —
in the original it shows the clear colour, the border was drawn for 16:9 [verified 2026-09-13/14; this corrects
the earlier reading of `LocationForegrounds` as side frames — they are the floor overlays of §3 step 8].

The remake deviates: the borders are not views but a **backdrop drawn under the world** (`drawLetterBoxBackdrop`,
`scene.cpp`, called by `GameScene::draw` / `SandboxScene::draw` before `drawWorld`; the world covers its middle,
so the sprites may be cropped by the world as well as by the screen): first `ScreenLayout::letterBoxFills()` —
native-px rectangles in the colour of the sprites' outer column (28, 99, 158 — uniform top to bottom on both
sheets) over each side strip from the screen edge to the play field and over the bands above and below the world
(rounded outwards to whole pixels; bands under 1 px are float rounding, e.g. 2560×1440, and get nothing) — then
over side strips the two sprites at `ScreenLayout::borderSpriteRect`: always scaled to the screen height (the
original: only past 1200 px), at the original's x (the inner edge on the play field's edge, the screen crops the
outer part); the bands of a squarer screen get the fill alone (the sprites are side art — the corner fragments
and a mirrored / darkened level background were tried and rejected, 2026-09-15). The bands: on a screen squarer than the `1024 × virtualHeight` field (aspect < 4:3 at floor 130 —
foldables, a resized desktop window) the width binds and `LetterBoxViewportYOffset` goes negative (1280×1153:
−155 virtual px, a 194-px band the original — which never met such a screen — leaves under the floor in the clear
colour, the world at the top). The remake centres the world instead: the ortho bottom is
`LetterBoxViewportYOffset / 2` (`ScreenLayout::orthoBottom`), the band on each side is
`ScreenLayout::worldBandNative()` native px, the same shift enters every native ↔ world conversion of `coords.h`
and `worldToTouch` (the original's formulas ignore the offset — 0 on all its screens); the bands show the fill. `GameView`'s pause dim is its first child (the original: right
after the border views) — over the world and the backdrop, under the sidebars. The
world beyond 3.41 m never shows on any width: a wider window only widens the frames.

The world camera (`FUN_000badac`, VFP argument `s0` = `GetPixelToMetersFactor() · FloorHeightInPixels / zoom`):

```
glOrthof(left = 0, right = ceil(WorldScaleWithFloor·w), bottom = LetterBoxViewportYOffset,
         top = ceil(LetterBoxViewportYOffset + h·WorldScaleWithFloor), −100, 100)       // virtual px, y up
MODELVIEW = T(LetterBoxFrameWidth + 512, 319) · S(zoom) · T(−centre) · S(1024/3.41) · T(0, floorM / zoom)
```

`st::Camera` (`WorldState+0x2f290`: `+8/+0xC` centre in virtual px, default (512, 319) = world (1.705, 1.062);
`+0x10` zoom, default 1; `+0x14` edge-scroll timer; `+0x18` corner flag). `CameraUtils::GetClampedCenter` keeps
the centre in `[512/zoom, 1024 − 512/zoom] × [319/zoom, 638 − 319/zoom]` (so at zoom 1 the camera cannot pan at
all); `CameraUtils::Update` scrolls at 300 px/s after 0.4 s in a 100-px edge zone (M3); `ZoomCameraOut` eases the
zoom 1.0 → 0.6 with a cubic (level complete, M4). With the defaults world (0, 0) lands at virtual
`(LetterBoxFrameWidth, floor)` and (3.41, 2.12459) at `(LetterBoxFrameWidth + 1024, floor + 638)`.

UI layouts are the same XML for every profile (01 §8): `Relative` percentages of the parent, and anchored views get
their percentage offset multiplied by `AnchorAspectCorrectionFactor` (x on horizontal anchors, y on vertical ones —
`UI::View::UpdateViewAnchors`), which pulls anchored elements toward the play field on wide screens. Asset profile
(`selectAssetProfile(w, h)`, with the two ints it sets = thumbnail display size / `LevelThumbnails_<n>` set):

| screen | profile | thumbnails |
|---|---|---|
| 480×320 | `480X320` | 52 / 65 |
| 800×480, 854×480, 960×540, and any `w = 1024` with `h ≤ 600` (1024×600 tablets) | `800X480` | 78 / 98 |
| 1136×640, 960×640 (iPhone 5 / 4) | `1024X768` + `DeviceParams::AssetScalingForWidescreen = 1` [verified: `selectAssetProfile`, `h == 640 && (w == 1136 || w == 960)`] — the chapter books at 0.85 (lifted by `(bookH − 0.85·bookH) / 3`), the comic pages at 0.85, the level buttons' panel / thumbs / frames / number at 0.89 with the title gap `0.0272·H·0.89`, the number's pulse 0.89 ↔ 0.979, the press zoom 0.89 ↔ 1.0235, and the buy-full-game button at 0.9 [verified: the `Init` / `Setup` / `AnimateButton` / `ZoomIn` / `ZoomOut` sites] | 104 / 130 |
| 1024×768; `w = 1280` with `h ≤ 800` (1280×720, 1280×800); anything unmatched | `1024X768` | 140 / 175 |
| 2048×1536 | `2048X1536` | 280 / 350 |

Consequence for the remake (10 §6): use the iOS `2048X1536` atlases (with `LevelThumbnails_350`) and the shared XML
layouts with this exact letterbox/anchor maths — the original's own wide-screen behaviour. The remake has one
sprite set scaled by PixelScale, so the `AssetScalingForWidescreen` flag (`ScreenParams::widescreenScaling`, M7)
follows the size-independent reading of the original's two entries — a screen shorter than the 677-px reference
play field, i.e. **`PixelScale < 1`** (true at 1136×640 and 960×640, false at 1024×768, 1280×720 and on every
modern phone; `tests/test_screen_layout.cpp`, `test_ui_layout.cpp`). On such a screen the 768-px-referenced
menus overflow the height by ≈ 768 / 677 = 1.13, which the 0.85–0.89 factors compensate.

## 2. Frame composition — `GameRenderer::Render`

1. `Clear` to (0.85, 0.8, 0.8, 1).
2. World camera (§1). `RenderWorld` (§3).
3. Screen-space ortho (`glOrthof(0, W, 0, H)` in native px, y up): `ToolboxRenderer::Render` when the render
   state's `+0x24` flag is set — `renderFrame` always sets it, the strip hides by sliding off (§7).
4. World camera again. The **held item** (render state `+0x30` = held object index, drawn last so it is on top of
   everything, the strip included; a *fixed* held item was already drawn in the passes and is skipped here): layer
   1 parts, then layer 0 parts (§4), then its attachment knots (§3 step 5). Then, by manipulation state (`+0x2c`)
   [verified: `GameRenderer::Render`, `FUN_000bbb6c` / `FUN_000c0308` / `FUN_000bba20`, GameItems frame table
   stride 20 — the earlier naming was wrong]:
   * **2** = `RotationGizmo_iPhone` (GameItems frame **100**) centred at the selected position (`+0x34`,
     `GameItemUtils::GetSelectedPos`: the object position, the grabbed body for ropes / zip lines), rotated by the
     item angle + the gizmo phase (`+0x3c`), and — when the object's flag bit 3 (flippable) is set — `FlipGizmo`
     (frame **44**) at position + 0.4 · (cos π/4, sin π/4) (the constant `DAT_0028e174`, unrotated);
   * **3** = `InvalidSelection` (frame **58**) at the selected position, rotated by the buzz angle (`+0x4c`), in
     the current (white) colour;
   * **1** = `TranslationGizmoBig` (frame **139**) rotated by the gizmo phase and scaled by the manipulation
     animation (`+0x40`).
   Gizmo colours by the ghost byte (`+0x44`): (0.6431, 0.7843, 0.9333, 1) normally, (0.8784, 0.2667, 0, 1) in
   ghost (`.bss 0x275324` / `0x275314`). The quads are `FUN_000bb8a0` rectangles of ±(w/2, h/2) · k around the
   origin of the pushed matrix.
5. `FUN_000c0440`: the tutorial hand's ghost items and the level-complete effects (confetti/sparkle particle
   systems, `+0x64f8` resources) and the two fade/flash overlays with `glColor4f` — M5.

The **fixed-item flash** lives in the per-item draw (`FUN_000bc12c`) [verified: `FUN_000bbcb4` disassembly]:
while the buzz amplitude (`+0x48`, `GameScreenTransitions+0x48`, 05 §5) is above 0.0001, every *fixed* item is
drawn with `GL_COMBINE`: `RGB = INTERPOLATE(TEXTURE, PRIMARY_COLOR, CONSTANT)` with the env colour
(1, 1, 1, amplitude) and the primary colour (1, 0.31, 0.122) — i.e. `texture·a + flash·(1 − a)` since
`OPERAND2_RGB` stays at its `GL_SRC_ALPHA` default — and `ALPHA = REPLACE(TEXTURE)`. The remake reproduces the
formula in a fragment shader (`mix(vec3(1.0, 0.31, 0.122), tex.rgb, a)`, alpha = `tex.a`) around the fixed items'
local parts.

## 3. World pass order — `GameRenderer::RenderWorld`

1. Background `LocationBackground0N` (1024×1024 texture, one 1024×768 frame) drawn as a 3.41 m wide rectangle
   `[0, 3.41] × [0, k·768]` with `k = 3.41 / textureWidth` under `glTranslatef(0, −0.432, 0)` (`0xbedd2f1b`), i.e.
   the frame spans y ∈ [−0.432, 2.1255] — the 130-px floor strip lies below the world origin. The second copy
   (`GameResources+0x64bc`, shifted by ±3.41 m by `renderState+0x14 · +0x10`) is the *previous* background
   during the sandbox background slide, not camera panning (at zoom 1 the camera cannot pan, §1). Textures of
   this page use `GL_NEAREST`, every other page `GL_LINEAR`, all `CLAMP_TO_EDGE`.
2. Goal markers (`FUN_000baec4`, `VisualWorldState` markers from `GameItems2`: goal_arrow / goal_circle / goal_cross
   per goal type, 02 §5) — drawn here, *behind* the items, when `VisualWorldState+0xc` is set (otherwise in step 9).
3. **Back layer**, per type in this order, each type's instances in collection order (`GetStartOfType`, i.e. the
   order they were added): Billboard 24, Pulley 21, Shelf 1, ZipLine 42, Pipe 17, Pipe90 18, LaundryBasket 32,
   Bucket 7, Scissors 6, Slingshot 34, RCTruck 35. For every item except the held one (a held *fixed* item is
   still drawn here and not in the §2 overlay): its **layer-1 parts**
   (§4), then its attachment knots (step 5 rule) because these types are flagged `DAT_0028db74[type] = 1`.
4. **Main layer**, per type: Hook 8, BoxingGlove 14, Rope 9, Scissors 6, TrapdoorLever 38, Balloon 5, BouncyBall 41,
   TennisBall 2, BowlingBall 3, SoccerBall 4, EightBall 16, Pinball 26, Book 15, ZipLine 42, RCController 36,
   Bumper 33, Spring 28, Dart 29, Skateboard 20, FishBowl 12, Helicopter 39, PiggyBank 13, Doll 19, PaperPlane 27,
   Slingshot 34, Magnet 25, Pulley 21, Trapdoor 37, Seesaw 22, CardboardBoxMedium 10, CardboardBoxSmall 11,
   GoalStar 23, Pipe 17, Pipe90 18, Shelf 1. Each item: **layer-0 parts**, then knots unless the type was in step 3.
5. Knots (`FUN_000bc304`): for every *connected* attachment point (record `+0x13` ≠ 0) whose kind is not 8 (pipe),
   frame 97 `RopeKnot` at the point's local position + (0, −0.03) (+ the offset of the attached body relative to
   body 0 when the point belongs to another body); skipped for Rope 9, Doll 19 and ZipLine 42.
6. **Front layer** (`FUN_000c0064`): HangingLamp 30, Bucket 7, RCTruck 35, LaundryBasket 32 — the drawable
   instances of each type (collection order) sorted by `VisibilitySortingUtils::GetVisibilityOrder` [verified]:
   an explicit-stack quicksort over the index array whose partition step takes the first index of the range as
   the pivot and, scanning the rest in order, moves every item `j` with `up_p · (pos_j − pos_p) > 0` (`up_p =
   Rotate(angle_p, (0, ±1))`, −1 for the lamp: it hangs) *in front of* the pivot, keeping the scan order; both
   sides are then sorted the same way (left part first when it has ≥ 2 elements). Items above a container in its
   own frame are therefore drawn before it, so a bucket drawn after the things stacked in it covers their
   bottoms. Then layer-0 parts (lamp shade, bucket front, truck body/wheels, basket front) + knots for the lamp.
7. RC signal waves (§5) for every RCController 36 and RCTruck 35.
8. **Floor overlays** from `LocationForegrounds` (frames 0 `LocationForeground00` 1024×157 grass, 1
   `LocationForeground01` 1024×147 shelf edge, 2 `LocationForeground03` 1024×165 planks, 3 `LocationForeground03_2`
   177×69 bush), drawn *over* the items under the same `translate(0, −0.432)` as the background: the "right" table
   (`DAT_00243c14` enable = [1, 1, 0, 1], `DAT_00243c18` frame = [0, 1, −1, 2] per `backgroundIndex`) places the
   frame as `[0, k·w] × [0, k·h]` — Classroom grass up to y = 0.091, Backyard edge up to 0.058, Room none,
   Treehouse planks up to 0.118; the "left" table (`DAT_00243c00` = [0, 0, 0, 1], `DAT_00243c04` = [−1, −1, −1, 3])
   puts the Treehouse bush at `bottom = textureHeight − k·h` ≈ 1024 m — off screen, dead art. The second set of
   copies (±3.41 m) belongs to the sandbox background slide like the background's. These strips are what the
   items sink into at the floor line (a ball on the floor shows behind the grass) [verified; the earlier "side
   frames" reading of this step was wrong — §1].
9. `VisualWorldState` goal markers (`SetGoalMarkers`, record 0x1c bytes: active, x, y, timer, angle,
   `frameStep` (−1 = not shown yet), kind), skipped while `frameStep < 0`; `UpdateGoals` starts the appear
   animation 0.7 s after load, one step per 1/15 s with a 0.4 s stagger, and stops at step 4 (M4). Per goal target
   (`itemHandles[i]`, or `itemHandles2[i]` for goal type 7): kind **6** (cross) for goal type 5, else kind **1**
   (circle), at the object's position. Plus one marker for the goal itself at `(clamp(width, 0, 3.41),
   clamp(height, 0, 2.12459))`: types 2 and 7 → kind **7** (arrow) at angle `angle·DegToRad − π/8`; type 3 → kind
   **8** at the *unclamped* (width, height); 6 → kind **5** angle π; 8 → kind **4** angle 0; 9 → kind **3** angle
   π/2; 10 → kind **2** angle 3π/2 (`0x40490fd8`-based constants); 4 and 5 add none. Frames of `GameItems2` for
   step n: kind 1 → `15 + n` (`goal_circle_1..5`), 6 → `20 + n` (`goal_cross`), 7 → `n` (`goal_arrow`, drawn with
   `AddQuadWithAnchorPointWithRotationAndMirror`: V flipped when π/2 < angle ≤ 3π/2), kinds 2/3/4/5 → `n + 6`
   for n ≤ 3 and 11 for n = 4 (`goal_arrow_down_1..4a, 5a`), kind 8 → `n + 6` / 13 (`..5b`). Anchor = frame
   centre; the side arrows are offset by half the frame **height** (× k): kind 2 +x, 3 −x, 5 −y, 4 and 8 +y.
   Then `FUN_000baec4` (the `_5_white` pulse overlay scaled by `VWS+0x18`) if `+0xc` is clear (markers in front
   of the items). The shipped thumbnails show no markers (they are crops of the set-up view before the
   animation).
10. Two vertex-coloured batches from `VisualWorldState` with blending mode 2 (additive): the sparkle geometry
    (`SparkleEffectUtils::GetGeometry`, `+0x237d0`) and a second effect batch (`+0xb388`).

Within an item all parts are accumulated into one batch in **item-local coordinates** and flushed by `FUN_000bc12c`
with `translate(pos) · rotate(angle · 57.29579) · scale(scale.x, scale.y) · translate(partOffset)` (flip =
`scale.x = −1`, so every part mirrors with the item; `partOffset` is (−0.2, 0) for the boxing glove, (0, 0)
otherwise). Exceptions drawn directly in world coordinates: the rope strip and the zip-line line (`FUN_000bbd84`,
`glDrawElements` under the camera matrix). The flush applies the **ghost tint** `glColor4f(0.6, 0.4, 0.4, 0.4)` when item state bit 1 is set
(05 §5), and for a *fixed* item (flags bit 2) whose buzz interpolator is > 0.0001 (05 §7, action 0xE) a
`GL_COMBINE`/`INTERPOLATE` pass that blends the texture toward the orange `(1.0, 0.31, 0.122)` by the interpolator
value (the "you cannot move this" flash).

### Quad primitives (`st::SpriteRenderer`, decoded from the disassembly)

All sizes are frame pixels × `k` (`renderState+0x10` = `GetPixelToMetersFactor()` = 3.41/1024): a w×h frame is
`w·0.00333 × h·0.00333` m. No pixel trim, no rotated/offset/trimmed frames in the atlases. Texture y down maps to
world y up (the top texture row is the top world edge).

| Primitive | Geometry |
|---|---|
| `AddQuadCenteredAt(frame, pos, page, k)` / `WithScale(…, scale, k, roundToPixels)` | half-size `w·0.5·k·scale.x × h·0.5·k·scale.y` around `pos` |
| `AddQuadWithAnchorPoint[WithRotation/WithScale](frame, anchorPx, pos, angle, scale, page, k)` | corners `(−ax, −ay)`, `(w−ax, −ay)`, `(−ax, h−ay)`, `(w−ax, h−ay)` (px from the frame's bottom-left) × k × scale, rotated by `angle` about the anchor, translated to `pos`; a negative scale mirrors the quad (the texture itself is never flipped) |
| `…WithRotationAndMirror` | as above; V flipped when `π/2 < angle ≤ 3π/2` and the mirror flag is set (goal arrow) |
| `AddQuadCenteredAtWithSrcRect(frame, pos, rect {yBottom, yTop, x0, x1}, page, k)` | a sub-rectangle of the frame (px) centred at `pos` (compressed spring) |
| `FUN_000bc080(frame, pos, scale, page; s0 = angle, s1 = k)` | the per-part helper: anchor = frame centre, i.e. a centred quad with rotation |
| `FUN_000bcec4(frame, pos, scale, page; s0 = angle)` / `FUN_000bd01c(…; no angle)` | anchor `(w/2, 0)` = bottom centre |
| `FUN_000bcf58(frame, pos, scale, page; s0 = length)` | bottom-centre anchor, `scale.y = length / (k·h)` — stretched to `length` metres |
| `FUN_000bac18(from, to, frame, page, rs)` | anchor `(0, h/2)`, rotation `atan2(to − from)`, `scale.x = |to − from| / (k·w)` — stretched between two points (slingshot elastic) |
| `FUN_000bbd84(count, points, …, frame)` (`RopeRenderUtils`) | strip: per consecutive point pair a quad `p0 ± n0·hw`, `p1 ± n1·hw` with `hw = w·0.5·k`, left normals (`CalculateNormals`, (−0, 1) when the segment is shorter than 1e-4, the last point repeats the previous normal), U across the frame, V from `(y1 − 0.5)/texH` at p0 to `(y0 + 0.5)/texH` at p1; `AddIndices` emits a segment only while the two link bodies still share a joint (cut ropes) |

The local functions receive their float arguments in VFP registers (`s0`, `s1`) although the exported API is
softfp — the decompile shows them as `in_s0`, which is why the rotations below were read from the disassembly.

## 4. Sprite parts per item type

Body-local positions in metres, listed in draw order; `r` = template half-size (04 §4), `s` = flip sign, `b[k]` =
`b2MulT(R0, p_k − p_0)` = position of body k in body 0's frame, `∠k` = (angle of body k − item angle) **· s** for
parts placed with `FUN_000bc080` (the flip mirrors the local frame, so the rotation flips too); `∠k` without `s`
where the exported `AddQuadWithAnchorPointWithRotation` is called directly (seesaw arm, trapdoor doors, lever).
"Layer 1" parts are drawn in the back pass (step 3), "layer 0" in the main pass (or the front pass for the four
container types). Single-sprite items (layer 0 only): Shelf 112 `ShelfTop`; TennisBall 138; BowlingBall 12;
SoccerBall 121; EightBall 0; Pinball 78; BouncyBall 11; CardboardBoxMedium 30; CardboardBoxSmall 31; Hook 57;
PaperPlane 73; HangingLamp 59 `LampShade` (front pass); LaundryBasket front 61 (front pass, back 60 in layer 1);
Bucket 26 (front pass, handle 27 in layer 1 at (−0.01, r)); Pipe 79 (layer 1: 84 `PipeBracketLeft` (−0.16, 0),
85 `PipeBracketRight` (0.17, 0), 83 `PipeBack`); Pipe90 80 (layer 1: 81 `Pipe90Back`, 82 `Pipe90Bracket`);
Shelf layer 1: 111 `ShelfBottom` at (0, −0.2·r); Billboard: layer 1 only, 110 `Shelf` or 6 `Book` by `itemData & 0xF`
(03 §24). WorldBound 31 and SelectionArea 40 draw nothing.

| Type | Parts in draw order (layer 0 unless noted) |
|---|---|
| 5 Balloon | one frame at (0, −0.07): 1 `Balloon`, or while popping `2 + clamp(int((0.15 − t)/0.15·4), 0, 3)` (§5) |
| 6 Scissors | layer 1: 106 `ScissorsBottom` at `Rotate(−angle, p_1 − pos)` (`GetBottomHalfPos` = body 1's world position, brought into the item frame); layer 0: 107 `ScissorsTop` at `Rotate(−angle, p_0 − pos)` (body 0); the halves are open by the cut angle `Scissors+0xC` (15° by default, 03 §6); if a cut is in progress (`Scissors+0x10 ≥ 0`): frame `101 + index` (`Scissors01..05`, anchor (23, 13) px, rotated by the cut direction `+0xC`) while `+0x18` = 0, else 122 `Sparkle03` at (r − 0.005, −0.01) scaled by `+0x20` |
| 9 Rope | `RopeRenderUtils`: a triangle strip of 98 `RopeSegment` along the link bodies (`CalculateBodyIndices/AddIndices/AddVertices`), knots via step 5 are skipped |
| 12 FishBowl | 42 `FishBowl` (0, 0); 41 `Fish` (0, −0.02); 43 `FishBowlHighlight` (0.09, 0.01) |
| 13 PiggyBank | intact: 74 `PiggyBank`; broken (state bit 0): 32 `Coins` at the raw world delta `p_1 − p_0`, then 75/76/77 `PiggyBankPiece1..3` at `p_k − p_0` (k = 2..4, world deltas, not rotated into the item frame) rotated by the raw body angle (anchor = frame centre), then for 0.2 s 69–72 `POW1..4` at (0, 0) (§5) |
| 14 BoxingGlove (flush offset (−0.2, 0)) [verified from the disassembly] | `arm = (b[1].x·scale.x + 0.2, b[1].y·scale.y)`: 14 `BoxingGloveAttachment` centred at (arm.x − 0.16, arm.y); 13 `BoxingGlove` at (arm.x − 0.03, arm.y); lattice from the hinge angle `θ = BoxingGlove+0x24` (03 §14): `lx = sin θ·0.2973·0.5`, `c = 0.01 + cos θ·0.2973`, `ly = (0.03 + c·0.5) − 0.2`, `hy = (float)((double)ly + 0.03)`; bottom-anchored plates (`FUN_000bcec4`, anchor (w/2, 0)): 19 `PlateNE` at (lx, hy) rotated −θ and again rotated θ − Pi, the same at (3lx, hy); 20 `PlateNW` at (lx, hy), (3lx, hy), (5lx, hy), each rotated θ and Pi − θ (the Pi ∓ θ copies point down: an X per hinge); 25 `Stand` bottom-anchored at (0, −0.2); 24 `Spring` stretched (`FUN_000bcf58`) from (0, −0.12) to length `c − 0.04`; 23 `Plunger` stretched from (0, c − 0.125) to length `0.35 − c`; 15 `Button` stretched from (0, 0.2) to `k · BoxingGlove+0x20` (36 px at rest, the `Start(36 → 8, 0.06 s)` interpolator squashes it after a hit — so `+0x20` *is* read, correcting 03 §14); 16 `ButtonSupport` bottom-anchored (0, 0.19); 18 `HingeSmall` ×6 bottom-anchored at (0, −0.17), (0, c − 0.17), (2lx, −0.17), (2lx, c − 0.17), (4lx, −0.17), (4lx, c − 0.17); 17 `HingeBig` ×3 at (lx, ly), (3lx, ly), (5lx, ly). None of these positions is multiplied by the scale (the flush mirrors them) |
| 15 Book | `7 + colour` (7 Blue, 8 Green, 9 Red, 10 Yellow; 8 when the item has no state) |
| 19 Doll | limbs at `(b[k].x·scale.x, b[k].y·scale.y)` rotated by ∠k: 34 `DollBackArm` at b[3]; 35 `DollBackLeg` at b[5]; 36 `DollBody` (0, 0); 39 `DollFrontLeg` at b[4]; 37 `DollDress` (0, 0); if attachment 0 is connected (record state ≠ 0): 96 `RopeDollFront` at (0.01, −0.04); 38 `DollFrontArm` at b[2]; 40 `DollHead` at b[1] |
| 20 Skateboard | 113 `Skateboard` (0, 0); 114 `SkateboardWheel` at b[1] and b[2] (rotated by ∠k) |
| 21 Pulley | layer 1: 87 `PulleyScrew` (0, 0.152); layer 0: 88 `PulleyWheel` centred (0, 0), not rotated; 86 `PulleyClevis` bottom-anchored at (0, −0.036) |
| 22 Seesaw | 108 `SeesawArm` is the **right half** of the arm: `AddQuadWithAnchorPointWithRotation` with anchor = b[1] passed *as pixels* (the arm body sits on the fulcrum, so ≈ (0, 0) px = the frame's bottom-left corner), position (0, −0.02), rotation ∠1 (no `s`), scale (1, 1); drawn again with scale (−1, 1) = the left half; 109 `SeesawFulcrum` (0, 0) |
| 23 GoalStar | 137 `StarGlow` (0, 0); `125 + spin` (`Star01..12`, §5) |
| 25 Magnet | 62 `Magnet`; if attracting (`GameItem+8`): `63 + pulse` (`Magnet01..06`, §5) at (0, 0.19) |
| 28 Spring | seat point = `MulT(R0, p_2 + R_2·(0, 0.015) − p_0)`, base point = `MulT(R0, p_1 + R_1·(0, −0.015) − p_0)`, `L = |seat − base| / k` px: when `L ≤ h` (69 px) 123 `Spring` is drawn centred at (0, 0) with the source rect `[x0, x1] × [cy − L/2, cy + L/2]` (the middle band of the frame), else centred at (0, 0) scaled (1, L/h); 124 `SpringSeat` centred at the seat point; 124 again at the base point with scale (1, −1). No body rotation is applied to the parts (the item angle rotates them all) |
| 29 Dart | 33 `Dart` (0, 0.007); when stuck (`GameItem+8`): 122 `Sparkle03` at (r, −0.002) scaled by `GameItem+0x10` |
| 33 Bumper | 28 `BumperOff` or 29 `BumperOn` (`GameItem+8`) |
| 34 Slingshot | `pouch` = `Slingshot+0xC/+0x10` (item-local, unflipped: the flush mirrors it), `dir = (float)atan2(0.076 − pouch.y, 0 − pouch.x)` (`.bss` 0x27db4c/50); layer 1: 115 `SlingshotElasticBack` stretched (`FUN_000bac18`) from `pouch + Rotate(dir, (0.02, 0.03))` to the frame point (0.04, 0.091) (`.bss` 0x27db54/58 + (0, 0.01), 0x27db5c/60); 117 `SlingshotFrameBack` centred (0.04, 0.08); 119 `SlingshotPocketBack` centred at `pouch + Rotate(dir, (0.02, 0.02))` rotated by `dir`; layer 0: 116 `SlingshotElasticFront` stretched from the pouch to (−0.02, 0.056) (0x27db64/68); 120 `SlingshotPocketFront` at the pouch rotated by `dir`; 118 `SlingshotFrameFront` (0, 0) |
| 35 RCTruck | layer 1: 93 `RCHitch` (−0.354, −0.005); front pass: if the built-in controller exists (`Truck+8`, a 4th body created by `CreatePhysics` — legacy layout): 91 `RCControllerButton` at b[3] + (0, 0.072) (`.bss` 0x27db6c/70) and 90 `RCController` at b[3] + (0, 0.15); 89 `RCAntenna` bottom-anchored at (−0.26, 0.1); 94 `RCTruck` (0, 0.04); 95 `RCTruckWheel` at b[1] and b[2] rotated by ∠k |
| 36 RCController | 91 `RCControllerButton` at b[1]; base at (0, 0.15): 90 `RCController` when the paired type (`Controller+8 >> 26`) is 35, 92 `RCHelicopterController` when it is 39 (any other value selects frame 150, which does not exist) |
| 37 Trapdoor | if the built-in lever exists (`Trapdoor+8`, legacy): 141 `TrapdoorLever` bottom-anchored at b[3] − (0.01, 0) and 142 `TrapdoorLeverBase` at b[3]; 140 `TrapdoorLeft` (64×23) at b[1] rotated by ∠1 (no `s`) with anchor (3 px, 0.6·h = 13.8 px) and 143 `TrapdoorRight` (66×23) at b[2] rotated by ∠2 with anchor (w − 4 = 62 px, 0.6·h) — the door hinges (frame table entries `+0xaf4`/`+0xb30` = frames 140/143); 144 `TrapdoorShelfLeft` (−0.8·r, 0); 145 `TrapdoorShelfRight` (0.8·r, 0) |
| 38 TrapdoorLever | 141 `TrapdoorLever` bottom-anchored at (−0.01, 0) rotated by ∠1 (no `s`); 142 `TrapdoorLeverBase` (0, 0) |
| 39 Helicopter | if the built-in controller exists (`Helicopter+8`: `CreatePhysics` then adds a controller body at (−0.5, −0.8·r) — legacy layout): 91 at b[1] + (0, 0.072), 92 at b[1] + (0, 0.15); 56 `HelicopterTailRotor` centred at (0.9·r, 0.25·r·0.6) rotated by `tailPhase` (`Helicopter+0x28`, raw); 45 `Helicopter` (0, 0); rotor frame `46 + (int(rotorPhase·10) mod 10)` (`HelicopterRotor01..10`, `+0x20`) at (−0.34·r, 0.51·r) |
| 42 ZipLine | layer 1: the line as a `RopeRenderUtils` strip of 148 `ZipLineSegment` (world coordinates) from the item position along the end vector `e`: `n = int(|e| / (k · 21 px) + 1)` pieces of `|e| / n`, points `pos + i·step·ê` and `pos + e`, skipped when `|e| < 1e-4`; layer 0 with `a1` = body 1's angle and `a2` = the trolley's (raw, the item angle of a zip line is 0): 147 `ZipLineAttachment` at `Rotate(a1, (−0.06, 0))` rotated `a1`; again at `e + Rotate(a1, (0.06, 0))` rotated `a1` with scale (−1, 1); 149 `ZipLineTrolley` at `b[2] + Rotate(a2, (0, −0.044))` rotated `a2` (b[2] goes through `Rotate(a1, Rotate(−a1, ·))` first — a no-op); if something hangs on the trolley (`ZipLine+0x4c`): 99 `RopeZipLine` at `b[2] + Rotate(a2, (0, −0.13))` rotated `a2` |


## 5. Animation constants (state kept outside `WorldState`)

| Animation | Where the state lives | Rule |
|---|---|---|
| Star spin | `VisualWorldState` (`+0x2238c`, 4 entries {handle, timer, frame}) | frame advances every **1/12 s** through `Star01..12`; on first sight each star gets a random start frame (`lrand48 % 12`) and a random phase (`lrand48 · 2⁻³¹ / 12`) — `VisualWorldStateUtils::UpdateStars` |
| Star collected | `GoalState` entry `{handle, index, state, t}` | `GoalStarUtils::Update`: state 1 for 0.4 s, scale = curve(t / 0.4) with points (0, 1) (0.1, 1.4) (0.25, 1.6) (0.45, 1.2) (0.7, 0.5) (1, 0) (`.bss` 0x27e284, `CurveUtils::GetValueAt`, 6 points), sparkle effect started at the star, sound `0x3d + collectedCount` (05 §7 id 0xD); at 0.4 s → action 7 (remove), state 2 |
| Balloon pop | `Balloon+8` popped, `+0xC` timer = 0.15 | frames 2–5 by `(0.15 − t)/0.15·4`; physics destroyed immediately, item removed (action 7) when the timer runs out; sound 0x1F, radial force action 0x13 (0.5 m, 25) |
| Piggy bank | `PiggyBank+8` timer | after `Break` the timer runs to 0.2 s: `POW1..4` frame = `69 + clamp(int(t/0.2·4), 0, 3)` for 0.2 s; pieces stay |
| Magnet pulse | `Magnet+0x14` timer, `+0x18` frame | while something is attracted: frame 0–5 advances every **1/30 s** (`Magnet01..06`) |
| Helicopter rotors | `Helicopter+0x20 rotorPhase`, `+0x28 tailPhase` (03 §39) | rotor frame = `int(rotorPhase·10) mod 10`; tail rotor sprite rotated by `tailPhase` |
| RC waves | `RadioController` (stride 0x50, up to 3 waves {x, scale, alpha, age}) / `Truck` (stride 0x4c) | `UpdateAnimation` [verified: decompile + disassembly, M4]: while the button is pressed (the truck follows its paired controller's button) a wave spawns when the newest is ≥ 1/3 s old (max 3); each wave: `age += dt`; controller: `scale = 0.1 + 1.1·age`, `alpha = 1 − age`, `x += 0.12·dt` from 0.01 (outward); truck: `scale = 1.2 − 1.1·age`, **`alpha = age`, `x −= 0.12·dt` from 0.13 (inward — the received signal)**, its waves only advance while the oldest is < 1 s old; dropped when `age ≥ 1`. Drawn (§3 step 7) as frame 146 `Wave` twice, mirrored, at antenna ± x, scaled (scale, 0.34) for the controller / (scale, 0.29) for the truck, colour = alpha |
| Scissors | `Scissors+8` state, `+0xC` cut angle | state 1 (triggered): the cut angle decreases by `2·dt` per step; at 0 the blade area is queried (`QueryAABB`) and every rope link inside is cut (03 §6). The `Scissors01..05` sprites are **not** a cut piece but the idle snip (`FUN_000e4314`, shared by `Update` and `UpdateSetUpMode` — it runs in set-up too): a timer `+0x14` (seeded 3–10 s) steps `+0x10` through frames 101–105 every 0.04 s, then a 0.5 s "snip" with `Sparkle03` at the tip scaled by a phase that rises from 0.2 by `4·dt` and falls back, and a spin angle; then the timer is re-seeded from `Random(3, 10)`. Frames are drawn anchored at (23, 13) px on the top half, rotated by the cut angle. The dart's idle wobble (`Dart+0x14..`) is the same machine without sprites [verified: disassembly] |
| Bumper | `Bumper+8` on, `+0xC` timer | frame 29 while on; `HandleCollision` sets on + 0.18 s, `Update` counts the timer down; the impulse is in 03 §33 |
| Boxing glove | `BoxingGlove+0x14..` interpolator, `+0x24` lattice angle | button height 36 → 8 px over 0.06 s after the trigger (03 §14); the lattice angle from `UpdateArmGeometry` every substep |
| Goal markers | `VisualWorldState` markers `{stepTimer, targetIndex, …}` | `UpdateGoals` [verified: decompile]: only while the touch is idle (else `HideGoalMarkers`); a marker appears when its predecessor is ≥ 0.4 s old, its frame steps every 1/15 s and stops at step 4 (0.7 s). A touch zeroes the marker counts and timers (`doFrame`); the set-up tail re-lays them with `SetGoalMarkers` (the animation replays) once no marker has been shown for 5 s (`GameScreenController+0xC9984`, campaign / test play) [verified: decompile, M4] |
| Sparkles / confetti | `SparkleEffect`, `LevelCompletedEffect` (outside `WorldState`) | trigger points only: sparkles at a collected star, confetti at the goal marker on completion — approximate in the remake (10 §1) |
| Ghost / fixed flash | item state bit 1 / `GameRenderState` interpolator | §3 flush rules |

## 6. Consequences for the remake

* Draw order is data: two type tables + the sorted front pass; the core's `renderState()` (10 §5.4) must expose
  items grouped by type in collection order plus the per-item body offsets and angles the parts need.
* The five per-item animation timers above live in `GameItem` state and are advanced in the fixed step (they are
  part of `WorldState` in the original: balloon, piggy, magnet, helicopter, scissors, RC waves); only the star spin,
  the collect curve, sparkles and the confetti are outside it and may be re-created approximately.
* Ghost tint, fixed-item flash, knots, waves and the floor overlays are cheap to reproduce exactly; the particle
  systems (sparkles, confetti) are not documented beyond their trigger points and stay approximate (10 §1).

## 7. The toolbox strip — `ToolboxRenderer::Render` [verified: decompile + `renderBackground`, `spriteIdFromAmount`]

Drawn in native px (y up) with the modelview translated to the strip button's centre (`Toolbox+8`, `+4`); every
sprite comes from the `UIElements` atlas at its frame size (the 2048X1536 profile drawn at the profile's
`PixelScale`; the remake scales by `ScreenLayout::pixelScale / 2`):

1. `renderBackground`: `toolbox_slide` (frame 75, 43 × 136) tiled to the left of the button — `ejectLength /
   (0.9 · w)` tiles of the slide's middle 90 % (source rect `cx ± 0.45 w`, full height), centres at
   `−0.45 w − i · 0.9 w`, then the fractional remainder as a narrower tile ending at `−ejectLength`; the end cap
   `toolbox_slide_end` (frame 76, 67 × 140) shows 95 % of its width (from its left edge) centred at
   `−(ejectLength + 0.475 · w)`.
2. Scissored to `[x − ejectLength, x] × [0, H]`: for every slot from `floor(screenToUniform(scroll))` to
   `ceil(screenToUniform(ejectLength + scroll))`, the type's `Button<Item>` icon (`ItemInfos+0x10`, table
   `aa::sim::kToolboxIconFrame`) centred at `GetCenterForSlot` = `(uniformToScreen(slot + 0.5) − ejectLength −
   scroll, 0)` at scale 1 (the slot's appear scale is never used — `ToolboxAnimationUtils::Display` has no caller),
   and for `amount > 1` the counter `toolbox_<amount>` (`spriteIdFromAmount`, frames for 2..32) at
   `(cx − w_icon/2 − 0.3 · w_counter, 0.43 · h_slide − h_counter/2)`.
3. The button `toolbox_button` (frame 71) and `toolbox_button_icon` (72) at the origin, both scaled by
   `Toolbox+0x1c` (the press animation, 1 → 1.2). `toolbox_button_left/right` are unused here.

## 8. Remake implementation status (M7)

M5 added the UI on top of the world: `aa_ui` draws through `aa_platform::UiRenderer` (rlgl quads, the §1 native
px with y down, one texture per UI sheet / thumbnail, scissor clipping) after `WorldRenderer::draw` of the game
scene (the world between the clear and the HUD, `GameScene::drawWorld`). `WorldRenderer::drawTutorialGhosts`
draws `RenderState::tutorialGhosts` — the tutorial hand's Shelf / Book copies (`FUN_000c0440`'s modes 2 / 3,
both layers, `glColor4f(0.5, 0.5, 0.5, 0.5)`; every other type draws nothing [verified]) after the markers and
before the particles; the hand itself is a UI view (`GameTutorialView`, 05 §5). M7 added the original letterbox
side frames: `import_assets.py --border-profile 1024X768` resamples `BORDER_BORDER` for the 2048 profile and
`GameView` places it using the §1 widescreen rules, with `BackgroundColor` (0, 0, 150) as the fallback when the
sheet is absent. The stopwatch / toolbar transitions of `GameScreenTransitionsUtils` are not drawn (no view of
the shipped scenes shows them). Still not drawn: the `_white` marker pulse and `RopeZipLine` on a loaded trolley.
`--viewer --screenshots` stays pixel-identical to the M4 set (the viewer runs no tutorial:
`setTutorialContext` is never called, so `tutorial_should_run` sees location −1).

### M4 status (2026-09-14)

M4 added the simulation view: `Session::renderState()` in state 4 reads the lerped render copy (dynamic objects)
and fills the §5 animation fields of `RenderItem` (`popped/popTimer`, `piggyTimer`, `magnetPulling/magnetFrame`,
`rotorPhase/tailPhase`, `bumperOn`, `snipStep/snipping/snipPhase/cutAngle`, `starFrame`, `waves`) plus
`RenderState::particles` / `completed`; `render/items.cpp` draws the balloon pop frames, the piggy POW frames
over the debris pieces, the magnet pulse, the rotor / tail phases, the bumper frame, the idle snip with its
sparkle, the star spin (`VisualState::updateStars`, seeded from the game `Random` instead of `lrand48`), the RC
waves of both items and the particle list (`Sparkle03` tinted; sparkles at a star, confetti at completion —
approximate). `--screenshots` (set-up) stays pixel-identical to the M3 set. Still not drawn: the `_white` marker
pulse, `RopeZipLine` on a loaded trolley, the tutorial hand, the letterbox borders, the stopwatch / toolbar UI
(M5 — see above).

### M3 status (2026-09-14)

`core/platform` implements §1–§4 and §7 for the set-up view: `ScreenLayout` (§1 maths,
`tests/test_screen_layout.cpp`, now in `core/sim`), `Atlas`/`AtlasSet` (raylib textures + frame tables, the
`UIElements` page included), `SpriteBatch` (the quad primitives above on rlgl), `WorldRenderer` (camera,
background, the three passes with the decoded type tables and the visibility sort, floor overlays, goal markers,
the fixed-item flash shader, then the strip, the held item and the overlay in the §2 order),
`ToolboxRenderer` (§7) and `render/items.cpp` (the §4 table, every type). `Session::renderState()`
(`core/sim/include/aa/sim/render_state.h`: `heldIndex`, `ManipulationOverlay`, `RenderBuzz`, `RenderToolbox`) is
the snapshot it draws from; `VisualState::setGoalMarkers` is the `SetGoalMarkers` port. `amazing_alex
--screenshots <dir>` + `tools/render_contact_sheet.py` produce the visual check against `LevelThumbnails_350`
(the world passes are pixel-identical to the M2 run outside the strip); `amazing_alex --script <file>` replays
pointer events for reproducible shots of the interaction. Not drawn in M3 (see the M4 status above): the simulation animations (§5), RC
waves, sparkles, the `_white` marker pulse, the idle-snip scissors sprites, `RopeZipLine` on a loaded trolley, the
tutorial hand, and the letterbox borders (§1: not in the imported profile). Known approximations: raylib's
`rlRotatef` converts degrees back to radians (a float rounding away from `glRotatef`), the rope strip draws every
segment (a cut rope's joints are gone but the strip still spans every link — the link bodies separate and the strip follows them), and the flash shader is GLSL 330 (desktop GL); the strip's slot culling draws every
slot that overlaps the extended length instead of the original's `floor/ceil` uniform range (the scissor hides
the difference).
