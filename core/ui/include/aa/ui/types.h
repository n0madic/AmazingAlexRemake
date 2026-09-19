// The UI engine's plain types (UI::UIPoint / UISize / UIRect / UIAnchor of the original, docs/06 §1).
// Coordinates are native screen pixels with the origin at the top-left corner, y down — the original's
// view space; the world renderer's y-up convention is converted at the GameView boundary.
#pragma once

#include <cstdint>
#include <string>

namespace aa::ui {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Size {
    float w = 0.0f;
    float h = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// UIAnchor::fromString [verified]: H: HNONE 0, LEFT 1, HCENTER 2, RIGHT 3, HPIVOT 4; V: VNONE 0, TOP 1,
// VCENTER 2, BOTTOM 3, VPIVOT 4, BASELINE 5.
enum class HAnchor : int { None = 0, Left = 1, Center = 2, Right = 3, Pivot = 4 };
enum class VAnchor : int { None = 0, Top = 1, Center = 2, Bottom = 3, Pivot = 4, Baseline = 5 };

struct Anchor {
    HAnchor h = HAnchor::None;
    VAnchor v = VAnchor::None;
};

HAnchor hAnchorFromString(const std::string& s);
VAnchor vAnchorFromString(const std::string& s);

// game::Anchor of the bitmap fonts (LabelView FontAnchorH / FontAnchorV): H LEFT 0, HCENTER 1, RIGHT 2;
// V TOP 0, VCENTER 1, BOTTOM 2.
enum class FontAnchorH : int { Left = 0, Center = 1, Right = 2 };
enum class FontAnchorV : int { Top = 0, Center = 1, Bottom = 2 };
FontAnchorH fontAnchorHFromString(const std::string& s);
FontAnchorV fontAnchorVFromString(const std::string& s);

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

// The screen numbers the layout reads (st::GameParams, docs/11 §1): native size, the anchor aspect
// correction, and the remake's UI sprite scale (the imported profile drawn at PixelScale / 2).
struct ScreenParams {
    float nativeWidth = 1024.0f;
    float nativeHeight = 768.0f;
    float anchorCorrectionX = 1.0f;
    float anchorCorrectionY = 1.0f;
    float pixelScale = 1.0f;
    float uiScale = 0.5f;               // sprite px → screen px
    bool letterBox = false;
    float letterBoxFrameWidth = 0.0f;   // native px of one side strip (ScreenLayout::playFieldNativeX)
    // DeviceParams::AssetScalingForWidescreen (docs/11 §1): the chapter books and comic pages ×0.85, the
    // level buttons ×0.89. The original sets it for the 1136×640 / 960×640 screens only; the remake's
    // equivalent is "the screen is shorter than the 677-px reference play field" (PixelScale < 1).
    bool widescreenScaling = false;
};

// One pointer event in screen px.
struct TouchEvent {
    int id = 0;
    Point position;
    Point previous;
    double time = 0.0;
};

}  // namespace aa::ui
