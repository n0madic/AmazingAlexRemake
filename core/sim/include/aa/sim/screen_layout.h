// GameApp::GameApp screen maths (docs/11-rendering.md §1): how the 1024 x 638 virtual play field plus its
// floor strip map onto a native window of any size. Pure arithmetic, no raylib.
#pragma once

#include <algorithm>

namespace aa::sim {

struct ScreenLayout {
    static constexpr float kPlayFieldWidth = 1024.0f;    // virtual px
    static constexpr float kPlayFieldHeight = 638.0f;    // virtual px above the floor strip
    static constexpr float kFloorMin = 39.0f;
    static constexpr float kFloorMax = 130.0f;
    static constexpr float kReferenceHeight = 677.0f;    // 638 + 39: the smallest virtual height
    // The imported UI profile is authored for a PixelScale of its own — 2 for 2048X1536 (the iOS bundle's
    // largest), 1 for 1024X768 (the APK's): its sprites are drawn at pixelScale / profilePixelScale (the
    // toolbox strip and the UI engine alike; a remake decision). kProfilePixelScale is the default the
    // tests and the viewer assume; the app passes the imported profile's (AppState::profilePixelScale).
    static constexpr float kProfilePixelScale = 2.0f;
    float profilePixelScale = kProfilePixelScale;
    float uiScale() const { return pixelScale / profilePixelScale; }

    int width = 1024;                 // native window size
    int height = 768;
    float scale = 1.0f;               // s
    float floor = 130.0f;             // floor strip, virtual px
    float virtualHeight = 768.0f;     // H = 638 + floor
    float pixelScale = 1.0f;          // PixelScale (UI layouts)
    float worldScaleWithFloor = 1.0f; // virtual px per native px
    bool letterBox = false;
    float letterBoxFrameWidth = 0.0f; // LetterBoxFrameWidth, virtual px on each side
    float letterBoxViewportYOffset = 0.0f;
    float anchorAspectCorrectionX = 1.0f;
    float anchorAspectCorrectionY = 1.0f;

    static ScreenLayout compute(int width, int height, float profilePixelScale = kProfilePixelScale);

    // The ortho rectangle FUN_000badac sets up (virtual px, y up): left 0, right ceil(WSWF·w),
    // bottom LetterBoxViewportYOffset, top ceil(bottom + h·WSWF). The remake deviates in the bottom: on a
    // screen squarer than the 1024 × virtualHeight field (aspect < 4:3 at floor 130) the width binds and
    // LetterBoxViewportYOffset goes negative — the original puts the whole of it below the world (the
    // world at the top, an empty band under the floor); the remake splits it, centring the world with
    // equal bands above and below, like the letterbox strips at the sides (docs/11 §1).
    float orthoRight() const;
    float orthoBottom() const { return letterBoxViewportYOffset * 0.5f; }
    float orthoTop() const;
    // The band on each side of the centred world, native px (0 on the original's screens; the same
    // shift enters every native ↔ world conversion, coords.h).
    float worldBandNative() const { return std::max(0.0f, -orthoBottom() / worldScaleWithFloor); }

    // Native pixels covered by the play field (for the letterbox scissor): x, width; the full height.
    float playFieldNativeX() const { return letterBoxFrameWidth / worldScaleWithFloor; }
    float playFieldNativeWidth() const { return kPlayFieldWidth / worldScaleWithFloor; }

    // The letterbox backdrop (a remake addition, docs/11 §1), drawn under the world, which covers its
    // middle: the fills — native-px rectangles (y down) in the colour of the BORDERIMAGE sprites' outer
    // column over the side strips (wider than the 16:9-authored sprite on 20:9 phones) and the bands
    // above and below the centred world, whole pixels rounded outwards, none when a band is under 1 px
    // (float rounding, e.g. 2560×1440) — and the two sprites over them.
    struct Fill {
        int x, y, w, h;
    };
    struct Fills {
        Fill rects[4];
        int count = 0;
    };
    Fills letterBoxFills() const;
    // BORDERIMAGE_LEFT / RIGHT (`spriteW` × `spriteH` screen px) over the side strips: scaled to the
    // screen height (the original scales only past 1200 px of width; the remake always) and set against
    // the play field's edge — the original's placement, x = LetterBoxFrameWidth − w and
    // NativeScreenWidth − LetterBoxFrameWidth − 1; the screen crops the outer part. Only with a letterbox:
    // the bands of a squarer screen get the fill alone (the sprites are side art).
    struct SpriteRect {
        float x, y, w, h;
    };
    SpriteRect borderSpriteRect(bool right, float spriteW, float spriteH) const;
};

}  // namespace aa::sim
