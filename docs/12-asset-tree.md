# Imported asset tree (`tools/import_assets.py` output)

Status: **contract** (2026-09-13). The importer's output format is what `core/data` loads; changes here need a
matching loader change and a bump of `manifest.json` `format`. Everything in the tree is derived from the
player's own copy of the game (`docs/10-architecture.md` §1, §4): nothing here is redistributed.

```
tools/import_assets.py <ipa|apk|bundle-dir|decrypted-dir> <assets-dir> [--profile 2048X1536] [--force]
```

Sources: an `.ipa` (`Payload/*.app/`), an `.apk` (`assets/Data/`), an unpacked bundle directory or the decrypted
tree of `decrypt_assets.py` — the importer finds the bundle root by `Common/Game/GameItems.plist`, decrypts
`.plist`/`.xml` on read (01 §2) and unwraps Android's `*.pvr.zip`. Runtime game data is byte-identical for the
`.ipa` and its decrypted tree (the Android package's extra `00_Classroom_free` — the lite edition's own chapter of 16
easy levels, `0_Location.plist` with an empty name — is skipped: it adds little to the full game, and every count and
gate stays the full game's); branding uses the best launcher artwork present in the supplied package, so an
app-only decrypted tree can use a smaller icon than the archive's top-level `iTunesArtwork`, and a bare `Data`
tree (no artwork at all) imports without `branding/icon.png` — a warning, not an error; every packaging
script keeps its generic icon then. Runs in ~1.5 s.

## 1. Layout

```
manifest.json                      format, source {kind, name, profiles}, profile, chapters[], counts{}, files{path: sha1}
branding/icon.png                  canonical RGBA launcher artwork (best IPA/APK icon, normalised to PNG; absent for a bare Data tree)
levels/<chapter>/index.json        {name: "CHAPTER_NAME_CHAPTER1", levels: [...play order...], unlisted: [...]}
levels/<chapter>/<Name>.json       one level (§2)
atlases/<name>.png + <name>.json   GameItems, GameItems2, LocationBackground00..03, LocationForegrounds, UIElements (§3)
ui/<profile>/<name>.json (+ .png)  KA3D SPRT/COMP/FONT containers as ka3d_dat.py emits them (01 §5), PNG copied —
                                   all but DROPPED_UI_CONTAINERS (the LotW / WoC book sheets, unused by the remake)
ui/<profile>/REMAKE_COMMON.json+png  the remake's own sprites, drawn at import time by tools/remake_ui.py (the
                                   music switch's note icons BUTTON_SMALL_MUSIC / _OFF, 06 §4), scaled by the profile width
ui/<profile>/BORDER_BORDER.json+png  with --border-profile <P>: <P>'s letterbox border sheet resampled by the
                                   profile-width ratio (rects, pivots and the PNG ×2 for 1024X768 → 2048X1536)
ui/fonts.json                      <profile>/XML/Fonts.xml as JSON
ui/scenes/<Name>.json              Common/XML/*Scene.xml and Dialogs.xml as JSON (view trees, key order kept)
tips.json                          Common/Tips.plist as JSON (keys sorted: the binary plist's order is arbitrary)
texts/<locale>.json                {id: text}; TEXTS_BASIC + TEXTS_LANGUAGE_SELECTION merged per locale
sounds/*.mp3, music/*.mp3          Sounds_high / Music_high (the `_low` sets when only those exist — APK)
thumbnails/*.jpg                   the largest LevelThumbnails_<size> set of the bundle (350 iOS, 175 APK; the
                                   renderer reads the size off the manifest's file names)
```

Manifest format 2 adds `branding/icon.png`; Android, macOS, Windows, Linux and Web packaging consume this canonical file.

`counts` (iOS 1.0.4): chapters 4, levels 116 (112 listed + 4 unlisted Treehouse files: `HoneyBucket`,
`OpenFire`, `RescuePiggy`, `Ricochet`), itemTypes 39 (every type but 12 FishBowl, 21 Pulley and the converted
40), locales 5, frames.GameItems 150, frames.GameItems2 26, frames.UIElements 77, ui.dat 35 (36 with the
border sheet: `ui.border` 1, M7; the bundle's 37 minus the two dropped book sheets), scenes 15, sounds 68, music 2, thumbnails 112. The importer aborts on a missing file, an unknown PVR pixel format, more than 2
attachments on an item, a handle whose type bits disagree with the item type, and on any count that differs
from the expected value (unknown or patched bundle).

Profile: the largest UI profile in the bundle unless `--profile` says otherwise (`2048X1536` on iOS, `1024X768`
on the APK); `manifest.profile` records it, `source.profiles` lists the available ones (10 §6). The `2048X1536`
profile ships no `BORDER_BORDER` container (the letterbox side frames, 11 §1); `--border-profile 1024X768` takes
it from that profile (both bundles have it) resampled ×2 — the tree the game and the tests use is imported with
it. On Android the same tree is packed into the APK as `assets/aa/**` and read in place through `AssetRoot`'s
platform file reader (10 §6).

## 2. Level JSON

The schema of 02 §2–§5 with the loader's legacy handling applied once: `version` is always 7 (`sourceVersion`
keeps the file's), a legacy type-40 item (`version ≤ 6`) is already `36 RCController` with its handle's type
bits rewritten and the paired Helicopter's `itemData` handle rewritten the same way; version-4 goal lists are
already in pair form.

```json
{
 "version": 7, "sourceVersion": 6,
 "title": "TEXT_LEVEL_NAME_03_10", "description": "TEXT_LEVEL_TIP_03_10", "authorName": "Noel",
 "backgroundIndex": 3,
 "toolbox": [{"type": 2, "amount": 1}],
 "items": [
  {"type": 31, "handle": 2080382976, "center": [0.0, 0.0], "angle": 0.0, "flags": 0,
   "ropeEnd": [0.0, 0.0], "itemData": 0, "attachments": []},
  {"type": 9, "handle": 603987971, "center": [1.94143558, 1.6383692], "angle": 0.0, "flags": 0,
   "ropeEnd": [0.256237984, -0.74132067], "itemData": 0,
   "attachments": [{"state": 2, "objectIndex": 4, "index": 0}, {"state": 2, "objectIndex": 0, "index": 0}]}
 ],
 "goal": {"type": 2, "itemCount": 1, "itemHandles": [9 ints], "itemHandles2": [9 ints], "timeLimit": 0,
          "height": 0.5, "width": 0.400000006, "angle": 295.100006, "negated": false},
 "rewardId": 21, "tested": true
}
```

Floats: every `<real>` is `float32(float(text))` — the original's `strtod` → `(float)` path (02 §6) — printed
with 9 significant digits; the importer re-parses each emitted token as `float32(strtod(token))` (what cJSON
plus the loader's `(float)` cast compute) and aborts if the bits differ. `tests/test_level_loader.cpp` checks
six shipped levels against bit patterns generated straight from the plists (`tools/gen_level_expectations.py`),
and the G3 level scenes (`tools/sim_conformance.py`) compare the positions/angles of all 116 levels bit for bit
with the emulated original. Handles and `itemData` are plain 32-bit signed ints as in the plist.

**The writer (M6):** `aa::data::writeLevelFile(path, Level)` / `levelToJson` emit the same schema (the sandbox
editor's save path, 05 §8): `version` 7, `sourceVersion` = the `Level`'s version, every key above in this order,
`attachments` with the records the item counts, the goal's nine-slot handle arrays, `rewardId`, `tested`. cJSON
prints the doubles the floats promote to with up to 17 significant digits, so `loadLevelFile` reproduces every
float bit for bit (`tests/test_level_writer.cpp`: all 116 shipped levels write → load field-identical, plus a
synthetic user level); the text is not byte-identical to the importer's (`%.9g`). User levels carry a literal
`title` (LEVEL_SHARE_CONTRAPTION_DEFAULT's text or whatever was saved) and the settings' player name as
`authorName`; no key is added for them.

## 3. Atlas JSON

```json
{"texture": "GameItems.png", "size": [1024, 1024],
 "frames": [{"name": "8ball.png", "x": 100, "y": 621, "w": 36, "h": 36, "rotated": false, "offset": [0, 0],
             "sourceSize": [36, 36]}, ...]}
```

Frames are listed **in the plist's file order**, because the frame index is what the renderer and the physics
templates use (01 §3, 03 §4: `TennisBall.png` = 138). `x, y` are texture pixels, y down, of the trimmed frame
(`w × h`); `st::Frame` is rebuilt as `x0 = x, x1 = x + w, y0 = y, y1 = y + h`. PVR v2 `RGBA8888` becomes an
RGBA PNG, `RGB565` an RGB PNG (explicit 5/6/5 unpacking with bit replication); any other pixel format is an
error, not a skip.

## 4. Loading (`core/data`)

`aa::data::AssetRoot` is the tree's one access interface: it reads the manifest and answers `read(rel)` /
`json(rel)` through a `FileReader` the platform supplies (`std::ifstream` by default, raylib's `LoadFileData`
on Android), `exists(rel)` / `list(prefix)` from the manifest's `files` map (no filesystem access), and still
resolves `path(rel)` for raylib's own texture / sound loaders. The loaders take a `JsonNode`: `loadLevel` →
`aa::sim::Level` (and `writeLevelFile` back, M6), `loadFrameTable` → `aa::sim::FrameTable`, `loadLevelIndex`;
each has a `…File(path)` twin for tools, tests and the sandbox levels in the save directory. The JSON wrapper (`aa/data/json.h`) is a thin
RAII layer over cJSON that reports errors with the JSON path (`levels/…/Playtime.json.items[3].center`).
M5 added `aa/data/ui_loaders.h`: `loadUiContainerFile(path, name)` → `UiContainer` (the `SPRT` sheets with their
sprites by name, the `COMP` composite sets, the `FONT` glyph tables with `leading` / `tracking` — **signed**
`s16` since M5, the importer used to emit them unsigned — `maxAscending` / `maxDescending`), `loadFontsConfigFile`
(`ui/fonts.json`: the outline font and offsets per font), `loadTextTableFile` (`texts/<locale>.json` → id →
string), `loadTipsFile` (`tips.json` — parsed but dead data, 06 §3), `SceneTree` (`ui/scenes/<Scene>.json`: the
raw view dictionaries, `view(name)`; the view classes read the named sub-dictionaries themselves like the
original's `Init(DataDictionary)`). `AssetRoot::exists` also answers for `thumbnails/<level>_350.jpg` (the UI's
`thumb:<level>` sheets) and `sounds/<clip>.mp3` / `music/<clip>.mp3`. Consumers: `aa_ui::ResourceProxy` (sprite sizes scaled by the
UI scale, 06 §1.1), `aa_game::Localization`, `aa_ui::AppState::loadCatalogue`.
