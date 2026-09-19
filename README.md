# Amazing Alex remake

An open-source, offline remake of Rovio's *Amazing Alex* (2012). It recreates the original campaign, physics,
presentation, sound, localisation and sandbox editor while running natively on current desktop, Android and web
platforms.

The repository contains only the remake's source code. It does **not** redistribute the original game data. To
play, provide your own copy of *Amazing Alex HD* as an `.ipa` or `.apk`; the included importer converts its assets
into the format used by the remake.

## What is included

- The complete four-chapter offline campaign with 116 levels, progression and saves.
- Physics and item behaviour reconstructed from the shipped game.
- The original menus, chapter comic, tutorials, five languages, music and sound effects.
- **My Contraptions**, an offline sandbox for building, testing and saving custom levels.
- Mouse, keyboard and touch input, including Android background/resume behaviour.
- Native builds for macOS, Windows, Linux and Android, plus an optional WebAssembly build.

Online-only books from the original game—World of Contraptions and Level of the Week—are not included.

## Quick start

### 1. Install the common requirements

- Python 3.10 or newer
- CMake 3.25 or newer
- A C++17 compiler

Create the Python environment used by the asset importer:

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
```

On Windows PowerShell, use `.venv\Scripts\python` instead of `.venv/bin/python` in the commands below.

### 2. Import your game data

The importer accepts an IPA, APK, unpacked application bundle or previously decrypted asset directory. Manual
archive extraction is not required.

For the iPad 1.0.4 IPA, which contains the highest-resolution assets:

```sh
.venv/bin/python tools/import_assets.py \
  Amazing_Alex_HD_1.0.4.ipa build/assets \
  --border-profile 1024X768
```

For an Android APK:

```sh
.venv/bin/python tools/import_assets.py \
  Amazing+Alex+HD+1.0.5.apk build/assets
```

A successful import creates `build/assets/manifest.json`. The generated asset tree is local, reproducible and
ignored by Git. (An Android package also carries the lite edition's own chapter of 16 easy levels; the importer
skips it.)

### 3. Build and play

#### macOS

Install the Xcode command-line tools, then build a Finder-launchable application:

```sh
make macos
open "build/app/Amazing Alex.app"
```

The app bundle contains your imported assets, so it starts without command-line options. It is ad-hoc signed,
not notarized; macOS may show a Gatekeeper warning when it is moved to another computer.

For a quicker local build without packaging, run `make run`.

#### Windows

Native Windows builds use CMake and Visual Studio 2022:

```powershell
cmake -S . -B build -A x64 -DAA_ASSETS="$PWD/build/assets"
cmake --build build --config Release --parallel
build\app\Release\amazing_alex.exe --assets build\assets
```

From macOS or Linux, install MinGW-w64 and optionally Wine, then run:

```sh
make windows
```

The cross-build produces `build-mingw/app/amazing_alex.exe`. If Wine is installed, the script also runs its
self-tests and the scripted game walkthrough.

#### Linux

The most reproducible Linux build uses Docker and Ubuntu 24.04:

```sh
make linux
build-linux-docker/app/amazing_alex --assets build/assets
```

The script installs build dependencies inside the container, builds the game and runs its tests and headless
walkthrough. For a native distribution build, see the [development guide](docs/13-development.md).

#### Android

Install Android SDK Platform 36, Build Tools, NDK 27 or newer, Ninja and a JDK. Make sure `ANDROID_SDK_ROOT` points
to your SDK, then run:

```sh
make android
adb install --no-incremental build/android/amazing_alex.apk
```

The arm64-v8a APK contains the imported assets and reads them in place from the package (nothing is extracted
into private storage). It uses a locally generated debug signing key and is intended for personal installation, not store distribution.

To validate the APK on a connected device or configured emulator:

```sh
tools/android_emulator.sh --keep
```

#### Web

Activate an Emscripten SDK environment first, then run:

```sh
make web
python3 -m http.server -d build-web/app 8080
```

Open <http://localhost:8080>. Browser saves are stored in that browser profile's IndexedDB.

## Common commands

```sh
make build       # configure and compile the native desktop game
make run         # build and launch it with build/assets
make headless    # run the complete scripted campaign and sandbox smoke test
make test-fast   # run the portable self-test and unit/integration suites
make test        # run every available test, including the optional original-game comparison harness
make viewer      # open the standalone level viewer
make help        # show targets and configurable paths
```

To use a different imported tree or build directory:

```sh
make run ASSETS=/path/to/assets BUILD=/path/to/build
```

## Controls

- Mouse or one finger: select, drag and place objects.
- Mouse wheel while holding an object: rotate it.
- Mouse wheel over a page: turn the page.
- `F`: flip the held object.
- `Z` / `Y`: undo / redo.
- `Space`: start or stop the simulation.
- `Esc` or Android Back: close the current screen, open pause, or return to the previous screen.

The game accepts `--fullscreen`, `--size WIDTHxHEIGHT`, `--locale en_EN|fr_FR|it_IT|de_DE|es_ES`, `--save-dir DIR`
and `--no-audio`. The complete runtime and diagnostic option list is in the
[development guide](docs/13-development.md#runtime-and-diagnostic-tools).

## Saves

Progress, settings and custom levels are stored outside the repository:

- macOS: `~/Library/Application Support/AmazingAlex`
- Linux: `$XDG_DATA_HOME/AmazingAlex` or `~/.local/share/AmazingAlex`
- Windows: `%APPDATA%\AmazingAlex`
- Android: the application's private data directory
- Web: the browser profile's IndexedDB

Corrupt JSON saves are preserved with a `.corrupt-N` suffix and replaced with safe defaults.

## More information

- [Development, testing and packaging guide](docs/13-development.md) — clean builds, detailed dependencies,
  architecture, platform build internals, the original-game conformance harness and troubleshooting.
- [Project and reverse-engineering overview](docs/00-overview.md)
- [Imported asset-tree format](docs/12-asset-tree.md)
- [Architecture and implementation plan](docs/10-architecture.md)
- [Gameplay reconstruction](docs/05-gameplay.md)

This project is an independent preservation/remake effort and is not affiliated with or endorsed by Rovio.
Original game assets remain the property of their respective copyright holders and are never distributed by this
repository.

## License

The remake's own source code and documentation are licensed under the [MIT License](LICENSE). Vendored
third-party libraries under `core/third_party/` keep their own licenses.
