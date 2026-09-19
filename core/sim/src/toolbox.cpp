// st::Toolbox / st::ToolboxUtils ports (toolbox.h). Every expression keeps the original's float shape.
#include "aa/sim/toolbox.h"

#include <cmath>
#include <limits>

namespace aa::sim {

ToolboxFrameSizes ToolboxFrameSizes::scaled(float factor) const {
    ToolboxFrameSizes s = *this;
    for (std::size_t i = 0; i < s.iconWidth.size(); ++i) {
        s.iconWidth[i] = iconWidth[i] * factor;
        s.iconHeight[i] = iconHeight[i] * factor;
    }
    s.buttonWidth = buttonWidth * factor;
    s.buttonHeight = buttonHeight * factor;
    s.endCapWidth = endCapWidth * factor;
    s.endCapHeight = endCapHeight * factor;
    return s;
}

ToolboxFrameSizes ToolboxFrameSizes::fromFrames(const FrameTable& uiElements) {
    ToolboxFrameSizes s;
    for (std::size_t t = 0; t < kItemTypeCount; ++t) {
        const Frame& f = uiElements.at(kToolboxIconFrame[t]);
        s.iconWidth[t] = std::fabs(f.x1 - f.x0);
        s.iconHeight[t] = std::fabs(f.y1 - f.y0);
    }
    const Frame& button = uiElements.at(toolbox_frames::kButton);
    const Frame& end = uiElements.at(toolbox_frames::kSlideEnd);
    s.buttonWidth = std::fabs(button.x1 - button.x0);
    s.buttonHeight = std::fabs(button.y1 - button.y0);
    s.endCapWidth = std::fabs(end.x1 - end.x0);
    s.endCapHeight = std::fabs(end.y1 - end.y0);
    return s;
}

float Toolbox::uniformToScreen(float u) const {
    // Toolbox::uniformToScreen: the widths of the whole slots before u plus the fraction of the next.
    const float fl = std::floor(u);
    int n = static_cast<int>(fl);
    if (n < 0) n = 0;
    if (n > slotCount) n = slotCount;
    float x = 0.0f;
    for (int i = 0; i < n; ++i) x = x + slots[static_cast<std::size_t>(i)].widthPx;
    if (n < slotCount) x = x + (u - static_cast<float>(n)) * slots[static_cast<std::size_t>(n)].widthPx;
    return x;
}

float Toolbox::screenToUniform(float px) const {
    // Toolbox::screenToUniform: px ≤ 0 → 0; otherwise the slot whose right edge passes px, fractional.
    if (!(px > 0.0f)) return 0.0f;
    float edge = slots[0].widthPx + 0.0f;
    for (int i = 0; i < slotCount; ++i) {
        if (!(edge < px)) {
            return static_cast<float>(i) + (1.0f - (edge - px) / slots[static_cast<std::size_t>(i)].widthPx);
        }
        if (i + 1 < slotCount) edge = edge + slots[static_cast<std::size_t>(i + 1)].widthPx;
    }
    return static_cast<float>(slotCount);
}

ScreenRect Toolbox::getToolboxRectangle() const {
    ScreenRect r;
    const float halfH = getHeight() * 1.3f * 0.5f;
    const float right = x + sizes.buttonWidth * 0.5f;
    const float w = getDisplayWidth() + sizes.buttonWidth;
    r.right = right;
    r.top = y + halfH;
    r.left = right - (w + sizes.endCapWidth);
    r.bottom = y - halfH;
    return r;
}

ScreenRect Toolbox::getDropRectangle() const {
    ScreenRect r;
    const float halfH = getHeight() * 1.3f * 0.5f;
    const float right = x + sizes.buttonWidth * 0.5f;
    r.right = right;
    r.top = halfH + y;
    r.left = right - (ejectLength + (sizes.endCapWidth + sizes.endCapWidth));
    r.bottom = y - halfH;
    return r;
}

ScreenRect Toolbox::getScrollRectangle() const {
    ScreenRect r;
    const float halfH = getHeight() * 1.3f * 0.5f;
    const float right = x - sizes.buttonWidth * 0.5f;
    r.right = right;
    r.left = right - (getDisplayWidth() + sizes.endCapWidth);
    r.top = halfH + y;
    r.bottom = y - halfH;
    return r;
}

ScreenRect Toolbox::getToolboxButtonRectangle() const {
    ScreenRect r;
    const float halfH = sizes.buttonHeight * 0.5f;
    const float halfW = sizes.buttonWidth * 0.5f;
    r.top = y + halfH;
    r.bottom = y - halfH;
    r.left = x - halfW;
    r.right = x + halfW;
    return r;
}

Vec2 Toolbox::getCenterForSlot(int slot) const {
    const float u = uniformToScreen(static_cast<float>(slot) + 0.5f);
    return Vec2((u - ejectLength) - scroll, 0.0f);
}

int Toolbox::getSlotForPos(Vec2 px) const {
    int best = -1;
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < slotCount; ++i) {
        const Vec2 c = getCenterForSlot(i);
        float d = px.x - (x + c.x);
        float tol = slots[static_cast<std::size_t>(i)].widthPx * 0.5f;
        if (d < 0.0f) d = -d;
        if (tol <= kSlotTolerancePx) tol = kSlotTolerancePx;
        if (d < bestDist && d < tol) {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

int Toolbox::getSlotIndexForType(ItemType type) const {
    for (int i = 0; i < slotCount; ++i) {
        if (slots[static_cast<std::size_t>(i)].type == type) return i;
    }
    return -1;
}

int Toolbox::getItemCount() const {
    int n = 0;
    for (int i = 0; i < slotCount; ++i) n += slots[static_cast<std::size_t>(i)].amount;
    return n;
}

ToolboxStripSlot Toolbox::makeSlot(ItemType type, int amount) const {
    // ToolboxStripSlot::ToolboxStripSlot(type, padding, amount): width = 2·padding + max(icon width, 50).
    ToolboxStripSlot s;
    s.type = type;
    s.amount = amount;
    float w = sizes.iconWidth[static_cast<std::size_t>(type)];
    if (!(w > kMinIconWidth)) w = kMinIconWidth;
    const float padding = getPaddingAroundItems();
    s.widthPx = static_cast<float>(static_cast<int>(padding + padding + w));
    s.heightPx = static_cast<float>(static_cast<int>(sizes.iconHeight[static_cast<std::size_t>(type)]));
    s.scale = 1.0f;
    return s;
}

void Toolbox::appendSlot(ItemType type, int amount) {
    if (slotCount >= kMaxSlots) return;
    slots[static_cast<std::size_t>(slotCount)] = makeSlot(type, amount);
    ++slotCount;
}

void Toolbox::addItem(ItemType type, Vec2 screenPx) {
    const int existing = getSlotIndexForType(type);
    if (existing < 0) {
        // A new slot where the item was dropped along the strip (floor(u + 0.5) — a double floor).
        const float u = screenToUniform((screenPx.x - (x + getDisplayLeft())) + scroll);
        const double fl = std::floor(static_cast<double>(u + 0.5f));
        int at = static_cast<int>(fl);
        if (at > slotCount) at = slotCount;
        if (at < 0) at = 0;
        if (slotCount >= kMaxSlots) return;
        for (int i = slotCount; i > at; --i) slots[static_cast<std::size_t>(i)] = slots[static_cast<std::size_t>(i - 1)];
        slots[static_cast<std::size_t>(at)] = makeSlot(type, 1);
        ++slotCount;
    } else {
        ToolboxStripSlot& s = slots[static_cast<std::size_t>(existing)];
        if (s.amount > -1) s.amount = s.amount + 1;
    }
    buttonState = 1;
}

void Toolbox::removeItem(ItemType type) {
    const int slot = getSlotIndexForType(type);
    if (slot < 0) return;   // the original indexes slot −1 here; nothing sensible happens
    ToolboxStripSlot& s = slots[static_cast<std::size_t>(slot)];
    if (s.amount == -1) return;
    s.amount = s.amount - 1;
    if (s.amount == 0) removeSlot(slot);
}

void Toolbox::removeSlot(int slot) {
    for (int i = slot + 1; i < slotCount; ++i) slots[static_cast<std::size_t>(i - 1)] = slots[static_cast<std::size_t>(i)];
    --slotCount;
}

}  // namespace aa::sim
