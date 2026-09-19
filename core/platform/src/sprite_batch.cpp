#include "aa/platform/sprite_batch.h"

#include <raylib.h>
#include <rlgl.h>

#include <cmath>
#include <vector>

namespace aa::platform {

using aa::sim::Frame;
using aa::sim::Vec2;

namespace {

Vec2 rotateVec(float angle, Vec2 v) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return Vec2(c * v.x - s * v.y, c * v.y + s * v.x);
}

// Corner offsets of AddQuadWithAnchorPoint*: bottom-left, bottom-right, top-left, top-right.
void anchoredCorners(const Frame& f, const QuadParams& q, float k, Vec2 out[4]) {
    const float w = std::fabs(f.x1 - f.x0);
    const float h = std::fabs(f.y1 - f.y0);
    const float left = -k * q.anchorPx.x * q.scale.x;
    const float right = (w - q.anchorPx.x) * k * q.scale.x;
    const float bottom = -k * q.anchorPx.y * q.scale.y;
    const float top = (h - q.anchorPx.y) * k * q.scale.y;
    const Vec2 local[4] = {Vec2(left, bottom), Vec2(right, bottom), Vec2(left, top), Vec2(right, top)};
    for (int i = 0; i < 4; ++i) {
        const Vec2 r = rotateVec(q.rotation, local[i]);
        out[i] = Vec2(r.x + q.pos.x, r.y + q.pos.y);
    }
}

}  // namespace

void SpriteBatch::setColor(float r, float g, float b, float a) { rlColor4f(r, g, b, a); }

void SpriteBatch::emit(const Atlas& atlas, const Vec2 c[4], float u0, float u1, float vTop, float vBottom,
                       bool flipV) {
    if (flipV) {
        const float t = vTop;
        vTop = vBottom;
        vBottom = t;
    }
    rlSetTexture(atlas.texture.id);
    rlBegin(RL_QUADS);
    // rlgl quads are wound bottom-left, bottom-right, top-right, top-left (any order works with culling off).
    rlTexCoord2f(u0, vBottom); rlVertex2f(c[0].x, c[0].y);
    rlTexCoord2f(u1, vBottom); rlVertex2f(c[1].x, c[1].y);
    rlTexCoord2f(u1, vTop);    rlVertex2f(c[3].x, c[3].y);
    rlTexCoord2f(u0, vTop);    rlVertex2f(c[2].x, c[2].y);
    rlEnd();
}

void SpriteBatch::centered(const Atlas& atlas, int frame, Vec2 pos, Vec2 scale) {
    const Frame& f = atlas.frame(frame);
    const float hw = std::fabs(f.x1 - f.x0) * 0.5f * k_ * scale.x;
    const float hh = std::fabs(f.y1 - f.y0) * 0.5f * k_ * scale.y;
    const Vec2 c[4] = {Vec2(pos.x - hw, pos.y - hh), Vec2(pos.x + hw, pos.y - hh), Vec2(pos.x - hw, pos.y + hh),
                       Vec2(pos.x + hw, pos.y + hh)};
    const float tw = static_cast<float>(atlas.width);
    const float th = static_cast<float>(atlas.height);
    emit(atlas, c, f.x0 / tw, f.x1 / tw, f.y0 / th, f.y1 / th);
}

void SpriteBatch::anchored(const Atlas& atlas, int frame, const QuadParams& q) { anchoredMirrored(atlas, frame, q, false); }

void SpriteBatch::anchoredMirrored(const Atlas& atlas, int frame, const QuadParams& q, bool mirror) {
    const Frame& f = atlas.frame(frame);
    Vec2 c[4];
    anchoredCorners(f, q, k_, c);
    const float tw = static_cast<float>(atlas.width);
    const float th = static_cast<float>(atlas.height);
    constexpr float kHalfPi = 1.5707964f;
    constexpr float kThreeHalfPi = 4.712389f;
    const bool flip = mirror && q.rotation > kHalfPi && q.rotation <= kThreeHalfPi;
    emit(atlas, c, f.x0 / tw, f.x1 / tw, f.y0 / th, f.y1 / th, flip);
}

void SpriteBatch::centeredRotated(const Atlas& atlas, int frame, Vec2 pos, float rotation, Vec2 scale) {
    const Frame& f = atlas.frame(frame);
    QuadParams q;
    q.pos = pos;
    q.anchorPx = Vec2(std::fabs(f.x1 - f.x0) * 0.5f, std::fabs(f.y1 - f.y0) * 0.5f);
    q.rotation = rotation;
    q.scale = scale;
    anchored(atlas, frame, q);
}

void SpriteBatch::bottomAnchored(const Atlas& atlas, int frame, Vec2 pos, float rotation, Vec2 scale) {
    const Frame& f = atlas.frame(frame);
    QuadParams q;
    q.pos = pos;
    q.anchorPx = Vec2(std::fabs(f.x1 - f.x0) * 0.5f, 0.0f);
    q.rotation = rotation;
    q.scale = scale;
    anchored(atlas, frame, q);
}

void SpriteBatch::bottomAnchoredStretched(const Atlas& atlas, int frame, Vec2 pos, float length, Vec2 scale) {
    const Frame& f = atlas.frame(frame);
    QuadParams q;
    q.pos = pos;
    q.anchorPx = Vec2(std::fabs(f.x1 - f.x0) * 0.5f, 0.0f);
    q.scale = Vec2(scale.x, (length / (k_ * std::fabs(f.y1 - f.y0))) * scale.y);
    anchored(atlas, frame, q);
}

void SpriteBatch::stretchedBetween(const Atlas& atlas, int frame, Vec2 from, Vec2 to) {
    const Frame& f = atlas.frame(frame);
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    QuadParams q;
    q.pos = from;
    q.anchorPx = Vec2(0.0f, std::fabs(f.y1 - f.y0) * 0.5f);
    q.rotation = static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(dx)));
    q.scale = Vec2(std::sqrt(dy * dy + dx * dx) / (k_ * std::fabs(f.x1 - f.x0)), 1.0f);
    anchored(atlas, frame, q);
}

void SpriteBatch::centeredSrcRect(const Atlas& atlas, float x0, float x1, float yTop, float yBottom, Vec2 pos) {
    const float hw = k_ * 0.5f * std::fabs(x1 - x0);
    const float hh = k_ * 0.5f * std::fabs(yBottom - yTop);
    const Vec2 c[4] = {Vec2(pos.x - hw, pos.y - hh), Vec2(pos.x + hw, pos.y - hh), Vec2(pos.x - hw, pos.y + hh),
                       Vec2(pos.x + hw, pos.y + hh)};
    const float tw = static_cast<float>(atlas.width);
    const float th = static_cast<float>(atlas.height);
    emit(atlas, c, x0 / tw, x1 / tw, yTop / th, yBottom / th);
}

void SpriteBatch::rect(const Atlas& atlas, int frame, float left, float bottom, float right, float top) {
    const Frame& f = atlas.frame(frame);
    const Vec2 c[4] = {Vec2(left, bottom), Vec2(right, bottom), Vec2(left, top), Vec2(right, top)};
    const float tw = static_cast<float>(atlas.width);
    const float th = static_cast<float>(atlas.height);
    emit(atlas, c, f.x0 / tw, f.x1 / tw, f.y0 / th, f.y1 / th);
}

void SpriteBatch::strip(const Atlas& atlas, int frame, const Vec2* points, int count, float halfWidth,
                        const bool* segmentMask) {
    if (count < 2) return;
    const Frame& f = atlas.frame(frame);
    const float tw = static_cast<float>(atlas.width);
    const float th = static_cast<float>(atlas.height);
    // FUN_000bbd84: the V range is inset by half a pixel at both ends.
    const float u0 = f.x0 / tw;
    const float u1 = f.x1 / tw;
    const float vBottom = (f.y1 - 0.5f) / th;
    const float vTop = (f.y0 + 0.5f) / th;
    // RopeRenderUtils::CalculateNormals: the left normal of each segment, the last point repeats it.
    constexpr float kMinLength = 1e-4f;
    std::vector<Vec2> normals(static_cast<std::size_t>(count));
    for (int i = 0; i + 1 < count; ++i) {
        const float dx = points[i + 1].x - points[i].x;
        const float dy = points[i + 1].y - points[i].y;
        const float len = std::sqrt(dy * dy + dx * dx);
        normals[static_cast<std::size_t>(i)] = len >= kMinLength ? Vec2(-(dy / len), dx / len) : Vec2(-0.0f, 1.0f);
    }
    normals[static_cast<std::size_t>(count - 1)] = normals[static_cast<std::size_t>(count - 2)];
    rlSetTexture(atlas.texture.id);
    rlBegin(RL_QUADS);
    for (int i = 0; i + 1 < count; ++i) {
        if (segmentMask != nullptr && !segmentMask[i]) continue;
        const Vec2 p0 = points[i];
        const Vec2 p1 = points[i + 1];
        const Vec2 n0(normals[static_cast<std::size_t>(i)].x * halfWidth, normals[static_cast<std::size_t>(i)].y * halfWidth);
        const Vec2 n1(normals[static_cast<std::size_t>(i + 1)].x * halfWidth, normals[static_cast<std::size_t>(i + 1)].y * halfWidth);
        // AddVertices: (p0 + n0, u1 vBottom), (p0 - n0, u0 vBottom), (p1 - n1, u0 vTop), (p1 + n1, u1 vTop)
        rlTexCoord2f(u1, vBottom); rlVertex2f(p0.x + n0.x, p0.y + n0.y);
        rlTexCoord2f(u0, vBottom); rlVertex2f(p0.x - n0.x, p0.y - n0.y);
        rlTexCoord2f(u0, vTop);    rlVertex2f(p1.x - n1.x, p1.y - n1.y);
        rlTexCoord2f(u1, vTop);    rlVertex2f(p1.x + n1.x, p1.y + n1.y);
    }
    rlEnd();
}

}  // namespace aa::platform
