#include "aa/platform/world_renderer.h"

#include "render/item_parts.h"

#include <raylib.h>
#include <rlgl.h>

#include <array>
#include <cmath>

namespace aa::platform {

using aa::sim::ItemType;
using aa::sim::RenderItem;
using aa::sim::RenderState;
using aa::sim::Vec2;
using aa::sim::ScreenLayout;

namespace {

// RenderWorld's three type tables (.rodata 0x233b38.., docs/11 §3 steps 3, 4 and 6).
constexpr std::array<int, 11> kBackTypes = {24, 21, 1, 42, 17, 18, 32, 7, 6, 34, 35};
constexpr std::array<int, 35> kMainTypes = {8, 14, 9, 6, 38, 5, 41, 2, 3, 4, 16, 26, 15, 42, 36, 33, 28, 29,
                                            20, 12, 39, 13, 19, 27, 34, 25, 21, 37, 22, 10, 11, 23, 17, 18, 1};
constexpr std::array<int, 4> kFrontTypes = {30, 7, 35, 32};

constexpr float kBackgroundYOffset = -0.432f;             // 0xbedd2f1b: the 130-px floor strip
constexpr float kRadToDeg = 57.29579f;                    // the game's own constant in FUN_000bc12c
constexpr float kGhostTint[4] = {0.6f, 0.4f, 0.4f, 0.4f};
constexpr int kLayerBack = 1;
constexpr int kLayerMain = 0;
// The overlay colours (.bss 0x275324 / 0x275314): normal and in ghost [verified].
constexpr float kGizmoColour[4] = {0.6431372761726379f, 0.7843137383460999f, 0.9333333373069763f, 1.0f};
constexpr float kGizmoGhostColour[4] = {0.8784313797950745f, 0.2666666805744171f, 0.0f, 1.0f};
constexpr float kFlashThreshold = 0.0001f;    // FUN_000bc12c: the flash pass runs while the buzz amplitude > this
// GameItems frames of the overlay (GameRenderer::Render, FUN_000bbb6c / FUN_000c0308 / FUN_000bba20).
constexpr int kFrameFlipGizmo = 44;
constexpr int kFrameInvalidSelection = 58;
constexpr int kFrameRotationGizmo = 100;
constexpr int kFrameTranslationGizmo = 139;

// FUN_000bbcb4: GL_COMBINE with RGB = INTERPOLATE(texture, primary colour (1, 0.31, 0.122), constant alpha =
// the buzz amplitude) — texture · a + flash · (1 − a) — and alpha = the texture's [verified: OPERAND2_RGB stays
// at its GL_SRC_ALPHA default, the env colour is (1, 1, 1, amplitude)]. rlgl has no fixed-function combiner,
// so a fragment shader reproduces the formula.
const char* kFlashFragmentShader330 =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform float amount;\n"
    "void main() {\n"
    "    vec4 t = texture(texture0, fragTexCoord);\n"
    "    finalColor = vec4(mix(vec3(1.0, 0.31, 0.122), t.rgb, amount), t.a);\n"
    "}\n";
// The same formula for the Android build (raylib on GLES 2: GLSL 100).
const char* kFlashFragmentShader100 =
    "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform float amount;\n"
    "void main() {\n"
    "    vec4 t = texture2D(texture0, fragTexCoord);\n"
    "    gl_FragColor = vec4(mix(vec3(1.0, 0.31, 0.122), t.rgb, amount), t.a);\n"
    "}\n";

const char* flashFragmentShader() {
    const int version = rlGetVersion();
    return version == RL_OPENGL_ES_20 || version == RL_OPENGL_ES_30 ? kFlashFragmentShader100 : kFlashFragmentShader330;
}

// Floor foreground tables DAT_00243c00/c04/c14/c18 per background index: enable + LocationForegrounds frame.
constexpr std::array<bool, 4> kLeftEnabled = {false, false, false, true};
constexpr std::array<int, 4> kLeftFrame = {-1, -1, -1, 3};
constexpr std::array<bool, 4> kRightEnabled = {true, true, false, true};
constexpr std::array<int, 4> kRightFrame = {0, 1, -1, 2};

bool inBackList(ItemType type) {
    for (int t : kBackTypes) if (t == static_cast<int>(type)) return true;
    return false;
}

bool drawable(const RenderItem& item) {
    // FUN_000bfde0: flag bit 0 set, and not the held item unless it is fixed (a held fixed item is drawn
    // in the passes, a held movable one in the overlay — M3).
    if ((item.flags & aa::sim::object_flags::kBase) == 0) return false;
    return !item.held || item.isFixed();
}

}  // namespace

std::vector<int> visibilityOrder(const std::vector<VisibilityItem>& items) {
    // VisibilitySortingUtils::GetVisibilityOrder: an explicit-stack quicksort whose partition moves every
    // item lying on the pivot's "up" side (up · (p_j − p_pivot) > 0) in front of the pivot, keeping the
    // scan order, so that a container is drawn after the things inside it.
    const int n = static_cast<int>(items.size());
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
    if (n <= 1) return order;
    std::vector<std::pair<int, int>> stack;
    stack.emplace_back(0, n - 1);
    while (!stack.empty()) {
        const auto [lo, hi] = stack.back();
        const VisibilityItem& pivot = items[static_cast<std::size_t>(order[static_cast<std::size_t>(lo)])];
        int ins = lo;
        for (int i = lo + 1; i <= hi; ++i) {
            const int j = order[static_cast<std::size_t>(i)];
            const VisibilityItem& it = items[static_cast<std::size_t>(j)];
            const float d = pivot.up.y * (it.position.y - pivot.position.y) + (it.position.x - pivot.position.x) * pivot.up.x;
            if (d > 0.0f) {
                for (int m = i; m > ins; --m) order[static_cast<std::size_t>(m)] = order[static_cast<std::size_t>(m - 1)];
                order[static_cast<std::size_t>(ins)] = j;
                ++ins;
            }
        }
        // ins = the pivot's final position; sort the part before it, then the part after it.
        if (lo + 1 < ins) stack.back() = {lo, ins - 1};
        else stack.pop_back();
        if (ins < hi - 1) stack.emplace_back(ins + 1, hi);
    }
    return order;
}

WorldRenderer::WorldRenderer(const AtlasSet& atlases) : atlases_(atlases), batch_(aa::sim::kPixelToMeters), toolbox_(atlases) {
    if (IsWindowReady()) {
        flash_ = LoadShaderFromMemory(nullptr, flashFragmentShader());
        if (flash_.id != 0) flashAmountLoc_ = GetShaderLocation(flash_, "amount");
        if (flash_.id == 0 || flashAmountLoc_ < 0) TraceLog(LOG_WARNING, "flash shader unavailable: fixed items will not light up");
    }
}

WorldRenderer::~WorldRenderer() {
    if (flash_.id != 0) UnloadShader(flash_);
}

void WorldRenderer::beginFlash(float amount) {
    if (flash_.id == 0 || flashAmountLoc_ < 0) return;
    rlDrawRenderBatchActive();
    SetShaderValue(flash_, flashAmountLoc_, &amount, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(flash_);
}

void WorldRenderer::endFlash() {
    if (flash_.id == 0 || flashAmountLoc_ < 0) return;
    rlDrawRenderBatchActive();
    EndShaderMode();
}

void WorldRenderer::setCamera(const RenderState& state, const ScreenLayout& layout) {
    // FUN_000badac: ortho over the letterboxed viewport in virtual px, then the world-to-virtual chain.
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, static_cast<double>(layout.orthoRight()), static_cast<double>(layout.orthoBottom()),
            static_cast<double>(layout.orthoTop()), -100.0, 100.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    const float zoom = state.camera.zoom;
    const float floorM = aa::sim::kPixelToMeters * layout.floor;
    rlTranslatef(layout.letterBoxFrameWidth + aa::sim::Camera::kDefaultCenterX, aa::sim::Camera::kDefaultCenterY, 0.0f);
    rlScalef(zoom, zoom, 1.0f);
    rlTranslatef(-state.camera.centerPx.x, -state.camera.centerPx.y, 0.0f);
    rlScalef(1.0f / aa::sim::kPixelToMeters, 1.0f / aa::sim::kPixelToMeters, 1.0f);
    rlTranslatef(0.0f, floorM / zoom, 0.0f);
}

void WorldRenderer::restoreCamera() {
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

void WorldRenderer::drawBackground(const RenderState& state) {
    // RenderWorld [verified: 0xc08dc]: a 3.41 m wide quad from y = -0.432 at x = slide (the sandbox
    // background change slides the new one in from the right); while the slide runs the previous
    // background draws one world width to the left of it.
    auto quad = [&](int bg, float x) {
        if (bg < 0 || bg >= AtlasSet::kBackgroundCount) return;
        const Atlas& atlas = atlases_.backgrounds[static_cast<std::size_t>(bg)];
        const float k = aa::sim::kWorldWidth / static_cast<float>(atlas.width);
        const aa::sim::Frame& f = atlas.frame(0);
        batch_.rect(atlas, 0, x, 0.0f, x + aa::sim::kWorldWidth, k * f.height());
    };
    rlPushMatrix();
    rlTranslatef(0.0f, kBackgroundYOffset, 0.0f);
    quad(state.backgroundIndex, state.backgroundSlide);
    if (state.backgroundSlide != 0.0f) quad(state.previousBackground, state.backgroundSlide - aa::sim::kWorldWidth);
    rlPopMatrix();
}

void WorldRenderer::drawFloorOverlay(const RenderState& state) {
    const int bg = state.backgroundIndex;
    if (bg < 0 || bg >= AtlasSet::kBackgroundCount) return;
    const Atlas& atlas = atlases_.foregrounds;
    const float k = aa::sim::kWorldWidth / static_cast<float>(atlas.width);
    rlPushMatrix();
    rlTranslatef(0.0f, kBackgroundYOffset, 0.0f);
    // The "left" strip of the Treehouse (frame 3, the bush) is placed at y = textureHeight − k·h metres by
    // the original (≈ 1024 m: off-screen), so only the "right" strip is ever visible.
    if (kRightEnabled[static_cast<std::size_t>(bg)] && kRightFrame[static_cast<std::size_t>(bg)] >= 0) {
        const int frame = kRightFrame[static_cast<std::size_t>(bg)];
        const aa::sim::Frame& f = atlas.frame(frame);
        batch_.rect(atlas, frame, 0.0f, 0.0f, k * f.width(), k * f.height());
    }
    if (kLeftEnabled[static_cast<std::size_t>(bg)] && kLeftFrame[static_cast<std::size_t>(bg)] >= 0) {
        const int frame = kLeftFrame[static_cast<std::size_t>(bg)];
        const aa::sim::Frame& f = atlas.frame(frame);
        const float bottom = static_cast<float>(atlas.height) - k * f.height();
        batch_.rect(atlas, frame, 0.0f, bottom, k * f.width(), bottom + k * f.height());
    }
    rlPopMatrix();
}

void WorldRenderer::drawParticles(const RenderState& state) {
    if (state.particles.empty()) return;
    const Atlas& atlas = atlases_.gameItems;
    constexpr int kSparkleFrame = 122;   // Sparkle03
    static const float kColours[6][3] = {{1.0f, 1.0f, 1.0f}, {1.0f, 0.3f, 0.3f}, {0.3f, 1.0f, 0.4f},
                                         {0.3f, 0.5f, 1.0f}, {1.0f, 0.9f, 0.2f}, {1.0f, 0.5f, 1.0f}};
    for (const aa::sim::RenderParticle& p : state.particles) {
        const float fade = p.maxLife > 0.0f ? p.life / p.maxLife : 0.0f;
        const float* c = kColours[p.colour % 6];
        SpriteBatch::setColor(c[0], c[1], c[2], fade);
        const float s = p.size / (atlas.frame(kSparkleFrame).width() * batch_.k());
        batch_.centered(atlas, kSparkleFrame, p.position, Vec2(s, s));
    }
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void WorldRenderer::drawMarkers(const RenderState& state) {
    // RenderWorld step 9: marker kind → GameItems2 frame of the animation step n (0..4).
    const Atlas& atlas = atlases_.gameItems2;
    const float k = batch_.k();
    for (const aa::sim::RenderMarker& m : state.markers) {
        const int n = m.frameStep;
        if (n < 0) continue;
        int frame = -1;
        Vec2 pos = m.position;
        bool mirror = false;
        auto sideFrame = [n]() { return n > 3 ? n + 7 : n + 6; };   // goal_arrow_down_1..4a, 5a
        switch (m.kind) {
        case 1: frame = 15 + n; break;                                                      // goal_circle
        case 2: frame = sideFrame(); pos.x += k * 0.5f * atlas.frame(frame).height(); break;
        case 3: frame = sideFrame(); pos.x -= k * 0.5f * atlas.frame(frame).height(); break;
        case 4: frame = sideFrame(); pos.y += k * 0.5f * atlas.frame(frame).height(); break;
        case 5: frame = sideFrame(); pos.y -= k * 0.5f * atlas.frame(frame).height(); break;
        case 6: frame = 20 + n; break;                                                      // goal_cross
        case 7: frame = n; mirror = true; break;                                            // goal_arrow
        case 8: frame = n < 4 ? n + 6 : n + 9; pos.y += k * 0.5f * atlas.frame(frame).height(); break;   // 5b
        default: continue;
        }
        const aa::sim::Frame& f = atlas.frame(frame);
        QuadParams q;
        q.pos = pos;
        q.anchorPx = Vec2(f.width() * 0.5f, f.height() * 0.5f);
        q.rotation = m.angle;
        batch_.anchoredMirrored(atlas, frame, q, mirror);
    }
}

void WorldRenderer::drawItem(const RenderItem& item, int layer, const RenderState& state) {
    render::ItemContext ctx{batch_, atlases_, state};
    render::drawWorldParts(item, layer, ctx);
    // FUN_000bc12c: translate(pos) · rotate(angle) · scale(scale) · translate(partOffset); ghost tint on
    // state bit 1 for movable items; every *fixed* item flashes while the buzz amplitude is above the
    // threshold (the tap on a fixed item lights up everything that cannot be moved).
    const Vec2 off = render::partOffset(item);
    const bool ghost = (item.state & 0x02) != 0 && !item.isFixed();
    const bool flash = item.isFixed() && state.buzz.amplitude > kFlashThreshold;
    rlPushMatrix();
    rlTranslatef(item.position.x, item.position.y, 0.0f);
    rlRotatef(item.angle * kRadToDeg, 0.0f, 0.0f, 1.0f);
    rlScalef(item.scale.x, item.scale.y, 1.0f);
    rlTranslatef(off.x, off.y, 0.0f);
    if (ghost) SpriteBatch::setColor(kGhostTint[0], kGhostTint[1], kGhostTint[2], kGhostTint[3]);
    if (flash) beginFlash(state.buzz.amplitude);
    render::drawLocalParts(item, layer, ctx);
    if (flash) endFlash();
    if (ghost) SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
    rlPopMatrix();
}

void WorldRenderer::drawTutorialGhosts(const RenderState& state) {
    // FUN_000c0440 [verified]: the tutorial's extra render entries — a Shelf (mode 2) or a Book (mode 3)
    // copy at the hand, both layers, with glColor4f(0.5, 0.5, 0.5, 0.5); every other type draws nothing.
    for (const aa::sim::TutorialGhost& g : state.tutorialGhosts) {
        if (g.type != ItemType::Shelf && g.type != ItemType::Book) continue;
        RenderItem item;
        item.type = g.type;
        item.position = g.position;
        item.angle = g.angle;
        item.halfSize = g.halfSize;
        item.bodyCount = 1;
        item.bodies[0].position = g.position;
        item.bodies[0].angle = g.angle;
        SpriteBatch::setColor(0.5f, 0.5f, 0.5f, 0.5f);
        drawItem(item, kLayerBack, state);
        drawItem(item, kLayerMain, state);
        SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void WorldRenderer::drawHeldItem(const RenderState& state) {
    // GameRenderer::Render: the held movable item after the world and the strip — layer 1, layer 0, knots.
    if (state.heldIndex < 0 || state.heldIndex >= static_cast<int>(state.items.size())) return;
    const RenderItem& item = state.items[static_cast<std::size_t>(state.heldIndex)];
    if (item.isFixed() || (item.flags & aa::sim::object_flags::kBase) == 0) return;
    drawItem(item, kLayerBack, state);
    drawItem(item, kLayerMain, state);
    drawKnots(item, state);
}

void WorldRenderer::drawOverlay(const RenderState& state) {
    const aa::sim::ManipulationOverlay& ov = state.overlay;
    if (ov.state == aa::sim::ManipulationOverlay::kNone || ov.objectIndex < 0) return;
    const Atlas& atlas = atlases_.gameItems;
    const float* colour = ov.inGhost ? kGizmoGhostColour : kGizmoColour;
    SpriteBatch::setColor(colour[0], colour[1], colour[2], colour[3]);
    switch (ov.state) {
    case aa::sim::ManipulationOverlay::kGizmos:
        // FUN_000bbb6c: RotationGizmo_iPhone at the selected position, turned by the item angle plus the
        // slow phase; FUN_000c0308: the FlipGizmo 0.4 m away at Pi / 4 (renderState's flipButtonPos) for
        // flippable items.
        batch_.centeredRotated(atlas, kFrameRotationGizmo, ov.position, ov.angle + ov.gizmoAngle);
        if (ov.showFlip) batch_.centered(atlas, kFrameFlipGizmo, ov.flipButtonPos);   // the touch rectangle's centre
        break;
    case aa::sim::ManipulationOverlay::kInvalid:
        // InvalidSelection at the selected position, turned by the buzz's random angle, in white.
        SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
        batch_.centeredRotated(atlas, kFrameInvalidSelection, ov.position, state.buzz.angle);
        break;
    case aa::sim::ManipulationOverlay::kTranslation:
        // FUN_000bba20: TranslationGizmoBig turned by the phase and scaled by the selection animation.
        batch_.centeredRotated(atlas, kFrameTranslationGizmo, ov.position, ov.gizmoAngle, Vec2(ov.animScale, ov.animScale));
        break;
    default:
        break;
    }
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void WorldRenderer::drawKnots(const RenderItem& item, const RenderState& state) {
    render::ItemContext ctx{batch_, atlases_, state};
    rlPushMatrix();
    rlTranslatef(item.position.x, item.position.y, 0.0f);
    rlRotatef(item.angle * kRadToDeg, 0.0f, 0.0f, 1.0f);
    rlScalef(item.scale.x, item.scale.y, 1.0f);
    render::drawKnots(item, ctx);
    rlPopMatrix();
}

void WorldRenderer::frontPass(const RenderState& state, ItemType type) {
    // FUN_000c0064: the type's drawable items sorted by the visibility order (the lamp hangs: its "up" is
    // down), then layer-0 parts + knots.
    std::vector<const RenderItem*> candidates;
    std::vector<VisibilityItem> vis;
    const float sign = type == ItemType::HangingLamp ? -1.0f : 1.0f;
    for (const RenderItem& item : state.items) {
        if (item.type != type || !drawable(item)) continue;
        candidates.push_back(&item);
        const float c = std::cos(item.angle);
        const float s = std::sin(item.angle);
        vis.push_back(VisibilityItem{item.position, Vec2(c * 0.0f - s * sign, c * sign + s * 0.0f)});
    }
    for (int i : visibilityOrder(vis)) {
        const RenderItem& item = *candidates[static_cast<std::size_t>(i)];
        drawItem(item, kLayerMain, state);
        if (!inBackList(type)) drawKnots(item, state);
    }
}

void WorldRenderer::drawForThumbnail(RenderState state, const ScreenLayout& layout) {
    state.camera = aa::sim::Camera{};
    state.markers.clear();
    state.heldIndex = -1;
    state.backgroundSlide = 0.0f;
    state.previousBackground = -1;
    state.particles.clear();
    state.tutorialGhosts.clear();
    setCamera(state, layout);
    rlDisableBackfaceCulling();
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
    drawBackground(state);
    for (int t : kBackTypes) {
        for (const RenderItem& item : state.items) {
            if (static_cast<int>(item.type) != t || !drawable(item)) continue;
            drawItem(item, kLayerBack, state);
            drawKnots(item, state);
        }
    }
    for (int t : kMainTypes) {
        for (const RenderItem& item : state.items) {
            if (static_cast<int>(item.type) != t || !drawable(item)) continue;
            drawItem(item, kLayerMain, state);
            if (!inBackList(item.type)) drawKnots(item, state);
        }
    }
    for (int t : kFrontTypes) frontPass(state, static_cast<ItemType>(t));
    drawFloorOverlay(state);
    rlDrawRenderBatchActive();
    restoreCamera();
}

void WorldRenderer::draw(const RenderState& state, const ScreenLayout& layout, const WorldRendererOptions& options) {
    setCamera(state, layout);
    rlDisableBackfaceCulling();   // GL ES 1.x default: mirrored quads (scale.x = -1) must not be culled
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);   // rlgl's vertex colour starts as (0, 0, 0, 0)
    const int scissorX = static_cast<int>(std::floor(layout.playFieldNativeX()));
    const int scissorW = static_cast<int>(std::ceil(layout.playFieldNativeWidth()));
    BeginScissorMode(scissorX, 0, scissorW, layout.height);

    drawBackground(state);
    if (state.markersBehindItems && options.drawMarkers) drawMarkers(state);
    for (int t : kBackTypes) {
        for (const RenderItem& item : state.items) {
            if (static_cast<int>(item.type) != t || !drawable(item)) continue;
            drawItem(item, kLayerBack, state);
            drawKnots(item, state);
        }
    }
    for (int t : kMainTypes) {
        for (const RenderItem& item : state.items) {
            if (static_cast<int>(item.type) != t || !drawable(item)) continue;
            drawItem(item, kLayerMain, state);
            if (!inBackList(item.type)) drawKnots(item, state);
        }
    }
    for (int t : kFrontTypes) frontPass(state, static_cast<ItemType>(t));
    // (The RC signal waves of step 7 are drawn with their controller / truck parts.)
    drawFloorOverlay(state);
    if (!state.markersBehindItems && options.drawMarkers) drawMarkers(state);
    drawTutorialGhosts(state);
    // Step 10: the sparkle / confetti batches (approximate particles, additive-ish: white / coloured).
    drawParticles(state);
    rlDrawRenderBatchActive();
    EndScissorMode();
    restoreCamera();

    // GameRenderer::Render: the strip over the window, then the held item and the overlay in the world.
    toolbox_.draw(state.toolbox, layout);
    setCamera(state, layout);
    rlDisableBackfaceCulling();   // a held flipped item is a mirrored quad
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
    BeginScissorMode(scissorX, 0, scissorW, layout.height);
    drawHeldItem(state);
    drawOverlay(state);
    rlDrawRenderBatchActive();
    EndScissorMode();
    rlEnableBackfaceCulling();
    restoreCamera();
}

}  // namespace aa::platform
