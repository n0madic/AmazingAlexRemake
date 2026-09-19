// st::SpriteRenderer quad primitives (docs/11 §4, decoded from AddQuad* in the Android build) on top of
// rlgl: every call appends one textured quad in the *current* rlgl matrix (the item transform pushed by
// the world renderer), sizes in metres = frame pixels × k (GetPixelToMetersFactor).
#pragma once

#include "aa/platform/atlas.h"
#include "aa/sim/types.h"

namespace aa::platform {

// The four corners of a frame around an anchor point given in frame pixels from the frame's bottom-left,
// scaled by (scale.x, scale.y) × k, rotated by `rotation` (radians) about the anchor, translated to `pos`.
// The texture is never mirrored by this call; a negative scale.x mirrors the quad (SlingshotElastic,
// SeesawArm), scale.y -1 flips it (SpringSeat, glove plates rotated by Pi ∓ θ instead).
struct QuadParams {
    aa::sim::Vec2 pos{0.0f, 0.0f};
    aa::sim::Vec2 anchorPx{0.0f, 0.0f};
    float rotation = 0.0f;
    aa::sim::Vec2 scale{1.0f, 1.0f};
};

class SpriteBatch {
public:
    explicit SpriteBatch(float pixelToMeters) : k_(pixelToMeters) {}

    float k() const { return k_; }

    // AddQuadCenteredAt / AddQuadCenteredAtWithScale: the frame centred at pos.
    void centered(const Atlas& atlas, int frame, aa::sim::Vec2 pos, aa::sim::Vec2 scale = {1.0f, 1.0f});
    // AddQuadWithAnchorPoint[WithRotation]: anchor from the frame's bottom-left, in px.
    void anchored(const Atlas& atlas, int frame, const QuadParams& q);
    // FUN_000bc080: centred anchor (w/2, h/2) with rotation and scale.
    void centeredRotated(const Atlas& atlas, int frame, aa::sim::Vec2 pos, float rotation,
                         aa::sim::Vec2 scale = {1.0f, 1.0f});
    // FUN_000bcec4 / FUN_000bd01c: bottom-centre anchor (w/2, 0), rotation (bcec4) or none (bd01c).
    void bottomAnchored(const Atlas& atlas, int frame, aa::sim::Vec2 pos, float rotation = 0.0f,
                        aa::sim::Vec2 scale = {1.0f, 1.0f});
    // FUN_000bcf58: bottom-centre anchor, stretched to `length` metres along its y axis.
    void bottomAnchoredStretched(const Atlas& atlas, int frame, aa::sim::Vec2 pos, float length,
                                 aa::sim::Vec2 scale = {1.0f, 1.0f});
    // FUN_000bac18: left-middle anchor (0, h/2) at `from`, rotated towards `to` and stretched to reach it.
    void stretchedBetween(const Atlas& atlas, int frame, aa::sim::Vec2 from, aa::sim::Vec2 to);
    // AddQuadCenteredAtWithSrcRect: a pixel sub-rectangle {x0, x1, yTop, yBottom} of the frame centred at pos.
    void centeredSrcRect(const Atlas& atlas, float x0, float x1, float yTop, float yBottom, aa::sim::Vec2 pos);
    // AddQuadWithAnchorPointWithRotationAndMirror: as anchored(), with the V axis flipped when the
    // rotation points left (pi/2 < angle <= 3pi/2) and `mirror` is set — the goal arrow marker.
    void anchoredMirrored(const Atlas& atlas, int frame, const QuadParams& q, bool mirror);

    // RopeRenderUtils strip: `count` points in the current (world) frame, one texture copy of the frame
    // per segment, `halfWidth` metres to each side; `segmentMask[i]` = draw segment i (rope cuts).
    void strip(const Atlas& atlas, int frame, const aa::sim::Vec2* points, int count, float halfWidth,
               const bool* segmentMask = nullptr);

    // FUN_000bb8a0: the whole frame stretched over an axis-aligned rectangle (backgrounds, floor overlays).
    void rect(const Atlas& atlas, int frame, float left, float bottom, float right, float top);

    // Colour for the following quads (rlColor4f), reset to white by the caller.
    static void setColor(float r, float g, float b, float a);

private:
    void emit(const Atlas& atlas, const aa::sim::Vec2 corners[4], float u0, float u1, float vTop, float vBottom,
              bool flipV = false);
    float k_;
};

}  // namespace aa::platform
