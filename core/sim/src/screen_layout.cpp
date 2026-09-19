#include "aa/sim/screen_layout.h"

#include <algorithm>
#include <cmath>

namespace aa::sim {

ScreenLayout ScreenLayout::compute(int width, int height, float profilePixelScale) {
    ScreenLayout l;
    l.width = width;
    l.height = height;
    l.profilePixelScale = profilePixelScale;
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    l.scale = h <= kReferenceHeight ? std::min(h / kReferenceHeight, w / kPlayFieldWidth) : 1.0f;
    l.floor = std::clamp(h - std::ceil(kPlayFieldHeight * l.scale), kFloorMin, kFloorMax);
    l.virtualHeight = kPlayFieldHeight + l.floor;
    l.pixelScale = h / l.virtualHeight > 1.0f ? h / l.virtualHeight : l.scale;
    l.worldScaleWithFloor = std::max(kPlayFieldWidth / w, l.virtualHeight / h);
    const float aspect = w / h;
    l.letterBox = aspect > kPlayFieldWidth / l.virtualHeight;
    l.letterBoxFrameWidth =
        l.letterBox ? std::ceil(std::fabs(kPlayFieldWidth - std::ceil(w * l.worldScaleWithFloor)) / 2.0f) : 0.0f;
    l.letterBoxViewportYOffset = std::ceil(l.virtualHeight) - std::ceil(h * l.worldScaleWithFloor);
    l.anchorAspectCorrectionX = 1.0f - (aspect - 4.0f / 3.0f) / 2.0f;
    l.anchorAspectCorrectionY = 1.0f - (1.0f / aspect - 3.0f / 4.0f) / 2.0f;
    return l;
}

ScreenLayout::Fills ScreenLayout::letterBoxFills() const {
    Fills f;
    const auto add = [&](int x, int y, int w, int h) { f.rects[f.count++] = Fill{x, y, w, h}; };
    if (letterBox) {
        const int stripW = static_cast<int>(std::ceil(playFieldNativeX()));
        add(0, 0, stripW, height);
        add(static_cast<int>(std::floor(static_cast<float>(width) - playFieldNativeX())), 0, stripW, height);
    }
    if (worldBandNative() >= 1.0f) {
        const int band = static_cast<int>(std::ceil(worldBandNative()));
        add(0, 0, width, band);
        add(0, height - band, width, band);
    }
    return f;
}

ScreenLayout::SpriteRect ScreenLayout::borderSpriteRect(bool right, float spriteW, float spriteH) const {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float scale = spriteH > 0.0f ? h / spriteH : 1.0f;
    const float sw = spriteW * scale;
    const float fieldX = playFieldNativeX();
    return SpriteRect{right ? w - fieldX - 1.0f : fieldX - sw, 0.0f, sw, h};
}

float ScreenLayout::orthoRight() const { return std::ceil(worldScaleWithFloor * static_cast<float>(width)); }

float ScreenLayout::orthoTop() const { return std::ceil(orthoBottom() + static_cast<float>(height) * worldScaleWithFloor); }

}  // namespace aa::sim
