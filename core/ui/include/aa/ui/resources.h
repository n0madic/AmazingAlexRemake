// UI::ResourceProxy + game::BitmapFont (docs/06 §2): the loaded sprite sheets, composites and fonts of one
// profile, looked up by name; sprite sizes are reported in screen px (sprite px × uiScale). Textures are the
// renderer's business (by sheet name); nothing here needs a window.
#pragma once

#include "aa/data/asset_root.h"
#include "aa/data/ui_loaders.h"
#include "aa/ui/types.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

class Renderer;

// A sprite reference: the sheet it lives in and its rectangle.
struct SpriteRef {
    const aa::data::UiSpriteSheet* sheet = nullptr;
    const aa::data::UiSprite* sprite = nullptr;
    bool valid() const { return sprite != nullptr; }
};

// game::BitmapFont over a FONT chunk [verified: drawString / getStringWidth]: glyphs advance by their
// width plus the tracking; the string width is the sum of the widths + tracking × (count − 1); the draw
// origin is the baseline, shifted by the max ascent (TOP) / descent (BOTTOM) / their half difference
// (VCENTER); the horizontal anchor subtracts the string width (RIGHT) or its half (HCENTER).
class BitmapFont {
public:
    BitmapFont(const aa::data::BitmapFontData* data, float scale) : data_(data), scale_(scale) {}
    const aa::data::BitmapFontData& data() const { return *data_; }
    float scale() const { return scale_; }
    // A live resize's uiScale change (ResourceProxy::setUiScale): mutated in place rather than rebuilding
    // the BitmapFont, so a `const BitmapFont*` fetched earlier this frame is never left dangling.
    void setScale(float scale) { scale_ = scale; }
    float leading() const { return static_cast<float>(data_->leading) * scale_; }
    float tracking() const { return static_cast<float>(data_->tracking) * scale_; }
    float maxAscending() const { return static_cast<float>(data_->maxAscending) * scale_; }
    float maxDescending() const { return static_cast<float>(data_->maxDescending) * scale_; }
    // getStringWidth over the UTF-8 string (unknown glyphs contribute nothing but the tracking).
    float stringWidth(const std::string& utf8) const;
    bool isCharacterSupported(int code) const { return data_->find(code) != nullptr; }
    // drawString(x, y, anchor): glyph quads through the renderer at the current draw state.
    void drawString(Renderer& renderer, const std::string& utf8, float x, float y, FontAnchorV v, FontAnchorH h) const;

private:
    const aa::data::BitmapFontData* data_;
    float scale_;
};

// Decodes UTF-8 into code points (the original converts to UTF-16 for the glyph lookup).
std::vector<int> decodeUtf8(const std::string& s);

class ResourceProxy {
public:
    ResourceProxy() = default;
    // Loads every container of `ui/<profile>/` and `ui/fonts.json`; sprite sizes scaled by `uiScale`.
    void load(const aa::data::AssetRoot& root, float uiScale);
    // A live resize's uiScale change: cheap, in-memory only (the loaded sprite sheets / font data are
    // untouched — only the scale multiplier imageSize()/imagePivot() and the fonts' own scale_ apply).
    void setUiScale(float uiScale);
    float uiScale() const { return uiScale_; }
    const std::string& profileDir() const { return profileDir_; }

    // ResourceProxy::GetSprite / IsCompoSprite / GetCompoSprite / GetFont.
    SpriteRef sprite(const std::string& name) const;
    const aa::data::CompoSprite* compo(const std::string& name) const;
    bool isCompo(const std::string& name) const { return compo(name) != nullptr; }
    const BitmapFont* font(const std::string& name) const;
    const aa::data::FontOutline* outline(const std::string& fontName) const;
    // The size an image name draws at (sprite or composite), in screen px; pivot likewise.
    Size imageSize(const std::string& name) const;
    Point imagePivot(const std::string& name) const;
    // The bounding box of a composite in sprite px: parts placed at (dx, dy) from the origin.
    Rect compoBounds(const aa::data::CompoSprite& compo) const;

    const std::vector<aa::data::UiSpriteSheet>& sheets() const { return sheets_; }

    // Level thumbnails (thumbnails/<level>.jpg) are exposed as one-sprite sheets named "thumb:<level>";
    // the renderer loads their texture from `thumbnailPath(name)` on first use.
    static std::string thumbnailName(const std::string& level) { return "thumb:" + level; }
    // A thumbnail file outside the asset tree (a user level's, ResourceProxy::LoadSpriteFromDocs).
    static std::string thumbnailNameForFile(const std::string& path) { return "thumb:@" + path; }
    static bool isThumbnail(const std::string& name) { return name.rfind("thumb:", 0) == 0; }
    static bool isUserThumbnail(const std::string& name) { return name.rfind("thumb:@", 0) == 0; }
    std::string thumbnailPath(const std::string& name) const;
    bool thumbnailExists(const std::string& name) const;
    // Forgets a thumbnail sheet (a re-saved user thumbnail is re-read on the next use; the renderer drops
    // its texture through purgeThumbnails).
    void dropThumbnail(const std::string& name) { thumbSheets_.erase(name); }
    void setThumbnailSize(int px) { thumbnailPx_ = px; }
    int thumbnailSize() const { return thumbnailPx_; }

private:
    std::string thumbnailFile(const std::string& name) const;   // thumbnails/<level>_<px>.jpg

    const aa::data::AssetRoot* root_ = nullptr;
    std::string profileDir_;
    int thumbnailPx_ = 350;
    mutable std::map<std::string, std::unique_ptr<aa::data::UiSpriteSheet>> thumbSheets_;
    float uiScale_ = 0.5f;
    std::vector<aa::data::UiSpriteSheet> sheets_;
    std::vector<aa::data::CompoSpriteSet> compoSets_;
    std::vector<aa::data::BitmapFontData> fontData_;
    std::map<std::string, std::unique_ptr<BitmapFont>> fonts_;
    aa::data::FontsConfig fontsConfig_;
    std::map<std::string, std::pair<int, int>> spriteIndex_;   // name → (sheet, sprite)
};

}  // namespace aa::ui
