# Gameplay logic

Sources: `GameScreenController`, `GameApp`, `st::GameScreen`, `st::GameStateUtils`, `st::GoalStateUtils`,
`st::LocationStateUtils`, `st::GameProgressUtils`, `st::Toolbox*`, `st::UndoQueueUtils`,
`st::GhostManipulationUtils`, `st::TutorialUtils`. **[verified]** unless tagged.

## 1. Game modes and screens

`GameScreenController::setMode(GameMode)` — modes seen in the code (analytics strings):

| Mode | Meaning |
|------|---------|
| 0 | chapter level ("Activate Chapter Level") — the campaign |
| 1 | sandbox editor, layout step (My Levels). `prepareForNewLevel`'s mode-1 branch [verified: 0xb7128]: the controller's own **editor toolbox** (`ToolboxUtils::SetFull`: a `GoalStar` slot of `3 − stars in the level` first when positive, then one slot of 0x20 per unlocked type in the fixed order Shelf, TennisBall, SoccerBall, Book, CardboardBoxMedium, CardboardBoxSmall, LaundryBasket, Balloon, Scissors, EightBall, Pipe, Pipe90, BoxingGlove, Hook, Rope, Bucket, BowlingBall, Seesaw, Slingshot, Dart, Spring, Skateboard, Pinball, Bumper, Doll, Trapdoor, PiggyBank, RCTruck, HangingLamp, PaperPlane, Magnet, BouncyBall, Helicopter, ZipLine — FishBowl, Pulley, Billboard and the controller / lever halves never appear; every slot's amount = cap (3 / 0x20) − the count already in the level, the slot removed at 0) becomes the active strip (`+0xc25b8`); nothing is fixed. `playNewLevel` ends with `saveSandboxLevelAndThumb` (the file exists from the first frame). Every edit clears `tested` (GameState+0x25b0). `doFrame` pushes no undo snapshots in modes 1 / 5 (`edited && mode != 1 && mode != 5`). `restartLevel` in 1 / 5 empties the level: `restoreGameState` of a default `LevelLayout` (the header too: the default title id, no author, background 0), the level's strip replaced by a fresh `Toolbox`, a plain-floor `WorldBound` added, `startLevelWithGoals(false)`; the editor strip keeps its amounts; no editor button reaches it [verified: 0xb9884..0xb9a1c; ported as `Session::restart`] |
| 5 | sandbox editor, toolbox step ("Move Forward in Own Level"), reachable only with `isShareAllowed` (= `tested`): 1 → 5 stashes the undo queue (`+0x79c90`) and resets it, remembers the layout (`+0x5a4`) and every object's handle in collection order (`+0x29a4`), makes the **level's** strip the active one at the editor strip's position with all slots removed (`RemoveAllSlots`), fixes the stars (`MarkAllStarsFixed`), and `SandboxView::ButtonPressed` saves the level right after. Dragging a layout item onto the strip (action 10's mode-5 branch) moves the object, its related item and the ropes it freed into the removed list (`+0x29b4`, by handle, highest current index first) and rebuilds the world from the ready layout without them (`UpdateSandboxToolboxLayout`: `StripItemHandle` / `CleanAttachments`, the attachment indices remapped by `physicsIndexSBOriginalToCurrent`, the strip preserved across `restoreGameState`); taking one out of the strip (action 8) pops the last removed handle of that type (+ its related item) and puts the item back **at its ready position** (`StartAdding` on the handle). Stars cannot be returned (action 9 is refused for type 23 — after the removing flag is set, and the ready layout predates `MarkAllStarsFixed`, so the original's stars come loose after the first strip move; the remake re-fixes them after every rebuild and refuses first, 10 §11 item 14 (o)). 5 → x undoes while `isActionEnabled(0)` — never, the step's only snapshot is its base — resets both queues and un-fixes; the step's back button reloads the file instead, and the play button is disabled in the step, so the forward path is the sharing view only (online, dropped) [verified: setMode 0xb9e70, ItemActions* 0xb904c / 0xbd32c, UpdateSandboxToolboxLayout 0xb8144] |
| 2 | World of Contraptions (downloaded) level |
| 3 | playing a friend's solution |
| 4 | test-playing an own sandbox level ("Activate Own Level"): 1 → 4 snapshots the layout (`+0xC72A0`) and `MarkAllObjectsFixed`; the run is "solved" when all three stars are collected (`collectedStars == 3` in modes 2 / 4); `setCompletedState`'s mode-4 branch restores the snapshot, sets `tested`, fixes everything, writes the `_solution` file (no offline reader) and `setEditorState` → mode 1, `toggleSimulation`'s stop half, `MarkAllObjectsNotFixed` (state 6 never entered); a stop touch or the 5 s idle stop go `setMode(1)` then `toggleSimulation`; 4 → 1 `MarkAllObjectsNotFixed` [verified] |

The background button of the editor (`SandboxView::ButtonPressed`): GameState+0x23a8 = (index + 1) mod 4,
`backgroundChanged(1)`: the previous background texture is kept and the new one slides in from 1024 virtual px
over 0.5 s (`GameScreenTransitions+0xb0`; RenderWorld draws the new background at x = slide and the old one at
slide − 3.41 m while the slide runs), `tested` clears. The world bound keeps its shape until the next
`restoreGameState` rebuilds it from the layout — whose background index `LevelLayoutUtils::Get` copies from
GameState+0x23a8, so the Treehouse floor hole appears at the next test play [verified: 0xb3f60, RenderWorld
0xc08dc, Get 0xca3f4]; the remake rebuilds the bound's physics at once in `Session::setBackground` (a deviation,
10 §11 item 14 (k)).

Screens (`UI::*Scene`): Splash → MainMenu → ChapterSelection (books) → LevelSelection (thumbnails, stars) →
LevelLoading ("Loading…" only — the tips are dead data, 06 §3) → **GameScene** → LevelCompleted overlay →
ChapterComplete / ChapterComplete3Stars → Comic (`COMIC_CH1*`, the Classroom only). Sandbox: the seventh book →
MyContraptions → LevelLoading (location 2 = an existing level, 3 = a new one) → SandboxScene (its own
`SandboxView` over the shared world drawing, the editor toolbox). Online: WorldOfContraptions, FeaturedLevels,
LevelSharing (dead). The scene transitions, the button handlers and the overlay rules are in 06 §1.2 / §1.3
[verified, M5 / M6]; the remake ports everything but the online scenes (`core/ui`, docs/10 §6).

## 2. Level life cycle

```
LoadLevelIndex(index)          → LevelLayoutUtils::LoadLevel (decrypt, parse) → GameStateUtils::CreateNew
                                 → LevelLayoutUtils::Apply (items, toolbox, goal) → WorldBound item (type 31) added
                                 → GamePhysicsUtils::CreateWorld(SetUp) → CreateDynamicPhysics → CreateAttachments
                                 → VisualWorldStateUtils::SetGoalMarkers → TutorialUtils::Start (chapter 0 only)
SET-UP state (GameScreenState "editor"):
   player drags items from the toolbox, moves/rotates/flips/deletes non-fixed items; every completed manipulation
   pushes the LevelLayout to the undo queue (UndoQueueUtils::Add) and marks the level "edited"
play button → toggleSimulation():
   setSetUpToSimulationTransitionState → world re-created in Simulation mode (static bodies, sensors removed),
   GameItemUtils::SetInitialState for every item, stopwatch shown, WasLevelImproved/Save of the attempt
SIMULATION state: GameScreen::UpdateSimulation each frame (04 §3); goals & stars evaluated
   ├─ goal reached (Action 11) → GameState.goalReached = 1 → startLevelCompleteSequence (toolbar / stopwatch
   │    retract, goal markers hidden, LevelCompletedEffect confetti + stars at the goal marker, countdown
   │    2.05 s campaign / 1.5 s test play / 3.35 s friend solution; the physics keeps running) → countdown ≤ 0
   │    → setCompletedState (controller state 6: the world freezes, doFrame only drains the queue; the render
   │    copy is the plain world state) → LocationStateUtils::MarkLevelAsDone + Save,
   │    GameProgressUtils::AddEarnedStars / CheckForNewLocationUnlocks / ApplyRewards → LevelCompletedView
   ├─ a touch outside every in-world button → Action 0x15 → next doFrame: toggleSimulation (stop); after the
   │    goal is reached the same touch queues 0x18 (skip the effect — the countdown is not shortened)
   ├─ no body moving for 5 s → toggleSimulation (stop) [verified: doFrame tail]
   └─ stop button / restart → setSimulationToSetUpTransitionState: world re-created in SetUp mode from the
        saved layout (restoreGameState) — objects return to their placed positions
```

* `ZoomCameraOut` is **not** part of the completion sequence [verified, M4: its callers are the editor / share
  paths]; the camera stays where it is while the confetti plays. `startLevelCompleteSequence` is 149 lines of
  effect set-up; only its state machine (flag `+0xC998C`, countdown `+0xC9988`) is ported, the effect is
  approximate (docs/10 §1).
* `setCompletedState` changes the controller state inside `doFrame`, which then **recurses for the same frame**
  (`goto LAB_000cb840`): the outer call's tail is skipped and the default branch runs — the queue drained, the
  render copy a plain copy, `RemoveInvalidItems`, the prev copy, no `StopRunawayObjects` and no idle stop
  [verified: decompile + disassembly; the G4 script `playtime` covers this frame bit-exactly].

* The player may stop the simulation at any time and continue editing; there is no time limit
  (`goal.timeLimit` unused, `GameParams` has no timer).
* After completion `continuePlaying(true)` recreates the level (`GameStateUtils::CreateNew`) for replay / improving
  the star count; `PlayNextLevel` advances if `LocationStateUtils::CanPlayNextLevel`.
* `GameScreenController::isShareAllowed`, `shareLevel`, `shareSolution`, `saveSandboxLevelSolution` — a *solution* is
  the full layout including toolbox items (`MarkAllSolutionItemsFromToolboxNotFixed` distinguishes them).

## 3. Goals and stars

* The **goal** (02 §5) is the win condition; the three **stars** (`GoalStar`, type 23, always exactly 3 per shipped
  level) are optional collectibles: a star is collected when any body touches its sensor (`GoalStarUtils::Update`),
  `GoalState.collectedStars++` (`GoalState+4`).
* The level counts as completed when the goal is reached, regardless of stars. The score stored per level is
  `state = 3 + collectedStars` (`LocationStateUtils::MarkLevelAsDone`, keeps the maximum).
* **Three stars end the level like the goal only in game modes 2 and 4** (World of Contraptions, test play):
  `doFrame` tests `GoalState+4 == 3` behind `getMode() == 2 || getMode() == 4`, sets `goalReached` and starts the
  completion sequence [verified: decompile + disassembly, M4]. In the campaign three stars without the goal do
  nothing (the run continues; the doctest `stars: three stars do not complete a campaign level` pins it).
* `GoalStarUtils::Update` [verified]: a star whose sensor body touches anything (`GetContactList` with a touching
  contact) goes to state 1, plays sound `0x3D + collectedStars` (counted before the increment: `StarCollected1..3`), increments `GoalState+4`
  and shrinks along a 0.4 s six-point curve; at the end action 7 removes it. `GoalStateUtils::Update` (type 7
  goals) accumulates the contact time of each target with the sensor (group −7) in `GoalState+8 + 4·i` and
  `IsGoalComplete` needs every target ≥ 0.3 s; the other goal types are pure predicates over the world state (03
  §3 / 02 §5), types 1 and 2 come from the contact listener (04 §5).
* `LevelCompletedView` shows the stars; "level improved" (`WasLevelImproved`) compares with the stored value.
* Stars collected in a run that does not reach the goal are not kept: `GoalState` is reset by
  `restoreGameState`/`CreateNew`, and only a completed run is scored (`MarkLevelAsDone` is called from
  `setCompletedState` only).

## 4. Progression

Per-location state (`st::LocationState`, saved encrypted in the documents folder via `LocationStateUtils::Save`):
one int per level:

| value | meaning |
|-------|---------|
| 0 | no level in this slot (indices ≥ the location's level count) — the button is not created |
| 1 | locked — button shown but disabled (`LevelSelectionView::Refresh`: visible if value > 0, enabled if value > 1) |
| 2 | unlocked, not completed |
| 3 + n | completed with n stars (n = 0..3); `GetLevelStarCount = clamp(value − 3, 0, 3)` |

`LocationState` layout: `+0 current level index`, then per level `{int state, bool played}` (8 bytes) from `+8`.
A fresh state (`Load` without a save file) sets levels 0–3 to 2 and the rest to 1. `played` is set by
`SetLevelPlayed` when a level is opened and only steers the level list's initial scroll position (first unplayed
level, backed up to a playable one).

Unlock rules (`MarkLevelAsDone`):
* Levels are grouped in **pages of 4** (level index `>> 2`). When a level is completed, if **≥ 3 of the 4 levels of its
  page are completed** (value > 2), the next page's four levels go from 1 → 2 (unlocked).
* `CanPlayNextLevel`: the next level's value must be ≥ 2.
* Chapters: `GameProgressUtils::CheckForNewLocationUnlocks` compares the total collected stars
  (`GetCollectedStarCount` = sum over the 4 locations) with `st::StarsToUnlockLocation = {0, 30, 75, 135}`
  → The Classroom is open, The Backyard needs 30 ★, Alex's Bedroom 75 ★, The Treehouse 135 ★ (of 348 total).
  The same function also sets three bytes that enable the three extra "books" on the chapter screen
  (`ChapterSelectionView::Refresh`, book slots 4–6, `CustomChapterInfo0..2`): `+0` = **My Contraptions** (set once
  location 0's *chapter-complete-shown* byte `+0x21` is set, i.e. after the Classroom is finished), `+0x10` = **World of
  Contraptions** and `+0x7A4` = **Level of the Week** (both set unconditionally at the end of every call, so they are
  open from the first refresh) [verified].
* `st::GameProgress` layout (0x800 bytes) [verified: ctor, `CheckForNewLocationUnlocks`, `UnlockItems`,
  `showChapterComplete`, `ChapterCompleteView::ButtonPressed`]: `+0` My-Contraptions flag; `+0x10` WoC flag;
  `+0x20 + 0x10·loc` per location {`+0` unlocked, `+1` "chapter complete" scene shown, `+2` "3 stars" scene shown,
  `+4` stars (int)}; `+0x160 + 0x10·type` sandbox item unlocked (byte per item type); `+0x7A0` CRC32; `+0x7A4` LotW flag.
* **Sandbox items unlock per chapter, not per level.** `LevelCompletedView::ShowPanels`, when every level of the
  chapter is completed, calls `UnlockItems(progress, saveSlot, location, allStars)` which sets a fixed item set
  [verified]: Classroom → Shelf, TennisBall, SoccerBall, Balloon, Scissors, Bucket, CardboardBoxMedium/Small,
  GoalStar, LaundryBasket (+ **Book** with all stars); Backyard → BowlingBall, Hook, Rope, EightBall, Pipe, Pipe90,
  Skateboard, Seesaw, Spring, Dart, Slingshot (+ **BoxingGlove**); Bedroom → Doll, Magnet, Pinball, PaperPlane,
  HangingLamp, Bumper, RCTruck, RCController, Trapdoor, TrapdoorLever (+ **PiggyBank**); Treehouse → BouncyBall,
  ZipLine (+ **Helicopter**). FishBowl (12) and Pulley (21) are never unlocked this way (the sandbox toolbox `ToolboxUtils::SetAll` only lists types whose byte is set). `ChapterCompleteScene.xml` carries the label
  `TEXT_ITEMS_UNLOCKED`, `ChapterComplete3StarsScene.xml` the label `TEXT_ITEM_UNLOCKED` (the bonus item).
  The per-level `rewardId` (02 §2) and the `st::Rewards` table it indexes (sticker name + item list: 1 Balloon [5],
  2 Scissors [6], 3 "8-Ball & Pipes" [16,17,18], 4 Piggy Bank [13], 5 Punching Glove [14], 6 "Hook, Rope & Bucket"
  [8,9,7], 7 Spring [28], 8 Trapdoor [37], 9 Doll [19], 10 "Seesaw & Bowling Ball" [3,22], 11 Slingshot [34], 12 Dart
  [29], 13 Skateboard [20], 14 "Pinball & Bumper" [26,33], 15 RC Truck [35], 16 Lamp [30], 17 Paper Plane [27],
  18 Magnet [25], 19 Bouncy Ball [41], 20 Zip Line [42], 21 Helicopter [39]; `Sticker*.png` frames) are only read by
  `GameProgressUtils::ApplyRewards`, which **has no caller** — a leftover of an earlier per-level reward design.
  `UnlockAllItems`, `LockAllItems`, `UnlockAllLevels` exist for debugging/IAP.
* Chapter completion (`GetCompletedLevelsCount == level count`) shows `ChapterCompleteScene`; 3-starring every level
  shows `ChapterComplete3StarsScene`.
* **Completion bookkeeping order [verified 2026-09-14: `setCompletedState` (campaign branch),
  `LevelCompletedView::ShowPanels`, `MarkLevelAsDone`, `CanPlayNextLevel`, `LocationStateUtils::Load`]:**
  `setCompletedState` (mode 0): `StopLoopingSounds`; `improved = WasLevelImproved(state, goal, current)`;
  `newStars = goal.collectedStars`, `oldStars = GetLevelStarCount(current)`; `MarkLevelAsDone`; if
  `newStars − oldStars > 0` → `AddEarnedStars(progress, diff, location)` (which calls
  `CheckForNewLocationUnlocks`); if `improved` → save `GameProgress`; save the `LocationState`; controller state
  6; `levelImproved (+0xC997C) = improved`. `MarkLevelAsDone` sets `status = max(status, 3 + stars)` and, when the
  page's next four levels exist (`pageStart + 4 < levelCount`) and ≥ 3 levels of the page have `status > 2`,
  turns the next page's **1 → 2 only** (0 / 2+ untouched). `CanPlayNextLevel` = `current < levelCount − 1 &&
  status[current + 1] > 1`. `LevelCompletedView::ShowPanels` (the result panel): `ShowStars(collectedStars)`;
  then only when `levelImproved` and `GetCompletedLevelsCount == levelCount`: `CheckForNewLocationUnlocks`,
  `UnlockItems(progress, slot, location, starCount == 3·levelCount)` (which saves the progress).
  `LocationStateUtils::Load` without a file: levels 0–3 = 2, the rest of the location 1, slots beyond it 0;
  with a file, a level missing from it is 1; both paths end with a repair pass — the first level is at least 2
  and, page by page (up to page `levelCount >> 2`), the first level with `status ≥ 2` unlocks the rest of its
  page. The **level list initial page** (`LevelSelectionView::Refresh`, 8 buttons per page): the first
  unplayed level, backed up while its status is < 2; when every level was played, level 1 if level 0 is not
  completed, else the compiled scan `idx = 3, 5, 7…` while `status[2], status[4], …` > 2; past the end → 0;
  returning from a level → `currentLevel >> 3`. `LevelLoadingScene::ActivationComplete` calls
  `SetLevelPlayed(level)` + save and, for location 0, sets `GameProgress.location[0].unlocked` + saves it.
  `MainMenuView::ButtonPressed(play)`: while location 0 is still locked (`GameProgress+0x20 == 0`, a fresh
  install) the first level opens directly (`LoadLocation(0)`, the comic, `LevelLoadingScene` pushed with
  `ChapterSelectionScene` / `LevelSelectionScene` inserted beneath); otherwise `ChapterSelectionScene` is pushed.

## 5. Set-up mode interaction (`st::GameTouchHandler`, `GameScreenController::ItemActions*`)

* **Touch state machine** (`GameTouchHandler::Process`, `TouchState` at `GameState+0x57320`, 0x70 bytes; ported 1:1
  in `core/sim/src/touch_handler.cpp`) [verified: decompile + disassembly, docs/10 §8 G5]. Touches arrive as native
  px with **y up** (`TouchUtils` flips `NativeScreenHeight − y`), with `double` timestamps; `Process` runs once per
  `doFrame` before `processActions`. States and the actions they queue:

  | state | meaning | enters on | leaves / queues |
  |---|---|---|---|
  | 0 | idle | — | a touch on a dynamic item → 1 (action 3 `selected`, hold timer 0.2 s); on a fixed item → 1 without action 3; on the strip slot → 0xD; on the strip button → 0xE; on empty space → 1 with no object |
  | 1 | pending | touch down | 5 px move (`25 < dx² + dy²`) or the 0.2 s hold → 2 (action 1) for a dynamic item, → 9 (action 0xE buzz) for a fixed one, → 0xB (pan) with no object; release → 0 |
  | 2 | dragging | 1 | every move → action 4; release → `FUN_000ceb68`: over the drop rectangle / colliding with the SelectionArea → action 9, else action 2; the ring / flip tests are not re-run |
  | 5 | ring rotate | a touch at 0.23..0.53 m from the selected item while its gizmos show (state bit of `+0x5738c`) | the finger angle (`CameraUtils::PixelToScreenPos`) turns the item → action 5; release → 0 |
  | 6 | flipping | the flip button (a ±0.1 m rectangle at item position + 0.4·(cos π/4, sin π/4) — the angle is the constant `DAT_0028e174`, not the item angle) → action 6 | `FlippingAnimation` done → 0; the release of the flip tap itself → 0 at once. Input is disabled meanwhile through `UI::SceneManager::SetUserInteractionEnabled(false)` [verified: `TouchesStarted/Moved/Finished`]: a new Began is only remembered (`SceneManager+0x80`) and that pointer's Moved / Ended are dropped until its Ended, while touches begun earlier still reach the handler |
  | 7 | from toolbox | 0xD + a 50 px² move at > 20° from the strip axis (or a non-scrollable strip) → action 8 | the item spawns under the finger → 2 |
  | 8 | returning | action 9 (drop over the strip, ghost with no good state, `releaseHeldItems` while adding) | action 10 (`removal finished`) → 0 |
  | 9 | buzz | a fixed item held 0.2 s or moved 6 px (action 0xE) | release → action 0xF → 0 |
  | 0xB | pan | 1 with no object + a move | native px deltas move the camera centre literally (no zoom / scale correction) [verified]; release → 0 |
  | 0xD | strip touch | a touch on a slot (tolerance max(width/2, 50 px)) | a move along the strip → 0xF (scroll, action 0x1B with the fling); away from it → 7 |
  | 0xE | strip button | a touch inside the button rectangle | release inside → the strip toggles (`buttonState`), sound; outside → nothing |
  | 0xF | strip scroll | 0xD | release → the fling (velocity × 0.95² per frame while |v| > 1 px) |
  | 10 (0xA) | pinch zoom | a second finger while in 1 **on a phone** (`DeviceParams::IsTablet` false; the Android build sets it to 1 unconditionally in `GameApp::GameApp` [verified: 0xa7b08], so it never gets here) | a move of either finger [verified: 0xc04bc]: `zoom += (curDist − prevDist) · 0.003` (native px between the fingers, current vs previous samples), the centre `−= (curMid − prevMid) · 0.5` in virtual px (`PixelToScreenPos` with the zoom *before* the update), then the zoom clamped to [1, 2.5] and the centre through `GetClampedCenter` at the new zoom; either finger's release → 0 with the gizmo object cleared (`0xbefb8`); the hold timer never fires out of 10 (M7) |
  | 3 | two-finger pending | **no writer in the shipped binary** (the Began branch writes 10 or nothing) — the Moved / Ended branches are ported as read and unit-tested by forcing the state (`tests/test_touch_two_finger.cpp`) | the first of the two fingers to move 5 px becomes the rotating finger (`+0xc`, `ringPx` from its current position); its moves run the ring angle (from the *touch start*, as state 5) → action 5; its release hands the ring to the other finger (`angleStart = angleCurrent`) or, alone, ends the manipulation (action 2, gizmo kept); another finger's release only clears its slot; a release with no rotating finger → 0 (gizmo kept) [verified: 0xc01ac, 0xc09a0, 0xbf100] |
  | 4 | two-finger rotate | no writer either | the secondary finger rotates (action 5); the primary lets go of the item (`+4 = −1`) once farther than the item's `halfSize` from it; the rotating finger's release returns to 2 when the primary still holds (the item's target becomes the new drag start, the primary's start px its current position) or drops the item (`FUN_000ceb68`) otherwise; the non-rotating finger's release clears the *primary* slot whichever finger it was [verified: 0xc0208, 0xbf19c] |

  Literals: drag threshold 5 px (`25 < d²`), strip drag-out 50 px² and 20°, hold timer 0.2 s (`0x3e4ccccd`),
  ring 0.23..0.53 m, corner box (100, 60) px, the 0.1 s velocity re-sample (`+0x60` double). **Flip** comes only
  from the flip button (state 6); the tap counter `+0x14` serves the fixed-item buzz — "double-tap flips" was wrong.
* **Pick** (`IntersectionQueries::GetNearestIntersectingObjectMinusBillboards` in the campaign): an AABB point query
  (± 0.04 m) of the set-up world, `TestPoint` on every fixture, the nearest object by squared distance to the touch,
  a movable object beats a fixed one; per-body flag bytes `PhysicsObject+0x90..0x92` (bit 0 selectable — cleared for
  an attached rope end; bit 1 gizmos — cleared by the templates for FishBowl body 0, Rope 0–2, GoalStar 0, Slingshot
  1, ZipLine 0/1) decide whether the picked body can be dragged and whether the gizmos show [verified]. Selection
  animation (`ManipulationAnimationUtils`: 0.1 s cubic 1 → 0.6, then 0.07 s back), adding animation 0.3 s over the
  curve (0, 0.6), (0.6, 1.15), (0.8, 0.9), (1, 1), removing 0.15 s `CubicInterp(1, 0.2)` then action 10.
* **Move**: `GameItemUtils::UpdatePos` = `CalculateSnap` (04 §7: AABB 1.1·halfSize, point radius 0.08 m) → `Snap` /
  `Unsnap` → `UnsnapAllNotAttached` → every body translated (`SetTransform`), attached ropes follow or detach
  (`FUN_000b8654`), a ball over a slingshot pouch drags the pouch; ropes (`RopeUtils::UpdatePos`, velocity-gated
  snapping: `v² < 2.5e-5`, or `< 1.96e-4` while snapped), slingshots and zip lines have their own paths. Drag =
  `b2Body::SetTransform`; the set-up world never steps, but `GameScreen::UpdatePaused` runs `b2World::Step(0, 1, 1)`
  every frame so contacts (ghost, `IsColliding`) stay current [verified]. `processActions` runs **twice** per
  `doFrame` (after the touch handler and after the camera update); the ghost update's actions are processed the
  next frame [verified].
* **Rotate**: `UpdateAngle` (every body rotated about the item position; free rotation, no step) from the ring
  (state 5) or the remake's `rotateHeld` (the touch angle; applied by the next `UpdatePos` — action 5 is ignored
  while a fresh toolbox item is still "adding", `+0xc99b9`). **Flip**: `FlippingAnimation` 0.15 s (`scale.x =
  −sign·cos(π·t/0.15)`), `GameItemUtils::Flip` at 0.075 s (`RemoveAllAttachments`, `scale.x` negated, bodies
  re-created in set-up mode, attachment sounds; scissors rotate by π instead), a colliding result flips back once.
* **Gizmos** (`renderFrame` → `GameRenderState+0x2c`): state 1 `TranslationGizmoBig` (frame 139, scaled by the
  selection animation) while pending / dragging / adding / returning, state 2 `RotationGizmo_iPhone` (frame 100,
  turned by the item angle + a phase advancing `dt·0.3·cos(1.5·t)` idle and `dt·π/4` while rotating) + `FlipGizmo`
  (frame 44) for flippable items (flag bit 3) after a drop or in the ring state, state 3 `InvalidSelection` (frame 58)
  for the buzz; colours (0.6431, 0.7843, 0.9333) / (0.8784, 0.2667, 0) in ghost. The gizmo object (`+0x5738c`) is
  the last dropped dynamic item whose body 0 has flag bit 1 [verified].
* **Fixed-item buzz** (actions 0xE / 0xF, `GameScreenTransitions+0x48`): the amplitude rises 0 → 1 in 0.1 s and
  decays to 0 in 1 s after the release; every *fixed* item is drawn through `GL_COMBINE` interpolate =
  `texture·a + (1, 0.31, 0.122)·(1 − a)` while `a > 0.0001` (11 §2); the `InvalidSelection` cross (random angle
  `Random::GetFloat(−π, π)`) goes as soon as the rise is over while the finger holds, or when the decay passes 0.8
  (`GameScreenTransitionsUtils::Update`) [verified].
* **Edge auto-scroll** (`CameraUtils::Update`, every frame while an item is dragged — state 2 only — and **only on
  phones**: `DeviceParams::IsTablet` skips it) [verified]: when the held item's screen position is within **100 px**
  of any view edge and not over the toolbox rectangle, a timer accumulates; after **0.4 s** the camera pans toward the
  item at 300 px/s per axis (the item is moved along, action 4). Leaving the edge zone resets the timer.
* **Ghost state** (`GhostManipulationUtils::Update`, called every frame for the held item with `enabled = mode ≠ 5`,
  i.e. off in the sandbox editor) [verified]:
  * *Colliding* = `PhysicsObjectUtils::IsColliding(item)` or any rope attached to it colliding (set-up world contacts,
    `WorldContactListenerSetUp::PreSolve` only disables the contact between the two halves of one Scissors).
  * Not colliding → if not in ghost: `SaveGoodState` (copies the `PhysicsObject` and `GameItem`) — so the *good state
    is the last collision-free pose seen during the drag*, refreshed every frame. If it was in ghost: attachment
    sounds, `ExitGhostState` (deletes the ghost copy), `SaveGoodState`.
  * Colliding and not yet in ghost → enter ghost. If a good state exists: the item gets state bit 1 (`+0xD & 2`,
    rendered with `glColor4f(0.6, 0.4, 0.4, 0.4)` — the reddish translucent tint) and a **ghost copy** item of the
    same type is created (non-collidable, state bit 2 = "is ghost", plus copies of attached ropes) at the last
    valid pose — for non-rope items refined by a 10-step bisection between the good position and the current
    position (`SetPos` + `b2World::Step(0, 1, 1)` + `IsColliding`) so the ghost sits as far toward the finger as
    it can without overlapping. With no good state (item just taken from the toolbox and never valid) nothing is
    created.
  * Finger up (`GameScreenController::endManipulationForActiveItem`) [verified]: not in ghost → `ManipulationEnded`
    and the ghost state is reset. In ghost → a **ghost animation** starts (`GhostAnimationUtils::Start`): the item
    glides in a straight line from its current position to the ghost copy's position at **4.5 m/s** (duration =
    distance / 4.5); with no ghost copy the animation has zero length and finishes immediately.
    When it finishes (`updateGhostAnimation`, state 2): no good state/ghost copy → **action 9** = `ManipulationAnimationUtils::
    StartRemoving` (shrink animation) and the item goes back to the toolbox; otherwise `RevertGhostState` (exact ghost
    pose, set-up data, attachments re-snapped, ropes re-laid), `ManipulationEnded`, then action **2** (drop) is
    queued — unless the last processed action (`GameScreenController+0xC99C0`) already was a 2, in which case the
    no-op action **0** is queued instead so `endManipulationForActiveItem` does not run twice [verified].
    `releaseHeldItems` is called only by the pause menu (`GameScene::SetPaused`: `doFrame(0)`, gizmos off,
    `releaseHeldItems`, `doFrame(0)`) and the sandbox play button: a fresh toolbox item or a ghost goes back, a plain
    drag is left to the touch layer's cancel [verified]. The campaign play button (`toggleSimulation`) does nothing
    while a manipulation is active.
    Action 8 is queued by `GameTouchHandler::Process` when a touch starts on a toolbox slot (new item taken out).
* **Toolbox** (`st::Toolbox`, 0x524 bytes: `open`, `y`, `x` (button centre, native px), `scroll`, `slotCount`,
  `buttonPressed`, `buttonScale`, `ejectLength`, slots × 0x14 `{type, amount (−1 = unlimited), widthPx, heightPx}`;
  `ToolboxUtils`) [verified]: sizes come from the `UIElements` frame table (`InitializeButtonSizesFromTextures`: per
  type the `Button<Item>` frame of `ItemInfos+0x10`, the button frame 71 = 142 × 140, the end cap 76 = 67 × 140 in the
  2048X1536 profile); slot width = 2·67 + max(icon width, 50); `toolboxY = (floorPx − 39)·0.04 + 2 + h/2`, the on-screen
  x = right edge − 0.7·button width; `DisplayToolbox` glides x in 0.6 s, `RetractToolbox` in 0.3 s; the strip extends at
  1000 px/s to min(total slot width + w/2, 0.75 · play-field width); `GameScreenTransitionsUtils::Reset` leaves the
  x / eject interpolators alone, so a running glide carries into the next level. Dragging a slot out spawns the item
  (`ItemActionsNewSelection`: refused when `objectCount + needed > 126` or the type already has more than 31 items;
  `WorldStateUtils::AddNewItem(state, type, touch pos, 0, fromToolbox = true)`, `CreatePhysics(mode 0)`,
  `ToolboxUtils::RemoveItem`, scale (0.6, 0.6) + `StartAdding`); dropping it back over the drop rectangle
  (`GetDropRectangle`) or into the SelectionArea returns it (action 9 → 10: `RemoveRelatedItems`, `FUN_000c9590`
  detaches every rope hanging on it — a rope left with both ends free goes back too — and `FUN_000c3478` puts the
  type back into the strip when object flag bit 7 is set (cleared only for RCController / TrapdoorLever, whose
  truck / trapdoor returns instead) and invalidates the item; `RemoveInvalidItems` removes it at the end of the
  frame with a swap-with-last (`PhysicsObjectsUtils::Remove`)). `ToolboxAnimationUtils::Display` (the slot pop-in)
  has no caller in the binary. Items that were spawned from the toolbox are the only non-fixed ones in campaign
  levels. `fromToolbox = true` is the "legacy built-in controller" of 10 §11: `Truck/Trapdoor/HelicopterUtils::
  SetInitialState` keep the controller as a 4th body until the first `ManipulationEnded` splits it into an
  RCController / TrapdoorLever item paired by handle.
* **Undo** (`UndoQueueUtils`): `UndoQueue{top, count, layouts[32] × 0x2400}`; `prepareForNewLevel` does `Reset`
  (count −1) then `saveUndoState` = the base snapshot (`LevelLayoutUtils::Get`, taken **before**
  `CreateSelectionAreaObject`); every frame that set `+0xc996d` (edited) pushes another `Get`; `Add` at count 0x1f
  `memmove`s the oldest out (so base + 31 edits fit, the 32nd edit drops the base); `undoLastMove` = `--count;
  restoreGameState(layouts[count])` while `count > 0`, `redoLastMove` while `count < top` (`isActionEnabled`);
  `restoreGameState` = `DestroyWorld`, `PartialReset`, `Apply`, toolbox from the layout, `CreateWorld`,
  `CreateDynamicPhysics`, `CreateAttachments`, `DisplayToolbox`, goal markers. `Get` stores the live rope end vector
  (moved by `UpdatePosFromAttachedObjects`), so a rope level's first snapshot differs from its file but the round trip
  is idempotent from then on. **Restart** (`restartLevel`) restores the base layout — without the SelectionArea,
  which only `prepareForNewLevel` creates — and zeroes the undo counter [verified].
* Tutorial (`st::tutorial_chap0_level0..6`, `TutorialUtils`, `GameTutorialView`) [verified: the seven script
  builders, `tutorial_should_run`, `TutorialUtils::Update`]: runs only in location 0 (the `LocationInfo` at `GameState+0x834`, whose first int is the location index —
  `LocationInfoUtils::Load` — must be 0) and only when the toolbox is non-empty — or, with an empty toolbox, on level 0 alone. The script
  is a looping list of hand states (`TutorialUtils::Update` indexes `step mod count`): `Setpos`, `Setimage` (0 =
  pointer, 1 = pressed), `Wait(t)`, `Fade(duration, from, to)`, `Move(LinearPath(1 s, a, b))` /
  `Move(CircularPath(1 s, centre, radius 0.4, …))`, `Setdragitem(slot, …)` (a ghost copy of toolbox slot `slot`
  follows the hand), `Setorientationitem(slot, angle)`. It stops when the controller calls `TutorialUtils::Stop`
  (player performs the action). Three builders and their data (world metres):
  * `click_position_tutorial(from, to)` — hand at `from`, fade in (1 s), wait, move 1 s to `to`, wait, press,
    wait, release, wait, fade out. **Level 0**: `from` = (2.5575, 1.5934), `to` = the `ButtonPlay` view centre
    (`screenToWorld` of its frame).
  * `fetch_all_items_tutorial(start, targets)` — hand at `start` = (2.5575, 0) (below the play field, i.e. the
    toolbox), fade in, then for every toolbox slot in order and every target listed for that slot's item type:
    move 1 s to the slot centre (`toolboxIdxToWorld`), wait, press, wait, pick the slot's ghost item, wait, drag
    it 1 s to the target, wait, release, wait, drop, wait (every wait 1 s); then fade out and wait 5 s. Targets per level: **1**: Shelf → (0.943, 0.754); **2**: Shelf → (1.575, 0.303);
    **3**: Shelf → (1.149, 1.077), (1.329, 0.335); **4**: Shelf → (0.966, 0.871), Book → (2.408, 1.304);
    **5**: Shelf → (2.647, 1.480), Book → (1.608, 1.693). `TutorialState+0x14` = the toolbox item count.
  * `fetch_item_rotate_tutorial(start, slot 0, target, angle)` — as above for one item, then
    `Setorientationitem(0, angle)` and a circular hand path (radius 0.4) to show the rotation. **Level 6**: target
    = (0.413 + 0.05·cos(π/2 − 0.5425), 1.369 + 0.05·sin(π/2 − 0.5425)) = (0.4388, 1.4127), angle −0.5425 rad.
  * `tutorial_trial_level1/3/5` are the free edition's variants (out of scope).
  * **Mechanics [verified, M5: `TutorialUtils::Update` disassembly, `Start`, `Stop`, `doFrame`, `FUN_000c0440`]:**
    `TutorialState` (`GameState+0x57398`): step (+0), time (+8), the ghost table start (+0x10), the item count
    (+0x14), the `Hand` (+0x18: position, alpha +0x20, angle +0x24, image +0x28), the drag slot / type
    (+0x2c / +0x34), the orientation slot / type (+0x30 / +0x38), the `ControlledItem` array (+0x3c, 0x14 bytes
    each: type, position, angle, visible), the state array (+0x48), the run flag (+0x54). `Update(dt)`: `step %=
    count`, the state's `update` (`Setpos` / `Setimage` / `Setdragitem(index, type)` / `Setorientationitem(index,
    type)` set their field, zero the time and advance; `Wait(t)` advances once `t < time`; `Fade(d, a, b)` sets
    alpha `a + (b − a) · time / d` and advances past `d`; `Move(path)` sets the position `path(time)`, at the end
    `path(duration)` and advances; `LinearPath(d, a, b)`, `CircularPath(d, centre, r, start, sweep)` = `centre + r ·
    (cos, sin)(start + sweep · t / d)`), then the dragged item follows the hand (type, visible), the oriented item
    takes `atan2(hand − item)`; every controlled item is copied into the GameState's extra render table
    (`GameState+0x31a0c`, up to 10 entries of 0x2c: visible, position and pivot at 0.5 × (the renderer's matrix
    doubles them), angle × 0.5, colour 0.5, mode 2 for the Shelf, 3 for the Book, 1 otherwise — `FUN_000c0440`
    draws modes 2 / 3 through the item renderer with `glColor4f(0.5, 0.5, 0.5, 0.5)` and nothing for mode 1; the
    gizmo-on-ghost branch keyed by `GameState+0x31bd8` is dead — only the constructor writes it); when the step
    reaches the count the items are zeroed; `time += dt`. `Start(state, gs)`: `Stop`, then with
    `tutorial_should_run` the level's script, the run flag, the items resized to the count, the ghost entries
    reserved. `Stop`: the run flag off, the step 0, the states dropped, the ghost entries hidden.
    `toolboxIdxToWorld(i)`: `screenToWorld(toolbox.x + min(c.x, 0), toolbox.y + c.y)` with `c =
    GetCenterForSlot(i)`. Hooks in `GameScreenController::doFrame`: `Update` at the head of every frame (any
    controller state, campaign / test-play modes) and once more inside the state-3 → 4 transition; `Start` the
    first frame the strip's eject length rests at its target after `playNewLevel` (`+0x29b0`) and with the 5 s
    goal-marker re-show; `Stop` whenever a touch hides the goal markers, at the head of `GameView::ButtonPressed`
    (every sidebar / play button, any state), in `GameScene::SetPaused` (state 2) and `ReturnFromSolutions`,
    each after clearing the gizmo object. `GameTutorialView::Update` toggles the
    view when the run flag changes (`GameScene::ShowOverlay(1)` shows it again on a run's way back in the
    campaign; state 4 leaves it alone) — `Stop` leaves the hand's alpha as it was, so after a stopped run the
    hand shows frozen at its last pose until the 5 s re-show restarts the script (an original quirk; the remake
    re-shows the view only while the script runs, since the frozen hand would otherwise stay on every later
    level of the location) — and places `ImageHand` at `(LetterBoxFrameWidth + (W − 2 · LBFW) / 3.41 · x,
    (H − FloorHeightInPixels) − (H − FloorHeightInPixels) / 2.12459 · y)` with the state's alpha, angle and image
    (0 = `BUTTON_HAND_POINT`, 1 = `BUTTON_HAND_TAP`), the pivot on the point. Remake: `core/sim/tutorial.cpp`
    (`TutorialState` in the session, `RenderState::tutorialGhosts`), `GameTutorialView` in `core/ui`; the level-0
    script needs the UI's `ButtonPlay` centre (`Session::setTutorialPlayButton`).

## 6. Simulation-mode interaction (`st::GameSimulationTouchHandler`)

Touches during simulation only hit UI (stop/restart/toolbar); no direct object interaction.
`GameSimulationTouchHandler::Process(touches, gameState, buttons, queue)` [verified: decompile + disassembly, M4]:

* Only the **first pending touch event** of the frame is examined; the ring is emptied afterwards.
* A touch that *begins* is tested against the 11 in-world `GameButton`s (press → sound, 0x16 on release — no
  effect); outside every button it queues **0x15** (`+0xC99B8 = 1` → the next `doFrame` calls
  `toggleSimulation`, i.e. the run stops) while the level-complete effect is not running, **0x18** (skip the
  animations) once it is. The flag tested is the `LevelCompletedEffect` active byte (`GameState+0x31BDC`, set by
  `LevelCompletedEffectUtils::Start` from `startLevelCompleteSequence`, cleared only by `PartialReset` — neither
  `Skip` nor `setCompletedState` clears it); since the handler runs in state 4 only, it is equivalent to the
  countdown flag `+0xC998C` the remake tests [verified: decompile].
* Touch state 2 — a slow-motion drag that would write `gameState.timeScale` — is dead code: nothing enters that
  state, `timeScale` stays 1.0 (04 §3).

Camera: `CameraUtils::Update/GetClampedCenter`; the completion sequence does not zoom (§2); letterboxing on non-4:3
screens (`GameParams::LetterBox*`, `AnchorAspectCorrectionFactor`).

## 7. Actions (`st::Action`, `ActionQueue`, `ActionProcessor`)

Items communicate with the game through queued actions (`st::Action` = 0x20 bytes: `+0` id, `+4` handle,
`+8` Vec2, `+0x10..` payload) processed after each physics step / frame: `GameScreenController::processActions`
first offers every action to `GameScreen::ProcessSimulationAction` (world-side ids, returns 1 when consumed), then
to the set-up handlers `ItemActionsForSelectedAddable` / `…Normal` / `…Completion` / `ItemActionsNewSelection` /
`ItemActionsMisc`, and records the id in `+0xC99C0` (last action). Full id table [verified from the handlers and
from a scan of every `st::Action` constructor call site in the disassembly — ids 0x10, 0x14 and 0x17 are never
constructed]:

| id | meaning | handler |
|----|---------|---------|
| 0 | no-op (queued after a ghost revert when the drop was already processed, see §5) | none |
| 1 | manipulation started on `handle` (`ManipulationStarted`, `SaveGoodState`, default snapping) | Normal |
| 2 | manipulation ended / item dropped (`endManipulationForActiveItem`, sound 9 `UIItemDeselected` at the item, toolbox hidden unless in ghost) | Addable |
| 3 | item selected (`StartSelection` animation, sound 8 `UIItemSelected`) | NewSelection |
| 4 | move selected item to touch position (`UpdatePos` with snapping) | Addable |
| 5 | rotate selected item (`UpdateAngle`) | Normal |
| 6 | flip selected item (`FlippingAnimationUtils::Start`, input disabled until done) | Normal |
| 7 | remove item (a popped balloon after its 0.15 s animation, a collected star after its 0.4 s curve — the only producers → `WorldStateUtils::InvalidateItem`: flags bit0 cleared, the object dropped by `RemoveInvalidItems` in the frame tail — after the lerp, so the render copy is re-mapped by handle) | simulation |
| 8 | new item taken from toolbox slot `payload` (`AddNewItem`, `CreatePhysics(mode 0)`, sound 5 `UIItemAdded`; in mode 5: move a layout item into the toolbox) | NewSelection |
| 9 | return item to toolbox (`StartRemoving` shrink animation, sound 6 `UIItemRemoved`) | Addable |
| 10 (0xA) | removal animation finished (`RemoveRelatedItems`, ghost state exit, selection cleared) | Completion |
| 11 (0xB) | goal complete → `startLevelCompleteSequence` | Misc |
| 12 (0xC) | break item (`GameItemUtils::Break` → `PiggyBankUtils::Break`: queued by `PostSolve` when the normal impulses of one contact sum to > 3.5 N·s; the bank's body becomes non-collidable, the coins effect starts, the activated bit is set, the debris pieces get their velocities from the impact) | simulation |
| 13 (0xD) | play sound (`{position, soundId, volume}`) — `soundId` is an `st::AudioId` index into `st::AudioFilenames` — full table in 06 §5 (e.g. 0x17 `BumperImpact`, 0x21 `Spring`, 0x24 `BoxingGloveTriggered`, 0x26 `SlingshotFire`, 0x2C `RCButtonClick`, 0x31 `SeesawMove`) | simulation |
| 14 (0xE) | "fixed item" buzz: touching a fixed item (flags bit 2) and dragging > 5 px or holding — sound 7 `UISelectBuzz`, shake interpolator 0 → 1 in 0.1 s, random angle, touch state 9 | Misc |
| 15 (0xF) | buzz/shake end (interpolator → 0 in 1 s) | Misc |
| 16 (0x10) | cycle the item's state low nibble 1 → 2 → 3 → 1 — **dead**: no producer anywhere in the binary | Misc |
| 17 (0x11) | attach sharp object (`GameItemUtils::AttachSharpObject`: a revolute joint with `collideConnected`, motor speed 0 and max torque 4·v² between the dart body and the stabbed body at the contact point, `stuck` set, sound 0x16) — queued by `PreSolve` when a dart-tip fixture (group −8) hits a stabbable object and the dot of the tip direction with the relative velocity is ≥ 0.65 | simulation |
| 18 (0x12) | `ApplyForcesUtils::ForceToItem` (`{handle, bodyIndex, force, point}`: `b2Body::ApplyForce` on that body when it is dynamic — the inlined `m_force / m_torque` writes) — bumper, boxing glove, helicopter landing | simulation |
| 19 (0x13) | `ApplyForcesUtils::ForceToRadius` (`{point, radius, amount}`: an AABB query; every Dynamic-category fixture whose body centre lies within `radius` pushes its body away from `point` with `(1 − d/radius)·amount·log2(1 + mass)`, once per fixture) — balloon pop | simulation |
| 20 (0x14) | **dead**: no producer, no handler | none |
| 21 (0x15) | simulation touch handler: a touch that begins outside every in-world button while the level is not yet completed → `+0xC99B8 = 1` → next `doFrame` calls `toggleSimulation` (stop, back to set-up; test-play mode 4 → 1). Same touch after completion queues 0x18 instead | Misc |
| 22 (0x16) | in-world `GameButton` `payload` released (the 11 `st::GameButton`s of the simulation touch handler; their press/release sounds go through 0xD) — no handler, no effect | none |
| 25 (0x19), 26 (0x1A) | "held item in the bottom-left corner" enter / leave. `CameraUtils::Update(dt, camera, itemHeld, itemScreenPos, overToolboxRect, queue)` [verified]: while an item is held, if its screen position is inside the corner box **x < 100, y < 60 px** with the camera scrolled up (`cy − 319/zoom > 0`), or **y ≤ 20 px** with the camera at the bottom, `Camera+0x18` is set and 0x19 queued; once y > 90 px the flag is cleared and 0x1A queued (0x1A also from the set-up touch handler on release). Nothing handles either — a leftover hotspot, no effect | none |
| 23 (0x17) | set `+0xC996D` (layout changed) — **dead**: handler exists, no producer | Misc |
| 24 (0x18) | skip level-completed animations | Misc |
| 27 (0x1B) | toolbox strip scroll by `payload` px; re-queued each frame with velocity × 0.95² while |v| > 1 (fling) | Misc |

## 8. Save data

* Settings (`st::Settings`, `SettingsUtils`): audio on/off, player name (`SettingsParams::DefaultPlayerName`).
  `SetAudioState(on)` writes `soundEffectsOn = on, musicOn = true`; `AudioEnabled = soundEffectsOn && musicOn`
  [verified]. The remake keeps `soundEffectsOn`, `musicOn`, `playerName` and adds `locale` (06 §3).
* Progress (`st::GameProgress`): per-location star totals (+0x24/+0x34/+0x44/+0x54), unlocked flags, unlocked item set.
* Location states (per chapter), solutions cache, sandbox levels + thumbnails (`SerializationUtils::Alloc*Path`).
  Settings, location states and levels are `DataDictionary` plists encrypted with the asset key
  (`SaveDictionary`); `Version::Get` stores the app version. **`GameProgress` is not a plist** [verified:
  `SerializationUtils::Save/Load(GameProgress&)`]: the file is AES(`GameParams::CryptingKey`) of `"ccgp"` + u32 version
  (4 written) + the raw 0x800-byte struct (layout in §4) with its CRC32 at `+0x7A0`; on load a version-3 file is
  CRC-checked, other versions are accepted as is.
* **Remake save files (M5, docs/10 §7):** plain JSON, one directory (`--save-dir`, default the OS user-data
  directory): `settings.json` (`soundEffectsOn`, `musicOn`, `playerName`, `locale`), `progress.json` (the
  `GameProgress` fields: the three feature flags, four `{unlocked, chapterCompleteShown, threeStarsShown, stars}`
  records, `unlockedItems`), `location_<n>.json` (`currentLevel`, `visited`, `finished`, the per-level
  `{status, played}` keyed by level name). Writes are atomic (temp file + rename); an unreadable file is set
  aside as `<file>.corrupt-<n>` and the defaults apply (reported on stderr). The original's encrypted plists /
  `ccgp` blobs are not imported.
* **My Contraptions (M6):** the original keeps `0_Location.plist` (`{name, levels[]}`, LocationInfo index −3,
  up to 96 names of 0x40 chars) in `AppConfig::SandboxDir` with `<name>.plist` (`LevelLayoutUtils::SavePlist`
  of the live layout, the author from the settings), `<name>_solution.plist` (written at a test-play completion
  and by an untested level's back button; read only by the online solution paths — dropped) and the thumbnail
  `<name>_<LevelThumbProfile>.jpg` (350 / 175 / 98 px per profile, JPEG 100: the world rendered with the
  default camera on a 0.1 / 0.1 / 0.3 clear, the square of side min(0.6308594 · H, W) at the viewport's left,
  vertically centred). A new level's name is `GenerateUniqueFilename` [verified: 0xcc0ac]: a fresh `st::Random`
  seeded with `currentTimeMillis` (the low 32 bits), `GetInt(0, 0x7fffffff)` — whose modulus `(hi − lo) + 1`
  wraps to INT_MIN, so the value is `CustomRand`'s 15 bits, 0..32767 — printed through `Format("{0}{1}", "",
  double)` (an empty spec is sprintf `%g`, exact here), repeated from a re-seeded `Random` while the list holds
  the name (case-insensitive); the remake ports it as is (`aa::sim::Random`, `std::to_string`). The legal prompt's acceptance is Settings
  profile +3 [verified]. The remake: `<save-dir>/sandbox/index.json` (`{name, levels[]}`), `sandbox/<name>.json`
  (the importer's level schema, 12 §1) and `sandbox/<name>.png` (PNG, the same crop and size; the texture is
  re-read after every save), `settings.json` gains `sandboxLegalAccepted`; a level whose file does not parse is
  dropped from the index on the next list refresh, as `MyContraptionsView::Refresh` does.
