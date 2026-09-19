// Ports of st::CubicInterpolator, st::CurveUtils::GetValueAt, st::Random, st::ManipulationAnimationUtils
// (the Start* / Reset half; Update lives in session.cpp next to the controller that owns the action
// queue), st::FlippingAnimationUtils::Start and st::GhostAnimationUtils (animations.h, ghost.h).
#include "aa/sim/animations.h"

#include "aa/sim/ghost.h"
#include "aa/sim/math_utils.h"

namespace aa::sim {

void CubicInterpolator::start(float from_, float to_, float duration_) {
    duration = duration_;
    t = 0.0f;
    from = from_;
    to = to_;
    value = duration_ > 0.0001f ? from_ : to_;
    active = duration_ > 0.0001f;
}

float CubicInterpolator::update(float dt) {
    if (!active) return to;
    if (t / duration < 1.0f) {
        t = t + dt;
        float r = t / duration;
        if (!(r < 1.0f)) r = 1.0f;
        value = cubicInterp(from, to, r);
        return value;
    }
    active = false;
    value = to;
    return to;
}

float curveValueAt(float t, const CurvePoint* points, int count) {
    if (count == 0) return 0.0f;
    float x0 = points[0].x;
    float y0 = points[0].y;
    float x1 = x0;
    float y1 = y0;
    if (count > 0 && x0 <= t) {
        for (int i = 0;; ++i) {
            if (i < count - 1) {
                x1 = points[i + 1].x;
                y1 = points[i + 1].y;
            }
            if (i + 1 == count) break;
            if (t < points[i + 1].x) break;
            y0 = points[i + 1].y;
            x0 = points[i + 1].x;
        }
    }
    const float r = (x1 - x0) > 0.0f ? (t - x0) / (x1 - x0) : 1.0f;
    return y0 + r * (y1 - y0);
}

std::uint32_t Random::customRand() {
    seed = seed * 0x41C64E6Du + 0x3039u;
    return (seed & 0x7FFFFFFFu) >> 16;
}

float Random::getFloat(float lo, float hi) {
    if (stubCount < 1) {
        const float r = static_cast<float>(static_cast<int>(customRand()));
        return lo + (r / 32767.0f) * (hi - lo);
    }
    const float s = stubs[stubCount - 1];
    --stubCount;
    return lo + (hi - lo) * s;
}

int Random::getInt(int lo, int hi) {
    if (stubCount < 1) {
        const int r = static_cast<int>(customRand());
        // (hi − lo) + 1 in the original's int arithmetic: GetInt(0, 0x7fffffff) (GenerateUniqueFilename)
        // wraps the modulus to INT_MIN and __aeabi_idivmod then returns the 15-bit dividend itself; the
        // unsigned sum keeps that without signed overflow.
        const int modulus = static_cast<int>(static_cast<std::uint32_t>(hi) - static_cast<std::uint32_t>(lo) + 1u);
        return r % modulus + lo;
    }
    const float s = stubs[stubCount - 1];
    --stubCount;
    const float a = static_cast<float>(lo);
    const float b = static_cast<float>(hi + 1);
    int v = static_cast<int>(a + (b - a) * s);
    if (hi <= v) v = hi;
    return v;
}

void ManipulationAnimation::reset() {
    state = kNone;
    t = 0.0f;
    handle = 0;
    scale = 1.0f;
}

void ManipulationAnimation::startAdding(int itemHandle) {
    t = 0.0f;
    state = kAdding;
    scale = 0.0f;
    gizmoScale = 0.0f;
    handle = itemHandle;
}

void ManipulationAnimation::startRemoving(int itemHandle) {
    if (state != kAdding) {
        t = 0.0f;
        state = kRemoving;
        gizmoScale = 0.0f;
        scale = 1.0f;
        handle = itemHandle;
        return;
    }
    handle = itemHandle;
    state = kAddingThenRemoving;
}

void ManipulationAnimation::startSelection(int itemHandle) {
    handle = itemHandle;
    t = 0.0f;
    state = kSelection;
    scale = 1.0f;
    gizmoScale = 0.6f;
}

void ManipulationAnimation::startDeselection(int itemHandle) {
    scale = 1.0f;
    t = 0.0f;
    gizmoScale = 1.0f;
    state = kDeselection;
    handle = itemHandle;
}

void FlippingAnimation::start(float scaleX, int itemHandle) {
    t = 0.0f;
    handle = itemHandle;
    scale = scaleX;
    sign = scaleX >= 0.0f ? -1.0f : 1.0f;
    startScale = scaleX;
    state = 1;
}

void GhostAnimation::start(Vec2 from_, Vec2 to_, int itemHandle) {
    from = from_;
    to = to_;
    handle = itemHandle;
    t = 0.0f;
    position = from_;
    const float len = length(Vec2(to_.x - from_.x, to_.y - from_.y));
    if (len <= kMinDistance) {
        state = 2;
    } else {
        state = 1;
        direction = Vec2((to_.x - from_.x) / len, (to_.y - from_.y) / len);
        duration = len / kSpeed;
    }
}

void GhostAnimation::update(float dt) {
    if (state != 1) return;
    t = dt + t;
    if (t < duration) {
        const float e = cubicInterp(-1.0f, 1.0f, (t / duration) * 0.5f + 0.5f);
        position.x = from.x + e * (to.x - from.x);
        position.y = from.y + e * (to.y - from.y);
    } else {
        state = 2;
        position = to;
    }
}

}  // namespace aa::sim
