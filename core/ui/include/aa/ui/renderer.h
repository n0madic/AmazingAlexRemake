// The drawing interface of the UI engine: BaseDraw sets one transform per view (gr::Context's RenderScene
// [verified]: translate, pivot, scale, rotation, alpha, clip) and the views emit sprites / colour rects
// in their local px. The raylib implementation lives in aa_platform; tests use a recording renderer.
#pragma once

#include "aa/ui/resources.h"
#include "aa/ui/types.h"

namespace aa::ui {

struct DrawState {
    Point translate;      // ceil((pivot + rect.xy) / scale − pivot), as BaseDraw computes it
    Point pivot;
    float scale = 1.0f;
    float angle = 0.0f;   // radians, the sum over the parents
    float alpha = 1.0f;   // the product over the parents
    Rect clip;            // screen px; w < 0 = no clipping
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void setState(const DrawState& state) = 0;
    // A sprite of `sheet` at (x, y) with size (w, h) in the current view space (before the transform).
    virtual void drawSprite(const SpriteRef& sprite, float x, float y, float w, float h) = 0;
    virtual void drawColorRect(const Rect& rect, Color color) = 0;
    // Remake-only: a multiplicative colour for the following sprites (the original draws every sprite
    // white-modulated). HighlightLabelView tints its `*word*` segments with it and restores kNoTint.
    static constexpr Color kNoTint{255, 255, 255, 255};
    virtual void setTint(Color) {}
};

}  // namespace aa::ui
