# Amazing Alex — project and reverse-engineering overview

Goal of the project: a faithful remake of **Amazing Alex** (Rovio, 2012; originally *Casey's
Contraptions* by Snappy Touch — hence the `st::` namespace in the code) on a modern engine.
This `docs/` folder records both the recovered asset formats, game logic and physics and the implemented remake
architecture, build and verification procedures.

## Reference builds

| File | Platform | Version | Contents |
|------|----------|---------|----------|
| `Amazing_Alex_HD_1.0.4.ipa` | iOS (iPad, armv7) | 1.0.4, 2012-10-16 | executable `Amazing Alex HD` (3.8 MB, **not stripped**: 9684 symbols with full C++ names), assets for 1024×768 and 2048×1536 |
| `Amazing+Alex+HD+1.0.5.apk` | Android (armeabi / armeabi-v7a) | 1.0.5, 2012-12-10 | `libamazingalex.so` (2.5 MB, exported symbols), `classes.dex` (143 KB JNI glue), assets for 480×320 / 800×480 / 1024×768 |

### Reference choice

**Primary reference: iOS 1.0.4** (logic and assets). Android is used as a secondary source:

* Game data (levels, `GameItems.plist`, item/background atlases, sounds, scene XML) is **byte-identical**
  on both platforms after decryption (the only difference is a trailing `\n`).
* iOS ships the largest UI profile, **2048×1536** (Android tops out at 1024×768).
* The item atlas `GameItems.pvr` is the same on both (1024×1024 RGBA8888, uncompressed) — see
  [01-asset-formats.md](01-asset-formats.md).
* The iOS executable carries a full symbol table: the item-type enum, goal types, class layout etc. were
  recovered from it.
* The Android library is ARM-mode code with scalar VFP (soft-float ABI), so numeric physics constants were
  recovered from its instructions while names come from the iOS symbol table.
* Android additionally contains the chapter `00_Classroom_free` (free edition) and `*.pvr.zip`
  (zip wrappers around the same PVR files).

Online features (Levels of the Week, World of Contraptions, level upload/download, Flurry,
Appirater, Game Center) are **out of scope** for the remake — the servers are dead; they are
mentioned only where they affect data formats.

## Document map

| Document | Contents |
|----------|----------|
| [01-asset-formats.md](01-asset-formats.md) | encryption, KA3D container (`.dat`), PVR, TexturePacker atlases, audio, thumbnails, scene XML |
| [02-level-format.md](02-level-format.md) | full `Level.plist` schema, handles, flags, attachments, goals, toolbox, `0_Location.plist` |
| [03-game-items.md](03-game-items.md) | catalogue of the 42 item types: sprites, physics bodies, behaviour |
| [04-physics.md](04-physics.md) | Box2D version, world, simulation step, collision filters, bounds, attachments, ropes |
| [05-gameplay.md](05-gameplay.md) | modes, level loop, goals & stars, progression, toolbox, undo, ghost state, tutorial |
| [06-ui-localization.md](06-ui-localization.md) | UI scenes, fonts, localisation |
| [07-levels-catalog.md](07-levels-catalog.md) | generated catalogue of all 116 levels |
| [08-physics-dump.md](08-physics-dump.md) | **generated, authoritative**: every body, fixture (vertices, density, friction, restitution, filter), joint and mass override that the original `CreatePhysics` code creates for each item type, in both physics modes — obtained by running the Android binary under Unicorn (`tools/uc_dump_physics.py`); JSON twin `08-physics-dump.json` |
| [09-physics-regression.md](09-physics-regression.md) | trajectory harness: the emulated original vs the vendored Box2D 2.2.1 — method, 55/55 bit-identical result, limitations |
| [10-architecture.md](10-architecture.md) | implemented remake architecture: fidelity contract, layering, asset import, deterministic core, verification gates, milestone history, remaining deviations |
| [11-rendering.md](11-rendering.md) | screen layout & letterbox maths, asset-profile table, frame composition, item draw order (type tables, container front pass), sprite parts per item type, animation constants |
| [12-asset-tree.md](12-asset-tree.md) | **contract**: the imported asset tree (`tools/import_assets.py` output) that `core/data` loads — layout, level/atlas JSON schemas, float discipline |
| [13-development.md](13-development.md) | clean builds, tests, platform packaging, runtime diagnostics and repository layout |

Confidence tags in the reconstruction documents distinguish facts verified from shipped data, machine code or
the original running under Unicorn. Architecture decisions, remake deviations and historical milestone results
are labelled separately; they are not claims about the original executable.

## Tooling (`tools/`)

| Script | Purpose |
|--------|---------|
| `decrypt_assets.py <src> <dst>` | decrypt every `.plist`/`.xml` (AES-256-CBC), convert binary plists to XML |
| `dump_levels.py <Levels>` | level data-mining: field distributions, attachment/toolbox examples |
| `gen_levels_catalog.py` | generates `docs/07-levels-catalog.md` |
| `ka3d_text.py <.dat> -o out.json` | parser for KA3D `TEXT` localisation bundles |
| `ka3d_dat.py <.dat>… [-o out.json]` | parser for KA3D `SPRT`/`COMP`/`FONT` UI atlases, composites and bitmap fonts |
| `dump_audio_ids.py <iOS binary> [-o t.md]` | dumps the `st::AudioId` → clip-name table (`st::AudioFilenames`) |
| `uc_harness.py <lib.so>` | Unicorn loader for the Android `.so`: maps it at base 0, stubs libc/libm imports, runs `.init_array` (all `.bss` constants become readable), lets you call exported functions |
| `uc_dump_physics.py <lib.so> <GameItems.plist> -o dump.json --md dump.md` | runs the game's own `CreatePhysics` for all 42 item types inside a real emulated `b2World` and records every body/fixture/joint → `docs/08-physics-dump.md` |
| `uc_trace.py <lib.so> <GameItems.plist> --out DIR --drop all --level L.plist` | ground-truth physics traces: records the game's Box2D construction calls as a scene file and steps the emulated world (09) |
| `trace_native/` (CMake) | replays a scene file on the vendored Box2D 2.2.1 (`core/third_party/Box2D`) |
| `vendor_aa_libm.py`, `gen_aa_libm_kat.py` | re-vendor the device libm (`core/third_party/aa_libm`, Bionic/msun) and regenerate its known-answer table |
| `trace_compare.py DIR`, `trace_perturb.py` | trajectory diff report / 1-ulp chaos-floor calibration |
| `uc_manifold_probe.py <lib.so> <GameItems.plist> <level.plist> --body N` | physics-divergence probe: the emulated original's polygon manifolds and the probed body's velocity at every contact-solver stage of a level scene (09 §5) |
| `run_physics_regression.sh [OUT]` | the whole regression: build, trace, replay, compare → `OUT/report.md`, `OUT/noise.md`; exit 1 unless 55 scenarios are bit-identical (CTest `physics_regression`, gate G2) |
| `import_assets.py <ipa\|apk\|dir> <out>` | converts the player's bundle into the engine-native tree of `docs/12-asset-tree.md` (levels/atlases/UI/texts/media + `manifest.json`) |
| `sim_conformance.py --dump <sim_scene_dump>` | gate G3: the core's `createPhysics` (`tests/sim_scene_dump`, `.scene` format) vs the emulated original for all 42 types × mode × flip (186 drops) and all 116 levels × mode with attachments (418 scenes); CTest `sim_conformance` |
| `render_contact_sheet.py <screens> <thumbnails> <out.png>` | M2 visual check: the viewer's `--screenshots` PNGs next to the original `LevelThumbnails_350` crops (Pillow) |
| `uc_setup_oracle.py <lib.so> <GameItems.plist> --levels DIR --script S.txt --out DIR` | gate G5a oracle: runs a set-up manipulation script (`tests/setup_scripts/*.txt`: `updatepos`, `setpos`, `updateangle`, `flip`, `started`, `ended`, `snap`, `detach`, `attachnearby`, `step0`, `colliding`…) with the game's own `GameItemUtils` / `AttachmentUtils` on a shipped level under Unicorn and dumps poses, attachment records, Box2D calls and actions (`.setup`) |
| `setup_conformance.py --dump <sim_setup_dump>` | gate G5a: the same scripts on the core (`tests/sim_setup_dump`) vs the oracle, line by line; CTest `setup_conformance` |
| `uc_sim_oracle.py <lib.so> <GameItems.plist> --levels DIR --script S.txt --out DIR` | gate G4 / G6 oracle: builds the real `st::GameState` under Unicorn, applies a shipped level through the play path (`LevelLayoutUtils::Apply` → `CreateWorld(1)` with the original contact listener), runs `tests/sim_scripts/*.txt` (`frames N`, `tap`, `goal`, `seed`, `listener off`) with a Python transcription of `doFrame` / `UpdateSimulation` calling every `…Utils::Update` and `ProcessSimulationAction`; dumps `.scene`, `.uc.traj` (per substep) and `.sim` (per-frame state, item blocks, actions) |
| `sim_run_conformance.py --dump <sim_run_dump> [--all-levels --seconds 5] [--known FILE]` | gates G4 / G6: the same scripts (or every level of the catalogue) on the core (`tests/sim_run_dump`) vs the oracle — scene, trajectories and `.sim` sections bit for bit; CTest `sim_run_conformance` (gate) and `sim_catalogue_conformance` (catalogue) |
| `decomp_dis.py fn <name>\|range <a> <b>\|rd <addr>\|bss <addr> <n> f` | disassembly helpers for porting (literal pool decoded, `.bss` after static init via Unicorn) — docs/10 §11 item 7 |
| `build_macos_app.sh`, `macos_icon.py` | build and ad-hoc sign the self-contained macOS `.app`, including imported assets and launcher artwork |
| `build_android.sh`, `android_icon.py`, `android_emulator.sh`, `build_windows_mingw.sh`, `windows_icon.py`, `build_linux_docker.sh` | platform builds and checks: APK packaging and emulator validation, MinGW/Wine cross-build, and the Docker Linux build |
| `build_web.sh` | Emscripten/WebAssembly build with the imported asset tree preloaded and browser persistence enabled |
| `amazing_alex --headless` / `--ui-screenshots <dir>` (built binary, not a script) | M5 / M6 smoke: the scripted walk of docs/10 §8 (G7: the campaign, then My Levels, the credits, the exit dialog) without a window (exit 0) / with one PNG per stop; `AA_WALK_DEBUG=1` traces every tap |
| `gen_level_expectations.py <Levels> <out.inc>` | regenerates `tests/expected_levels.inc` (float32 bit patterns of six shipped levels for `test_level_loader`) |

Working directories (not committed, reproducible with the scripts): `extracted/ipa`, `extracted/apk`
— unpacked packages; `extracted/ios_dec`, `extracted/android_dec` — decrypted assets;
`extracted/texts` — localisation as JSON.

## Key facts (summary)

* Engine: in-house `ka-core` by Kajak Interactive (namespaces `lang`, `io`, `gr`, `hgr`, `game`, `UI`, `pf`);
  game logic in `st::` (Snappy Touch); physics: **Box2D SVN trunk of spring 2011 (pre-2.2.0)**.
* World: **3.41 × 2.12459 m** (1024×768 px with a 130 px floor), 1 px = 3.41/1024 m; gravity (0, −9.8).
* Simulation: fixed step **1/120 s**, `b2World::Step(dt, 10, 10)`, render-state interpolation.
* Levels: XML plists encrypted with AES-256-CBC (key embedded in the binary); 116 levels in 4 chapters.
* Items: 42 types (`st::ItemType`); body shapes are hard-coded, sizes derived from sprite frames.
* Simulation time runs at **0.8 × real time** (04 §3) — the only "feel" parameter that is not a physics constant.

## How to answer a new question about the original

The Android library can be executed function by function (`tools/uc_harness.py`): `Emu.call` runs any exported
function, `Emu.hook_function` intercepts any address, `run_static_initializers()` makes every `.bss` constant
readable. `tools/uc_dump_physics.py` shows the recipe for building a real `b2World` and an item: create the frames
array, call `InitializePhysicsObjectTemplates` and `CollisionFiltersUtils::Create`, `PhysicsObjectsUtils::Add`,
`PhysicsObjectUtils::CreatePhysics(obj, world, handles, mode)`; then step with `b2World::Step(1/120, 10, 10)` and
read body state (`+0xC` position, `+0x48` velocity, `+0x54` force, `+0x5C` torque, layout in 04 §1). The Android
emulator route is not viable on Apple Silicon (no arm64 system image with `armeabi-v7a` support boots under
Hypervisor.framework).

## Not investigated (out of scope for the remake)

* Online sharing protocol (`SharingManager`, `UploadOperation`, `DownloadOperation`, `HttpOperation`, cJSON,
  `ServerUtils::CRC16`, SHA-1 signatures with `UploadKey`) — servers are gone.
* Flurry analytics, Appirater, Game Center, ads (`AlexAds`, free edition), IAP (`00_Classroom_free`).
* Low-level ka-core renderer implementation beyond the behaviour needed by the remake (`gr::EGL_*`, the exact
  `hgr::ParticleSystem` implementation); sprite composition and draw order are documented in 11.
* `classes.dex` (JNI glue) — assumed to contain no game logic (143 KB; `jadx` available if needed).

## Remake implementation

The architecture and the milestone order live in [10-architecture.md](10-architecture.md). Physics is done: the
vendored Box2D 2.2.1 with the trunk's behaviour back-ported (`core/third_party/Box2D`) is bit-identical to the
original on all 55 harness scenarios (09), with the device's own libm vendored as `core/third_party/aa_libm`. Keep
`tools/run_physics_regression.sh` green and run `aa_libm_selftest` on every new platform/compiler before trusting
its numbers. M0–M7 are done (10 §10): the core builds every level's world bit-exactly (G3), the set-up phase —
pick, drag, snap, rotate, flip, ghost, toolbox strip, undo, edge scroll — is ported and checked against the
emulated original's own set-up functions (G5a) and by doctest suites (G5), the simulation — the 1/120 s loop,
the contact listener, every item's `Update`, goals, stars, the completion sequence — runs bit-identically to the
real `GameState` under emulation on 39 scripted runs (G4) and on all 116 levels for 5 s (G6; the last
divergence, a solver rounding in `02_Room/LaunchingRamp`, was found and fixed in the M5 follow-up, 09 §5), the
viewer plays levels end to end with the mouse (README), since M5 (2026-09-14) the full campaign is playable:
the original's UI screens, progression and saves, audio, five locales and the Classroom tutorial, and since M6
(2026-09-14) the sandbox editor with My Contraptions, the Classroom comic, the credits and the dialogs — the
whole offline feature set (10 §10); M7 (2026-09-14) brings it to Android (an APK without Gradle, the assets
bundled and read in place, multi-touch, the original's pause-on-background), Windows (mingw-w64,
checked under Wine) and Linux (gcc 13 in Docker), with the two-finger touch states, the widescreen UI tweaks and the
original's letterbox side frames — iOS excluded (no toolchain here, 10 §10). The whole plan is done.
