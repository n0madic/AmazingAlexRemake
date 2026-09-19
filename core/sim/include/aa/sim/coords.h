// The original's screen ↔ world conversions (docs/11 §1, docs/05 §5) [verified: st::screenToWorld
// (0xcf318), WorldStateUtils::ScreenPtToWorldPt / WorldPtToScreenPt, CameraUtils::PixelToScreenPos /
// ScreenToPixelPos, st::pixelSizeToWorld]. "Native px" are window pixels with y up; "screen pt" are
// virtual play-field pixels (1024 × 638 above the floor strip); "world" is metres. The remake adds
// ScreenLayout::worldBandNative() to the native y of every conversion: the band under the centred world on a
// screen squarer than the field (0 on the original's screens, which ignore LetterBoxViewportYOffset here).
#pragma once

#include "aa/sim/render_state.h"
#include "aa/sim/screen_layout.h"
#include "aa/sim/types.h"

namespace aa::sim {

// st::screenToWorld: native px → world metres through the camera and the letterbox.
inline Vec2 screenToWorld(const ScreenLayout& layout, const Camera& camera, Vec2 nativePx) {
    const float invZoom = 1.0f / camera.zoom;
    const float sx = camera.centerPx.x + invZoom * ((layout.worldScaleWithFloor * nativePx.x - layout.letterBoxFrameWidth) - 512.0f);
    const float ny = nativePx.y - layout.worldBandNative();
    const float sy = camera.centerPx.y + invZoom * ((layout.worldScaleWithFloor * ny - layout.floor) - 319.0f);
    return Vec2(kPixelToMeters * sx, kPixelToMeters * sy);
}

// WorldStateUtils::WorldPtToScreenPt: metres → virtual px (the floor strip added to y).
inline Vec2 worldPtToScreenPt(const ScreenLayout& layout, Vec2 world) {
    return Vec2(world.x / kPixelToMeters + 0.0f, layout.floor + world.y / kPixelToMeters);
}

// WorldStateUtils::ScreenPtToWorldPt.
inline Vec2 screenPtToWorldPt(const ScreenLayout& layout, Vec2 screenPt) {
    const float y = screenPt.y - layout.floor;
    return Vec2(kPixelToMeters * screenPt.x, kPixelToMeters * y);
}

// CameraUtils::ScreenToPixelPos: virtual px → native px (Camera+0 = the native screen width).
inline Vec2 screenToPixelPos(const ScreenLayout& layout, const Camera& camera, Vec2 screenPt) {
    const float w = static_cast<float>(layout.width);
    return Vec2(w * 0.0009765625f * (camera.zoom * (screenPt.x - camera.centerPx.x) + 512.0f),
                w * 0.0009765625f * (camera.zoom * (screenPt.y - camera.centerPx.y) + 319.0f) + layout.worldBandNative());
}

// CameraUtils::PixelToScreenPos: native px → virtual px.
inline Vec2 pixelToScreenPos(const ScreenLayout& layout, const Camera& camera, Vec2 nativePx) {
    const float w = static_cast<float>(layout.width);
    const float invZoom = 1.0f / camera.zoom;
    return Vec2(camera.centerPx.x + invZoom * ((1024.0f / w) * nativePx.x - 512.0f),
                camera.centerPx.y + invZoom * ((1024.0f / w) * (nativePx.y - layout.worldBandNative()) - 319.0f));
}

// st::pixelSizeToWorld: a native px delta → metres (3.41 m per native screen width).
inline Vec2 pixelSizeToWorld(const ScreenLayout& layout, Vec2 deltaPx) {
    const float w = static_cast<float>(layout.width);
    return Vec2((deltaPx.x * kWorldWidth) / w, (deltaPx.y * kWorldWidth) / w);
}

}  // namespace aa::sim
