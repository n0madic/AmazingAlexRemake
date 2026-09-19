// st::GameRenderer::Render / RenderWorld (docs/11 §2–§3, §7): the camera, the background, the three item
// passes, the floor overlays and the goal markers, then the toolbox strip (screen space), the held item and
// its overlay (gizmos, flip button, InvalidSelection), drawn from a RenderState with rlgl.
#pragma once

#include "aa/platform/atlas.h"
#include "aa/platform/sprite_batch.h"
#include "aa/platform/toolbox_renderer.h"
#include "aa/sim/render_state.h"
#include "aa/sim/screen_layout.h"

#include <raylib.h>

#include <vector>

namespace aa::platform {

struct WorldRendererOptions {
    bool drawMarkers = true;     // goal markers with frameStep >= 0 (the viewer's toggle)
};

class WorldRenderer {
public:
    explicit WorldRenderer(const AtlasSet& atlases);
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Draws one frame into the current raylib frame (between BeginDrawing/EndDrawing): the world passes
    // under the original's projection / modelview scissored to the play field, the toolbox strip over the
    // window, the held item and the overlay back in the world; restores raylib's matrices.
    void draw(const aa::sim::RenderState& state, const aa::sim::ScreenLayout& layout, const WorldRendererOptions& options);
    // GameRenderer::RenderWorldForScreenshot's pass (the sandbox thumbnail): the world with the default
    // camera — background, items, floor overlay — no markers, strip, held item or overlay.
    void drawForThumbnail(aa::sim::RenderState state, const aa::sim::ScreenLayout& layout);

private:
    void setCamera(const aa::sim::RenderState& state, const aa::sim::ScreenLayout& layout);
    void restoreCamera();
    void drawBackground(const aa::sim::RenderState& state);
    void drawFloorOverlay(const aa::sim::RenderState& state);
    void drawMarkers(const aa::sim::RenderState& state);
    void drawParticles(const aa::sim::RenderState& state);
    void drawItem(const aa::sim::RenderItem& item, int layer, const aa::sim::RenderState& state);
    void drawKnots(const aa::sim::RenderItem& item, const aa::sim::RenderState& state);
    void frontPass(const aa::sim::RenderState& state, aa::sim::ItemType type);
    void drawTutorialGhosts(const aa::sim::RenderState& state);
    void drawHeldItem(const aa::sim::RenderState& state);
    void drawOverlay(const aa::sim::RenderState& state);
    void beginFlash(float amount);
    void endFlash();

    const AtlasSet& atlases_;
    SpriteBatch batch_;
    ToolboxRenderer toolbox_;
    Shader flash_{};          // the fixed-item flash (GL_COMBINE interpolate of the original)
    int flashAmountLoc_ = -1;
};

// VisibilitySortingUtils::GetVisibilityOrder (docs/11 §3 step 6): the partition sort of the front pass.
// `items` = (position, up vector) per item; returns the draw order as indices into `items`.
struct VisibilityItem {
    aa::sim::Vec2 position;
    aa::sim::Vec2 up;
};
std::vector<int> visibilityOrder(const std::vector<VisibilityItem>& items);

}  // namespace aa::platform
