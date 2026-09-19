#include "aa/platform/atlas.h"

#include "aa/data/frame_table_loader.h"

#include <stdexcept>

namespace aa::platform {

Atlas loadAtlas(const aa::data::AssetRoot& root, const std::string& name, bool nearest) {
    Atlas atlas;
    aa::data::AtlasInfo info;
    atlas.frames = aa::data::loadFrameTable(root.json("atlases/" + name + ".json").root(), &info);
    atlas.texture = LoadTexture(root.atlasPngPath(name).c_str());
    if (atlas.texture.id == 0) throw std::runtime_error("cannot load atlas texture " + root.atlasPngPath(name));
    atlas.width = atlas.texture.width;
    atlas.height = atlas.texture.height;
    if (info.width != 0 && (info.width != atlas.width || info.height != atlas.height)) {
        throw std::runtime_error("atlas " + name + ": JSON size does not match the PNG");
    }
    // SpritePage::Load: LocationBackground textures use GL_NEAREST, every other page GL_LINEAR.
    SetTextureFilter(atlas.texture, nearest ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(atlas.texture, TEXTURE_WRAP_CLAMP);
    return atlas;
}

void AtlasSet::load(const aa::data::AssetRoot& root) {
    gameItems = loadAtlas(root, "GameItems", false);
    gameItems2 = loadAtlas(root, "GameItems2", false);
    foregrounds = loadAtlas(root, "LocationForegrounds", false);
    uiElements = loadAtlas(root, "UIElements", false);
    for (int i = 0; i < kBackgroundCount; ++i) {
        backgrounds[static_cast<std::size_t>(i)] = loadAtlas(root, "LocationBackground0" + std::to_string(i), true);
    }
}

void AtlasSet::unload() {
    for (Atlas* a : {&gameItems, &gameItems2, &foregrounds, &uiElements, &backgrounds[0], &backgrounds[1],
                     &backgrounds[2], &backgrounds[3]}) {
        if (a->loaded()) UnloadTexture(a->texture);
        a->texture = Texture2D{};
    }
}

}  // namespace aa::platform
