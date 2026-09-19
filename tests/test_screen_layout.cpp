// docs/11-rendering.md §1: the GameApp screen maths against the table of test vectors.
#include "aa/sim/coords.h"
#include "aa/sim/screen_layout.h"

#include <doctest.h>

#include <cmath>

using aa::sim::ScreenLayout;

namespace {

struct Vector {
    int w, h;
    float floor, height, pixelScale, worldScaleWithFloor;
    bool letterBox;
    float frame, yOffset;
};

// The rows of the docs/11 §1 table (PixelScale / WorldScaleWithFloor rounded there to 3–4 digits);
// the original computes in float (ceilf), which the table's last row reflects.
constexpr Vector kVectors[] = {
    {480, 320, 39, 677, 0.469f, 2.1333f, false, 0, -6},
    {800, 480, 39, 677, 0.709f, 1.4104f, true, 53, 0},
    {960, 540, 39, 677, 0.798f, 1.2537f, true, 90, 0},
    {1024, 600, 39, 677, 0.886f, 1.1283f, true, 66, 0},
    {1024, 768, 130, 768, 1.0f, 1.0f, false, 0, 0},
    {1136, 640, 39, 677, 0.945f, 1.0578f, true, 89, 0},
    {1280, 720, 82, 720, 1.0f, 1.0f, true, 128, 0},
    {1280, 800, 130, 768, 1.042f, 0.96f, true, 103, 0},
    {1920, 1080, 130, 768, 1.406f, 0.7111f, true, 171, 0},
    {2048, 1536, 130, 768, 2.0f, 0.5f, false, 0, 0},
    {2560, 1440, 130, 768, 1.875f, 0.5333f, true, 171, -1},   // 1440 × 0.53333336f rounds above 768
    // M7: the 20:9 / 19.5:19 phones (2400×1080 = the arm64 AVD in landscape) — each side frame is
    // about a third of the width, the play field keeps its 1024 virtual px.
    {2400, 1080, 130, 768, 1.406f, 0.7111f, true, 342, 0},
    {2340, 1080, 130, 768, 1.406f, 0.7111f, true, 320, 0},
    {2556, 1179, 130, 768, 1.535f, 0.6514f, true, 321, 0},
    {960, 640, 41, 679, 0.938f, 1.0667f, false, 0, -4},
};

}  // namespace

TEST_CASE("screen layout: the docs/11 test vectors") {
    for (const Vector& v : kVectors) {
        INFO(v.w << "x" << v.h);
        const ScreenLayout l = ScreenLayout::compute(v.w, v.h);
        CHECK(l.floor == doctest::Approx(v.floor));
        CHECK(l.virtualHeight == doctest::Approx(v.height));
        CHECK(l.pixelScale == doctest::Approx(v.pixelScale).epsilon(0.002));
        CHECK(l.worldScaleWithFloor == doctest::Approx(v.worldScaleWithFloor).epsilon(0.002));
        CHECK(l.letterBox == v.letterBox);
        CHECK(l.letterBoxFrameWidth == doctest::Approx(v.frame));
        CHECK(l.letterBoxViewportYOffset == doctest::Approx(v.yOffset));
    }
}

TEST_CASE("screen layout: the play field keeps 1024 virtual px on every window") {
    for (const Vector& v : kVectors) {
        const ScreenLayout l = ScreenLayout::compute(v.w, v.h);
        // native px covered by the play field × virtual px per native px = 1024
        CHECK(l.playFieldNativeWidth() * l.worldScaleWithFloor == doctest::Approx(1024.0f));
        CHECK(l.orthoRight() >= 1024.0f);
        CHECK(l.orthoTop() - l.orthoBottom() >= 638.0f);
    }
}

TEST_CASE("screen layout: a screen squarer than the field centres the world between two bands") {
    // 1280x1153 (aspect 1.11): scale 1, floor 130, WSWF 0.8; the world is 960 native px tall and
    // LetterBoxViewportYOffset = 768 − ceil(1153 · 0.8) = −155 virtual px; the remake splits it.
    const ScreenLayout l = ScreenLayout::compute(1280, 1153);
    CHECK(l.letterBoxViewportYOffset == -155.0f);
    CHECK(l.orthoBottom() == -77.5f);
    CHECK(l.orthoTop() == std::ceil(-77.5f + 1153.0f * 0.8f));
    CHECK(l.worldBandNative() == doctest::Approx(96.875f));
    // (The ceil in LetterBoxViewportYOffset makes the bands 0.75 px more than the leftover height: the
    // upper band is that much thinner — sub-pixel.)
    CHECK(l.worldBandNative() * 2.0f + 768.0f / l.worldScaleWithFloor == doctest::Approx(1153.0f).epsilon(1e-3));
    // The conversions carry the band: the floor line (world y 0) sits `band + floor / WSWF` px up the
    // window, and screenToWorld is the inverse of screenToPixelPos ∘ worldPtToScreenPt there.
    const aa::sim::Camera cam;
    const aa::sim::Vec2 origin = aa::sim::screenToPixelPos(l, cam, aa::sim::worldPtToScreenPt(l, aa::sim::Vec2(0.0f, 0.0f)));
    CHECK(origin.y == doctest::Approx(96.875f + 130.0f / 0.8f));
    const aa::sim::Vec2 back = aa::sim::screenToWorld(l, cam, origin);
    CHECK(back.x == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(back.y == doctest::Approx(0.0f).epsilon(1e-3));
    const aa::sim::Vec2 pt = aa::sim::pixelToScreenPos(l, cam, origin);
    CHECK(pt.y == doctest::Approx(130.0f));
    // The original's screens have no band.
    CHECK(ScreenLayout::compute(1024, 768).worldBandNative() == 0.0f);
    CHECK(ScreenLayout::compute(1920, 1080).worldBandNative() == 0.0f);
    CHECK(ScreenLayout::compute(2560, 1440).worldBandNative() < 1.0f);
}

TEST_CASE("screen layout: the letterbox fills cover the side strips and the bands, whole pixels outwards") {
    const auto check = [](const ScreenLayout::Fill& r, int x, int y, int w, int h) {
        CHECK(r.x == x);
        CHECK(r.y == y);
        CHECK(r.w == w);
        CHECK(r.h == h);
    };
    SUBCASE("1024x768: nothing") { CHECK(ScreenLayout::compute(1024, 768).letterBoxFills().count == 0); }
    SUBCASE("2560x1440: the sides, the 0.94-px band ignored") {
        const ScreenLayout l = ScreenLayout::compute(2560, 1440);
        const ScreenLayout::Fills f = l.letterBoxFills();
        REQUIRE(f.count == 2);
        const int stripW = static_cast<int>(std::ceil(l.playFieldNativeX()));
        check(f.rects[0], 0, 0, stripW, 1440);
        check(f.rects[1], static_cast<int>(std::floor(2560.0f - l.playFieldNativeX())), 0, stripW, 1440);
        CHECK(f.rects[1].x + f.rects[1].w >= 2560);
    }
    SUBCASE("2400x1080: the 20:9 strips of 481 px") {
        const ScreenLayout l = ScreenLayout::compute(2400, 1080);
        const ScreenLayout::Fills f = l.letterBoxFills();
        REQUIRE(f.count == 2);
        check(f.rects[0], 0, 0, 481, 1080);
        check(f.rects[1], 1919, 0, 481, 1080);
    }
    SUBCASE("1280x1153: the two 97-px bands, no sides") {
        const ScreenLayout::Fills f = ScreenLayout::compute(1280, 1153).letterBoxFills();
        REQUIRE(f.count == 2);
        check(f.rects[0], 0, 0, 1280, 97);
        check(f.rects[1], 0, 1153 - 97, 1280, 97);
    }
}

TEST_CASE("screen layout: the border sprites sit against the play field, scaled to the height") {
    // A 198 × 800 sprite (the sheet at PixelScale 1).
    SUBCASE("1280x720: scaled to the height, exactly over the 128-px strips (the original's placement)") {
        const ScreenLayout l = ScreenLayout::compute(1280, 720);
        const ScreenLayout::SpriteRect a = l.borderSpriteRect(false, 198.0f, 800.0f);
        const ScreenLayout::SpriteRect b = l.borderSpriteRect(true, 200.0f, 800.0f);
        CHECK(a.h == 720.0f);
        CHECK(a.w == doctest::Approx(198.0f * 0.9f));
        CHECK(a.x == doctest::Approx(128.0f - 198.0f * 0.9f));
        CHECK(b.x == 1280.0f - 128.0f - 1.0f);
    }
    SUBCASE("2400x1080: the 20:9 strips of 481 px, the sprite (267 px) against the field") {
        const ScreenLayout l = ScreenLayout::compute(2400, 1080);
        const ScreenLayout::SpriteRect a = l.borderSpriteRect(false, 278.0f, 1125.0f);
        CHECK(a.x == doctest::Approx(l.playFieldNativeX() - 278.0f * 0.96f));
        CHECK(a.x > 0.0f);
    }
}

TEST_CASE("screen layout: the widescreen tweak flag is PixelScale < 1 (the 1136x640 / 960x640 originals and nothing larger)") {
    // DeviceParams::AssetScalingForWidescreen is set by selectAssetProfile for the two iPhone sizes only;
    // the remake's equivalent (docs/11 §1) is a screen shorter than the 677-px reference.
    CHECK(ScreenLayout::compute(1136, 640).pixelScale < 1.0f);
    CHECK(ScreenLayout::compute(960, 640).pixelScale < 1.0f);
    CHECK(ScreenLayout::compute(1024, 768).pixelScale >= 1.0f);
    CHECK(ScreenLayout::compute(1280, 720).pixelScale >= 1.0f);
    CHECK(ScreenLayout::compute(1280, 800).pixelScale >= 1.0f);
    CHECK(ScreenLayout::compute(2400, 1080).pixelScale >= 1.0f);
    CHECK(ScreenLayout::compute(2048, 1536).pixelScale >= 1.0f);
}
