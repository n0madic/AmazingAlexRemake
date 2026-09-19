#include "aa/platform/toolbox_renderer.h"

#include "aa/sim/toolbox.h"

#include <raylib.h>
#include <rlgl.h>

#include <cmath>

namespace aa::platform {

using aa::sim::Frame;
using aa::sim::RenderToolbox;
using aa::sim::RenderToolboxSlot;
using aa::sim::ScreenLayout;
using aa::sim::Vec2;
namespace tf = aa::sim::toolbox_frames;

namespace {

constexpr float kSlideTileFraction = 0.9f;    // renderBackground: each tile shows 90 % of the slide's width
constexpr float kSlideEndFraction = 0.95f;    // the end cap shows 95 % of its width
constexpr float kCounterYFraction = 0.43f;    // the counter sits at 0.43 · slide height above the strip axis
constexpr float kCounterXFraction = 0.3f;     // and 0.3 · its own width left of the icon's left edge

float width(const Frame& f) { return std::fabs(f.x1 - f.x0); }
float height(const Frame& f) { return std::fabs(f.y1 - f.y0); }
Vec2 center(const Frame& f) { return Vec2((f.x0 + f.x1) * 0.5f, (f.y0 + f.y1) * 0.5f); }

}  // namespace

void ToolboxRenderer::drawBackground(const RenderToolbox& toolbox, SpriteBatch& batch) {
    // st::renderBackground: `ejectLength / (0.9 · w)` tiles of the slide's middle 90 % to the left of the
    // button, then the fractional remainder as a narrower tile.
    const Atlas& atlas = atlases_.uiElements;
    const Frame& slide = atlas.frame(tf::kSlide);
    const float k = batch.k();
    const float tileW = width(slide) * kSlideTileFraction;          // frame px
    const float h = height(slide);
    const Vec2 c = center(slide);
    const float count = toolbox.ejectLength / (tileW * k);           // in tiles of native px
    const int whole = static_cast<int>(std::floor(count));
    const float fraction = count - static_cast<float>(whole);
    for (int i = 0; i < whole; ++i) {
        const float x = -tileW * 0.5f * k - static_cast<float>(i) * tileW * k;
        batch.centeredSrcRect(atlas, c.x - tileW * 0.5f, c.x + tileW * 0.5f, c.y - h * 0.5f, c.y + h * 0.5f, Vec2(x, 0.0f));
    }
    if (fraction > 0.0f) {
        const float partW = fraction * tileW;
        const float x = fraction * 0.5f * tileW * k - toolbox.ejectLength;
        batch.centeredSrcRect(atlas, c.x - partW * 0.5f, c.x + partW * 0.5f, c.y - h * 0.5f, c.y + h * 0.5f, Vec2(x, 0.0f));
    }
    // The end cap: 95 % of its width (from the left edge), centred 0.475 · w beyond the eject length.
    const Frame& end = atlas.frame(tf::kSlideEnd);
    const float ew = width(end);
    const float eh = height(end);
    const Vec2 ec = center(end);
    batch.centeredSrcRect(atlas, ec.x - ew * 0.5f, ec.x + ew * kSlideTileFraction * 0.5f, ec.y - eh * 0.5f, ec.y + eh * 0.5f,
                          Vec2(-(toolbox.ejectLength + ew * kSlideEndFraction * 0.5f * k), 0.0f));
}

void ToolboxRenderer::drawSlots(const RenderToolbox& toolbox, const ScreenLayout& layout, SpriteBatch& batch) {
    // The slots whose centres fall in the extended length, clipped to [x − ejectLength, x] × the window height.
    const Atlas& atlas = atlases_.uiElements;
    const float k = batch.k();
    const float eject = toolbox.ejectLength;
    if (eject <= 0.0f || toolbox.slots.empty()) return;
    rlDrawRenderBatchActive();
    const int sx = static_cast<int>(toolbox.x - eject);
    const int sw = static_cast<int>(eject);
    // raylib's scissor rectangle is y-down from the top of the window; the full height needs no flip.
    BeginScissorMode(sx, 0, sw < 0 ? 0 : sw, layout.height);
    // Slot centres relative to the button: Toolbox::getCenterForSlot = uniformToScreen(slot + 0.5) − eject − scroll.
    float left = 0.0f;
    for (const RenderToolboxSlot& s : toolbox.slots) {
        const float cx = (left + s.widthPx * 0.5f) - eject - toolbox.scroll;
        left += s.widthPx;
        if (cx + s.widthPx * 0.5f < -eject || cx - s.widthPx * 0.5f > 0.0f) continue;
        const int icon = aa::sim::kToolboxIconFrame[static_cast<std::size_t>(s.type)];
        batch.centered(atlas, icon, Vec2(cx, 0.0f));
        if (s.amount > 1) {
            const int counter = tf::counterFrame(s.amount);
            if (counter < 0) continue;
            const Frame& iconFrame = atlas.frame(icon);
            const Frame& counterFrame = atlas.frame(counter);
            const float slideH = height(atlas.frame(tf::kSlide));
            const Vec2 pos((cx - k * 0.5f * width(iconFrame)) - k * width(counterFrame) * kCounterXFraction,
                           k * (kCounterYFraction * slideH - height(counterFrame) * 0.5f));
            batch.centered(atlas, counter, pos);
        }
    }
    rlDrawRenderBatchActive();
    EndScissorMode();
}

void ToolboxRenderer::draw(const RenderToolbox& toolbox, const ScreenLayout& layout) {
    if (!toolbox.visible || !atlases_.uiElements.loaded()) return;
    // GameRenderer::Render: glOrthof(0, W, 0, H) in native px, translated to the button centre. The
    // projection is pushed; the modelview is set in place (an rlgl push in MODELVIEW mode redirects to the
    // per-vertex transform matrix, which a pop in another mode would leave behind).
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, static_cast<double>(layout.width), 0.0, static_cast<double>(layout.height), -1.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlTranslatef(toolbox.x, toolbox.y, 0.0f);
    rlDisableBackfaceCulling();
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
    SpriteBatch batch(toolbox.spriteScale);
    drawBackground(toolbox, batch);
    rlDrawRenderBatchActive();
    drawSlots(toolbox, layout, batch);
    // The round button and its icon, both scaled by the press animation.
    const Vec2 scale(toolbox.buttonScale, toolbox.buttonScale);
    batch.centered(atlases_.uiElements, tf::kButton, Vec2(0.0f, 0.0f), scale);
    batch.centered(atlases_.uiElements, tf::kButtonIcon, Vec2(0.0f, 0.0f), scale);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

}  // namespace aa::platform
