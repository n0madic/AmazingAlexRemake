// The original's small maths helpers with their exact float semantics: the `sinf/cosf/atan2f` of the shipped
// binary are wrappers that call the double libm and round the result to float (Bionic's own; the remake
// routes them to aa_libm), and st::Rotate combines them with VFP multiplies (unfused).
#pragma once

#include "aa/sim/types.h"

#include <cmath>

extern "C" {
#include "aa_libm.h"
}

namespace aa::sim {

// The game's DegToRad constant and its π global — a DegToRad·180 product, one ulp below the rounded π
// (docs/10 §11 item 7). Every angle literal of the port derives from these two.
constexpr float kDegToRad = 0.017453289f;
constexpr float kGamePi = kDegToRad * 180.0f;
static_assert(kGamePi == 3.14159202576f, "the game's Pi is 0x40490fd8");

inline float sinF(float x) { return static_cast<float>(aa_sin(static_cast<double>(x))); }
inline float cosF(float x) { return static_cast<float>(aa_cos(static_cast<double>(x))); }
inline float atan2F(float y, float x) {
    return static_cast<float>(aa_atan2(static_cast<double>(y), static_cast<double>(x)));
}
inline float asinF(float x) { return static_cast<float>(aa_asin(static_cast<double>(x))); }
// logf / powf of the shipped binary: local wrappers over the double libm as well [verified: the ELF imports
// only log / pow; logf, atan2f, asinf, powf are `vcvt.f64.f32; bl log@plt; vcvt.f32.f64` wrappers].
inline float logF(float x) { return static_cast<float>(aa_log(static_cast<double>(x))); }
inline float powF(float x, float y) { return static_cast<float>(aa_pow(static_cast<double>(x), static_cast<double>(y))); }
inline float acosF(float x) { return static_cast<float>(aa_acos(static_cast<double>(x))); }

// st::Length(Vec2): sqrt(y·y + x·x) with an unfused multiply-add (VFP vmla) and an exact square root.
inline float length(Vec2 v) {
    const float yy = v.y * v.y;
    return std::sqrt(yy + v.x * v.x);
}

// st::Normalize(Vec2): each component divided by length(v).
inline Vec2 normalize(Vec2 v) {
    const float len = length(v);
    return Vec2(v.x / len, v.y / len);
}

// st::Rotate(float angle, Vec2 const&): (c·x − s·y, c·y + s·x) with c = cosF(angle), s = sinF(angle).
inline Vec2 rotate(float angle, Vec2 v) {
    const float c = cosF(angle);
    const float s = sinF(angle);
    return Vec2(c * v.x - s * v.y, c * v.y + s * v.x);
}

}  // namespace aa::sim
