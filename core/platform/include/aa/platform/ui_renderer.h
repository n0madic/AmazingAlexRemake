// The UI engine's Renderer on raylib: one texture per sprite sheet (the profile PNGs, the level
// thumbnails), the per-view transform of BaseDraw as an rlgl matrix, alpha and scissor clipping.
#pragma once

#include "aa/ui/renderer.h"
#include "aa/ui/resources.h"

#include <raylib.h>

#include <map>
#include <string>

namespace aa::platform {

class UiRenderer : public aa::ui::Renderer {
public:
    explicit UiRenderer(const aa::ui::ResourceProxy& resources) : resources_(&resources) {}
    ~UiRenderer() override;
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    void setState(const aa::ui::DrawState& state) override;
    void drawSprite(const aa::ui::SpriteRef& sprite, float x, float y, float w, float h) override;
    void drawColorRect(const aa::ui::Rect& rect, aa::ui::Color color) override;
    void setTint(aa::ui::Color tint) override { tint_ = tint; }
    // Flushes the batch and ends a scissor left by the last clipped view: raylib keeps the scissor across
    // EndDrawing, and the next ClearBackground / render-texture pass would honour it.
    void endFrame();
    // Drops the loaded thumbnail textures (LevelSelectionScene::PurgeThumbs).
    void purgeThumbnails();
    void unload();

private:
    const Texture2D* texture(const aa::data::UiSpriteSheet& sheet);
    void applyClip();
    const aa::ui::ResourceProxy* resources_;
    std::map<std::string, Texture2D> textures_;
    aa::ui::DrawState state_;
    aa::ui::Color tint_ = aa::ui::Renderer::kNoTint;
    bool clipping_ = false;
};

}  // namespace aa::platform
