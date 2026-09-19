#include "aa/ui/resources.h"

#include "aa/ui/renderer.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace aa::ui {

HAnchor hAnchorFromString(const std::string& s) {
    if (s == "LEFT") return HAnchor::Left;
    if (s == "HCENTER") return HAnchor::Center;
    if (s == "RIGHT") return HAnchor::Right;
    if (s == "HPIVOT") return HAnchor::Pivot;
    return HAnchor::None;
}

VAnchor vAnchorFromString(const std::string& s) {
    if (s == "TOP") return VAnchor::Top;
    if (s == "VCENTER") return VAnchor::Center;
    if (s == "BOTTOM") return VAnchor::Bottom;
    if (s == "VPIVOT") return VAnchor::Pivot;
    if (s == "BASELINE") return VAnchor::Baseline;
    return VAnchor::None;
}

FontAnchorH fontAnchorHFromString(const std::string& s) {
    if (s == "HCENTER") return FontAnchorH::Center;
    if (s == "RIGHT") return FontAnchorH::Right;
    return FontAnchorH::Left;
}

FontAnchorV fontAnchorVFromString(const std::string& s) {
    if (s == "VCENTER") return FontAnchorV::Center;
    if (s == "BOTTOM") return FontAnchorV::Bottom;
    return FontAnchorV::Top;
}

std::vector<int> decodeUtf8(const std::string& s) {
    std::vector<int> out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int code = 0;
        std::size_t extra = 0;
        if (c < 0x80) code = c;
        else if ((c & 0xE0) == 0xC0) { code = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { code = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { code = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (i + extra >= s.size() + (extra == 0 ? 1 : 0) && extra > 0 && i + extra > s.size() - 1 + 1) break;
        bool ok = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            if (i + k >= s.size()) { ok = false; break; }
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            code = (code << 6) | (cc & 0x3F);
        }
        if (!ok) break;
        out.push_back(code);
        i += extra + 1;
    }
    return out;
}

float BitmapFont::stringWidth(const std::string& utf8) const {
    const std::vector<int> codes = decodeUtf8(utf8);
    if (codes.empty()) return 0.0f;
    int width = 0;
    for (int code : codes) {
        if (const aa::data::Glyph* g = data_->find(code)) width += g->w;
    }
    // getStringWidth: tracking × (count − 1) + the glyph widths.
    return static_cast<float>(data_->tracking * (static_cast<int>(codes.size()) - 1) + width) * scale_;
}

void BitmapFont::drawString(Renderer& renderer, const std::string& utf8, float x, float y, FontAnchorV v, FontAnchorH h) const {
    const std::vector<int> codes = decodeUtf8(utf8);
    if (codes.empty()) return;
    switch (v) {
    case FontAnchorV::Top: y += maxAscending(); break;
    case FontAnchorV::Center: y += static_cast<float>(data_->maxAscending - ((data_->maxAscending + data_->maxDescending) >> 1)) * scale_; break;
    case FontAnchorV::Bottom: y -= maxDescending(); break;
    }
    if (h == FontAnchorH::Center) {
        // drawString halves the integer width (>> 1) before the subtraction.
        const int w = static_cast<int>(stringWidth(utf8) / scale_ + 0.5f);
        x -= static_cast<float>(w >> 1) * scale_;
    } else if (h == FontAnchorH::Right) {
        x -= stringWidth(utf8);
    }
    for (int code : codes) {
        const aa::data::Glyph* g = data_->find(code);
        if (!g) continue;
        // Sprite::draw(x, y, anchor pivot): the glyph's pivot is (0, ascent) — the baseline sits at y.
        SpriteRef ref;
        static thread_local aa::data::UiSprite glyphSprite;   // a sprite view of the glyph rectangle
        glyphSprite.name.clear();
        glyphSprite.x = g->x;
        glyphSprite.y = g->y;
        glyphSprite.w = g->w;
        glyphSprite.h = g->h;
        glyphSprite.pivotX = 0;
        glyphSprite.pivotY = g->ascent;
        static thread_local aa::data::UiSpriteSheet glyphSheet;
        glyphSheet.name = data_->name;
        glyphSheet.texture = data_->texture;
        ref.sheet = &glyphSheet;
        ref.sprite = &glyphSprite;
        renderer.drawSprite(ref, x, y - static_cast<float>(g->ascent) * scale_, static_cast<float>(g->w) * scale_,
                            static_cast<float>(g->h) * scale_);
        x += static_cast<float>(g->w + data_->tracking) * scale_;
    }
}

void ResourceProxy::load(const aa::data::AssetRoot& root, float uiScale) {
    uiScale_ = uiScale;
    root_ = &root;
    const std::string profile = "ui/" + root.manifest().profile + "/";
    profileDir_ = root.path(profile.substr(0, profile.size() - 1));   // the renderer's LoadTexture base
    sheets_.clear();
    compoSets_.clear();
    fontData_.clear();
    fonts_.clear();
    spriteIndex_.clear();
    const std::string json = ".json";
    for (const std::string& file : root.list(profile)) {   // sorted; the profile has no subdirectories
        if (file.size() <= json.size() || file.compare(file.size() - json.size(), json.size(), json) != 0) continue;
        const std::string name = file.substr(profile.size(), file.size() - profile.size() - json.size());
        aa::data::UiContainer c = aa::data::loadUiContainer(root.json(file).root(), name);
        for (auto& s : c.sheets) sheets_.push_back(std::move(s));
        for (auto& s : c.compoSets) compoSets_.push_back(std::move(s));
        for (auto& f : c.fonts) fontData_.push_back(std::move(f));
    }
    for (std::size_t i = 0; i < sheets_.size(); ++i) {
        for (std::size_t j = 0; j < sheets_[i].sprites.size(); ++j) {
            spriteIndex_.emplace(sheets_[i].sprites[j].name, std::make_pair(static_cast<int>(i), static_cast<int>(j)));
        }
    }
    for (const aa::data::BitmapFontData& f : fontData_) fonts_[f.name] = std::make_unique<BitmapFont>(&f, uiScale_);
    fontsConfig_ = aa::data::loadFontsConfig(root.json("ui/fonts.json").root());
    // The thumbnail size is whatever LevelThumbnails_<size> the importer copied (the iOS bundle's 350, the
    // Android package's 175): read it off the first thumbnails/<level>_<size>.jpg in the manifest.
    for (const auto& [file, sha1] : root.manifest().sha1) {
        if (file.compare(0, 11, "thumbnails/") != 0) continue;
        const std::size_t us = file.rfind('_');
        const std::size_t dot = file.rfind('.');
        if (us == std::string::npos || dot == std::string::npos || dot <= us + 1) continue;
        const int px = std::atoi(file.substr(us + 1, dot - us - 1).c_str());
        if (px > 0) thumbnailPx_ = px;
        break;
    }
}

void ResourceProxy::setUiScale(float uiScale) {
    uiScale_ = uiScale;
    // Mutate each BitmapFont's scale in place rather than replacing it — a caller can hold a `const
    // BitmapFont*` fetched earlier this same frame (font()'s callers never cache it across frames, but
    // nothing should have to rely on that), and this is simpler regardless.
    for (auto& [name, font] : fonts_) font->setScale(uiScale_);
}

std::string ResourceProxy::thumbnailPath(const std::string& name) const {
    // LevelThumbnails_<size>/<level>_<size>.jpg, copied as thumbnails/<level>_<size>.jpg (docs/12 §1);
    // "thumb:@<path>" names a file as it is (a user level's thumbnail in the save directory).
    if (isUserThumbnail(name)) return name.substr(7);
    return root_->path(thumbnailFile(name));
}

std::string ResourceProxy::thumbnailFile(const std::string& name) const {
    return "thumbnails/" + name.substr(6) + "_" + std::to_string(thumbnailPx_) + ".jpg";
}

bool ResourceProxy::thumbnailExists(const std::string& name) const {
    // A user level's thumbnail lives in the save directory, outside the manifest.
    if (isUserThumbnail(name)) return std::filesystem::exists(thumbnailPath(name));
    return root_->exists(thumbnailFile(name));
}

SpriteRef ResourceProxy::sprite(const std::string& name) const {
    SpriteRef ref;
    if (isThumbnail(name)) {
        auto it = thumbSheets_.find(name);
        if (it == thumbSheets_.end()) {
            if (!thumbnailExists(name)) return ref;
            auto sheet = std::make_unique<aa::data::UiSpriteSheet>();
            sheet->name = name;
            sheet->texture = thumbnailPath(name);
            aa::data::UiSprite s;
            s.name = name;
            s.w = thumbnailPx_;
            s.h = thumbnailPx_;
            s.pivotX = thumbnailPx_ / 2;
            s.pivotY = thumbnailPx_ / 2;
            sheet->byName[name] = 0;
            sheet->sprites.push_back(s);
            it = thumbSheets_.emplace(name, std::move(sheet)).first;
        }
        ref.sheet = it->second.get();
        ref.sprite = &it->second->sprites[0];
        return ref;
    }
    const auto it = spriteIndex_.find(name);
    if (it == spriteIndex_.end()) return ref;
    ref.sheet = &sheets_[static_cast<std::size_t>(it->second.first)];
    ref.sprite = &ref.sheet->sprites[static_cast<std::size_t>(it->second.second)];
    return ref;
}

const aa::data::CompoSprite* ResourceProxy::compo(const std::string& name) const {
    for (const aa::data::CompoSpriteSet& set : compoSets_) {
        if (const aa::data::CompoSprite* c = set.find(name)) return c;
    }
    return nullptr;
}

const BitmapFont* ResourceProxy::font(const std::string& name) const {
    const auto it = fonts_.find(name);
    return it == fonts_.end() ? nullptr : it->second.get();
}

const aa::data::FontOutline* ResourceProxy::outline(const std::string& fontName) const {
    const auto it = fontsConfig_.find(fontName);
    return it == fontsConfig_.end() ? nullptr : &it->second;
}

Rect ResourceProxy::compoBounds(const aa::data::CompoSprite& c) const {
    // game::CompoSprite: parts are drawn with their pivot at (dx, dy) relative to the composite's origin;
    // the composite's size is the union of the part rectangles.
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    bool first = true;
    for (const aa::data::CompoPart& p : c.parts) {
        const SpriteRef s = sprite(p.sprite);
        if (!s.valid()) continue;
        const float x0 = static_cast<float>(p.dx - s.sprite->pivotX);
        const float y0 = static_cast<float>(p.dy - s.sprite->pivotY);
        const float x1 = x0 + static_cast<float>(s.sprite->w);
        const float y1 = y0 + static_cast<float>(s.sprite->h);
        if (first) {
            minX = x0; minY = y0; maxX = x1; maxY = y1;
            first = false;
        } else {
            minX = std::min(minX, x0); minY = std::min(minY, y0);
            maxX = std::max(maxX, x1); maxY = std::max(maxY, y1);
        }
    }
    return Rect{minX, minY, maxX - minX, maxY - minY};
}

Size ResourceProxy::imageSize(const std::string& name) const {
    if (const aa::data::CompoSprite* c = compo(name)) {
        const Rect b = compoBounds(*c);
        return Size{b.w * uiScale_, b.h * uiScale_};
    }
    const SpriteRef s = sprite(name);
    if (!s.valid()) return Size{};
    return Size{static_cast<float>(s.sprite->w) * uiScale_, static_cast<float>(s.sprite->h) * uiScale_};
}

Point ResourceProxy::imagePivot(const std::string& name) const {
    if (const aa::data::CompoSprite* c = compo(name)) {
        const Rect b = compoBounds(*c);
        return Point{-b.x * uiScale_, -b.y * uiScale_};
    }
    const SpriteRef s = sprite(name);
    if (!s.valid()) return Point{};
    return Point{static_cast<float>(s.sprite->pivotX) * uiScale_, static_cast<float>(s.sprite->pivotY) * uiScale_};
}

}  // namespace aa::ui
