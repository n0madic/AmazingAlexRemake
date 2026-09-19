// The small set-up-mode animation states of GameScreenController (docs/05 §5, docs/11 §2) and the helpers
// they share: st::CubicInterp / CubicInterpolator, st::CurveUtils::GetValueAt and st::Random. Every
// constant is the original's (`[verified]` in docs/05 §5); the float expression shapes are kept.
#pragma once

#include "aa/sim/math_utils.h"
#include "aa/sim/types.h"

#include <cstdint>

namespace aa::sim {

// st::CubicInterp(a, b, t) = a + (b − a)·(3t² − 2t³), written as the binary evaluates it.
inline float cubicInterp(float a, float b, float t) {
    return a + (b - a) * ((t * 3.0f) * t + ((t * -2.0f) * t) * t);
}

// st::CubicInterpolator (0x18 bytes): the fixed-item buzz amplitude.
struct CubicInterpolator {
    bool active = false;
    float from = 0.0f;
    float to = 0.0f;
    float duration = 0.0f;
    float t = 0.0f;
    float value = 0.0f;

    void start(float from_, float to_, float duration_);
    // Advances and returns the value; a finished interpolator returns `to`.
    float update(float dt);
};

// st::CurvePoint / CurveUtils::GetValueAt: piecewise-linear interpolation, clamped at both ends.
struct CurvePoint {
    float x;
    float y;
};
float curveValueAt(float t, const CurvePoint* points, int count);

// st::Random: an LCG (x·0x41C64E6D + 0x3039, bits 16..30) with a stub queue the tests can prefill.
struct Random {
    static constexpr int kMaxStubs = 8;

    std::uint32_t seed = 1;
    int stubCount = 0;
    float stubs[kMaxStubs] = {};

    void setSeed(int s) { seed = static_cast<std::uint32_t>(s); }
    std::uint32_t customRand();
    float getFloat(float lo, float hi);
    int getInt(int lo, int hi);
};

// st::ManipulationAnimationState (0x18 bytes): selection / deselection / adding / removing scale animation
// of the held item. `scale` is applied to the object (doFrame), `gizmoScale` to the translation gizmo.
struct ManipulationAnimation {
    static constexpr int kNone = 0;
    static constexpr int kAdding = 1;        // 0.3 s over kAddingCurve
    static constexpr int kRemoving = 2;      // 0.15 s cubic 1 → 0.2, then action 10
    static constexpr int kAddingThenRemoving = 3;
    static constexpr int kSelection = 4;     // 0.1 s cubic 0.6 → 1 (gizmo)
    static constexpr int kDeselection = 5;   // 0.07 s cubic 1 → 0.6 (gizmo)

    int state = kNone;
    float t = 0.0f;
    float scale = 1.0f;
    float gizmoScale = 0.0f;
    int reserved = 0;
    int handle = 0;

    void reset();
    void startAdding(int itemHandle);
    void startRemoving(int itemHandle);
    void startSelection(int itemHandle);
    void startDeselection(int itemHandle);
};

// st::FlippingAnimationState (0x1c bytes): scale.x runs −sign·cos(π t / 0.15) over 0.15 s; the physics flip
// happens at t = 0.075; an item that collides at the end flips back once.
struct FlippingAnimation {
    int state = 0;             // 0 idle, 1 running, 2 finished this frame
    int handle = 0;
    float t = 0.0f;
    float sign = 0.0f;         // −1 when the item started unflipped (scale.x ≥ 0), else +1
    float scale = 0.0f;        // the scale.x the controller copies to the object
    float startScale = 0.0f;
    bool flippedBack = false;

    void start(float scaleX, int itemHandle);
};

// GameScreenController+0xc1ce0..: the fixed-item buzz (action 14 / 15): amplitude interpolator, the buzzed
// item and a random shake angle in (−π, π).
struct BuzzState {
    CubicInterpolator amplitude;
    int handle = 0;
    float angle = 0.0f;
};

}  // namespace aa::sim
