// Sprite pages: one raylib texture plus the frame table of its atlas JSON (docs/12-asset-tree.md §3).
#pragma once

#include "aa/data/asset_root.h"
#include "aa/sim/frame_table.h"

#include <raylib.h>

#include <array>
#include <string>

namespace aa::platform {

struct Atlas {
    Texture2D texture{};
    aa::sim::FrameTable frames;
    int width = 0;    // texture size in px (the frame rectangles are in these units)
    int height = 0;

    bool loaded() const { return texture.id != 0; }
    const aa::sim::Frame& frame(int index) const { return frames.at(index); }
};

// The atlases the world renderer draws from (docs/11 §3): GameItems, GameItems2 (goal markers), the four
// LocationBackground0N pages, LocationForegrounds (floor overlays) and UIElements (the toolbox strip, §7).
struct AtlasSet {
    static constexpr int kBackgroundCount = 4;

    Atlas gameItems;
    Atlas gameItems2;
    Atlas foregrounds;
    Atlas uiElements;
    std::array<Atlas, kBackgroundCount> backgrounds;

    // Loads every page through aa_data + LoadTexture; throws on a missing file. Needs an open window.
    void load(const aa::data::AssetRoot& root);
    void unload();
};

// LoadTexture + frame table; `nearest` selects GL_NEAREST (the original's setting for backgrounds),
// otherwise GL_LINEAR.
Atlas loadAtlas(const aa::data::AssetRoot& root, const std::string& name, bool nearest);

}  // namespace aa::platform
