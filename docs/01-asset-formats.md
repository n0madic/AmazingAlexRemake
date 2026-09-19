# Asset formats

Paths are relative to the bundle root: iOS `Payload/Amazing Alex HD.app/`, Android `assets/Data/`.

## 1. Asset tree

```
Common/
  Game/GameItems.{plist,pvr}          item sprite atlas (150 frames, 1024×1024 RGBA8888)
  Game/GameItems2.{plist,pvr}         goal markers goal_arrow/circle/cross (512×1024)
  Game/LocationBackgrounds/
      LocationBackground00..03.{plist,pvr}   chapter backgrounds (1024×512, RGB565)
      LocationForegrounds.{plist,pvr}        foreground overlays (1024×1024 RGBA)
  XML/*Scene.xml, Dialogs.xml         UI scene layouts (plist, encrypted)
  Tips.plist                          loading-screen tips (binary plist, encrypted)
  TEXTS_BASIC.dat, TEXTS_LANGUAGE_SELECTION.dat   localisation (KA3D TEXT, 5 languages)
  localization_android                same format, en_EN only (Android strings)
  EmailLevelShareBody*_{en,fr,it,de,es}.html   "share level" e-mail templates
Levels/{00_Classroom,01_Backyard,02_Room,03_Treehouse}/
  0_Location.plist                    level order + chapter name (encrypted)
  <Level>.plist                       level (encrypted)                     → 02-level-format.md
LevelThumbnails_{65,98,130,175,350}/<Level>_<size>.jpg   thumbnails for the level-select screen
Sounds_high/*.mp3 (68 files), Music_high/{Music,Theme}.mp3   (Android: Sounds_low, Music_low)
<profile>/  = 2048X1536 | 1024X768 | 800X480 | 480X320
  *.png + *.dat                       KA3D UI atlases (SPRT/COMP/FONT)
  UIElements.{plist,pvr}              TexturePacker atlas of buttons and toolbox icons
  XML/loadlist.xml                    asset load list for the profile (plist, encrypted)
  XML/Fonts.xml                       font/outline definitions (plist, encrypted)
```

The asset profile is picked by `selectAssetProfile(w, h)` from the screen resolution **[verified: function name]**.
In-game (non-UI) assets are shared by all profiles and scaled by the camera.

## 2. Encryption **[verified]**

Every text asset with extension `.plist` or `.xml` (levels, `0_Location.plist`, the `GameItems*.plist`,
`LocationBackground*.plist` and `UIElements.plist` atlases, `Tips.plist`, all XML under `Common/XML` and
`<profile>/XML`) is encrypted:

| Parameter | Value |
|-----------|-------|
| Algorithm | AES-256, **CBC** mode, IV = 16 zero bytes (`AES::Decrypt(..., BlockMode)` + `lang::AESUtil`) |
| Key | `m1sOWGxsS23AoseNsM5Lsp9S21YtMxks` (32 ASCII bytes) — assembled byte by byte in `GameApp::GameApp()` into `st::GameParams::CryptingKey` (defeats `strings`) |
| Padding | PKCS#7 |
| Plaintext tail | the iOS packer appends `"\n\0"`, the Android packer `"\0"` before padding (for binary plists the `\n` must be dropped or the trailer will not parse) |
| Not encrypted | `.dat` (KA3D), `.pvr`, `.png`, `.jpg`, `.mp3`, `.html` |

The same key encrypts player save files (`st::SerializationUtils::Save/Load`, `Containers::DataDictionary::Save/Load`)
and levels uploaded to the server (`UploadOperation::UploadLevel`). A second key, `st::GameParams::UploadKey`
(19 bytes, also assembled in the constructor), is only used by the server API — not needed.

Script: `tools/decrypt_assets.py <bundle> <out>` — decrypts and converts binary plists to XML.
Sanity check: the first 64 bytes of every ciphertext are identical because every plaintext starts with
`<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "` (CBC with a fixed IV).

## 3. TexturePacker atlases (`*.plist` + `*.pvr`) **[verified]**

cocos2d "format 2" TexturePacker output:

```xml
<key>frames</key><dict>
  <key>TennisBall.png</key><dict>
    <key>frame</key><string>{{x,y},{w,h}}</string>      <!-- texture pixels, y down -->
    <key>offset</key><string>{0,0}</string>
    <key>rotated</key><false/>
    <key>sourceColorRect</key><string>{{0,0},{w,h}}</string>
    <key>sourceSize</key><string>{w,h}</string>
  </dict> ...
</dict>
<key>metadata</key><dict> format=2, size={1024,1024}, textureFileName=GameItems.pvr </dict>
```

`st::SpritePage::Load` iterates the keys **in file order** (`DataDictionary::GetKeys` preserves insertion
order) and appends them to a `CountedArray<st::Frame>`; **frame index = position in the file**
(`GameItems.plist` keys are alphabetically sorted: `8ball.png` = 0 … `ZipLineTrolley.png` = 149).
The index is hard-coded in the renderer and in the physics template table, so an importer must keep
this order (or use the type → frame-name table in 03-game-items.md).

`st::Frame` is 20 bytes: `+4 = y+h`, `+8 = y`, `+0xC = x`, `+0x10 = x+w` (floats, pixels); `+0` unused here.

`rotated` is `false` and `offset` is `{0,0}` for every frame. Frames carry no pivot: items are drawn
centred (`AddQuadCenteredAt`) with half-size `(w−2)/2` px (1 px trimmed on each side).

The full `GameItems.plist` frame list with indices is in 03-game-items.md.

## 4. PVR textures **[verified]**

PVR **v2** header (52 bytes, little-endian) followed by raw pixels, no mip levels:

| Offset | Field | GameItems.pvr | LocationBackground00.pvr |
|---:|-------|---|---|
| 0 | headerLength | 0x34 | 0x34 |
| 4 | height | 1024 | 512 |
| 8 | width | 1024 | 1024 |
| 12 | numMipmaps | 0 | 0 |
| 16 | flags | `0x8012` → pixel format **0x12 = OGL_RGBA_8888**, bit 0x8000 = has alpha | `0x13 = OGL_RGB_565` |
| 20 | dataLength | 4 194 304 | 1 048 576 |
| 24 | bpp | 32 | 16 |
| 28..40 | R/G/B/A masks | ff, ff00, ff0000, ff000000 | f800, 07e0, 001f, 0 |
| 44 | `"PVR!"` | | |
| 48 | numSurfaces | 1 | 1 |

PVRTC compression is **not** used — every texture is raw, which is why the Android and iOS files are identical.
On Android the files are zip-wrapped (`*.pvr.zip`, single entry). Converting to PNG is trivial
(`PIL.Image.frombytes("RGBA", (w, h), data)`; RGB565 needs 16-bit unpacking).
Loader: `st::TextureUtils::LoadCompressedTexture` (handles uncompressed data despite the name).

## 5. KA3D container (`*.dat`) **[verified: TEXT, SPRT, COMP, FONT — parser `tools/ka3d_dat.py` round-trips all 75 UI files]**

ka-core engine format. All integers are **big-endian**. Strings are `u16 length + UTF-8` ("pstr").

```
"KA3D" u32 size            size = length of the rest of the file
<TAG>  u32 size u16 version(=1|2)   TAG ∈ {TEXT, SPRT, COMP, FONT}
```

### TEXT (localisation) — `Common/TEXTS_BASIC.dat`
```
LDAT u32 size: u16 n, n × pstr  locales ("en_EN","fr_FR","it_IT","de_DE","es_ES")
LIDS u32 size: u16 n, n × pstr  string ids (417)
TXGP u32 size: n × pstr         texts of one locale in LIDS order (one chunk per locale)
```
Parser: `tools/ka3d_text.py`. `*word*` inside texts is emphasis markup (bold/colour) used by level tips.
Ids: `TEXT_LEVEL_NAME_<chapter>_<n>`, `TEXT_LEVEL_TIP_<chapter>_<n>`, `CHAPTER_NAME_CHAPTER1..4`,
`ITEM_*`, `SK_*` (soft keys), `TEXT_CREDITS_*`, `TEXT_TUTORIAL_*`, …

### SPRT (UI atlas) — `<profile>/MENU_*.dat`, `BOOKS_*.dat`, `LOGO_*.dat`, `POP_UP_*.dat`, `SPLASH_*.dat`, `BACKGROUND_*.dat`
```
u16 version(1), pstr pngName, u16 count,
count × { pstr name, u16 x, u16 y, u16 w, u16 h, u16 pivotX, u16 pivotY }
```
(example: `BUTTON_SMALL_SOUND_OFF` → x=762 y=102 w=101 h=88 pivot=(49,44)). Texture = sibling PNG.

### COMP (composite sprites) — `MENU_COMPOSPRITES.dat`, `BOOKS_COMPOSPRITES.dat`
```
u16 version(1|2), u16 count,
count × { pstr compoName,
          u16 parts,  parts × { pstr spriteName, s16 dx, s16 dy }      // CompoSprite::addSprite(index, name, dx, dy, sprite)
          [version 2:] u16 extra, extra × { pstr name, s16 a, s16 b } } // read and discarded by the loader; 0 in all files
```
(`game::CompoSpriteSet::CompoSpriteSet` loader.) Sprites are looked up by name in the already loaded SPRT sets
(exception "Sprite … not loaded" otherwise), so the SPRT files of a scene must be loaded first. Example:
`BOOK_BACKYARD_DE` = `BOOK_BACKYARD` at (0,0) + `TEXT_BACKYARD_DE` at (1,2).

### FONT (bitmap font) — `FONT_1..4.dat`, `FONT_3_OUTLINES.dat`, `NUMBERFONT.dat`
```
u16 version(1), pstr pngName,
u16 leading   (BitmapFont::getLeading — line height, e.g. 64 for FONT_1, 100 for FONT_3, 228 for NUMBERFONT),
u16 tracking  (BitmapFont::getTracking — extra horizontal advance between glyphs, 1 / 46),
u16 glyphCount (145 for FONT_1),
glyphCount × { u16 charCode, s16 x, s16 y, s16 w, s16 h, s16 ascent }
```
(`game::BitmapFont::BitmapFont` loader.) Each glyph becomes a sprite `createSprite(str(charCode), x, y, w, h,
pivotX = 0, pivotY = ascent)`; the font's `maxAscending = max(ascent)`, `maxDescending = max(h − ascent)`, so
`ascent` is the baseline offset from the glyph's top. Glyphs are not in code order in the file (a hashtable is
built); `drawString` advances x by `Sprite::getWidth() + tracking` per glyph [verified]. There is no kerning table.
`Fonts.xml` links `FONT_3` → `FONT_3_OUTLINES` with `OutlineOffsetX/Y = 10` (outline drawn from a separate atlas).
`NUMBERFONT.png/.dat` — digits for counters.

## 6. Audio **[verified: file list]**

68 MP3s in `Sounds_high` (`BookImpact`, `BalloonPop`, `HelicopterLoop`, `RCTruckStart`, `PiggyBankBreak1..`,
`ScissorsCut`, `SlingshotUnstretch`, `BumperImpact`, …) plus `Music.mp3` (in-game) and `Theme.mp3` (menu).
Event → clip mapping lives in `st::AudioSystemUtils::audioClips` / `st::SoundRenderer::Render`
(`st::AudioId::Enum`); iOS uses the high-quality set, Android the `_low` variants
(`AudioSystemUtils::IsHighQualityAudio`). Impact sounds are chosen in `*Utils::HandleCollisionSounds`
from the item type and the impact velocity (only when the normal relative velocity exceeds 0.5 m/s).

## 7. Level thumbnails

`LevelThumbnails_<size>/<LevelFile>_<size>.jpg`, size ∈ {65, 98, 130, 175, 350} px (width).
User-made levels get thumbnails rendered into an FBO (`st::ScreenshotUtils::CreateLevelThumbnail`).

## 8. UI layouts (`Common/XML/*Scene.xml`) **[verified: format]**

Plist trees of engine `UI::View`s: key = view name, containing `Relative {X,Y,W,H}` in percent of the
parent, `Anchor {H,V {Self, View{Target}}}`, `Font`, `Text*`, `Image`, and child views. Scenes: Splash,
MainMenu, ChapterSelection, LevelSelection, LevelLoading, Game, Sandbox, MyContraptions,
WorldOfContraptions, FeaturedLevels, ChapterComplete, ChapterComplete3Stars, Comic, Credits, Dialogs.
See 06-ui-localization.md.
