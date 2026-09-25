#include "aa/platform/ui_renderer.h"

#include <rlgl.h>

#include <cmath>

namespace aa::platform {

UiRenderer::~UiRenderer() { unload(); }

void UiRenderer::unload() {
    for (auto& [name, tex] : textures_) {
        if (tex.id != 0) UnloadTexture(tex);
    }
    textures_.clear();
}

void UiRenderer::purgeThumbnails() {
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (aa::ui::ResourceProxy::isThumbnail(it->first)) {
            if (it->second.id != 0) UnloadTexture(it->second);
            it = textures_.erase(it);
        } else {
            ++it;
        }
    }
}

const Texture2D* UiRenderer::texture(const aa::data::UiSpriteSheet& sheet) {
    const std::string& key = sheet.name;
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        std::string path;
        if (aa::ui::ResourceProxy::isThumbnail(key)) path = sheet.texture;
        else path = resources_->profileDir() + "/" + sheet.texture;
        Texture2D tex = LoadTexture(path.c_str());
        if (tex.id != 0) SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        it = textures_.emplace(key, tex).first;
    }
    return it->second.id != 0 ? &it->second : nullptr;
}

void UiRenderer::setState(const aa::ui::DrawState& state) {
    rlDrawRenderBatchActive();
    state_ = state;
    applyClip();
}

void UiRenderer::endFrame() {
    rlDrawRenderBatchActive();
    if (clipping_) {
        EndScissorMode();
        clipping_ = false;
    }
}

void UiRenderer::applyClip() {
    if (clipping_) {
        EndScissorMode();
        clipping_ = false;
    }
    if (state_.clip.w >= 0.0f) {
        const int x = static_cast<int>(std::floor(state_.clip.x));
        const int y = static_cast<int>(std::floor(state_.clip.y));
        const int w = static_cast<int>(std::ceil(state_.clip.w));
        const int h = static_cast<int>(std::ceil(state_.clip.h));
        BeginScissorMode(x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
        clipping_ = true;
    }
}

namespace {

// The view transform of BaseDraw: screen = scale · (translate + pivot + R(angle) · (p − pivot)).
void pushViewMatrix(const aa::ui::DrawState& s) {
    rlPushMatrix();
    rlScalef(s.scale, s.scale, 1.0f);
    rlTranslatef(s.translate.x + s.pivot.x, s.translate.y + s.pivot.y, 0.0f);
    if (s.angle != 0.0f) rlRotatef(s.angle * 57.29578f, 0.0f, 0.0f, 1.0f);
    rlTranslatef(-s.pivot.x, -s.pivot.y, 0.0f);
}

}  // namespace

void UiRenderer::drawSprite(const aa::ui::SpriteRef& sprite, float x, float y, float w, float h) {
    if (!sprite.valid() || w <= 0.0f || h <= 0.0f) return;
    const Texture2D* tex = texture(*sprite.sheet);
    if (!tex) return;
    const aa::data::UiSprite& s = *sprite.sprite;
    const float tw = static_cast<float>(tex->width);
    const float th = static_cast<float>(tex->height);
    const float u0 = static_cast<float>(s.x) / tw;
    const float v0 = static_cast<float>(s.y) / th;
    const float u1 = static_cast<float>(s.x + s.w) / tw;
    const float v1 = static_cast<float>(s.y + s.h) / th;
    const float alpha = state_.alpha < 0.0f ? 0.0f : (state_.alpha > 1.0f ? 1.0f : state_.alpha);
    const unsigned char a = static_cast<unsigned char>(std::lround(static_cast<float>(tint_.a) * alpha));
    pushViewMatrix(state_);
    rlSetTexture(tex->id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint_.r, tint_.g, tint_.b, a);
    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlTexCoord2f(u0, v0);
    rlVertex2f(x, y);
    rlTexCoord2f(u0, v1);
    rlVertex2f(x, y + h);
    rlTexCoord2f(u1, v1);
    rlVertex2f(x + w, y + h);
    rlTexCoord2f(u1, v0);
    rlVertex2f(x + w, y);
    rlEnd();
    rlSetTexture(0);
    rlPopMatrix();
}

void UiRenderer::drawColorRect(const aa::ui::Rect& rect, aa::ui::Color color) {
    if (rect.w <= 0.0f || rect.h <= 0.0f) return;
    // DrawBackgroundColor runs with the view alpha slot at 0: the colour's own alpha applies.
    pushViewMatrix(state_);
    rlSetTexture(0);
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlVertex2f(rect.x, rect.y);
    rlVertex2f(rect.x, rect.y + rect.h);
    rlVertex2f(rect.x + rect.w, rect.y + rect.h);
    rlVertex2f(rect.x + rect.w, rect.y);
    rlEnd();
    rlPopMatrix();
}

}  // namespace aa::platform
