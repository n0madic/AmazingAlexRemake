# UI, fonts and localisation

## 1. Scenes and views

The UI is the engine's retained view tree (`UI::Scene` → `UI::View` subclasses: `Button`, `ToggleButton`,
`SlidingButton`, `LabelView`, `OutlineLabelView`, `ImageView`, `ScrollView`, `PageControl`, `TextFieldView`,
`ActivityIndicator`, dialogs). Layouts are loaded from the encrypted plists in `Common/XML/`:

| File | Scene / views | Notes |
|------|---------------|-------|
| `SplashScene.xml` | `SplashView` | Rovio logo (`SPLASH_SPLASH`) |
| `MainMenuScene.xml` | `MainMenuView` | play / my levels / world of contraptions / settings; `MENU_MENU_MAINMENU` atlas |
| `ChapterSelectionScene.xml` | `ChapterSelectionView` | four "books" (`BOOKS_BOOK_*`, `BOOKS_COMPOSPRITES` compose book + localised title), star counters, lock overlays |
| `LevelSelectionScene.xml` | `LevelSelectionView`, `LevelSelectorButton` | paged grid of `LevelThumbnails_*`; star icons; `MENU_MENU_LEVELMENU` |
| `LevelLoadingScene.xml` | `LevelLoadingView` | "Loading…" label + activity indicator (the `Tips.plist` texts are dead data, §3) |
| `GameScene.xml` | `GameView` (`SidebarLeft`: Pause, Menu, Restart, Solutions, Audio buttons, `LabelLevelNumber`; `ButtonPlay`; borders), `LevelCompletedView` (`PanelResult`, stars, next/retry/share), `GameTutorialView`, `SolutionsView` | `MENU_MENU_INGAME`, `MENU_MENU_RESULTS`, `MENU_THUMBNAIL` |
| `SandboxScene.xml` | `SandboxView` | editor toolbar (`MENU_EDITOR`), save/test/share |
| `MyContraptionsScene.xml` | `MyContraptionsView` | list of user levels |
| `WorldOfContraptionsScene.xml`, `FeaturedLevelsScene.xml` | online browsing (out of scope) |
| `ChapterCompleteScene.xml`, `ChapterComplete3StarsScene.xml` | end-of-chapter panels (`MENU_CHAPTER_COMPLETION`) |
| `ComicScene.xml` | `ComicView` | intro/outro comic strips (`COMIC_CH1`, `COMIC_CH1_END` — the Classroom only) |
| `CreditsScene.xml` | `CreditsView` | scrolling credits (`TEXT_CREDITS_*`) |
| `Dialogs.xml` | `MessageDialog`, `InfoDialog`, `LoadingDialog` | `POP_UP_POP_UP` atlas |

View attributes (plist keys): `Relative {X, Y, W, H}` (percent of parent), `Anchor {H {Self, View {Target}}, V {…}}`
with anchors `LEFT/HCENTER/RIGHT`, `TOP/VCENTER/BOTTOM`; `Image` (sprite name from a KA3D SPRT atlas), `Font`,
`FontAnchorH/V`, `Text*` (localisation id, `DUMMY` = set at runtime), `Hidden`, and named child views.
Sizes are resolution independent; the atlas profile supplies the pixels. `UI::Animator` drives slide/fade
transitions (`GameScreenTransitionsUtils`: toolbox/stopwatch/toolbar retract & display, background slide, camera zoom).

### 1.1 The UI engine subset the remake ports (M5) [verified: decompile + disassembly, 2026-09-14]

`core/ui` (`aa_ui`, no raylib) ports the `UI::` classes the shipped campaign scenes use; the layout maths and the
timings come from `View::Init` / `UpdateViewAnchors` / `BaseDraw` / `HitTest`, `Button*`, `LabelView*`,
`ScrollView` / `PageControl`, `Animator`, `Scene` / `SceneManager` / `EventHandler`:

* **View** (`View::Init(DataDictionary)`): `Relative {X, Y, W, H}` — the flags per axis; `X`/`Y` are a percent of
  the parent's size, **`W`/`H` a percent of the screen** (`GameParams::NativeScreenWidth/Height`); `Anchor {H {Self,
  View {Name, Target}}, V {…}}` with self anchors `LEFT/HCENTER/RIGHT/HPIVOT`, `TOP/VCENTER/BOTTOM/VPIVOT/BASELINE`
  and target anchors of the same names (`Name` = a sibling view, else the parent); `Padding {Left, Right, Top,
  Bottom}`; `Pivot {X, Y}`; `Scale`; `Alpha`; `Angle`; `Hidden`; `Interaction`; `ClipSubviews`;
  `BackgroundColor {R, G, B, A}` (all four channels required); `Background` (an `ImageView` attribute: a
  full-screen `FILL` image). `SetPosition` rounds with `ceilf`, `SetSize` with `floor(x + 0.5)`.
  `UpdateViewAnchors`: the target views are resolved by name among the subviews of every ancestor; the
  `AnchorAspectCorrectionFactor` multiplies the relative offset only when a target view exists and the self anchor
  is LEFT / RIGHT (TOP / BOTTOM); with the parent as target the parent's size replaces the screen size.
  `BaseDraw`: `screen = scale · (ceil((pivot + rect) / scale − pivot) + pivot + R(angle) · (p − pivot))`, the alpha
  is the product up the parents, the angle the sum, the clip the intersection of the clipping ancestors' real
  frames; `DrawBackgroundColor` fills with the colour's alpha × the view's **own** alpha (the context alpha slot
  is 0 for the fill). `HitTest` descends the subviews last-first (interactable + visible + inside).
  Every `<Scene>::Init` passes the root view's frame to `<View>::Init(UIRect)` — the scene dictionaries carry
  the subviews only (`GameView`, `LevelCompletedView`, `GameTutorialView`, `MainMenuView`, … have no attributes
  of their own).
* **Button**: states 0 disabled / 1 normal / 2 highlighted / 3 selected; subviews background (`ImageBackground`),
  state image (`ImageState{Normal,Disabled,Selected,Highlighted}`, `Localized…` variants), label (`TextFont`,
  `TextState*`), overlay (`SetOverlayForState(image, state, offset %)` — the chapter books' lock at (4 %, 0)).
  Press → ZoomOut (the image views to 1.15 in 0.1 s, curve 4) + sound 3 at 0.2; release → state 3, ZoomIn (1.0 in
  0.05 s) + sound 4, `ButtonAboutToBePressed`; ZoomIn's end in state 3 → state 1 + `ButtonPressed`. The hit area
  doubles while highlighted / selected. `SetState(0)`: alpha 0.5 (`ChangeAlpha`) and no interaction.
  **ToggleButton**: `ImageState*On` fill the image slots 0..3 and show while **unchecked**, `ImageState*Off` the
  slots 4..7 and show while checked (`SetChecked` marks the images dirty; `Update` re-applies them) — the play
  button shows `BUTTON_LARGE_PLAY` (On) in set-up and `BUTTON_LARGE_STOP` (Off) while checked in simulation.
  **SlidingButton** [verified: `Init(dict)`, `LayoutMenuButtons`, `ShowMenu`, `HideMenu` disassembly]: its own
  `View::Init` reads the position / anchors; the inner button is built bare (`AnimateOnlyBackground`) and takes
  only `ImageBackground` (the slider's frame copies the background's size) and the `ImageState*` /
  `LocalizedImageState*` keys; `Direction` "DOWN" → 1, "UP" → 0 (the constructor's default). The menu view
  starts as `{0, h/2, w, 0}` and opens in 0.3 s to `{0, h/2, w, h/2 + n·itemH}` (DOWN) or
  `{0, −n·itemH, w, h/2 + n·itemH}` (UP), `itemH` = `SetMenuItemHeight` or the slider's height; `LayoutMenuButtons`
  centres button i at `menuH − (i + 0.5)·itemH` (DOWN, clamped ≥ 0 — the last-added button sits nearest the main
  button and they emerge from under it) or `(i + 0.5)·itemH` (UP, clamped ≤ menuH); the main button turns by π.
  The main menu adds `ButtonCredits` then `ButtonAudio` (`ButtonAutoShare` is never added); the remake adds its
  `ButtonMusic` between them (§4).
* **ImageView**: `Image`, `LocalizedImage`, `DrawMode` (`STRETCH` default, `FIT`, `SCALE` = fill, `TILE`, `CENTER`),
  `AutoResize` / `AutoResizeW` / `AutoResizeH` (the frame takes the image size), `Angle`; a `thumb:<level>` image
  is the level thumbnail (`thumbnails/<level>_350.jpg`), `thumb:@<path>` a thumbnail file outside the asset tree
  (a user level's, `ResourceProxy::LoadSpriteFromDocs`).
* **LabelView** / **OutlineLabelView** / **HighlightLabelView**: `Font`, `FontAnchorH` (LEFT / HCENTER / RIGHT),
  `FontAnchorV` (TOP / VCENTER / BOTTOM), `Text` (a text id) / `NonLocalizedText`, `AutoResize` (**both** axes),
  `AutoResizeW`, `AutoResizeH`, `MaxRows`, `MaxLetters` (ellipsis), `WrapText` on spaces and `\n`; the auto-resized
  height is `maxDescending + lines × leading`; the outline label draws the outline font under the fill font offset
  by the `Fonts.xml` offsets; the highlight label switches the font inside `*word*` segments (remake: outlined
  like the outline label when its font has a `Fonts.xml` entry, `HilightColor` tints the highlighted letters, the
  wrap measures the lines without the markers — §3).
* **ScrollView** (`HorizontalScrolling`, `VerticalScrolling`, `Paging`, `RubberBandFactor`, `ClipSubviews`): the
  content view's frame is (−offset, contentSize); paging clamps with a pad of `pageSize · factor · 0.5`; a drag over
  20 px cancels the child's touch (`TouchFilter`); a fling decelerates 0.3 s (ease-out, `velocity × 0.3`); with
  paging `|drag| > page / 2` snaps, else ±1 page when `|v| > 1250 px/s`; `GetActivePage = (offset + page / 2) /
  page`; taps in the `PageControlAreaLength` strips page. `ScrollViewDelegate`: `StartedDecelerating` at the end
  of `EndDragScrolling` (the finger let go), `FinishedDecelerating` when the scroll animation ends, `Moved` from
  `Update` when the offset changed [verified: the vtable order and the call sites; M5 called Finished at the drag
  end — fixed in M6]. **PageControl**: `ImageActive` / `ImageInactive`,
  `ImagePadding` (% of the screen width), `ShowPageNumber` + `Label` — the dots centred on the view, spaced by the
  inactive width + padding, the active one bottom-aligned, the number label at the active dot's x with its bottom
  at `dotY + dotH / 2.05`.
* **Animator**: `Animate(View*, params)` animates to **absolute** targets, `Animate(Array<View*>, params)` applies
  **deltas** (each |delta| ≥ 1e-4); params `{frame, angle, alpha, scale, pivot, curve, delay, duration, repeat}`
  (repeat 0 = forever); curves 0 linear, 1 t², 2 1 − (1 − t)², 3 / 4 smoothstep; `AnimationStarted / Finished /
  Canceled` to the delegate.
* **SceneManager**: a scene stack (`PushScene`, `PopScene`, `SetRootScene`, `InsertScene(index)`,
  `PopScenesUntil`, `RemoveScene`), the simultaneous / non-simultaneous transition machines (`activating`,
  `inactivating`, the popping flag decides the draw order), scene states 0 inactive / 1 active / 2 activating /
  3 inactivating, `EventHandler` (touch id → the view it started in / is over), `SetUserInteractionEnabled`, the
  back key 0x28 pops the top scene (unless it handles `KeyDown`; `View::KeyDown` offers the key to the
  interactable, visible subviews from the topmost down and stops at the first that takes it [verified:
  0x10872c] — a dialog on top of its view answers the key). `PushScene` appends the named scene even when
  it is already in the stack; `RemoveScene` erases matches between the root and the top only (indices
  1 .. count − 2, no transition) — with the game on top, `RemoveScene(GameScene)` is a no-op [verified].
  `LevelLoadingView::KeyDown` returns true: the back key does nothing on the loading screen [verified].

**Remake fix (a deviation):** `Button::TouchesFinishedInside` on a button in the Normal state returns without
releasing the static processed-touch id, and `AnimationFinished` releases it only when a delegate exists; since
`ChapterSelectionView::Refresh` resets every unlocked book to Normal on a page change, a book pressed during the
page snap left every button of the original dead. The port releases the id in both paths.

* **Dialogs** (M6; `core/ui/dialogs.cpp`) [verified: `DialogBackground::Init`, `InfoDialog::Init / ButtonPressed /
  KeyDown / SetMessage`, `MessageDialog::Init / ButtonPressed / KeyDown / SetTitle / SetMessage`, the delegates]:
  a dialog is a full-screen view built from the scene's entry (`DialogType`, `Title`, `Message`) and the type's
  layout in `Dialogs.xml` (`InfoDialog`, `LegalDialog`): an inert full-frame "InvisibleBackground", the
  **DialogBackground** (`ImageTop` / `ImageMiddle` tiled / `ImageBottom`, the `…Wide` pieces (`POPUP_*`) for a
  `LegalDialog`; the frame takes the top piece's width, the middle piece the height left), then the labels and
  buttons. `InfoDialog`: `Background/Message` (re-initialised from the scene's own `Message` dictionary, sized to
  the box minus its paddings and re-wrapped) and `ConfirmButton`; `MessageDialog(single)`: `Background/Title` +
  `Background/Message` (outline labels; `SetMessage` re-sizes and re-wraps) and `SingleConfirmButton` or
  `ConfirmButton` + `CancelButton`; after the anchors the title moves up (in screen percent) when its bottom
  comes within 10 px of the message. Without a delegate a button hides the dialog; with one, confirm →
  `MessageConfirmed(dialog id)`, cancel → `MessageCanceled`; the back key presses confirm (InfoDialog) / cancel
  (MessageDialog, confirm when single). `Show` / `Hide` = `SetVisible`.

**Dropped** (unused by the ported scenes or out of scope): `TextFieldView`, `ActivityIndicator`, `LetterBoxView`
(the letterbox strips are the `GameView` borders, 11 §8), `LoadingDialog`, `LoginErrorDialog`, `SolutionsView`,
`PanelSolution`, `LevelSharingView`, the share / WoC buttons of the result panel (`ButtonShare*`, `ButtonMenuWoC`,
`ButtonRetryWoC` exist but are inert), `ResizeParent = false` buttons, the `NewContentIndicator` of `PageControl`,
`SetStartPageForNumbering`, the Rovio news / buy buttons (present, disabled), the main menu's `LinkSlider`
(Twitter / Facebook / video) and the LotW / WoC books (dropped, 2026-09-15: not built, their book sheets not
imported — 12 §1), the credits'
privacy-policy / EULA links (present, disabled), `UpdateLocale` at run time (the locale is fixed at start-up:
`--locale` → `settings.json` → the OS language).

**Sprite scale (a remake decision, not verified):** the imported 2048X1536 profile is authored for PixelScale 2;
its sprites are drawn at `pixelScale / 2` (`ScreenLayout::kProfilePixelScale`, shared with the toolbox strip) —
at 1024×768 every UI sprite is half its atlas size, as on a non-retina iPad.

### 1.2 Scene flow [verified: GameApp::update, SplashView, MainMenuView / LevelLoadingScene / GameScene ButtonPressed & hooks]

`GameApp::update`: state 0 → `SetRootScene(SplashScene)`; when the splash's second page completed
(`IsPageCompleted(2)`: `2 · n < timer`; page 0 lasts 2 s — a tap on the Rovio logo skips to 2 s — page 1 with the
title picture and "Loading…" until 4 s; the remake halves both — `SplashView::kSplashPageTime` = 1 s, nothing is
loaded on that screen) → `SetRootScene(MainMenuScene)`. `MainMenuView::ButtonPressed(play)`: a fresh
install (the Classroom not unlocked) loads location 0 and calls `showChapterComic(0)` (§1.3): the loading scene is
**inserted** beneath the comic when it opened, else pushed; then `SetLoadingLocation(1, 0)` and
`ChapterSelectionScene` / `LevelSelectionScene` inserted beneath (`InsertScene(1)`, `InsertScene(2)`); otherwise
`PushScene(ChapterSelectionScene)`. `ButtonCredits` → `Hide(true)` (the top panel slides up 0.3 s) whose
`AnimationFinished` pushes `CreditsScene`; the back key → the `ExitDialog` (in the original an `InfoDialog` —
one check button, the back key confirming, an empty box: its Init ignores the entry's SK_EXIT /
ITEM_ARE_YOU_SURE strings; the remake, 2026-09-15: a two-button `MessageDialog` with those texts, the cross or
the back key dismissing it) whose confirmation leaves the game (`AppState::quitRequested`).
`ChapterSelectionView`: the original's seven books (`BOOK_<NAME>_<LANG>` composites in a paging `ScrollView` of
7.5 book widths, page = a book width; the panel slides in from the right, the star counter from below) — the
remake builds five (`kBookCount`: the chapters and My Contraptions, a panel of 5.5 widths), the books 4–5 (LotW,
WoC) are dropped; the last book (My Contraptions) opens with `GameProgress+0` (set when the Classroom's chapter panel was
shown) → `PurgeThumbs`, a blank `LocationInfo` with index −3, `PushScene(MyContraptionsScene)`; a chapter book →
`LoadLocation` + `PushScene(LevelSelectionScene)` + `showChapterComic(0)` for the Classroom only. `LevelSelectionView`: 96
`LevelSelectorButton` slots, 4 × 2 per page (`SelectorArea` percents), thumbnails, numbers ("?" when locked), names,
stars, the initial page = the first unplayed level's (`SetReturningFromGame`: the current level's); a level →
`LevelLoadingScene::SetLoadingLocation(1, level)` + `PushScene`. `LevelLoadingScene`: `Activate` shows the view
(0.7 s animation) and stops the music; `ActivationComplete` selects the level (`GameApp::selectLevel`), marks the
Classroom unlocked on its first level, marks the level played and saves; the animation's end
(`LevelLoadingView::AnimationFinished`): location 1 → `RemoveScene(GameScene)` + `PushScene(GameScene)`, locations
2 / 3 → `RemoveScene(SandboxScene)` + `PushScene(SandboxScene)` (§1.3), location 8 →
`PopScenesUntil(ChapterSelectionScene)`, location 0 → `PopScene` (itself); `InactivationComplete` resets the
location to 0 and the level to −1, so the loading scene pops itself when the game returns onto it.
`GameScene`: `Activate` → `GameView::Show` + `Music.mp3`; `GameView::ButtonPressed`: pause ↔ open / close the
menu, menu → `showChapterComplete` else `PopScene`, restart → `RestartLevel`, audio → `SetAudioState` + save
(remake: music → `setMusicState` + save, §4);
`LevelCompletedView::ButtonPressed`: forward → `showChapterComplete` / `PlayNextLevel` / (last level) back to the
books through the loading scene (location 8), retry → `ReplayLevel`, menu → `showChapterComplete` else `PopScene`.
`UI::showChapterComplete`: with every level completed and the chapter panel not shown yet → the shown byte set,
the progress saved, `PushScene(ChapterCompleteScene)`; else with every star and the 3-stars panel not shown →
likewise `ChapterComplete3StarsScene`; `ChapterCompleteView::ButtonPressed` chains to the 3-stars panel under
the same rule, else `PopScenesUntil(ChapterSelectionScene)` + `showChapterComic(1)` for the Classroom;
`ChapterComplete3StarsView::ButtonPressed` → `PopScenesUntil(ChapterSelectionScene)` + the same comic call.
`GameScene::ShowOverlay`: 1 (the controller's state 1 = `displayGoals`, one frame at a campaign level's start
— `startLevelWithGoals(true)` from `playNewLevel` — before the transition to set-up; a run's stop goes to
state 5 directly and emits only 10 [verified: doFrame's dispatch, corrected in M6; 10 §11 item 14 (n)]) shows
the tutorial view in the campaign (hides it in the other modes); 2 (completion start) retracts the pause menu
and the play button; 3 (state 6) → `LevelCompletedView::Show`; 8 (state 3, the set-up → simulation transition;
the port takes it at 4, the same frame) retracts the pause menu and shows the stop button — the tutorial view
is left alone; 10 / 7 (back in state 2) → the pause menu, the play button, the stop button hidden.

### 1.3 The editor, My Contraptions, the comics, the credits (M6) [verified: decompile + disassembly, 2026-09-14]

* **Comics** — `UI::showChapterComic(type)`: type 0 (begin) when `LocationState.visited` is clear, type 1 (end)
  when `finished` is clear: the resource set `location + 5` / `+ 9` loaded (every set 5..12 maps to `COMIC_CH1` /
  `COMIC_CH1_END`: the bundle carries the Classroom's comic only, and all four callers guard on location 0),
  `ComicScene::setComicView(type, location)` builds a fresh `ComicView` from `ComicViewBegin{loc+1}` /
  `ComicViewEnd{loc+1}` of `ComicScene.xml`, `PushScene`; the flag is set and the location saved either way
  (without the scene the end comic just restarts the theme). `ComicView::Init`: the `Background` image, a bare
  silent tap button over the whole view, the hidden `ButtonNext`, one `ImageView` per `Frames` entry
  (`CH1_PANEL_1..4` / `_5..7`; ×0.85 on 16:9 phones — not applied, as M5's other widescreen tweaks).
  `Update`: the frames `0..shown` visible; after 2 s the next frame; with every frame shown and 1 s more the
  next button; a tap reveals the next frame (or the button). `ButtonNext` → `PopScene`; the back key pops the
  begin comic and is inert on the end comic. The scene's transitions are immediate (`Update` sets the state).
* **Credits** — `CreditsView::Init`: `Background`, `ButtonBack`, `PanelScroll` (vertical, no paging) holding
  `ImageHeader`, `LabelTitle` (`TitleText`), `LabelVersion` (`VersionText` "v{0}" with `Version::Get` = "1.0.5"; the remake writes
  "Remake by n0madic / based on v1.0.5" — `CreditsView::kVersionText`, not localized),
  `LabelCopyright`, `ButtonPrivacyPolicy` / `ButtonEula` (Rovio links, inert here), `ImageSubHeader`, then for
  each of the 28 groups of the view's name table (29 in the remake, see below) `LabelTitle<Group>` / `Label<Group>` when the dictionary has
  them, `ImageFooter`; the content height = the footer's bottom + its paddings + 1.2 screen heights; the offset
  starts at 5. The list starts one screen height down (`ImageHeader` at Y 95 %) and creeps up at 6.5 % of the
  screen height per second once the back button's slide (0.3 s, from above the top edge to 3 % below it) has
  ended; below 5 px it wraps to the end; a finger scroll pauses it (`ScrollViewStartedDecelerating`) and the
  scroll animation's end resumes it. `ButtonBack` → the button slides out, then `PopScene`. **An original
  quirk, fixed in the remake:** the name table lacks `PostProductionLead`, whose labels the XML chain runs
  through — from `LabelTitleOperations` on the original's labels anchor to the missing view (= the content
  view's bottom) and overlap the earlier groups; the port adds the group (its texts are empty outside en_EN),
  so the chain is continuous (10 §11 item 14 (d)).
* **My Contraptions** — `MyContraptionsView::Init`: like `LevelSelectionView` (background strips, `LabelTitle`
  CHAPTER_NAME_MYC, `ButtonBack`, `ButtonAdd`, the `ButtonTrash` toggle, `PanelLevelContent` + `LevelPages`,
  96 `LevelSelectorButton`s on the `SelectorArea` grid) plus the hidden dialogs `ErrorParsingLevel` /
  `ErrorLevelStorageFull` (single-button `MessageDialog`s) and `LegalTextDialog` (an `InfoDialog` of type
  `LegalDialog`). `Refresh` (`Activate` sets the flag, `Show` refreshes): `LoadFromDocs`, from the last slot down
  `Setup(3, i)` per listed level (a level whose title cannot be read is removed from the index, saved, and the
  refresh restarts), the "add" tile (`Setup(6)`: the `LEVEL_EMPTY_SLOT` frame only) after the last while under
  96, `ceil((count + 1) / 8)` pages (12 when full). `Setup(3)`: `LEVEL_FRAME{id}`, the title (max 20 letters,
  2 rows), the thumbnail from the docs directory when it loads; `SetTrashCanVisible` only for the user-level
  types. `Show` hides the dialogs, clears the trash cans and shows the legal prompt while Settings profile +3 is
  clear; its confirmation sets the byte and saves. `ButtonAdd` (or the add tile): under 96 levels
  `SetLoadingLocation(3, −1)` + `PushScene(LevelLoadingScene)` (thumbnails released), else the storage-full
  dialog; a level: with the trash toggle on its files (`.plist`, `_solution.plist`, the thumbnail) and its
  index entry go and the list refreshes, else `SetLoadingLocation(2, i, path)` + `PushScene`. `Activate` plays
  the theme; `InactivationComplete` → `Hide`. `LevelLoadingScene::ActivationComplete`: case 2 `LoadLevel` (a
  parse failure → location 0, level −1, the loading scene pops itself and `MyContraptionsScene::ShowParsingError`),
  `SandboxScene::SetGameMode(1)`; case 3 `SetGameMode(1)`, `CreateNewSandbox`, background 0, a unique name added
  to the index and saved, the level index = the last, the author from the settings, the title
  LEVEL_SHARE_CONTRAPTION_DEFAULT ("My level"; `st::LevelLayout::LevelLayout`).
* **The editor** — `SandboxView::Init`: `SidebarLeft` (`EDITOR_SIDEBAR_PLAY_BUTTON_FLIP`, holding `ButtonBack`),
  `ButtonBackground`, `SidebarRight` (holding the `ButtonPlay` toggle, state 1, and the `ButtonReady` toggle,
  state 0), `LabelInstructions` (hidden; its `TextWorkingContraption` / `TextCompleteDesign` / `TextThreeStars` /
  `TextPiecesToToolbox` / `TextAtleastOneItem` ids), the `CONSTRUCTION_STRIPE` borders, the letterbox borders;
  the left panel's hidden place = its anchored position (off screen), shown = one panel width right; the right
  sidebar's hidden x = the screen width. `Show`: `setMode(1)`, `playNewLevel` (the file saved at once), the
  render flag on, `MarkAllObjectsNotFixed`, the panels slide in (0.2 s), the view fades in (0.3 s), the play
  button enabled, the instructions = three-stars when tested else working-contraption,
  `RemoveScene(LevelLoadingScene)`. `Hide(true)` slides the panels out and fades the view; its end clears the
  render flag. `ShowGameControls`: play enabled outside the toolbox step; in the step the ready button follows
  the strip's item count (`Update`), elsewhere it is enabled once tested; the background button visible outside
  the step. `HideGameControls(animated)` slides the background button to y = −h/2 — and `ShowGameControls`
  moves it by a zero delta, so the original never brings it back after an animated hide (the scene's
  `Inactivate`); the port returns it to its anchored place (a deviation). `ShowSimulationControls`: the left
  panel hidden at once, play checked, the background button hidden; `HideSimulationControls` the reverse.
  `SandboxScene::ShowOverlay`: 8 (state 3; the port's 4) → the simulation controls; 10 (state 5, a run's end) →
  `MarkAllObjectsNotFixed` + the game controls back; 1 / 2 hide the panels (state 1 is never entered:
  `playNewLevel` starts a sandbox level without the goals display, 10 §11 item 14 (n)). `ButtonPressed` (the
  gizmo off first): **back** in the step → `LevelLayoutUtils::LoadLevel` of the file, then `setMode(1)` — the
  5 → 1 transition un-fixes the stars the file holds fixed (the port loads in the current mode for the same
  reason),
  the play button back, ready when tested, the "complete design" text; in mode 1 → deferred while an item is
  being added / removed, else `MarkAllObjectsFixed` + `saveSandboxLevelAndThumb` (+ the `_solution` file when
  untested — dropped) + `PopScene`; **background** → the next of 4 + `backgroundChanged(1)`, ready disabled;
  **play** checked → `setMode(4)` + `toggleSimulation`, unchecked → `toggleSimulation` + `setMode(1)`;
  **ready** in mode 1 → nothing unless tested, else play disabled, `setMode(5)`, the background button hidden,
  the pieces-to-toolbox text, the ready button disabled, the level saved; in the step → the sharing view (online,
  dropped; otherwise the "at least one item" text, the level saved). `KeyDown`: the back button (deferred to the
  next frame) while the left panel shows, the play button (stop) during a run, else consumed. A touch on the
  view hides the instructions and goes to the controller. `Activate`: `Music.mp3`; `Inactivate` stops it.
  The remake's `SandboxScene` owns its own `aa::sim::Session`; the world is drawn by the platform subclass.

Screen params: `st::GameParams::ScreenWidth/Height`, `NativeScreenWidth/Height`, `PixelScale`,
`LetterBox`, `LetterBoxFrameWidth`, `LetterBoxViewportYOffset`, `AnchorAspectCorrectionFactor`,
`FloorHeightInPixels` (130 at 1024×768, down to 39 on wide screens), `WorldScaleWithFloor`, `Orientation` — the
play field keeps its 3.41 m width, the floor strip shrinks and the sides are covered by the chapter frames on
wider screens; anchored views are pulled toward the play field by `AnchorAspectCorrectionFactor`. The exact
formulas, the profile table of `selectAssetProfile` and the `AssetScalingForWidescreen` tweaks are in 11 §1.

## 2. Fonts

Bitmap fonts `FONT_1..4` (KA3D `FONT` + PNG, see 01 §5) rendered by `game::BitmapFont` (`drawString`, tracking,
leading, ascender/descender, `isCharacterSupported`); `FONT_3`/`FONT_4` have separate outline atlases combined by
`UI::OutlineLabelView` using `Fonts.xml` offsets. `NUMBERFONT` for star/level counters. Glyph coverage: Latin-1 for
the five shipped languages.

## 3. Localisation

* Bundles: `TEXTS_BASIC.dat` (417 ids × 5 locales: en_EN, fr_FR, it_IT, de_DE, es_ES),
  `TEXTS_LANGUAGE_SELECTION.dat` (4 ids), `localization_android` (Android-only strings, en_EN).
  Parsed by `game::TextGroupSet` (`loadLocaleCodes`, `loadTextGroup`); the current locale is chosen from the device
  language (`UI::Localization`). Exported JSON: `extracted/texts/*.json` (`tools/ka3d_text.py`).
* Id families: `TEXT_LEVEL_NAME_cc_nn` / `TEXT_LEVEL_TIP_cc_nn` (229, all 116 levels, `cc` = chapter 00..03, `nn` = 01..),
  `CHAPTER_NAME_CHAPTER1..4` + `CHAPTER_NAME_LOTW/WOC/MYC`, `CHAPTER_DESCRIPTION_*`, `TEXT_CREDITS_*` (65),
  `TEXT_MSGBOX_*` (18 dialogs), `TEXT_EMAIL_*`/`LEVEL_SHARE_*` (sharing), `TEXT_SETTINGS_*`, `RESULT_*`,
  `FREE_SELLUP_*`/`FREE_COMING_*` (free-edition upsell), `ITEM_*`/`SK_*` (legacy menu strings), `TEXT_ABOUT_*`.
* Tip markup: `*text*` → highlighted words (rendered with the accent colour/outline font in the loading and game
  views). Line breaks are literal `\n`.
* `Tips.plist` (22 tips): `{Image, Text, objectType, Platform}` — generic tips (`objectType 0`, 14 of them) and
  item-specific ones (5 balloon, 17 pipe, 14 boxing glove, 22 seesaw, 34 slingshot, 35 RC truck); `Platform = 1`
  marks iOS-only wording (multi-touch rotate). **Dead data [verified, M5]:** neither the Android `.so` nor the iOS
  binary references `Tips.plist`, `objectType` or the level descriptions (`TEXT_LEVEL_TIP_*`) — `LevelLoadingView`
  shows the title picture and "Loading…" only. The remake keeps the loader (`aa::data::loadTips`) and the JSON;
  `Tips.plist` stays unused.
* **Remake addition — the level tip in the game (2026-09-25).** `GameView` shows the campaign level's
  `description` (`TEXT_LEVEL_TIP_*`) on entering a level (after the view's show fade, with the level name; also
  on the in-game "next level"; not on a restart or a replay) at the bottom left of the play field: a dark
  translucent `TipPanel` with the `LabelTip` (`HighlightLabelView`, FONT_4 outlined, the `*highlighted*` words'
  fill tinted yellow through `HilightColor` and `Renderer::setTint`) right of a small info button (`ButtonTip`,
  `BUTTON_SMALL_BASE` + `BUTTON_SMALL_INFO`) and left of the toolbox strip — both placed every frame from
  `Toolbox::getToolboxRectangle()`, the button on the strip's centre line; when the strip leaves less than 35 %
  of the play field beside it the panel goes above the strip, full width. The tip stays up for
  `clamp(1 s + 0.05 s × letters, 2.5 s, 7 s)` plus a 0.25 s fade each way; the info button shows it again (or
  hides it) without stopping the tutorial or releasing a held item; the pause menu and the level's completion
  hide it. The panel takes no touches (the world gets them). The dictionaries: `remake::gameTipPanel()` /
  `gameTipLabel()` / `gameTipButton()`. The loading screen stays the original's. Font fact that cost time: FONT_4
  is a white fill over a dark-red outline font, but FONT_3's pair is the other way round (`FONT_3_OUTLINES` is
  the white letter, `FONT_3` the blue stroke) — `HilightColor` tints the fill, so it suits FONT_4 only.
* Chapter titles inside the books are pre-rendered per language (`CHAPTER_TEXT_CHAPTER_TEXT_{EN,FR,IT,DE,ES}.png` and
  `BOOKS_COMPOSPRITES` `BOOK_<CHAPTER>_<LANG>` composites), as are the share-email pictures (`SHARE_EMAIL_<LANG>`).

## 4. Audio UI

`ButtonAudio` toggles `SettingsUtils::SetAudioState`; `GameApp::playMusic(AudioId)` switches `Theme.mp3` (menus) /
`Music.mp3` (game), `BackgroundMusicUtils` cross-fades; `AudioSystemUtils::Mute/Unmute` on app deactivation.

**Remake addition — the music switch (2026-09-15).** The original's single toggle silences everything; the remake
adds `ButtonMusic` (the note icon, `BUTTON_SMALL_MUSIC` / `_OFF` drawn by `tools/remake_ui.py` into the
`REMAKE_COMMON` container, 12 §1) next to `ButtonAudio` in the main menu's `SettingsSlider` (gear → speaker →
note → info) and in the pause sidebar (`ButtonAudio` moved from `Relative.Y` 32 % to 22 %, `ButtonMusic` at
36 %; the dictionaries live in `core/ui/remake_views.h`). The two `Settings` flags become independent:
`soundEffectsOn` is the speaker (the master mute — `Mute` / `Unmute` as before, everything off), `musicOn` the
note (`AudioSystem::setMusicEnabled`: the music streams keep running at gain 0, so switching back on resumes
mid-track). `setAudioState(on)` no longer forces `musicOn = true`; `audioEnabled()` is gone. Old save files keep
their meaning (`soundEffectsOn = false` still mutes everything).

**Audio system rules [verified 2026-09-14: decompile + disassembly of `SoundSystemUtils::Play/PlayLooping/Stop/
SetClipVolume/Update`, `BackgroundMusicUtils::Play/Stop/Update`, `AudioSystemUtils::Mute/Unmute`, `GameApp::update`,
`GameApp::playMusic/stopMusic`, `UI::Button::PlayPress`, `SettingsUtils::SetAudioState/AudioEnabled`]:**

* `st::AudioSystem` = `{AudioOutput*, float master (+4), int handles[] (+8), int count (+0xC), capacity}`.
  `Mute` writes `master = 0`, `Unmute` `master = 1`. `SettingsUtils::SetAudioState(on)` writes `soundEffectsOn =
  on, musicOn = true` (`Settings+4/+5`, both default true); `AudioEnabled = soundEffectsOn && musicOn`;
  `MainMenuView::ButtonPressed` / `GameView::ButtonPressed` (the audio toggle) flip it, save the settings and
  call `Mute` / `Unmute`.
* `SoundSystemUtils::Play(id, volume, pos, audio)` **refuses** (returns −1) when `pos.x ∉ [−1, 4.41]`,
  `pos.y ∉ [−1, 3.12459]` (1 m around the play field) or when the playing list already holds **20** clips
  (`0x13 < count`); otherwise `playAudio(file, volume × master, loop = false)` and the handle is appended to the
  list. `PlayLooping` applies the position filter only (no count limit, not listed). `Stop(handle)`,
  `SetClipVolume(handle, volume)` (the caller multiplies by `master` itself); `Update` (once per frame from
  `GameApp::update`, after the scene update / draw and `BackgroundMusicUtils::Update`) drops finished clips from
  the list. UI buttons: `PlayPress` = `Play(3, 0.2, (0, 0))`, `PlayRelease` = `Play(4, 0.2, (0, 0))`, skipped
  when the button is silent (`Button+0x204`).
* **Music** (`st::BackgroundMusic` = `{enabled, state, current, next}`; `state` 0 stopped / 1 playing / 2 fading):
  `Play(id)` does nothing while `current == id`; state 0 → `playAudio(file, 0.3 for Theme (id 1) / 0.2 for
  Music (id 2), loop)` + the track gains set to `master`, `current = id`, state 1; state 1 → `next = id`, state 2;
  state 2 → `next = id`. `Stop` stops the clip and zeroes the three fields. `Update(dt)`: state 1 keeps the track
  gain at `master` (so a mute silences the music at once); state 2 lowers the gain by `(0.3 for Theme, 0.2 for
  Music) × 2·dt` per frame and, once it reaches 0, stops the clip and `Play(next)`. Callers: `MainMenuScene`,
  `ChapterSelectionScene`, `LevelSelectionScene`, `ChapterComplete*Scene::Activate` → `playMusic(1)`;
  `LevelLoadingScene::Activate` → `stopMusic()`; `GameScene::Activate` → `stopMusic(); playMusic(2)`;
  `GameScene::Inactivate` → `stopMusic()`.

## 6. Sound renderer — the looping clips

`st::SoundRenderer::Render(WorldState&, GameResources const&, AudioSystem&)` runs once per frame in
`GameScreenController::doFrame`'s simulation branch (state 4, after `UpdateSimulation` / the paused copy and
before the three-star test); `StopLoopingSounds` runs in `restoreGameState` (before `DestroyWorld`) and in
`setCompletedState` [verified 2026-09-14: decompile + disassembly]. It walks the object collection and keeps the
clip handles in the item blocks (the remake's `GameItem::clipHandle` / `clipId`, excluded from the G4 dumps):

| type | block fields | rule |
|---|---|---|
| Skateboard (20) | `+8` handle; `+0xC`, `+0x10` the two wheel `b2LineJoint*` | `v` = max of the two `|GetJointSpeed()|` (compared in double); no handle and `v > 8` → `PlayLooping(0x33 SkateboardRoll, 0.3)`; a handle and `v < 8` → `Stop`, handle −1; else `SetClipVolume(clamp((v − 8) / 50, 0, 0.5) × master)` |
| Magnet (25) | `+8` pulling, `+0xC` nearest distance², `+0x10` handle | not pulling → `Stop` (if any); pulling without a handle → `PlayLooping(0x2E MagnetActive, 1.0)`; then `d = dist² − 0.006`: `d > 0` → volume `clamp(1 − d, 0, 1)`, else `clamp(1 + 200·d, 0.1, 1)`; `SetClipVolume(volume × master)` |
| RCController (36) | `+8` paired handle (type byte: Truck → clips 0x2A start / 0x29 loop / 0x2B end, else Helicopter 0x34 / 0x35 / 0x36), `+0x10` pressed, `+0x14` handle, `+0x18` current clip id | `startDone = (cur == start && !isClipPlaying(handle))`. Pressed: `cur == end` → `Stop`, `Play(start, 0.2)`, `cur = start`; `handle == −1` → `Play(start, 0.2)`, `cur = start`. Released: `cur == loop` or `startDone` → (if a handle) `Stop`, `Play(end, 0.2)`, `cur = end`. Finally `startDone && cur == start` → `Stop`, `PlayLooping(loop, 0.2)`, `cur = loop` |
| ZipLine (42) | `+0x10` handle; body 2 (the trolley) velocity | `v = |bodies[2].linearVelocity|`; no handle and `v > 0.1` → `PlayLooping(0x3C ZipLineLoop, 0.3)`; a handle and `v < 0.1` → `Stop`; else `SetClipVolume(clamp(v − 0.1, 0, 0.2) × master)` |

`StopLoopingSounds` stops and clears the handle of every skateboard, magnet, controller (`+0x14`) and zip line;
the controller keeps `+0x18`. The remake ports the renderer in `core/sim/src/sound_renderer.cpp` over the
`aa::sim::SoundSink` interface (`core/platform/src/audio.cpp` implements it on raylib: one-shots as sound
aliases, loops as looping music streams, the 20-clip list, the position filter, the master volume).

## 5. Audio ids (`st::AudioId` → clip)

`st::AudioFilenames` (`__DATA,__data`, dumped by `tools/dump_audio_ids.py`) maps the `AudioId::Enum` value used by
`SoundSystemUtils::Play(id, volume, position, audio)` / `PlayLooping` and the play-sound action (05 §7, action 13) to
a clip in `Sounds_high/` (`Sounds_low/` on Android low-end). Id 0 = no sound; 1–2 are music, 3–12 UI, the rest gameplay.

| id | clip | id | clip | id | clip |
|----|------|----|------|----|------|
| 0 (`0x0`) | `` | 24 (`0x18`) | `CardboardBoxImpact.mp3` | 48 (`0x30`) | `TrapdoorLever.mp3` |
| 1 (`0x1`) | `Theme.mp3` | 25 (`0x19`) | `LaundryBasketImpact.mp3` | 49 (`0x31`) | `SeesawMove.mp3` |
| 2 (`0x2`) | `Music.mp3` | 26 (`0x1a`) | `PaperPlaneImpact.mp3` | 50 (`0x32`) | `SkateboardWheelImpact.mp3` |
| 3 (`0x3`) | `UIButtonPush.mp3` | 27 (`0x1b`) | `TruckImpact.mp3` | 51 (`0x33`) | `SkateboardRoll.mp3` |
| 4 (`0x4`) | `UIButtonRelease.mp3` | 28 (`0x1c`) | `LampImpact.mp3` | 52 (`0x34`) | `HelicopterStart.mp3` |
| 5 (`0x5`) | `UIItemAdded.mp3` | 29 (`0x1d`) | `DollImpact1.mp3` | 53 (`0x35`) | `HelicopterLoop.mp3` |
| 6 (`0x6`) | `UIItemRemoved.mp3` | 30 (`0x1e`) | `DollSqueak.mp3` | 54 (`0x36`) | `HelicopterEnd.mp3` |
| 7 (`0x7`) | `UISelectBuzz.mp3` | 31 (`0x1f`) | `BalloonPop.mp3` | 55 (`0x37`) | `HelicopterImpact.mp3` |
| 8 (`0x8`) | `UIItemSelected.mp3` | 32 (`0x20`) | `PiggyBankBreak1.mp3` | 56 (`0x38`) | `HelicopterBladesImpact.mp3` |
| 9 (`0x9`) | `UIItemDeselected.mp3` | 33 (`0x21`) | `Spring.mp3` | 57 (`0x39`) | `BouncyBallImpact1.mp3` |
| 10 (`0xa`) | `UIStickerThump.mp3` | 34 (`0x22`) | `ScissorsClose.mp3` | 58 (`0x3a`) | `BouncyBallImpact2.mp3` |
| 11 (`0xb`) | `UIHitMetal.mp3` | 35 (`0x23`) | `ScissorsCut.mp3` | 59 (`0x3b`) | `BouncyBallImpact3.mp3` |
| 12 (`0xc`) | `UIMarkerStroke.mp3` | 36 (`0x24`) | `BoxingGloveTriggered.mp3` | 60 (`0x3c`) | `ZipLineLoop.mp3` |
| 13 (`0xd`) | `CaseyGiggle4.mp3` | 37 (`0x25`) | `BoxingGloveHit.mp3` | 61 (`0x3d`) | `GoalStarPickup1.mp3` |
| 14 (`0xe`) | `CrowdCheer.mp3` | 38 (`0x26`) | `SlingshotFire.mp3` | 62 (`0x3e`) | `GoalStarPickup2.mp3` |
| 15 (`0xf`) | `SoccerBallImpact.mp3` | 39 (`0x27`) | `SlingshotStretch.mp3` | 63 (`0x3f`) | `GoalStarPickup3.mp3` |
| 16 (`0x10`) | `TennisBallImpact.mp3` | 40 (`0x28`) | `SlingshotUnstretch.mp3` | 64 (`0x40`) | `RopeTie1.mp3` |
| 17 (`0x11`) | `BowlingBallImpact.mp3` | 41 (`0x29`) | `RCTruckLoop.mp3` | 65 (`0x41`) | `RopeUntie1.mp3` |
| 18 (`0x12`) | `BilliardBallImpact.mp3` | 42 (`0x2a`) | `RCTruckStart.mp3` | 66 (`0x42`) | `PipeSnap.mp3` |
| 19 (`0x13`) | `PinballImpact.mp3` | 43 (`0x2b`) | `RCTruckEnd.mp3` | 67 (`0x43`) | `PipeUnsnap.mp3` |
| 20 (`0x14`) | `BookImpact.mp3` | 44 (`0x2c`) | `RCButtonClick.mp3` | 68 (`0x44`) | `ResultScreenStar1.mp3` |
| 21 (`0x15`) | `BucketImpact.mp3` | 45 (`0x2d`) | `RCRemoteImpact.mp3` | 69 (`0x45`) | `ResultScreenStar2.mp3` |
| 22 (`0x16`) | `DartImpact.mp3` | 46 (`0x2e`) | `MagnetActive.mp3` | 70 (`0x46`) | `ResultScreenStar3.mp3` |
| 23 (`0x17`) | `BumperImpact.mp3` | 47 (`0x2f`) | `TrapdoorOpen.mp3` |  |  |

Looping clips (stopped by `SoundRenderer::StopLoopingSounds`): `RCTruckLoop`, `HelicopterLoop`, `ZipLineLoop`,
`SkateboardRoll` (0x33, volume 8.0 in `SoundRenderer::Render`), `MagnetActive` (0x2E).
