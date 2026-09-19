// The per-item renderer FUN_000bd4f0 and its helpers (docs/11 §4): which frames each item type draws, where,
// in which layer. Decoded from the Android build's disassembly (softfp hides the float arguments in the
// decompile): body offsets are MulT(R0, p_k − p_0), part rotations (a_k − a_item) · s for the centred helper.
#pragma once

#include "aa/platform/atlas.h"
#include "aa/platform/sprite_batch.h"
#include "aa/sim/render_state.h"

namespace aa::platform::render {

struct ItemContext {
    SpriteBatch& batch;
    const AtlasSet& atlases;
    const aa::sim::RenderState& state;
};

// The flush offset FUN_000bc12c translates by after the item transform ((-0.2, 0) for the glove).
aa::sim::Vec2 partOffset(const aa::sim::RenderItem& item);

// Parts drawn in world coordinates (the rope strip in layer 0, the zip line in layer 1): called before the
// item transform is pushed.
void drawWorldParts(const aa::sim::RenderItem& item, int layer, ItemContext& ctx);

// Parts drawn in item-local coordinates (inside translate · rotate · scale · translate(partOffset)).
void drawLocalParts(const aa::sim::RenderItem& item, int layer, ItemContext& ctx);

// FUN_000bc304: RopeKnot at every connected attachment point (not for Rope, Doll, ZipLine, nor pipe ends).
void drawKnots(const aa::sim::RenderItem& item, ItemContext& ctx);

}  // namespace aa::platform::render
