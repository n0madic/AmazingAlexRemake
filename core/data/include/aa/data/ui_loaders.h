// Loaders for the UI side of the imported asset tree (docs/12-asset-tree.md §1, §4): the KA3D SPRT / COMP /
// FONT containers of one UI profile (`ui/<profile>/<name>.json`), the scene view trees
// (`ui/scenes/<Name>.json`), `ui/fonts.json`, the text bundles (`texts/<locale>.json`) and `tips.json`.
// Plain structs, no raylib: the view layout and the text measuring run on these without a window.
#pragma once

#include "aa/data/json.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace aa::data {

// game::Sprite: one rectangle of a sheet plus its pivot (in sprite pixels from the top-left corner).
struct UiSprite {
    std::string name;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int pivotX = 0;
    int pivotY = 0;
};

// game::SpriteSheet (a SPRT chunk): the sprites of one PNG, looked up by name (UI::ResourceProxy::GetSprite).
struct UiSpriteSheet {
    std::string name;      // container name, e.g. "MENU_MENU_INGAME"
    std::string texture;   // PNG file name next to the JSON
    std::vector<UiSprite> sprites;
    std::unordered_map<std::string, int> byName;

    const UiSprite* find(const std::string& spriteName) const;
};

// game::CompoSprite (a COMP chunk entry): parts drawn in order, each a sprite of some sheet offset by (dx, dy)
// from the composite's origin. The books of the chapter screen are `BOOK_<CHAPTER>_<LANG>` composites of the
// book plate and its pre-rendered localised title (docs/06 §3).
struct CompoPart {
    std::string sprite;
    int dx = 0;
    int dy = 0;
};

struct CompoSprite {
    std::string name;
    std::vector<CompoPart> parts;
};

struct CompoSpriteSet {
    std::string name;
    std::vector<CompoSprite> compos;
    std::unordered_map<std::string, int> byName;

    const CompoSprite* find(const std::string& compoName) const;
};

// game::BitmapFont (a FONT chunk): glyph rectangles by UTF-16 code with the KA3D metrics. `ascent` is the
// glyph's pivotY (the baseline is `ascent` px below the glyph's top); the font's `maxAscending` /
// `maxDescending` are the maxima over the glyphs, as the constructor computes them [verified:
// BitmapFont::BitmapFont]. `leading` and `tracking` are signed 16-bit in the original (readShort); the
// outline fonts carry a negative tracking.
struct Glyph {
    int code = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int ascent = 0;
};

struct BitmapFontData {
    std::string name;
    std::string texture;
    int leading = 0;
    int tracking = 0;
    int maxAscending = 0;
    int maxDescending = 0;
    std::map<int, Glyph> glyphs;

    const Glyph* find(int code) const;
};

// One `ui/<profile>/<name>.json` container: the chunk kinds the importer emits (a file holds one kind).
struct UiContainer {
    std::vector<UiSpriteSheet> sheets;
    std::vector<CompoSpriteSet> compoSets;
    std::vector<BitmapFontData> fonts;
};

UiContainer loadUiContainer(const JsonNode& root, const std::string& name);
UiContainer loadUiContainerFile(const std::string& path, const std::string& name);

// ui/fonts.json: the outline font paired with FONT_3 / FONT_4 and the outline offsets (docs/06 §2).
struct FontOutline {
    std::string outlineFont;
    int offsetX = 0;
    int offsetY = 0;
};
using FontsConfig = std::map<std::string, FontOutline>;
FontsConfig loadFontsConfig(const JsonNode& root);
FontsConfig loadFontsConfigFile(const std::string& path);

// texts/<locale>.json: text id → localised string (docs/06 §3).
using TextTable = std::unordered_map<std::string, std::string>;
TextTable loadTextTable(const JsonNode& root);
TextTable loadTextTableFile(const std::string& path);

// tips.json: the loading-screen tips (docs/06 §3). `objectType` 0 = generic, otherwise the item type the
// tip is about; `platform` 1 = iOS-only wording (multi-touch) — skipped on desktop.
struct Tip {
    std::string image;
    std::string text;
    int objectType = 0;
    int platform = 0;
};
std::vector<Tip> loadTips(const JsonNode& root);
std::vector<Tip> loadTipsFile(const std::string& path);

// ui/scenes/<Name>.json: the view dictionaries stay JSON (the view classes read their own named
// sub-dictionaries as the original's `Init(DataDictionary)` does — docs/06 §1). The document owns the tree.
class SceneTree {
public:
    SceneTree() = default;
    explicit SceneTree(JsonDoc doc) : doc_(std::move(doc)) {}
    static SceneTree loadFile(const std::string& path);

    JsonNode root() const { return doc_.root(); }
    // The dictionary of a top-level view (e.g. "GameView"), a null node when absent.
    JsonNode view(const std::string& name) const { return doc_.root().optional(name.c_str()); }

private:
    JsonDoc doc_;
};

}  // namespace aa::data
