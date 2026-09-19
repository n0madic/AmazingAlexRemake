// st::ToolboxRenderer::Render (docs/11 §7): the toolbox strip in screen space — the tiled slide, its end
// cap, the slot icons with their counters (scissored to the extended length) and the round button.
#pragma once

#include "aa/platform/atlas.h"
#include "aa/platform/sprite_batch.h"
#include "aa/sim/render_state.h"
#include "aa/sim/screen_layout.h"

namespace aa::platform {

class ToolboxRenderer {
public:
    explicit ToolboxRenderer(const AtlasSet& atlases) : atlases_(atlases) {}

    // Draws the strip with an ortho over the window in native px (y up), between BeginDrawing / EndDrawing,
    // and restores raylib's matrices afterwards.
    void draw(const aa::sim::RenderToolbox& toolbox, const aa::sim::ScreenLayout& layout);

private:
    void drawBackground(const aa::sim::RenderToolbox& toolbox, SpriteBatch& batch);
    void drawSlots(const aa::sim::RenderToolbox& toolbox, const aa::sim::ScreenLayout& layout, SpriteBatch& batch);

    const AtlasSet& atlases_;
};

}  // namespace aa::platform
