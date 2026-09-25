# Remake architecture

Status: **implemented**. This document began as the phase-2 plan on 2026-09-13; M0–M7 were completed by
2026-09-14 (§10 records what each milestone delivered). Sections 3–8 describe the resulting architecture and
verification system. Section 11 retains known deviations and unverified platform boundaries. Facts about the
original are cross-referenced to the reconstruction documents instead of being silently mixed with remake
decisions.

## 1. Goals — the fidelity contract

"Faithful remake" means, in testable terms:

| Property | Contract | Gate |
|---|---|---|
| Physics | every body trajectory equals the original's to the last bit | `tools/run_physics_regression.sh`: 55/55 bit-identical (09) — **done** |
| Item logic | forces/impulses/state transitions of every item (`*Utils::Update`, collision handlers) reproduce the original's bits | item-logic traces (§8, gate G4) — **done** |
| Rules | goals & stars (05 §3), progression & unlocks (05 §4), toolbox/undo/ghost semantics (05 §5), action ids (05 §7) | rule tests (G5) |
| Feel | fixed 1/120 s step, `Step(dt, 10, 10)`, `ClearForces` after each step, accumulator fed with `wallDt × 0.8`, wall dt clamped to [0, 0.1] s, render state = lerp of the two last physics states for dynamic-flag objects (04 §3) | code review + G4 |
| Presentation | original sprites, atlases, backgrounds, UI layouts (`*Scene.xml`), bitmap fonts, sounds, five languages (01, 06) | visual comparison, no bit gate |

Non-goals (00 "Not investigated"): online sharing / World of Contraptions / Level of the Week, IAP and the free
edition, ads/analytics, ka-core renderer internals, exact particle effects (confetti/sparkles are re-created
approximately; the "effects polish" once listed under M6 is dropped with them — decided 2026-09-14), original
save-file compatibility (§7).

**Asset ownership (decided):** the remake repository contains code and docs only. Rovio's assets are never
redistributed; the player supplies the original `.ipa` or `.apk` and runs the importer (§4) once.

## 2. Engine choice (decided 2026-09-13)

**raylib (6.0, vendored) + vendored Box2D 2.2.1 (patched, `core/third_party/Box2D`) + vendored device libm
(`aa_libm`) + own C++17 core.** Unit tests: doctest (vendored, decided 2026-09-13). Rejected: Godot (its own physics/scene model and scripting layer fight a bit-exact simulation; heavy),
SDL3 alone (would need an own sprite/atlas/audio/UI stack on top — raylib already is that thin layer), Axmol/cocos
(ships its own Box2D that the patched copy would have to replace; heavy, and its abstractions add nothing the game
needs). raylib is small, actively maintained, C, supports macOS/Windows/Linux/Android/iOS/web, and decodes PNG/MP3
itself.

## 3. Layering and the dependency rule

```
app/                      executable: argument parsing (the game; `--viewer` = the M2–M4 level viewer)
core/platform/  aa_platform   raylib: window, camera, sprites, the UI renderer, input, audio, the game application
core/ui/        aa_ui         the ported UI engine subset + the campaign scenes (06 §1) — no raylib, draws through
                              an abstract `Renderer`; owns the `Session` of the game scene
core/game/      aa_game       settings / progress / location states + their JSON files (05 §4, §8; §7), localisation
core/data/      aa_data       loaders for the imported asset formats (§4) → plain structs (levels, frames, UI
                              containers, scene trees, fonts, texts, tips)
core/sim/       aa_sim        the deterministic game core (§5): no raylib, no I/O, no platform libm
core/third_party/            Box2D (frozen), aa_libm (frozen), raylib 6.0 (pinned release), cJSON 1.7.18, doctest 2.4.11
```

Third-party code is vendored as pinned release tarballs with their licence files, like Box2D (the project is not
a git repository and must build offline); no `FetchContent`/submodules.

Rule: arrows point down only; `aa_sim` depends on Box2D and `aa_libm` and nothing else, and is a **headless
library** — the harness, the conformance tests and a future replay tool link it without a window. Its inputs are
plain structs it defines itself (`Level`, `FrameTable` — the atlas frame rects the template sizes are computed
from, 04 §4, passed once at start-up exactly like the harness's `InitializePhysicsObjectTemplates(frames)` recipe in
00); `aa_data` fills those structs from the imported files, so the dependency runs `aa_data → aa_sim`, never back. Everything the
original keeps in `st::` game state lives in `aa_sim`; the platform layer owns only GPU/audio/input resources and
the view tree. `aa_platform` is the only layer that knows about pixels; `aa_sim` works in world metres (02 §6).

Set-up mode is part of the core, not of the UI: it is physics-driven (separate SetUp `b2World` with selection
sensors, 04 §2; ghost validation bisects with `b2World::Step(0, 1, 1)`, 05 §5; snapping uses attachment records,
04 §7). The platform layer converts pointer events to world coordinates and calls the core.

## 4. Assets: import once, load native formats (decided; implemented in M0 — the output format is the contract
of `docs/12-asset-tree.md`)

`tools/import_assets.py <ipa|apk|dir> <assets-dir>` (Python, reusing `decrypt_assets.py`, `ka3d_dat.py`,
`ka3d_text.py`) produces an engine-native tree; the C++ side never sees AES, plists, PVR or KA3D:

| Original (01) | Imported as |
|---|---|
| `Levels/**/*.plist`, `0_Location.plist` (02) | `levels/<chapter>/<name>.json` — same schema (02 §2–§5), `version` normalised to 7 (legacy type 40 → 36, version-4 goals converted as the loader does, 02 §5) |
| `GameItems.plist/.pvr`, `GameItems2`, backgrounds, foregrounds, `UIElements` | `atlases/<name>.png` (PVR → PNG; the bundles use only RGBA8888/RGB565 — any other pixel format is an importer error, not a skip) + `<name>.json` frame table (name, rect, offset, rotated, source size) — frame **index** order preserved (03 §4, 01 §3: file order matters) |
| KA3D `SPRT`/`COMP`/`FONT` `.dat` + `.png` (one UI profile) | `ui/<profile>/<name>.json` + `.png` |
| `TEXTS_BASIC.dat`, `TEXTS_LANGUAGE_SELECTION.dat` | `texts/<locale>.json` |
| `Common/XML/*Scene.xml`, `Dialogs.xml`, `Fonts.xml`, `Tips.plist` | `ui/scenes/<name>.json`, `ui/fonts.json`, `tips.json` |
| `Sounds_high/*.mp3`, `Music_high/*.mp3`, `LevelThumbnails_350/*.jpg` | copied (raylib decodes MP3/JPG) |

Why converters rather than runtime loaders: the parsers exist and are verified (01), the C++ core stays free of
crypto/plist/PVR code, the intermediate tree is inspectable, and the converter can assert invariants once (frame
count 150, 116 levels, version fields) instead of the game handling every legacy case at runtime. Cost: one
command for the player; the importer validates its input and refuses unknown bundles.

**Floats (decided — forced by 02 §6):** the original parses every `<real>` with `strtod` into a double and the
level loader casts it to float (`GetValueFloat`), so the importer computes `float32(float(text))` (the same two
roundings), emits it as a decimal with 9 significant digits and re-parses it to check the bits round-trip. The
C++ loader takes cJSON's `valuedouble` (parsed with `strtod`) and casts it to `float` — the original's own path,
not `strtof` (the round trip the importer checks is exactly this one). JSON parsing: cJSON (MIT, the library the
original itself used for sharing). Verified on real data by `tests/test_level_loader.cpp` and the G3 level scenes.

## 5. The deterministic core (`aa_sim`)

### 5.1 Data model — where state lives

Mirror the original's plain-data structures with **value semantics** (copyable by assignment, no pointers between
them, integer handles instead of object pointers). This is not a style preference: the original snapshots whole
states by `memcpy` — `prevState` for render interpolation (04 §3), the 31-entry undo ring of `LevelLayout` (05 §5),
the ghost "good state" copy of `PhysicsObject` + `GameItem` (05 §5), the layout snapshot on mode 1 → 4 (05 §1) — so
copyability *is* behaviour.

| Struct | Contents | Original |
|---|---|---|
| `Level` | parsed level file: header, toolbox slots, `LevelItem[]` (type, handle, centre, angle, flags, rope end, itemData, ≤ 2 attachments), goal | `st::LevelLayout` (02 §3) |
| `WorldState` | `GameItem[]` (per item: type, handle, state bits, animation/logic fields) + `PhysicsObject[]` (04 §4: flags, position, angle, scale, template size, attachment records, body handles) + handle table | `st::WorldState`, `HandleManager` |
| `PhysicsWorld` | the `b2World` and the `b2Body*` arrays behind `PhysicsObject` body slots; **not** copied — rebuilt from `WorldState` on every mode transition exactly as the original does (`DestroyWorld/CreateWorld/CreateDynamicPhysics/CreateAttachments`, 04 §2) | `GamePhysicsUtils` |
| `GameState` | mode, accumulator, goal state (`GoalState`, collected stars), `goalReached`, paused flag, time scale | `st::GameState`, `GoalState` |
| `Toolbox`, `UndoQueue` (ring of 31 `Level` layouts), `GhostState`, `Camera` (world-space: centre, zoom) | 05 §5 | `st::Toolbox`, `UndoQueue*`, `GhostManipulation*`, `CameraUtils` |
| `ActionQueue` | `Action{id, handle, Vec2, payload}` with the original ids (05 §7) | `st::Action*` |
| `Session` | one loaded level: owns all of the above; the API the platform layer talks to | `GameScreenController` + `GameScreen` |

Body slots hold indices into `PhysicsWorld`, never `b2Body*` — a copied `WorldState` must remain valid after the
world is rebuilt.

### 5.2 Execution model — sequential by construction

Single-threaded. `Session::advance(wallDt)` implements 04 §3 literally: clamp, `accumulator += wallDt × 0.8`, then
per fixed step the item updates **in the original order** (Balloon, BoxingGlove, RadioController, Helicopter,
Slingshot, Magnet, PaperPlane, Spring, Bumper, TrapdoorLever, Seesaw), `Step(1/120, 10, 10)`, `ClearForces`,
state copy-out, Scissors/GoalStar updates, action processing; after the loop the per-frame updates (PiggyBank,
Dart set-up, goal evaluation) and the lerp. Nothing in the core may run concurrently: determinism, not
performance, is the requirement (the original ran on 2012 phones), and the platform thread calls the core from
one place. Rendering and audio decoding may use raylib's own threads; they never touch core state.

Float discipline: `float` wherever the original used `float`, `double` only where it called libm (04 §1);
`-ffp-contract=off -fno-fast-math` `PUBLIC` on `aa_sim` (as on Box2D); no `std::sin/cos/atan2/pow/exp/…` — the
`aa_*` functions only (`aa_libm/README.md`); expression shapes copied from the decompile and checked by gate G4,
never "simplified" (09 §2: association changes bits).

### 5.3 Item behaviour — free functions per type, switch dispatch

`core/sim/items/<item>.cpp` exposes for each type what the original's `st::<Item>Utils` exposes: `createPhysics
(obj, world, mode)`, `update(dt)` (only the types listed in 04 §3), `setInitialState`, collision handlers
(`beginContact`/`preSolve` cases), `getMagneticCenter`, `flip` hooks, and the render description (§6). Dispatch is a
`switch (ItemType)` in `physics_object.cpp`, as in `PhysicsObjectUtils::CreatePhysics`. No class hierarchy: a
1:1 mapping to the original's namespaces keeps each function comparable with its decompile and with the harness
traces, and 03 §2 / 08 are organised the same way.

`createPhysics` is hand-written from 03 §2 with the flip sign `s` and the template sizes (04 §4) — not
data-driven from the 08 dump — because the dump holds one flip state and vertex order (which follows `s`) affects
manifold point order and therefore bits. The dump is the oracle (gate G3), not the source.

The contact listener (`WorldContactListener`: sharp groups, glove trigger, goal sensors, stabbing, sounds — 04 §5,
02 §5, 03 §3) is one core class per mode; `PreSolve` gating logic is ported from the decompile and verified by G4.

### 5.4 Core API (as implemented in M3/M4; `core/sim/include/aa/sim/session.h`)

```
Session::load(const Level&, GameMode, SceneRecorder*)   // 05 §2: layout → WorldState → SetUp world → attachments →
                                                        // markers → prepareForNewLevel (fixed flags, SelectionArea,
                                                        // toolbox from the layout, undo reset + base snapshot)
Session::setViewport(const ScreenLayout&)               // native px ↔ world; the strip geometry follows the window
Session::setToolboxFrameSizes(const ToolboxFrameSizes&) // from the UIElements frame table (ToolboxFrameSizes::fromFrames)
Session::pointerDown/Move/Up/Cancel(id, nativePx)       // y down as the window reports it; queued, drained by advance()
Session::rotateHeld(dAngle), flipHeld(), takeFromToolbox(slot), returnHeld(), scrollToolbox(px), toggleToolbox()
Session::undo(), redo(), canUndo(), canRedo()           // UndoQueue (32 layouts, 0x1f cap)
Session::play(SceneRecorder* = nullptr), stop(), restart(), pause()   // toggleSimulation halves (the play
                                                        // layout through LevelLayoutUtils::Get, the simulation
                                                        // world with the WorldContactListener), restartLevel,
                                                        // GameScene::SetPaused's release
Session::advance(wallDt)                                // doFrame: the set-up branch (state 2), the simulation
                                                        // branch (state 4: touch handler → actions → accumulator
                                                        // → UpdateSimulation substeps → completion → tail), the
                                                        // default branch (state 6, completed); a state change
                                                        // recurses like the original
Session::renderState() → items (item order, lerped poses in simulation + the 11 §5 animation fields), heldIndex,
                          ManipulationOverlay, RenderBuzz, RenderToolbox, camera, markers, particles, completed
Session::controllerState(), accumulator(), playTime(), goalState(), random(), completing(), stopRequested(),
        substepCount(), renderPose(i)                   // GameScreenController / GameState fields (HUD, dump tools)
Session::queueAction(a), setSubstepObserver(fn), setAbsoluteTime(s)   // the conformance tools' hooks
Session::drainActions() / drainProcessedActions() → the processed st::Action list of the frame (+ substep index)
Session::drainEvents() → Sound (action 0xD → AudioId, the UI sounds), Buzz, GoalComplete, StarCollected,
                          LevelCompleted
Session::setSoundSink(SoundSink*)                       // M5: the looping clips (SoundRenderer::Render's
                                                        // Play / PlayLooping / Stop / SetClipVolume) go to the
                                                        // platform's AudioSystem synchronously; null = headless
Session::levelCompletePos()                             // +0xc9990: where the completion effect started (the popup)
Session::setTutorialContext(location, level), setTutorialPlayButton(nativePxYDown), tutorial()
                                                        // M5: tutorial_should_run's inputs, level 0's ButtonPlay,
                                                        // the TutorialState the GameTutorialView reads (05 §5)
Session::renderState().tutorialGhosts                   // the hand's Shelf / Book ghosts (11 §8)
Session::setMode(GameMode), markAllObjectsFixed() / NotFixed() / markAllStarsFixed(), setBackground(i),
        sandboxLevel(), tested(), setUnlockedItems(), editorToolbox(), editorToolboxActive(), removedHandles()
                                                        // M6: GameScreenController::setMode's 1 ↔ 4 / 1 ↔ 5
                                                        // transitions, WorldStateUtils::MarkAll*, the background
                                                        // button, LevelLayoutUtils::Get for the file, the
                                                        // editor strip (ToolboxUtils::SetFull), the toolbox step
Session::renderState().backgroundSlide / previousBackground   // the background change's slide (11 §2)
```

M4's simulation members (all value-semantic, §5.1): `accumulator_` (+0xC28F4), `prevState_` (the 0x2F2B4
memcpy), `renderPoses_` (the lerped copy's poses, re-mapped by handle across `removeInvalidItems`), `playTime_`,
`goalState_` (`GoalState`: reached, stars, the type-7 contact timers), `completing_` / `completionTimer_`,
`idleTime_`, `stopRequested_`, `SimulationContext` (transient pointers for the contact listener — `WorldState`,
`PhysicsWorld`, `ActionQueue`, `Goal`, `GoalState`, the time seed — refreshed whenever the world is rebuilt,
never stored in a snapshot). M6's editor members: `editorToolbox_` (`+0xc2094`, the controller's strip of every
unlocked item) with `editorActive_` standing for the `+0xc25b8` pointer — `toolbox_` stays the level's strip
that `LevelLayoutUtils::Get` / `Apply` read and write, every interaction path goes through the active one —
`readyLayout_` (`+0x5a4`), `testPlayLayout_` (`+0xc72a0`), `undoBackup_` (`+0x79c90`), `handlesAtReady_`
(`+0x29a4`), `removedHandles_` (`+0x29b4`), `unlocked_` (the GameProgress unlock bytes), the background slide
interpolator (`GameScreenTransitions+0xb0`). The per-item simulation state lives in `GameItem` (popped / pop timer, glove
state + interpolator + lattice angle, scissors state / snip machine, dart stuck / wobble, helicopter throttle /
rotor / tail phases, slingshot fired, piggy timer, magnet timer / frame, bumper on / timer, lever unlocked,
seesaw direction, controller button) so the `WorldState` snapshots carry it like the original's item blocks.

The platform never reads `WorldState` directly; `renderState()` is the only view, so the core can be replayed and
tested without a renderer.

## 6. Platform layer (`aa_platform`, raylib)

* **Camera & letterbox (decided — it is the original's own rule, 11 §1):** the play field keeps its 3.41 m width
  on every window; the floor strip shrinks from 130 to 39 virtual px as the window gets wider, the sides outside
  the play field are covered by the UI's border images (`BORDERIMAGE_LEFT/RIGHT`, only in the `1024X768`/`800X480`
  profiles — the imported `2048X1536` profile has none, so M2 scissors the play field and leaves the clear colour
  there until M5 imports a border), and anchored UI views are pulled toward the play field by
  `AnchorAspectCorrectionFactor`. This is exactly how the original ran on 16:9 Android phones, so 16:9/16:10
  desktop windows need no new design. Pan/zoom from the core's `Camera` (edge auto-scroll, completion zoom-out,
  05 §5–§6); the `FUN_000badac` matrix chain is set up directly on rlgl (`WorldRenderer::setCamera`).
* **Sprites:** atlas frame tables from §4; per-type render functions (`render/items.cpp`) mirror the original's
  renderer switch (multi-sprite items, 03 §1 footnote): body transforms come from `renderState()`, animation frame
  indices too when the original stores them in item state (balloon pop, piggy POW, rotor, magnet pulse, the
  scissors snip, bumper — 11 §5); the RC waves, the star spin, the goal-marker animation and the particle lists
  (sparkles, confetti — approximate) live in the core's `VisualState` (the original's `VisualWorldState`) and reach
  the renderer through `renderState()` as well (M4). Draw order,
  per-item part order and the container front pass are documented in 11 §3–§4 and are reproduced as is.
* **UI (implemented in M5, `core/ui` — `aa_ui`):** the ported `UI::` engine subset (06 §1.1: `View` layout /
  draw / hit-test, `ImageView`, the label views, `Button` / `ToggleButton` / `SlidingButton`, `ScrollView` +
  `PageControl`, `Animator`, `Scene` / `SceneManager` / `EventHandler`) built from the imported scene JSON, so
  screens are data as in the original; the campaign scenes (Splash, MainMenu, ChapterSelection, LevelSelection,
  LevelLoading, Game with `GameView` / `LevelCompletedView` / `GameTutorialView`, ChapterComplete ×2) with the
  06 §1.2 flow; `aa_ui` draws through an abstract `Renderer` (`DrawState` = translate / pivot / scale / angle /
  alpha / clip, `drawSprite`, `drawColorRect`) that `aa_platform::UiRenderer` implements on rlgl (one texture per
  sprite sheet / thumbnail, scissor clipping); the `GameScene` owns the `Session` and lets the platform subclass
  draw the world between the clear and the HUD (`drawWorld`). Bitmap fonts from KA3D `FONT` with the outline
  variants (06 §2); localisation from the TEXT bundles (`--locale` → `settings.json` → the OS language, 06 §3).
  UI coordinates: native px, origin top-left, y down; the world renderer's y-up space is converted at the
  `GameScene` boundary. The UI sprites are drawn at `pixelScale / 2` (06 §1.1, a remake decision).
  **One UI profile (decided):** iOS `2048X1536` atlases (the largest; the game data and the XML layouts are
  profile-independent, 01 §1) with the original's letterbox/anchor maths above. The Android `800X480` profile is
  only a lower-resolution atlas set with the same layouts, so it brings nothing for wide screens; the
  `AssetScalingForWidescreen` tweaks (books, comic, level buttons ×0.85–0.9, 11 §1) are applied by the screen
  size, not by profile: the flag is `PixelScale < 1` (a screen shorter than the 677-px reference — the two
  original entries, 1136×640 and 960×640, and nothing larger; implemented in M7).
* **Input mapping (decided 2026-09-13, implemented in M3):** touch on mobile is the original's model. Desktop: the
  mouse is the one finger — the original's `GameTouchHandler::Process` state machine runs unchanged on
  `pointerDown/Move/Up` in native px (left-drag moves, empty-space drag pans through state 0xB, the strip scrolls
  / takes items through states 0xD/0xE/0xF), the rotation gizmo ring (`RotationGizmo_iPhone`, 05 §5 — it exists
  precisely for single-pointer rotation) rotates, the flip button flips (the only flip source; "double-click
  flips" was a misreading), the mouse wheel while holding rotates by 5° as a convenience (`rotateHeld` + the
  pointer re-sent so the next `UpdatePos` applies it), keyboard shortcuts for play/stop/undo/redo/restart/return
  (README). The two-finger states are ported in M7 (05 §5): the pinch (10) needs a second pointer and
  `IsTablet` false, so it runs on the desktop only in the unit tests; 3 / 4 have no writer in the binary. The edge
  auto-scroll follows the phone rule (`DeviceParams::IsTablet` false) — a desktop window is treated as a phone
  (`--tablet` / `--phone` override it); the Android build is a tablet, as the original's (`GameApp::GameApp`
  sets `IsTablet = 1` unconditionally [verified: 0xa7b08]) — no pinch zoom, no edge scroll on any Android device.
* **Audio (implemented in M5, `audio.cpp`):** `AudioSystem` implements the core's `SoundSink` (06 §4 rules:
  the position filter, the 20-clip list, the master volume / mute, looping clips by handle, the two music tracks
  with the original's fade-out) on raylib's `Sound` aliases (one-shots) and looping `Music` streams; the clip
  table of 06 §5 (MP3, decoded by raylib). One-shots come from the core as `SessionEvent::Sound`, the looping
  clips through the `SoundSink` (`SoundRenderer::Render`, 06 §6), the UI clips from the view tree
  (`SoundPlayer`), the music from the scenes (`AppAudio`).
* **The application (`app.cpp`, `runApp`):** the window, the asset root, the frame tables, the templates, the
  `ScreenLayout`, the `ResourceProxy`, the localisation, the save store, the scene manager with the twelve
  scenes (M6 adds the comic, the credits, My Contraptions and the sandbox editor with its own `Session`),
  `GameApp::update`'s splash → main-menu state machine, the mouse as the one finger (`SceneManager::touches*`),
  Esc = the back key (the main menu answers with its exit dialog; `AppState::quitRequested` ends the loop), the
  viewer's wheel / F / Z / Y conveniences inside a level or the editor; the sandbox thumbnail
  (`writeThumbnail`: `WorldRenderer::drawForThumbnail` into a render texture, the original's crop, PNG); `--headless`
  runs the scripted walk without a window (exit 0 on success), `--ui-screenshots` writes one PNG per stop of the
  same walk (README).
* **Files & saves:** assets root from the command line; saves in the OS user-data directory (§7, `--save-dir`).
* **Platforms (implemented in M7, 2026-09-14 — `platform.h`, `platform_desktop.cpp`, `platform_android.cpp`):**
  the per-OS answers (`PlatformInfo`: data / asset directories, the OS languages, `IsTablet`, the mobile flag,
  the launch arguments), a log sink, the lifecycle hooks and the asset file reader — remake code standing in
  for the original's Java activity / `framework::OSInterface`. *Android* (`platform/android/`, a `NativeActivity` with
  `hasCode="false"`, `sensorLandscape` where the original manifest said `landscape` (0), `configChanges`
  orientation|keyboardHidden|screenSize as raylib's `rcore_android.c` requires, minSdk 24, targetSdk 34 —
  Android 16 ignores `screenOrientation` for apps targeting 36 on ≥ 600 dp screens, §11 item 15):
  `libamazing_alex.so` (raylib's `android_main` → our `main`; `-u ANativeActivity_onCreate` on our link,
  `--wrap=fopen` from raylib's INTERFACE options: a relative read-mode path opens from the APK's assets, an
  absolute one — the saves — through the wrap's fallback to the real fopen) built by `tools/build_android.sh`
  without Gradle (CMake + NDK → aapt2 → the asset tree as `assets/aa/` → zipalign → apksigner with a generated debug key; the launcher icon is the
  imported `branding/icon.png` resized by `tools/android_icon.py` into the mipmaps and an adaptive icon at
  packaging time — the player's own copy, like the assets, so `platform/android/res` holds only a stand-in). The window is the display
  (`InitWindow(0, 0)`, then `GetScreenWidth/Height` feed the `ScreenLayout` — a fixed size would add raylib's own
  framebuffer letterbox on top of ours); the tree is read in place from the APK: `PlatformInfo::assetDir = "aa"` and `AssetRoot` reads every file
  through `assetFileReader()` (raylib's `LoadFileData`), answers `exists` / `list` from the manifest, and hands
  `path()` to raylib's texture / sound / music loaders — no extraction, no `std::filesystem` on the tree (a
  copy extracted by an earlier build is removed on start);
  saves in `internalDataPath/saves`, `TMPDIR = internalDataPath/tmp`; the locale from `AConfiguration`; the
  arguments from the `debug.amazingalex.args` property (92 chars) or `files/args.txt`, relative paths under the
  data directory; stdout / stderr piped to logcat (`amazing_alex`). Input: raylib's per-frame touch points diffed
  by id into Began / Moved / Ended (`TouchTracker` in `app.cpp` — the original's `nativeInput` →
  `TouchUtils::QueueTouches*` model; `adb shell input tap` is too short for a per-frame poll, the scripts use
  150 ms swipes); `KEY_BACK` → the back key (the original's `nativeKeyInput` maps KEYCODE_BACK to 0x56, which the
  views accept like 0x28; KEYCODE_MENU → 0x57 reaches no view), read from raylib's pressed-key queue
  (`GetKeyPressed`) rather than `IsKeyPressed`: a down + up burst inside one poll leaves no key state to see. Lifecycle: `APP_CMD_PAUSE` / `RESUME` (raylib's
  command callback wrapped — the frame loop is blocked while paused, so both hooks run inside `EndDrawing`'s
  poll) → the fingers cancelled (`GameApp::touchCancel`), `SceneManager::pause(true)` (`Scene::setPaused` on
  the activating, inactivating and top scenes — `GameScene::setPaused(true)` [verified: 0x115e04]: state 2 →
  the held item released and the pause menu opened without animation, state 4 while not completing → the
  simulation stopped first; the sandbox scene has no override), the audio suspended
  (`AudioSystem::setSuspended`: `GameApp::activateAudio(false)` stops the output; here the master volume and the
  streams paused, apart from the user's mute). *Desktop* (`platform_desktop.cpp`): the environment's locale,
  nothing else — except macOS, where `CFLocaleCopyPreferredLanguages` follows the environment's variables (a
  Finder launch has no `LANG`) and `Contents/Resources/assets` next to the executable is the asset tree when a
  manifest lives there: the `.app` bundle of `tools/build_macos_app.sh` (the `amazing_alex_bundle` target,
  `platform/macos/Info.plist.in`, the imported tree copied in, `tools/macos_icon.py` → `AppIcon.icns` from canonical
  `branding/icon.png`, an ad-hoc `codesign`; `CFBundleShortVersionString` = the CMake project version). *Windows*:
  `cmake/toolchains/mingw-w64.cmake` + `tools/build_windows_mingw.sh` (a static libstdc++ / winpthread .exe,
  checked under Wine — headless only, Wine has no GL here); `tools/windows_icon.py` converts the canonical icon
  to a multi-resolution ICO embedded as the executable's icon; MSVC through the CI workflow. *Linux*:
  `tools/build_linux_docker.sh` (ubuntu:24.04, gcc 13, the X11 / Wayland / GL dev packages; the canonical PNG
  is copied next to the build for package metadata). Linux and Windows windows also load `branding/icon.png`
  through raylib after creation.
  iOS is not built (no Xcode / iOS SDK here; raylib 6.0 has no iOS platform) — §10.

## 7. Save data (implemented in M5 — `core/game`, `SaveStore`)

Own versioned JSON files (`settings.json`, `progress.json`, `location_<n>.json`, `sandbox/index.json` +
`sandbox/<name>.json` + `sandbox/<name>.png` — M6, 05 §8)
implementing the *semantics* of 05 §4 and §8: per-level state values 0/1/2/3+n, pages of four, unlock thresholds
`{0, 30, 75, 135}`, per-chapter item unlocks, "chapter complete" flags, played flags. Not the original binary
layout (`ccgp` + 0x800 bytes + CRC32) and not encrypted: there is no server to talk to and nothing to protect.
Importing an original save is out of scope (the format is documented in 05 §8 should anyone want it). Writes are
atomic (temp file + rename); a corrupt file is renamed aside and replaced by defaults, never silently overwritten.
Default directory: `~/Library/Application Support/AmazingAlex` (macOS), `$XDG_DATA_HOME/AmazingAlex` or
`~/.local/share/AmazingAlex` (Linux), `%APPDATA%\AmazingAlex` (Windows); `--save-dir` overrides it; the scripted walk modes (`--headless`, `--ui-screenshots`) without `--save-dir` use a fresh temporary directory (they mark levels done and flip the audio setting) (the tests use
a temporary directory). The field lists are in 05 §8.

## 8. Verification — gates, in layers

| Gate | Checks | Tooling |
|---|---|---|
| G1 `aa_libm_selftest` | the libm produces the reference bits on this platform/compiler | exists (`aa_libm/README.md`) |
| G2 physics regression | vendored Box2D = emulated original, 55 scenarios | exists (09) |
| G3 **CreatePhysics conformance** | for every item type × mode × flip, the core's bodies/fixtures/joints/mass overrides equal the 08 dump bit for bit | `tests/sim_scene_dump.cpp` emits the core's construction in the `.scene` format; `tools/sim_conformance.py` compares 186 item variants and all 116 levels in both modes (418/418) |
| G4 **simulation runs** | scripted runs of shipped levels (`tests/sim_scripts/*.txt`: `level`, `frames N`, `tap`, `goal`, `seed`, `listener off`) on the real `st::GameState` under Unicorn — `LevelLayoutUtils::Apply`, `GamePhysicsUtils::CreateWorld(1)` with the original `WorldContactListener`, every `…Utils::Update` and `GameScreen::ProcessSimulationAction` called by a Python transcription of `UpdateSimulation` / `doFrame` — vs `Session::play` + `advance(1/60)` in `aa_sim`: the `.scene`, the `.traj` of every substep and a `.sim` trace (per frame: accumulator, play time, goal state, `Random`, object flags / states, the lerped copy, the item blocks' animation fields, removals; every action with its payload words) bit-identical | `tools/uc_sim_oracle.py`, `tests/sim_run_dump.cpp`, `tools/sim_run_conformance.py` (`--sections`, `--md`, `--known`); CTest `sim_run_conformance` (label gate) — **M4: 39/39 identical** (the 16 G2 levels + one level per item family, 300 frames; `playtime` 720 frames through the goal, the 2.05 s countdown and state 6; `klassroom_completion` / `klassroom_tap` the injected goal and the stop touch) |
| G5 rule tests | goal types (02 §5), star/score rules, unlock rules, toolbox/undo/ghost state machine, action handling | unit tests with scripted pointer input against the headless core — M3: `tests/test_setup_undo.cpp` (the layout round trip over all 116 levels, the undo queue), `test_setup_pick_drag.cpp`, `test_setup_snap_ghost.cpp`, `test_setup_camera_limits.cpp` (`tests/setup_rig.h`: synthetic levels, `advance(1/60)` between pointer events, assertions on `drainActions()`, the touch state, poses, attachment records, ghost bits, strip counts, undo depth, the camera) |
| G5a **set-up manipulation conformance** | the ported `GameItemUtils::SetPos/UpdatePos/UpdateAngle/Flip/ManipulationStarted/Ended`, `AttachmentUtils::CalculateSnap/Detach/UnsnapAllNotAttached/AttachToNearbyItems`, `b2World::Step(0, 1, 1)` under the set-up listener and `IsColliding[WithAnother]` produce the same object poses, attachment records, rope ends, Box2D calls, snap results and actions as the emulated original on scripted manipulations of shipped levels | `tools/uc_setup_oracle.py` (a WorldState-shaped block in the harness, a `TouchState` buffer, `ActionQueueUtils::Add` hooked), `tests/sim_setup_dump.cpp`, `tools/setup_conformance.py`, the scripts in `tests/setup_scripts/`; CTest `setup_conformance` — M3: 7/7 scripts identical; 8/8 since `pipes_rotated.txt` (2026-09-14: the aligned snap at a non-zero angle — the earlier `pipes.txt` never actually snaps, so the port's `CalculateSnap` rotating the world offset instead of the local point went unnoticed) |
| G6 whole-catalogue runs | all 116 levels with the pre-placed layout only (toolbox items are the solution, so the goal is rarely reached — no level reaches it within 5 s; `Playtime` does at 6.3 s, G4): the G4 comparison for 5 s of play | `sim_run_conformance.py --all-levels --seconds 5 --known tests/sim_known_divergences.txt`; CTest `sim_catalogue_conformance` (label catalogue, ≈ 2.5 min) — **116/116 identical** (M4: 115 + 1 known; the `02_Room/LaunchingRamp` solver rounding found and fixed in the M5 follow-up, 09 §5) |

| G7 M6 tests | the level writer (`test_level_writer.cpp`: every shipped level write → load field-by-field bit-identical, a synthetic user level with attachments / a rope / a book / the Treehouse background), the sandbox files (`test_sandbox_store.cpp`: the index, unique names, save / list / load / remove with the thumbnail, the cap, a corrupt level file, a corrupt index set aside, the legal setting), the editor's core (`test_sandbox_modes.cpp`: `SetFull`'s order and amounts against the unlock set and the level's counts, `CreateNewSandbox`'s empty level, 1 → 5 → 1 with an item dragged into the strip and back out (its ready position), the level strip in the file, 1 → 4 fixing, a test play completing into a tested editor, the stop touch, the background slide + the world bound, the `tested` flag, the step's stars staying fixed across a strip move, the editor's `restart`), the dialogs (`test_ui_layout.cpp`: `InfoDialog` / `LegalDialog` / `MessageDialog` frames, texts, the back key, the delegate; the back key reaching a shown dialog through `View::keyDown`; the credits chain through PostProductionLead) | doctest cases in `aa_tests`; the `--headless` walk continues after the campaign: the My Contraptions book (the flag and the Classroom item set granted by the walk) → the legal prompt (first run) → a new level (index + file asserted) → three stars and a ball placed from the editor strip → the background button (the slide) → the test play completing into a tested editor (the ready button) → the toolbox step (the ball into the strip and back to its ready position) → back (the file reloaded, nothing fixed any more) → back (the file holds the contraption, every item fixed, the thumbnail rewritten) → the list with the level and the add tile → re-open (the contraption back) → Esc → the trash toggle → delete (the index, the files) → the books → the main menu → the credits (auto-scroll, the remake's version line; `--ui-screenshots` adds `20b_credits_operations`, the groups after PostProductionLead) → Esc → the exit dialog → confirm (`quitRequested`); on a fresh save the Classroom's begin comic (the frames tapped through) over the first level and over the level list, on a mastered save the end comic after the chapter panels; `--ui-screenshots` adds the M6 stops |
| G7 UI / game tests (M5) | the loaders (`test_ui_loaders.cpp`: frame counts, a glyph, a scene attribute, a text id, the tips), the progression (`test_progression.cpp`: fresh states, the page unlock at 3 of 4, `MarkLevelAsDone` keeps the maximum, `WasLevelImproved`, `CanPlayNextLevel`, the 30 / 75 / 135 thresholds, `UnlockItems`, the chapter-complete flags, save / load round trip, a corrupt file → defaults + aside), the layout and the engine (`test_ui_layout.cpp`: frames of MainMenu / GameView views at 1024×768 and 1280×720 derived by hand from `UpdateViewAnchors`, `WrapText`, hit test + draw, the settings slider's open / closed menu frames and button rows, `SceneManager` RemoveScene / duplicate push / PopScenesUntil, the shared touch id release), the tutorial (`test_tutorial.cpp`: `tutorial_should_run`, the three builders' step lists and constants, the step machine's timings, the dragged / oriented ghosts, the level-6 ring path) | doctest cases in `aa_tests`; the app's `--headless` walk (Splash → MainMenu (the settings menu opened, its layout asserted) → ChapterSelection → LevelSelection (both scrolled back to the Classroom / Playtime when a save opens elsewhere) → the loading screen (the back key ignored) → Playtime → the result panel → levels 1 and 2 solved with the tutorial's targets (the page unlock at 3 of 4 asserted) → the next level → the pause menu with the audio toggle persisted → back to the level list; with a save on the chapter's last level the chapter-complete panels) exits 0; `--ui-screenshots <dir>` for the eye check (`AA_WALK_DEBUG=1` traces every tap) |

| G1 / G7 on other platforms (M7) | `aa_libm_selftest` and `aa_tests` (112 cases, the asset-backed ones with the tree pushed / mounted, `AA_ASSETS` + `AA_SOURCE_DIR` from the environment) on the Android arm64 emulator (NDK 27 build, `adb push` + `adb shell`), under Wine (mingw-w64) and in Docker (gcc 13); the `--headless` walk on each; on Android through the APK (`tools/android_emulator.sh`: a fresh install, then twice on a kept save directory — assets are read in place from the APK, nothing extracted), `--ui-screenshots` pulled through `run-as` and compared with the macOS set at 2400×1080 (pixel-identical up to a few hundred edge pixels), a single-finger `adb shell input` session (menu → comic → level → a drag → play → HOME → resume with the pause menu) | `tools/build_android.sh`, `tools/android_emulator.sh`, `tools/build_windows_mingw.sh`, `tools/build_linux_docker.sh`; `.github/workflows/ci.yml` builds Linux / Windows (MSVC) / macOS + the Android library and runs G1 + `aa_tests` without assets (unverified from here — no runner) |

G3 and G4 reuse the scene/trajectory formats and comparison scripts of 09, so the core is checked with the same
instrument as Box2D. Each ported item was required to have G3 and G4 coverage before it counted as done. A
G7 emulated oracle for `TutorialUtils::Update` was not built: the step machine is small, its constants are read
from the literal pools and the script builders are checked one step at a time (`test_tutorial.cpp`).

## 9. Failure modes and how the design contains them

| Risk | Containment |
|---|---|
| Platform floating point (FMA, x87, fast-math, another libm) changes bits | flags `PUBLIC` on `aa_sim`/Box2D/`aa_libm`; G1 on every new platform; G2/G3/G4 data is portable (`.scene`/`.traj` replay without Unicorn, 09 §4) |
| Level float parse path differs from the original | closed: 02 §6 (`strtod` → double → `(float)`); the importer asserts the round-trip |
| A ported item formula is "obviously equivalent" but not bit-equal | G4 per item; 09 §2 lesson: test expression shapes, never trust the decompile's association |
| State copies drift from the original's snapshot semantics (undo, ghost, lerp) | value-semantic structs (§5.1), no pointers between them; G5 |
| Draw order / animation timing | closed: 11 §3–§5 (type tables, container front pass, per-item parts, timers); rendering has no bit gate, so a wrong order is a visible, not a silent, failure |
| Importer fed a wrong/patched bundle | the importer validates counts and hashes of known files and reports what it could not find; the game refuses to start without a complete tree |
| Save corruption | atomic writes, defaults on failure (§7) |
| Desktop input cannot express the touch gestures | gizmo ring + wheel (§6); tutorial texts that mention two fingers are `Platform = 1` tips (06 §3) and are skipped on desktop |

## 10. Milestone history

| M | Deliverable | Exit criterion |
|---|---|---|
| M0 — **done 2026-09-13** | CMake tree (`aa_libm`, Box2D, raylib 6.0, cJSON, doctest, `aa_sim`, `aa_data`, `aa_platform`, `app`, tests); `import_assets.py` (docs/12); float path closed (§4); `run_physics_regression.sh` fails by exit code (`trace_compare.py --expect-identical 55`); G2/G3 are registered in CTest only when the harness (`.venv`, the Android `.so`) exists | builds on macOS (Linux/Windows by construction, unverified — §11); the importer converts the iOS bundle completely (116 levels, 150 frames, 5 locales; deterministic); G1/G2 green |
| M1 — **done 2026-09-13** | `aa_sim` skeleton: `Level`, `FrameTable`, templates for all 42 types (04 §4, §7; checked against 08), filters (04 §5), `WorldState`/`HandleManager`, `PhysicsWorld` with the recording construction primitives (`SceneRecorder` writes the `.scene` grammar of `uc_trace.py`), `createPhysics` for 18 simple types (1 Shelf, 2/3/4/16/26/41 balls, 7 Bucket, 8 Hook, 10/11/12 boxes, 15 Book, 23 GoalStar, 24 Billboard, 30 HangingLamp, 31 WorldBound, 32 LaundryBasket), `applyLayout`/`Session::load` building the set-up world | **G3 green: 134/134 scenes identical** — 90 drops (18 types × {simulation, set-up} × {normal, flipped} + Book colours 1–3 × 4 + WorldBound backgrounds 1–3 × 2) and the 22 shipped levels made only of those types, in both modes; `tools/sim_conformance.py`, CTest `sim_conformance` |
| M2 — **done 2026-09-13** | `createPhysics` for all 42 types (joints, in-place fixture edits, body destruction; `PhysicsObject::joints`), attachments on load (`AttachmentUtils::CreateJoint` + `RopeUtils::UpdatePosFromAttachedObjects` ports), the per-type default item blocks of `st::ItemInfos` (`GameItem::defaults`), `Session::renderState()` / `VisualState` (goal markers) / `Camera`, `Session::rebuildWorld`; `aa_platform`: `ScreenLayout`, `Atlas`, `SpriteBatch`, `WorldRenderer`, `render/items.cpp`, the level viewer (`amazing_alex`, keys in the README) with `--screenshots`; `tools/render_contact_sheet.py`, `tools/decomp_dis.py` | **G3 green: 418/418** (186 drops: 42 types × 2 modes × 2 flips + Book colours + WorldBound backgrounds; all 116 levels × 2 modes with attachments; ≈ 10 s); `aa_tests` 27/27 (screen layout vectors, `renderState()` of all 116 levels in both modes); 116 screenshots + contact sheet checked against the thumbnails |
| M3 — **done 2026-09-14** | set-up interaction: the `GameTouchHandler::Process` state machine (`touch_handler.cpp`), `GameItemUtils` / `AttachmentUtils` / `GhostManipulationUtils` / the animation utils ports (`interaction.cpp`, `attachments.cpp`, `animations.cpp`, rope / slingshot / zip-line set-up paths), `processActions` + the five `ItemActions*` handlers, the toolbox strip (`toolbox.cpp`), undo / redo / play / stop / restart / pause, edge auto-scroll, `Session::renderState()` with the held item, gizmos, buzz and strip; `aa_platform`: the held item + overlay + fixed-item flash in `WorldRenderer`, `ToolboxRenderer`, the viewer plays with the mouse (`--script` replays pointer events); `ScreenLayout` moved into `core/sim`; harness fixes (rope end-to-end joints run through `AttachmentChanged`, one hook per address, joint pointer reuse) | **G3 418/418 unchanged**, G2 55/55, `aa_tests` 52 cases (54 800 assertions), **G5a 7/7** (`setup_conformance`); scripted play of Classroom levels (strip take-out / return, snap, ghost, flip button, ring / wheel rotation, undo, buzz) checked by screenshots (also a 1280×720 window and a mirrored held item); the M2 screenshot set is pixel-identical outside the strip |
| M4 — **done 2026-09-14** | the simulation: `Session::advance` = `doFrame` (set-up / simulation / completed branches, the recursion on a state change, the common tail with `StopRunawayObjects`, `RemoveInvalidItems`, the prev copy, the 5 s idle stop), `updateSimulation` (`simulation.cpp`: 1/120 s substeps, `getStateFromPhysics`, `lerpState`, the trailing updates with the pre-loop accumulator), the `WorldContactListener` port (`contact_listener.cpp`: balloon pops, dart stab, scissors / bumper / helicopter / glove handlers, collision sounds, piggy break, goals 1 / 2), the world-side actions 7 / 12 / 13 / 17 / 18 / 19 (`forces.cpp`, `sounds.cpp`), every `…Utils::Update` (Balloon, PaperPlane, Magnet, Helicopter, Slingshot, Spring, BoxingGlove, Bumper, RadioController, TrapdoorLever, Seesaw, Scissors + `RopeUtils::Cut`, GoalStar, PiggyBank, Dart set-up mode), `SetInitialState`, the original `HandleManager` semantics, goals (`goals.cpp`: `GoalStateUtils::Update`, `IsGoalComplete` for types 3–10), the three-stars rule, the completion sequence (state machine only) → state 6, the simulation touch (0x15 / 0x18), `VisualState` (goal-marker animation, star spin, RC waves, particles), the renderer's 11 §5 animations, the viewer's HUD line 3 and `play` / `stop` / `sim N` script commands, the G4 / G6 tooling | **G4 39/39, G6 115/116 + 1 known** (§8), G3 418/418 and G5a 7/7 unchanged, G2 55/55, `aa_tests` 64 cases (54 892 assertions: `test_simulation.cpp` — step bits, HandleManager, lerp, dart / balloon removal, goals 3 / 4 / 6 / 7 / 8 / 9 / 10, stars, the stop touch, the idle stop, `SetInitialState`); scripted play of Playtime (self-completing), CatchBall with the shelf dragged out of the strip (star, goal, confetti, state 6, restart), DoomedBalloon / SkiLiftSabotage (scissors, cut rope) checked by screenshots; the M3 screenshot set pixel-identical |
| M5 — **done 2026-09-14** | `core/data`: the UI container / scene / font / text / tip loaders; `core/game`: `Settings`, `GameProgress`, `LocationState` (the `LocationStateUtils` / `GameProgressUtils` ports), `SaveStore` (JSON, atomic, aside), `Localization`; `core/sim`: `SoundSink` + `SoundRenderer::Render` (`sound_renderer.cpp`, the looping clips), `tutorial.cpp` (`TutorialUtils`, the seven Classroom scripts) with the `doFrame` hooks, `levelCompletePos`; `core/ui`: the UI engine subset (06 §1.1) and the campaign scenes (06 §1.2) — `SplashScene`, `MainMenuScene`, `ChapterSelectionScene`, `LevelSelectionScene` + `LevelSelectorButton`, `LevelLoadingScene`, `GameScene` (`GameView`, `LevelCompletedView`, `GameTutorialView`), `ChapterCompleteScene` ×2, the progression writes in the original order; `aa_platform`: `AudioSystem`, `UiRenderer`, `app.cpp` (`runApp`: the game, `--headless`, `--ui-screenshots`), the world renderer's tutorial ghosts; `amazing_alex` = the game, `--viewer` = the old tool; the importer's signed FONT leading / tracking; raylib's JPEG decoder for the thumbnails | **G4 39/39, G6 116/116 (LaunchingRamp closed, 09 §5), G3 418/418, G5a 7/7, G2 55/55**; `aa_tests` 92 cases (55 280 assertions: + `test_ui_loaders` 5, `test_progression` 10, `test_ui_layout` 8, `test_tutorial` 5); `--headless` exits 0 (fresh install and second run, a corrupt save set aside, the audio toggle persisted, a save edited to the last level → both chapter panels, the Backyard unlocked, the items unlocked); `--ui-screenshots` at 1024×768 and 1280×720 checked by eye (every scene, the tutorial hand on levels 0 / 1 with the shelf ghost, the pause menu, the result panel, the letterbox borders), the five locales' menus and level lists; the M4 viewer screenshot set pixel-identical |
| M6 — **done 2026-09-14** | the sandbox editor and My Contraptions, the chapter comics, the credits, the dialogs (scope decided 2026-09-14; effects polish dropped as a §1 non-goal, mobile builds → M7). `core/data`: `writeLevelFile` (the importer's schema back to disk); `core/game`: the sandbox location in `SaveStore` (index, level files, thumbnails, unique names, the cap), `Settings::sandboxLegalAccepted`; `core/sim`: `Session::setMode` (the 1 ↔ 4 / 1 ↔ 5 transitions of `GameScreenController::setMode`), the editor toolbox (`ToolboxUtils::SetFull` + `prepareForNewLevel`'s amounts), the toolbox step (`UpdateSandboxToolboxLayout`, the mode-5 branches of actions 8 / 9 / 10), the test-play completion, the `tested` flag, the background change with its slide; `core/ui`: `dialogs.cpp` (`DialogBackground`, `InfoDialog`, `MessageDialog`), `extra_scenes.cpp` (`ComicScene` / `ComicView` + `showChapterComic` at its four call sites, `CreditsScene` / `CreditsView`, `MyContraptionsScene` / `MyContraptionsView`), `sandbox_scene.cpp` (`SandboxScene` with its own `Session`, `SandboxView`), `LevelSelectorButton` types 3 / 6 + the trash can, the main menu's credits button and exit dialog, the seventh book, `LevelLoadingScene` locations 2 / 3, the `AppState` sandbox location (the fifth `LocationInfo`), `ScrollViewDelegate::StartedDecelerating`; `core/platform`: `PlatformSandboxScene`, the thumbnail writer, the background slide in `WorldRenderer`, the walker's M6 tail (§8 G7) | `aa_tests` 105 cases (`test_level_writer`, `test_sandbox_store`, `test_sandbox_modes`, the dialog and key cases), G2 55/55, G3 418/418, G5a 7/7, G4 39/39, **G6 116/116** unchanged; `--headless` exits 0 on a fresh save, a second run and a mastered save (§8 G7); `--ui-screenshots` at 1024×768 and 1280×720 checked by eye (comic, editor in modes 1 / 4 / 5, the background slide, the list with a thumbnail and the trash cans, the legal prompt, the credits, the exit dialog); the M4 viewer screenshot set pixel-identical (116/116) |
| M7 — **done 2026-09-14** | platforms (scope decided 2026-09-14: Android, Linux, Windows; iOS excluded — no Xcode / iOS SDK on this machine, raylib 6.0 has no iOS platform). `core/sim`: the two-finger states 3 / 4 / 10 of `GameTouchHandler::Process` (05 §5); `core/ui`: `Scene::setPaused` / `SceneManager::pause` / `GameScene::setPaused`, the `AssetScalingForWidescreen` tweaks (11 §1), the side borders' > 1200 px scaling and the right border's −1; `core/platform`: `platform.h` + the desktop / Android answers, the touch loop, the BACK key, the lifecycle hooks, the asset extraction, `AudioSystem::setSuspended`, `--tablet` / `--phone`; the importer's `--border-profile`; `platform/android/` + `tools/build_android.sh` + `tools/android_emulator.sh`; `cmake/toolchains/mingw-w64.cmake` + `tools/build_windows_mingw.sh`; `tools/build_linux_docker.sh`; `.github/workflows/ci.yml` | `aa_tests` 112 cases (the two-finger, widescreen, border, 20:9 layout, pause-chain and level-button-centre cases added), G2 / G3 / G5a / G4 / G6 unchanged on macOS; Android arm64 emulator: G1, `aa_tests` 110/110 (before the pause-chain case), the APK's headless walk (fresh + twice on a kept save), the scene screenshots at 2400×1080 = the macOS set, a touch session and the background / resume by hand (§8); Windows (mingw + Wine): G1, `aa_tests`, headless; Linux (Docker, gcc 13): G1, `aa_tests` 112/112, headless |
| M8 (follow-up, not planned) | iOS: an Xcode toolchain and either raylib's SDL backend or a custom iOS platform for raylib (6.0 ships none), the same `platform.h` answers from `NSBundle` / `NSSearchPathForDirectoriesInDomains`, the touch loop unchanged | the campaign on an iPhone / iPad |

## 11. Open items

Closed on 2026-09-13 (all four look-ups done and documented): level float parse path (02 §6), draw order and
sprite parts (11 §3–§4), animation constants (11 §5), tutorial scripts (05 §5); desktop input, UI profile and the
sandbox scope were decided (§6, §10). Remaining:

1. ~~Trapdoor door anchors~~ — decoded (11 §4: (3 px, 0.6·h) and (w − 4, 0.6·h) of frames 140/143), M2.
2. The particle systems (sparkles on star pick-up, level-complete confetti) and the `FUN_000c0440` overlays stay
   approximate by decision (§1); their trigger points are documented (11 §3 step 10, §5).
3. ~~Glove-lattice helper rotations and the visibility sort key~~ — decoded from the disassembly in M2 (11 §3
   step 6, §4). Still only visually checked: the seesaw arm's odd anchor (b[1] passed as pixels) and the
   trapdoor door anchors match the thumbnails at rest; opened doors / moving arms are M4's check. 11 was derived
   from the Android build only (the iOS renderer is NEON code, harder to read); the assets and layouts are
   identical, so no difference is expected.
8. **Letterbox borders — closed in M7:** `tools/import_assets.py --border-profile 1024X768` resamples the
   original's `BORDER_BORDER` sheet ×2 into the imported profile (12 §2), `GameView` places and scales it as the
   original (11 §1); without the sheet the views still fall back to their `BackgroundColor` (0, 0, 150).
9. ~~Rope root `SetActive(false/true)`~~ — closed: the disassembly of `UpdateLinkPositionsFromExtremes`
   (`ldr r4, [r4, #0x98]` … `mov r0, r4` for both calls, the fixture walk to category 0x10 = the selection
   bit) touches the root body at slot 0, which is what `items/rope.cpp` does [verified].
10. ~~Legacy built-in controller paths~~ — closed in M3: `WorldStateUtils::AddNewItem(state, type, pos, angle,
    fromToolbox)` sets `Truck/Trapdoor/Helicopter+8 = fromToolbox`; `ItemActionsNewSelection` passes `true`
    (`str r12 = 1, [sp]`), so every item dragged out of the strip carries its controller as a 4th body until the
    first `ManipulationEnded` splits it into an RCController / TrapdoorLever item paired by handle (05 §5); version-7
    levels keep `+8 = 0`. Covered by `test_setup_snap_ghost.cpp` (truck: 4 bodies → 3 + the controller item) — not
    by G5a, whose harness has no live `GameItem` collection for `AddNewItem`.
11. **M3 leftovers:** the two-finger touch states 3 / 4 / 10 are ported in M7 (05 §5: 10 is the phone pinch,
    which the Android original never enters because it is always a tablet; 3 / 4 have no writer in the binary
    and are pinned by forced-state tests only); the
    scissors / dart idle animations are ported in M4 (see 12); the empty-space pan applies native px deltas literally as the original does (a
    zoomed camera pans faster in world terms); `GameMode` 1 / 5 (sandbox) paths and `ToolboxUtils::SetFull` are
    carried but not exercised; `MarkAllSolutionItemsFromToolboxNotFixed` (saved solutions) is M5; `pause()` cancels
    the pointers itself where the original's UI layer does; the fixed-item flash formula is what the GL state says
    (texture at amplitude 1, orange at 0 — the flash is strongest at the end of the 1 s decay) and has not been
    compared against a device; the strip glide always starts off screen (the original's very first level glides
    from x = 0, a fresh `GameScreenTransitions`); after `restart` the SelectionArea is absent until the next level
    (original quirk, asserted by a test); `undo` / `redo` refuse while a touch is in progress (any state but idle; the
    original's UI decides — a rebuilt world under a live touch state would index stale objects), and `undo` /
    `redo` / `play` / `stop` / `restart` refuse while the flip animation has input disabled (the original's
    buttons are disabled with the touches); `restoreGameState` resets the touch state, the ghost copy and its
    glide, the selection / flip animations and the pending touch events together with the world (the original
    keeps them and reads stale indices from its fixed 126-slot arrays — a zeroed slot; the remake's vectors have
    no such slot); `play` refuses while a ghost glide runs (`LevelLayoutUtils::Get` copies every object, so the
    original would carry the ghost copy into the simulation as a real item); fixture user data (body index + 1,
    per item in docs/08) is ported only where the pick query reads it — the rope ends and the slingshot (pouch
    = 2) — every other fixture carries 0, which the pick treats as "no per-body selectable byte" (equivalent
    while only rope ends clear that bit).
12. **M4 leftovers and deviations:** (a) ~~`02_Room/LaunchingRamp` diverges inside Box2D~~ — closed: the
    2.2.1 solver's re-derived transforms vs the trunk's stored ones (09 §5, `useBodyTransforms`), G6 116/116;
    (b) the scissors / dart idle timers (`Scissors+0x14`, `Dart+0x18`) are seeded in the original by
    `Random::SetSeed((int)&item)` — the item block's heap address [verified: `ScissorsUtils::SetInitialState`,
    `DartUtils::SetInitialState`], which no two launches of the original share either; the remake and the oracle
    seed with the item handle (`GameItem::setInitialState`, `override_idle_timers`) — closed as the only
    reproducible choice, the timing is deterministic per level; (c) sparkles, confetti and the star-spin
    start frame / phase (`lrand48` in the original) are approximate (particle lists in `VisualState`, seeded
    from the game `Random`); (d) `SoundRenderer::Render`'s clip-handle writes into the item blocks (looping
    sounds) are not modelled — audio is M5, the fields are excluded from the G4 dumps; (e) `RenderWorld`'s
    goal-marker loop runs to the *item count*, so whether the original draws a marker for the goal's own item
    when it is the last one is unverified (the remake draws every laid-out marker); (f) the goal markers a touch
    hid come back 5 s after the last touch (`doFrame`'s set-up tail, `+0xC9984` → `SetGoalMarkers`, campaign /
    test play only) — ported, with `TutorialUtils::Start` there since M5; (g) the bouncy
    ball's sound pitch draws from `TimeUtils::GetAbsoluteTime` — `setAbsoluteTime` seeds it (the dump tools pass
    0); (h) `restart()` / `load()` with a stop touch pending toggle into play like the original (`doFrame`'s first
    statement; `+0xC99B8` is not cleared by `restartLevel` / `prepareForNewLevel`), and an action 11 queued by
    the frame in which the run stops is processed in set-up and starts the completion sequence there — also as
    the original (`restartLevel` / `prepareForNewLevel` clear the queue, `toggleSimulation` does not); (i) `TruckUtils::UpdateAnimation`, `RadioControllerUtils::UpdateAnimation` were checked against the
    disassembly (11 §5), `VisualWorldStateUtils::UpdateGoals` / `UpdateStars` from the decompile only (render
    side, no bit gate); (j) the HUD font of the viewer is small at 1024 px; (k) `gameState.timeScale` and the
    slow-motion drag are dead paths (04 §3) and are carried as constants.
13. **M5 leftovers and deviations:** (a) the UI sprite scale `pixelScale / 2` is a remake decision (06 §1.1) —
    the original picks a profile per device and draws its sprites 1:1; (b) the level-complete popup and the
    tutorial hand use the original's 4:3 mapping (`W / 3.41`, `(H − floor) / 2.12459`), which drifts from the
    world position on letterboxed windows exactly as the original did; (c) the loading screen shows
    "Loading…" only — faithful: the shipped `LevelLoadingScene` is `Background` + `LabelLoading` and
    `LevelLoadingView::Init` reads nothing else, the tips are dead data; the remake shows the level's tip in the
    game instead (a remake addition, 06 §3); (d) `PageControl`'s page-number label: x = the active
    dot's x, y = dot y − label height + activeH / 2.05 with the division in double [verified: `RefreshPages`
    disassembly] — a device screenshot would still be welcome, none is available; (e) the transition's second
    `TutorialUtils::Update` is approximated in the toggling frame (the port takes doFrame case 3 at once; inert
    in practice, the play button has already stopped the script) and `ShowOverlay(1)` shows the view in the
    campaign and hides it in every other mode [verified]; (f) the level-0 script needs the UI's `ButtonPlay`
    centre: the game runs it on every walk (`07b_game_tutorial`), the viewer draws no UI and the headless core
    has none by design — not a defect; (g) `UpdateLocale` on a live tree: the original calls
    `SceneManager::UpdateLocale` only from `initializeSplash` / `initializeGame` after `LoadLocalization` — there
    is no live switch in the original either, faithful; (h) the dialogs, `LetterBoxView`, `TextFieldView`, `ActivityIndicator`, the Rovio news / credits / buy
    / share / WoC / My Levels buttons are present but inert (06 §1.1; the main menu's links slider and the LotW /
    WoC books were dropped 2026-09-15); (i) the audio was checked
    for state (mute persisted, clips loaded, no errors), not by ear; (j) the `--headless` walk plays Playtime
    and solves levels 1 and 2 with the tutorial's own targets (the page unlock at 3 of 4 asserted end-to-end),
    scrolls the books / the level list back on a save that opens elsewhere, and takes the chapter-panel path on a
    last-level save; the "forward on a chapter's last level" branch is covered by the `SceneManager` unit test
    only (no Classroom last-level solution script); the G7 tutorial oracle was judged unnecessary — the tutorial
    changes no physics, progression or save, the doctest pins every constant read from the disassembly, and a
    manual walkthrough of all seven scripts passed; (k) the chapter-0 comic after
    the chapter panels (`showChapterComic`) — ported in M6; (l) `MarkAllSolutionItemsFromToolboxNotFixed`
    (saved solutions) is still not ported (its offline caller is `prepareForNewLevel`'s campaign branch with the
    level's own `_solution` plist — a file the remake never writes: online / sharing); (m) the shared touch id of `Button` is released where
    the original leaks it (06 §1.1, a remake fix); (n) a save that cannot be written is logged to stderr and the
    game goes on (the original would crash on its file API); (o) the frozen tutorial hand after a stopped run
    (05 §5) is kept as in the original; (p) `GameView::ButtonPressed` also runs `Session::pause()` (the held
    item returns to the strip) — the original only clears the gizmo and stops the tutorial, the pause menu's
    touch cancel drops a plain drag where it is; (h) is closed in M6 for the dialogs, the credits, the comic,
    My Contraptions and the sandbox (06 §1.1 / §1.3).
14. **M6 leftovers and deviations:** (a) the sandbox thumbnail is a PNG (`sandbox/<name>.png`) where the
    original wrote a JPEG at quality 100 — the same crop and size; its pixels have no gate (05 §8); (b) the
    `_solution` files (a completed test play, an untested level's back button) are not written — nothing
    offline reads them (the readers are `LevelLoadingScene` cases 6 / 7 and `SharingManager`); (c) the editor's
    background button never comes back after an animated `HideGameControls` in the original (a zero-delta
    show animation) — the port returns it to its anchored place (06 §1.3); (d) the credits' name table lacks
    `PostProductionLead`, so the groups after it overlap the earlier ones in the original — the remake adds
    the group and the chain is continuous (a deviation, 06 §1.3); (e) `GenerateUniqueFilename` is ported as
    is: `st::Random` seeded with the wall clock's low 32 bits, `GetInt(0, 0x7fffffff)` = `CustomRand`'s
    0..32767 (the modulus wraps to INT_MIN), `%g` → `std::to_string` (05 §8); (f) the sandbox
    location has no `LocationState` file (the original's `LocationStateUtils::Load` with index −3 finds
    none) — `AppState::loadLocation` builds a fresh one and never saves it; (g) the `--headless` walk grants
    `GameProgress.myContraptions` and the Classroom item set as a finished chapter would (the campaign part of
    the walk stops at level 3), and builds its three-star contraption from the editor strip; a hand-played
    session in the window was not part of this stage's checks; (h) the toolbox step's forward path (the
    sharing view) is dropped with the online features — the step is reachable and its back button reloads the
    file, as in the original; (i) the `AssetScalingForWidescreen` tweaks are applied since M7 (11 §1: `PixelScale < 1`); (j) `restartLevel`'s sandbox branch is ported, see (p), but has no button in
    `SandboxView`, as in the original; (k) the original's world bound keeps the previous background's shape until
    the next `restoreGameState` (the test play's snapshot carries the new index) — the remake rebuilds its
    physics in `Session::setBackground` at once (a deviation, 05 §1); (l) Linux / Windows builds: item 6 (M7); (m) `ErrorParsingLevel` is a dead path, in the original as here: `LocationInfoUtils::
    LoadFromDocs` / `AppState::loadSandboxLocation` blank the title of a level whose file does not parse and
    `MyContraptionsView::Refresh` drops such an entry from the index before `LevelLoadingScene` case 2 could
    fail on it, and `MyContraptionsView::Show` starts with `HideAllDialogs` anyway — ported, untested by the
    walk; (n) an M5 reading corrected in M6 [verified: the writers of
    the controller's state field `+0x31c80` — `displayGoals` 1, `setSetUpState` 2, `setSimulationState` 4,
    `setSimulationToSetUpTransitionState` 5, `setCompletedState` 6 / 2, `setEditorState` 7, `setLevelMenuState`
    8 — and doFrame's state-1 case]: state 1 is the goals-display state entered by `startLevelWithGoals(true)`
    (= `playNewLevel` in the campaign; `continuePlaying` passes false and `handleButtonRelease(1)` has no
    caller) — one frame that displays the toolbox, then `ShowOverlay(1)` and the transition to set-up (state
    5); a run's stop (`toggleSimulation`) goes to state 5 directly, so `ShowOverlay(1)` (06 §1.2) is not "a run's
    way back". The port's `GameScene` shows the tutorial view when the script runs and re-shows it on the 4 → 2
    change, which is a no-op there (`ShowOverlay(8)` leaves the view alone); the M5 gates are unchanged.; (o) fixed after the M6 code
    review (2026-09-14): `View::keyDown` did not forward to subviews (the original's `View::KeyDown` does,
    0x10872c — the dialogs' back-key handlers were unreachable); `SandboxScene::reloadLevel` loaded the
    step's file in mode 1, so `setMode(1)`'s 5 → 1 un-fix never ran and the stars stayed locked after the
    step's back button (the walk asserts it now); `SandboxView::hide` faded to the current alpha instead of
    0 (0x13ace4); `MyContraptionsView::refresh` recursed without bound when the index could not be written
    (a read-only save directory) — a bounded restart now. Two original bugs of the toolbox step fixed on
    the user's call (2026-09-14): its `readyLayout` is taken before `MarkAllStarsFixed` (0xba014 < 0xba114)
    and `UpdateSandboxToolboxLayout` does not re-fix after its `restoreGameState`, so from the first strip
    move on the original's stars are movable in the step, and a star dragged onto the strip sets the
    removing flag (+0xc99ba) before action 9's star refusal (0xb8938 < 0xb8958), which then gates every
    later action (the touch stays in its returning state) until the step's back button — the port re-fixes
    the stars after every rebuild of the step and refuses the star before touching the flag
    (`test_sandbox_modes.cpp`, "keeps its stars fixed"). (p) `restartLevel`'s mode 1 / 5 branch is ported
    (`Session::restart`: an empty level — the plain-floor world bound, a fresh level strip, the default
    header; the editor strip's amounts stay) [verified: 0xb9884..0xb9a1c]; still no editor button reaches it,
    as in the original.
15. **M7 leftovers and deviations (2026-09-14):** (a) iOS is not built (§10, M8); (b) no physical Android device
    was available — real touch timing, audio latency and GPU drivers stay unverified (the emulator's GLES 2 is
    SwiftShader); (c) the two-finger gestures were not hand-tested on a device: the Android original is always
    a tablet (no pinch, no edge scroll) and the remake reproduces that — `--phone` in the launch arguments enables
    the pinch for a hand check; states 3 / 4 are unreachable and pinned by forced-state tests only; (d) the
    widescreen flag's `PixelScale < 1` rule is the remake's reading of the original's two-entry table (11 §1) —
    it fires on no modern phone, as the original's did not either; the thumbnail's ≈ 15 px offset seen with
    it on turned out to be three M5 deviations in `LevelSelectorButton` / `LevelSelectionView`, fixed the
    same day: `Setup` set the button's state (the original's `Refresh` calls `SetVisible` / `SetInteraction`
    after `Setup`, never `SetState`), so `Button::SetState → ZoomIn` captured the views' frames and pivots
    mid-setup and its 0.05 s animation put them back (the level number therefore sat at the panel's top-left
    instead of its centre — visible at every size); `Update` ran `RefreshThumbs` after `Refresh` instead of
    before it (the thumb's centre pivot was captured too late); `FrameNormal`'s pivot was forced to its centre
    where the original keeps the sprite's (the hole's centre, anchored HPIVOT / VPIVOT to the panel's centre)
    — the test "the level button's thumbnail, frame hole and number share the panel centre" pins all three;
    (e) `sensorLandscape` where the original's
    manifest locked `landscape` (0); targetSdk 34 so that Android 16 honours it on tablets (apps targeting 36 lose
    fixed orientations on ≥ 600 dp screens) — moving to 36 needs a resizable / multi-orientation layout; (f)
    the Android build keeps the frame loop blocked while paused (raylib's `PollInputEvents`), so `SetPaused`
    runs from within the pause command — the same observable state on resume (the pause menu), not the same
    instant; the audio output is muted and its streams paused rather than stopped (`activateAudio(false)`); (g)
    the emulator's `adb shell input tap` is too short for raylib's per-frame poll — the scripts tap with
    150 ms swipes; (h) a `wm size` override on the AVD (1920×1080, found set) was reset for the 2400×1080
    checks; (i) live desktop window resizing, an `.aab` / Play signing, a Web build, the x86_64 Android ABI
    (`--abi x86_64` in `tools/build_android.sh` is a one-line option, untested) and gamepad input are out of
    scope; (j) the CI workflow is written, not run; the Windows *window* (WGL) is unverified — Wine has no
    OpenGL here — and MSVC only through the workflow; (k) the Linux leg (Docker, gcc 13 on
    ubuntu:24.04) ran on 2026-09-14: G1 3405/0, `aa_tests` 112/112, the headless walk — after one fix,
    `aa_libm` now links glibc's `libm` for the exact `sqrt` it leaves to the platform (§11 item 6); (l)
    `SaveStore::defaultDir` stays the desktop OS directories — `runApp`'s `saveDirFor` takes
    `AppOptions::dataDir/saves` (`PlatformInfo::dataDir`, Android's `internalDataPath`) on a mobile build; (m) the level buttons' press zoom now animates
    the panel / thumbs / frames / number as `LevelSelectorButton::ZoomIn / ZoomOut` do (M5 animated the
    button's empty background view — a fix found while porting the widescreen factors); (n) the right border's
    `− 1` px of the original is applied since M7; (o) raylib collapses `AMOTION_EVENT_ACTION_CANCEL` into a
    touch-point count of 0 (Ended, where the original's `nativeInput` action 3 queued Cancelled — a button
    under the finger would fire); `platform_android.cpp` wraps `android_app::onInputEvent` ahead of raylib's
    callback and `takeTouchCancel()` lets `TouchTracker` cancel its fingers in that frame instead — checked
    by construction only (the emulator's `input` tool sends no CANCEL); (p) the frame loop samples raylib's
    touch points once per frame, so a down + up inside one event batch (a tap during a long frame — the
    loading scene, the extraction, a heavy simulation frame) is lost, and an up + down of the reused pointer
    id reads as a move; the original received every event through `nativeInput` — the faithful cure is to
    queue the motion events from the `onInputEvent` wrapper (already in place for CANCEL) instead of polling;
    (q) code review 2026-09-14: `GameScene::update` now runs `onControllerState` only while the level menu
    is closed (doFrame's delegate calls come from doFrame, which the open menu skips) — before, a pause from
    the running simulation was undone by the next frame's state-2 overlay (the menu slid back to Shown with
    `menuOpen_` still true: a frozen game that looked live); the lifecycle hooks are cleared by an RAII guard
    so an exception unwinding out of the frame loop cannot leave them pointing at destroyed objects.
5. `strtod` correctness on Bionic (02 §6) is assumed, not tested.
4. Licences: Box2D (zlib), msun (Sun + BSD, `aa_libm/NOTICE`), raylib (zlib), cJSON (MIT), doctest (MIT) — all
   permissive; the game's assets stay the player's own copy.
6. **Portability (M0 exit) — closed in M7 (2026-09-14):** macOS arm64 (Apple clang), Android arm64 (NDK 27
   clang, G1 + `aa_tests` on the emulator), Windows x86_64 (mingw-w64 gcc, G1 + `aa_tests` + the headless walk
   under Wine; the window itself and MSVC only through the CI workflow, unrun) and Linux (gcc 13 in Docker:
   G1 + `aa_tests` + the headless walk, item 15 (k); `aa_libm` links `m` there for the platform `sqrt`). `aa_fp_strict` carries the MSVC flags; raylib 6.0's bundled GLFW needs `CMAKE_POLICY_VERSION_MINIMUM
   3.5` under CMake ≥ 4, set by the top-level file.
7. **Lessons from M1/M2 for the remaining ports (M3/M4):** the decompiler output drops VFP register arguments
   (masses, inertias, aspect ratios, densities passed in `s0–s3`; the renderer's part rotations and stretch
   lengths; even *local* functions take floats in `s0/s1` although the exported ABI is softfp) and hides `strh`
   filter writes after a `memcpy` of a filter (the bucket's interior polygon carries group index 10, the
   container sensors −7); HangingLamp and the bucket's interior use *double* multiplications (`vcvt.f64.f32` /
   `vmul.f64`) that must be written as explicit `(float)((double)x * c)`. `.bss` constants can be an ulp off the
   literal (the glove's fist offset `0x3e8f5c28`, one ulp below 0.28f — `tools/decomp_dis.py bss`), the game's
   `Pi` global is `DegToRad·180 = 0x40490fd8` (every angle limit is a product of it), a hidden `s0` argument can
   carry the flip sign (`FUN_000ebe98`), and reconstructed argument lists for softfp calls can be shuffled
   (the seesaw's anchor/position pair). Read the disassembly (`tools/decomp_dis.py fn/range`, literal pool
   decoded) next to every decompiled function before porting it; `aa_sim` builds with `-Werror=double-promotion`.
   Item state starts as the `st::ItemInfos` default block (scissors 15°, glove button 36 px, default end
   vectors), not zero — `GameItem::defaults`, and the harness copies the same block.
16. **Deliberate deviation (2026-09-19): the four unlisted Treehouse levels are offered.** `HoneyBucket`,
    `OpenFire`, `RescuePiggy`, `Ricochet` exist as files but were cut from `0_Location.plist` before release, so
    the original never offered them (docs/02 §1, 12 §1); G3/G4/G6 already covered them (they are the difference
    between "112" and "116" in every gate count in §8/§10). `AppState::loadCatalogue`
    (`core/ui/app_state.cpp`) now appends a chapter's `unlisted` names after its `levels`, so they play as
    Treehouse levels 33-36 — new `LocationState` slots; existing saves' indices 0-31 are untouched, and
    `LocationInfo::maxStarCount()` / the chapter-complete check are derived from the live list, so Treehouse
    now needs 36 levels at 3 stars, not 32. Known gaps, both cosmetic: no thumbnail ships for them (none were
    ever produced), so their level-select button shows no image; their title/tip are literal English strings
    from the pre-localisation level format (`version` 6, not the shipped levels' 7) rather than locale ids, so
    they display in English on every locale (`Localization::text` falls back to the id, which for them already
    is the display text). `docs/07-levels-catalog.md` (`gen_levels_catalog.py`) still lists only the original's
    112 in `0_Location.plist` order, since it documents the original's own index, not the remake's.
