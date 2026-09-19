#include "aa/data/ui_loaders.h"

#include <algorithm>

namespace aa::data {

namespace {

// KA3D FONT fields are `readShort` (signed 16-bit) in the original; an older importer wrote them as u16.
int signed16(int raw) { return raw >= 0x8000 ? raw - 0x10000 : raw; }

UiSpriteSheet loadSheet(const JsonNode& chunk, const std::string& name) {
    UiSpriteSheet sheet;
    sheet.name = name;
    sheet.texture = chunk.getString("texture");
    for (const JsonNode& s : chunk.child("sprites").array()) {
        UiSprite sprite;
        sprite.name = s.getString("name");
        sprite.x = s.getInt("x");
        sprite.y = s.getInt("y");
        sprite.w = s.getInt("w");
        sprite.h = s.getInt("h");
        sprite.pivotX = s.getInt("pivotX");
        sprite.pivotY = s.getInt("pivotY");
        sheet.byName[sprite.name] = static_cast<int>(sheet.sprites.size());
        sheet.sprites.push_back(sprite);
    }
    return sheet;
}

CompoSpriteSet loadCompoSet(const JsonNode& chunk, const std::string& name) {
    CompoSpriteSet set;
    set.name = name;
    for (const JsonNode& c : chunk.child("compoSprites").array()) {
        CompoSprite compo;
        compo.name = c.getString("name");
        for (const JsonNode& p : c.child("parts").array()) {
            CompoPart part;
            part.sprite = p.getString("sprite");
            part.dx = p.getInt("dx");
            part.dy = p.getInt("dy");
            compo.parts.push_back(part);
        }
        set.byName[compo.name] = static_cast<int>(set.compos.size());
        set.compos.push_back(compo);
    }
    return set;
}

BitmapFontData loadFont(const JsonNode& chunk, const std::string& name) {
    BitmapFontData font;
    font.name = name;
    font.texture = chunk.getString("texture");
    font.leading = signed16(chunk.getInt("leading"));
    font.tracking = signed16(chunk.getInt("tracking"));
    for (const JsonNode& g : chunk.child("glyphs").array()) {
        Glyph glyph;
        glyph.code = g.getInt("code");
        glyph.x = g.getInt("x");
        glyph.y = g.getInt("y");
        glyph.w = g.getInt("w");
        glyph.h = g.getInt("h");
        glyph.ascent = g.getInt("ascent");
        // BitmapFont::BitmapFont: maxDescending = max(h - ascent), maxAscending = max(ascent).
        font.maxDescending = std::max(font.maxDescending, glyph.h - glyph.ascent);
        font.maxAscending = std::max(font.maxAscending, glyph.ascent);
        font.glyphs[glyph.code] = glyph;
    }
    return font;
}

}  // namespace

const UiSprite* UiSpriteSheet::find(const std::string& spriteName) const {
    const auto it = byName.find(spriteName);
    return it == byName.end() ? nullptr : &sprites[static_cast<std::size_t>(it->second)];
}

const CompoSprite* CompoSpriteSet::find(const std::string& compoName) const {
    const auto it = byName.find(compoName);
    return it == byName.end() ? nullptr : &compos[static_cast<std::size_t>(it->second)];
}

const Glyph* BitmapFontData::find(int code) const {
    const auto it = glyphs.find(code);
    return it == glyphs.end() ? nullptr : &it->second;
}

UiContainer loadUiContainer(const JsonNode& root, const std::string& name) {
    UiContainer out;
    for (const JsonNode& chunk : root.child("chunks").array()) {
        if (chunk.has("glyphs")) out.fonts.push_back(loadFont(chunk, name));
        else if (chunk.has("compoSprites")) out.compoSets.push_back(loadCompoSet(chunk, name));
        else if (chunk.has("sprites")) out.sheets.push_back(loadSheet(chunk, name));
        else throw JsonError(chunk.path() + ": unknown chunk kind");
    }
    return out;
}

UiContainer loadUiContainerFile(const std::string& path, const std::string& name) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadUiContainer(doc.root(), name);
}

FontsConfig loadFontsConfig(const JsonNode& root) {
    FontsConfig out;
    for (const auto& [font, node] : root.child("Fonts").members()) {
        FontOutline o;
        o.outlineFont = node.getString("OutlineFont");
        o.offsetX = node.getInt("OutlineOffsetX");
        o.offsetY = node.getInt("OutlineOffsetY");
        out[font] = o;
    }
    return out;
}

FontsConfig loadFontsConfigFile(const std::string& path) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadFontsConfig(doc.root());
}

TextTable loadTextTable(const JsonNode& root) {
    TextTable out;
    for (const auto& [id, node] : root.members()) out[id] = node.getString();
    return out;
}

TextTable loadTextTableFile(const std::string& path) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadTextTable(doc.root());
}

std::vector<Tip> loadTips(const JsonNode& root) {
    std::vector<Tip> out;
    for (const JsonNode& t : root.child("Tips").array()) {
        Tip tip;
        tip.image = t.getString("Image", "");
        tip.text = t.getString("Text");
        tip.objectType = t.getInt("objectType", 0);
        tip.platform = t.getInt("Platform", 0);
        out.push_back(tip);
    }
    return out;
}

std::vector<Tip> loadTipsFile(const std::string& path) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadTips(doc.root());
}

SceneTree SceneTree::loadFile(const std::string& path) { return SceneTree(JsonDoc::parseFile(path)); }

}  // namespace aa::data
