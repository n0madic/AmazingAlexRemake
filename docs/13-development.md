# Development, testing and packaging

This document contains the engineering details intentionally omitted from the user-facing README: reproducible
build setup, asset conversion, test gates, platform packaging, repository layout and troubleshooting.

## Toolchain

The known working baseline is:

- CMake 3.25 or newer and Python 3.10 or newer;
- Apple Clang on macOS arm64;
- GCC 13 in the Ubuntu 24.04 Docker build;
- Visual Studio 2022 in CI and MinGW-w64 for cross-compilation;
- Android NDK 27, SDK Platform 36 and Android Build Tools;
- Emscripten for the optional web target.

Install the Python tools into the repository-local environment:

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
```

`pycryptodome` and Pillow are required by the importer. Unicorn and LIEF support the optional original-binary
research harness. All C/C++ dependencies are vendored under `core/third_party`; there are no Git submodules.

## Reproducible clean build

Starting from a clean checkout and an original package:

```sh
.venv/bin/python tools/import_assets.py \
  Amazing_Alex_HD_1.0.4.ipa build/assets \
  --border-profile 1024X768

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAA_ASSETS="$PWD/build/assets"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/app/amazing_alex --assets build/assets --headless
```

The IPA/APK, `.venv`, `extracted/`, `build/` and all `build-*` directories are ignored. Consequently,
`git clean -xfd` deletes the original packages as well as every reproducible output. Keep the original IPA/APK
outside the checkout or back it up before performing such a clean.

The importer refuses to write into a non-empty destination unless `--force` is supplied. A clean rebuild should
remove `build/assets` and import again instead of mixing output from two importer versions.

## Asset import pipeline

`tools/import_assets.py` accepts an IPA/APK directly, an unpacked package or a tree produced by
`tools/decrypt_assets.py`. It locates the bundle's `Data` root, decrypts resources while reading and emits the
engine-native tree documented in [12-asset-tree.md](12-asset-tree.md):

- 116 levels and four chapter indexes;
- GameItems, background, foreground and UI atlases as PNG plus JSON;
- UI scenes, fonts, dialogs, tips and remake-only sprites;
- five merged localisation tables;
- sound effects, music and the largest level thumbnails;
- canonical launcher artwork at `branding/icon.png`;
- `manifest.json` with source metadata, counts and SHA-1 for every emitted file.

The default is the largest profile in the package. The IPA's `2048X1536` profile has no side-frame sheet, so
`--border-profile 1024X768` imports and scales it. Android packages normally use their largest profile without
that option. Validation rejects an unknown or patched package when expected campaign, item, locale or frame
counts differ.

## CMake options and Make targets

| Option | Default | Purpose |
| --- | --- | --- |
| `AA_BUILD_APP` | `ON` | Build raylib, the platform layer and game executable. |
| `AA_BUILD_TESTS` | `ON` | Build doctest suites and conformance dump tools. |
| `AA_ASSETS` | empty | Imported tree used by asset-backed tests. |
| `AA_BUILD_WEB` | `OFF` | Select the Emscripten application. |
| `AA_WEB_ASSETS` | empty | Asset tree preloaded into a web build. |
| `AA_WEB_FAVICON` | empty | Optional PNG copied beside the web build as `favicon.png`. |
| `AA_WINDOWS_ICON` | empty | Optional ICO embedded into the Windows executable. |
| `AA_NOINTRO` | `OFF` | Make the application start at the main menu instead of the splash. |

The root Makefile provides `build`, `test`, `test-fast`, `run`, `headless`, `viewer`, `macos`, `windows`, `linux`,
`android` and `web`. Override paths with `BUILD`, `ASSETS`, `WINDOWS_BUILD`, `LINUX_BUILD`, `ANDROID_OUT` and
`WEB_BUILD`; `CONFIG` selects the configuration and `ICON` overrides imported branding.

Floating-point targets disable contraction and fast-math. On 32-bit x86 they use SSE2 instead of x87. These
flags are part of the physics compatibility contract and must not be relaxed for release builds.

## Test layers

### Portable and production-path tests

`make test-fast` runs:

- **G1 / `aa_libm_selftest`** — known-answer tests for the device-compatible libm;
- **`aa_tests`** — loaders, saves, progression, UI, tutorials, physics, interaction, sandbox and touch tests.

Asset-dependent cases use `AA_ASSETS`. Without a valid imported tree they report that they were skipped.

`make headless` executes the production application through a scripted walkthrough: splash and comic, campaign
levels and page unlock, saves, My Contraptions creation/test/reopen/delete, credits and dialogs. Without an
explicit `--save-dir`, it uses a temporary directory and does not touch player progress.

### Original-game conformance harness

The optional gates compare the remake with the Android ARM binary under Unicorn. They require the exact reference
packages, `.venv`, the unpacked APK and decrypted resources:

```sh
mkdir -p extracted/ipa extracted/apk
unzip -q Amazing_Alex_HD_1.0.4.ipa -d extracted/ipa
unzip -q "Amazing+Alex+HD+1.0.5.apk" -d extracted/apk
.venv/bin/python tools/decrypt_assets.py \
  "extracted/ipa/Payload/Amazing Alex HD.app" extracted/ios_dec

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAA_ASSETS="$PWD/build/assets"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake registers the following only when the selected Python interpreter and
`extracted/apk/lib/armeabi-v7a/libamazingalex.so` exist:

- **G2 / `physics_regression`** — 55 bit-identical physics scenarios;
- **G3 / `sim_conformance`** — world construction for all implemented item types and modes;
- **G5a / `setup_conformance`** — scripted setup manipulation;
- **G4 / `sim_run_conformance`** — selected full simulation scripts;
- **G6 / `sim_catalogue_conformance`** — five seconds across the complete catalogue.

Without the harness, CTest intentionally registers only G1 and `aa_tests` and prints a configure-time skip
message. Use `ctest --test-dir build -N` to confirm which layer will run.

Individual investigation commands:

```sh
tools/run_physics_regression.sh build/physics_regression
.venv/bin/python tools/sim_conformance.py --dump build/tests/sim_scene_dump
.venv/bin/python tools/setup_conformance.py --dump build/tests/sim_setup_dump
.venv/bin/python tools/sim_run_conformance.py --dump build/tests/sim_run_dump
.venv/bin/python tools/sim_run_conformance.py \
  --dump build/tests/sim_run_dump --all-levels --seconds 5 \
  --known tests/sim_known_divergences.txt
```

The harness is sensitive to its Unicorn runtime and host architecture. An `Illegal instruction` from
`uc_trace.py` is a harness/runtime failure, not evidence of a physics mismatch.

### Regenerating research documents

```sh
.venv/bin/python tools/uc_dump_physics.py \
  extracted/apk/lib/armeabi-v7a/libamazingalex.so \
  extracted/ios_dec/Common/Game/GameItems.plist \
  -o docs/08-physics-dump.json --md docs/08-physics-dump.md

mkdir -p extracted/texts
.venv/bin/python tools/ka3d_text.py \
  extracted/ios_dec/Common/TEXTS_BASIC.dat \
  -o extracted/texts/TEXTS_BASIC.json
.venv/bin/python tools/gen_levels_catalog.py \
  extracted/ios_dec/Levels extracted/texts/TEXTS_BASIC.json \
  docs/07-levels-catalog.md
```

## Platform packaging details

### macOS

`tools/build_macos_app.sh` builds `amazing_alex_bundle`, copies assets to `Contents/Resources/assets`, creates
`AppIcon.icns`, applies an ad-hoc signature and, unless `--no-run` is used, runs the bundled headless walkthrough.

```sh
tools/build_macos_app.sh \
  --assets build/assets --build build \
  [--icon custom.png] [--debug] [--no-run]
```

It does not provide Developer ID signing or notarisation. Bundles containing original assets must not be
redistributed.

### Windows

The MSVC build is exercised by CI without proprietary assets. The cross-build requires MinGW-w64 and converts
`branding/icon.png` into a multi-resolution executable resource:

```sh
tools/build_windows_mingw.sh \
  --assets build/assets --build build-mingw \
  [--icon custom.png] [--no-run]
```

When Wine is present it runs G1, asset-backed `aa_tests` and the headless walkthrough. `--no-run` builds without
requiring Wine.

### Linux

`tools/build_linux_docker.sh` mounts source and assets read-only, installs compiler and GLFW dependencies in an
Ubuntu 24.04 container, then runs G1, `aa_tests` and the headless walkthrough. The host build directory remains as
a cache and contains the executable and canonical icon.

```sh
tools/build_linux_docker.sh \
  --assets build/assets --build build-linux-docker \
  [--image ubuntu:24.04] [--icon custom.png]
```

### Android

The APK pipeline deliberately does not use Gradle. `tools/build_android.sh`:

1. cross-compiles `libamazing_alex.so` with CMake and the NDK;
2. stages the imported tree under `assets/aa`;
3. creates density and adaptive launcher icons;
4. packages resources with `aapt2`;
5. adds the arm64 library, aligns the APK and signs it with a generated debug key.

Defaults are arm64-v8a, minSdk 24 and SDK Platform 36. Override discovery where necessary:

```sh
tools/build_android.sh \
  --assets build/assets --out build/android \
  --abi arm64-v8a --sdk "$ANDROID_SDK_ROOT" --ndk /path/to/ndk \
  --build-tools VERSION --platform 36 [--icon custom.png]
```

The game reads the bundled tree in place from the APK (`aa::data::AssetRoot` with the platform's file reader,
10 §6); nothing is extracted into private storage, and a stamped copy left by an earlier build is removed on start.
`tools/android_emulator.sh` checks a fresh install and repeated runs against retained saves;
`--screenshots` adds the UI screenshot walk, `--touch` performs a basic touch session and `--keep` retains state.

Android-native tests can be built separately:

```sh
cmake -S . -B build-android-tests -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_static -DAA_BUILD_APP=OFF \
  -DAA_ASSETS=/data/local/tmp/aa_assets
cmake --build build-android-tests
adb push build/assets /data/local/tmp/aa_assets
adb push build-android-tests/tests/aa_tests /data/local/tmp/
adb shell 'cd /data/local/tmp && ./aa_tests'
```

Source-dependent tests may additionally require `AA_SOURCE_DIR` and checked-in oracle files at a matching device
path.

### Web

`tools/build_web.sh` requires an active Emscripten environment. It preloads the asset tree, enables C++ exception
catching for loader diagnostics, places the compiler cache in the output tree and uses the imported artwork as
the favicon.

```sh
tools/build_web.sh --assets build/assets --out build-web [--nointro]
```

Serve `build-web/app` over HTTP; `file://` is unsupported. `--nointro` or the `?nointro` URL parameter starts at
the main menu.

## Runtime and diagnostic tools

The desktop application accepts:

```text
--assets DIR       imported asset tree
--save-dir DIR     alternate save directory
--locale LOCALE    en_EN, fr_FR, it_IT, de_DE or es_ES
--size WxH         initial landscape window size
--fullscreen       full-screen mode
--no-audio         disable audio output
--headless         scripted production-path smoke test
--ui-screenshots D run that walk and save screenshots
--frames N         close after N frames
```

`--viewer` opens campaign levels directly through the core render state. Useful options are `--level`, `--script`,
`--screenshots`, `--screenshot-markers` and `--no-markers`.

Generate a visual catalogue with:

```sh
build/app/amazing_alex \
  --viewer --assets build/assets --screenshots build/screens
python3 tools/render_contact_sheet.py \
  build/screens build/assets/thumbnails build/contact_sheet.png
```

Set `AA_WALK_DEBUG=1` to print every action in the headless/UI screenshot walkthrough.

`--unlock-all` is undocumented in `--help` on purpose (a manual-testing aid, not a player-facing feature): every
chapter reports open regardless of the collected-star thresholds and every level of one reports at least
unlocked. It works by overriding what `GameProgress::locationUnlocked()` / `LocationState::status()` *report*
(`unlockAll` on each, set by `AppState` after a chapter's real state has already loaded) rather than writing
`LocationProgress::unlocked` or a `LevelSlot::status` — the fields `SaveStore` (de)serialises — so it never
touches a real save: `AppState::saveLocation()` / `saveProgress()` write nothing while it is on, so neither
opening a chapter or a level nor completing one reaches `location_N.json` / the progress file (the in-memory
state advances for the session only — it is a cheat, so progress made with it does not count; a completion
stored on a still-locked page would otherwise unlock the rest of the page through the load-time page repair).
Settings and sandbox files save as usual. Safe to point at a real `--save-dir` to reach a level (the four
unlisted Treehouse ones, 02 §1) without risk.

## Architecture and repository layout

```text
CMakeLists.txt        top-level targets, strict floating-point policy and test gates
core/third_party/     pinned Box2D, aa_libm, raylib, cJSON and doctest
core/sim/             deterministic simulation, physics, items and interaction
core/data/            imported resource loaders and sandbox level writer
core/game/            progression, saves and localisation
core/ui/              menus, campaign, dialogs, comic and sandbox editor
core/platform/        renderer/audio/input plus desktop, Android and web adapters
app/                  executable / Android shared-library entry point
platform/             per-OS packaging inputs (android/, macos/, web/, windows/)
tests/                doctest suites, conformance dump tools and scripts
tools/                importer, research harnesses, generators and packaging scripts
docs/                 reconstruction evidence, formats and architecture
```

The layer direction is `third_party → sim → data/game/ui → platform → app`. The deterministic simulation does
not depend on raylib. Runtime code never parses encrypted plist, PVR or KA3D containers; the importer normalises
those formats before the game starts.

See [10-architecture.md](10-architecture.md) for ownership rules and implementation milestones, and
[09-physics-regression.md](09-physics-regression.md) for the evidence and comparison method.

## CI boundaries

GitHub Actions builds Linux, Windows/MSVC, macOS and the Android shared library and runs portable tests. Original
packages cannot be stored in CI, so CI does not exercise asset import, asset-backed cases, a complete APK, the
production walkthrough with assets or Unicorn comparison gates. Those remain local release checks.
