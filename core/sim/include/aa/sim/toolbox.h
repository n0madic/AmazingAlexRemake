// st::Toolbox / st::ToolboxUtils (docs/05 §5): the strip of item slots in screen space (native px, y up).
// Geometry: `x`/`y` is the centre of the round toolbox button at the right end; the slots extend to the
// left over `displayWidth` (= ejectLength − buttonWidth/2), scrolled by `scroll`. Button and slot sizes
// come from the UIElements frame table (ToolboxUtils::InitializeButtonSizesFromTextures) [verified].
#pragma once

#include "aa/sim/frame_table.h"
#include "aa/sim/types.h"

#include <array>

namespace aa::sim {

// The pixel sizes ToolboxUtils::InitializeButtonSizesFromTextures reads from the UIElements atlas: per
// item type the icon frame (`toolbox_<type>`), the toolbox button (frame 71 `toolbox_button`, 142 × 140 in
// the 2048X1536 profile) and the strip end cap (frame 76 `toolbox_slide_end`, 67 × 140). The original ships
// one atlas per resolution; the remake scales the one imported profile to the window (docs/11 §1).
struct ToolboxFrameSizes {
    std::array<float, kItemTypeCount> iconWidth{};    // DAT_002937ec[type]
    std::array<float, kItemTypeCount> iconHeight{};   // DAT_00293898[type]
    float buttonWidth = 142.0f;    // DAT_00293944 (ejectLengthFromDisplayLength adds half of it)
    float buttonHeight = 140.0f;   // DAT_00293948 (Toolbox::getHeight)
    float endCapWidth = 67.0f;     // DAT_0029394c (Toolbox::getPaddingAroundItems)
    float endCapHeight = 140.0f;   // DAT_00293950

    // Everything multiplied by `factor` (the profile → window scale).
    ToolboxFrameSizes scaled(float factor) const;
    // ToolboxUtils::InitializeButtonSizesFromTextures: the sizes read from the UIElements frame table
    // (kToolboxIconFrame per type, frame 71 button, frame 76 end cap).
    static ToolboxFrameSizes fromFrames(const FrameTable& uiElements);
};

// st::ItemInfos[type]+0x10: the UIElements frame of a type's strip icon (`Button<Item>`) [verified: the
// table after the static initialisers]. Types without an icon share frame 0 (8Ball) / 4 (BouncyBall).
constexpr std::array<int, kItemTypeCount> kToolboxIconFrame = {
    0, 30, 35, 5, 33, 1, 28, 7, 16, 27, 9, 10, 13, 21, 6, 3, 0, 23, 24, 12, 31, 25, 29, 14, 2, 19, 22, 20, 34, 11,
    17, 0, 18, 8, 32, 26, 0, 36, 0, 15, 4, 4, 37};
// UIElements frames of the strip itself (ToolboxRenderer::Render, renderBackground).
namespace toolbox_frames {
constexpr int kButton = 71;        // toolbox_button (142 × 140)
constexpr int kButtonIcon = 72;    // toolbox_button_icon
constexpr int kSlide = 75;         // toolbox_slide (43 × 136): tiled along the strip, 90 % of its width per tile
constexpr int kSlideEnd = 76;      // toolbox_slide_end (67 × 140)
constexpr int kCounterMin = 2;     // st::spriteIdFromAmount: `toolbox_<amount>` for 2..32
constexpr int kCounterMax = 32;
// DAT_00244850: the frame of `toolbox_<n>` for n = 2..32 (index n − 2).
constexpr std::array<int, 31> kCounter = {50, 61, 65, 66, 67, 68, 69, 70, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                          51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 62, 63, 64};
inline int counterFrame(int amount) {
    if (amount < kCounterMin || amount > kCounterMax) return -1;
    return kCounter[static_cast<std::size_t>(amount - kCounterMin)];
}
}  // namespace toolbox_frames

// A rectangle as the original returns it: {top, bottom, left, right}.
struct ScreenRect {
    float top = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
    bool contains(Vec2 p) const { return left < p.x && p.x < right && bottom < p.y && p.y < top; }
};

// st::ToolboxStripSlot (0x14 bytes): type, amount (−1 = unlimited), width = 2·padding + max(icon width, 50),
// height = icon height, scale (the appear animation).
struct ToolboxStripSlot {
    ItemType type = ItemType::None;
    int amount = 0;
    float widthPx = 64.0f;
    float heightPx = 64.0f;
    float scale = 1.0f;
};

struct Toolbox {
    static constexpr int kMaxSlots = 64;
    static constexpr float kMinIconWidth = 50.0f;
    static constexpr float kSlotTolerancePx = 50.0f;   // GetSlotForPos: at least ±50 px per slot

    bool open = true;              // +0
    float y = 0.0f;                // +4: button centre (native px, y up)
    float x = 0.0f;                // +8
    float scroll = 0.0f;           // +0xc: strip scroll offset (px, ≥ 0 when clamped)
    int buttonState = 1;           // +0x10: 1 = strip extended, 0 = retracted (toggled by the button)
    int slotCount = 0;             // +0x14
    bool buttonPressed = false;    // +0x18
    float buttonScale = 1.0f;      // +0x1c: 1 → 1.2 while pressed
    float ejectLength = 0.0f;      // +0x20: how far the strip is out (px)
    std::array<ToolboxStripSlot, kMaxSlots> slots{};
    ToolboxFrameSizes sizes;       // the runtime constants (not part of the original struct)

    // --- st::Toolbox ---------------------------------------------------------------------------
    float getHeight() const { return sizes.buttonHeight; }
    float getPaddingAroundItems() const { return sizes.endCapWidth; }
    float ejectLengthFromDisplayLength(float displayLength) const { return displayLength + sizes.buttonWidth * 0.5f; }
    float getDisplayWidth() const { return ejectLength - sizes.buttonWidth * 0.5f; }
    float getDisplayLeft() const { return -(getDisplayWidth() + sizes.buttonWidth * 0.5f); }
    float getWidth() const { return ejectLength + sizes.buttonWidth * 0.5f + sizes.endCapWidth; }
    bool isScrollable() const { return getDisplayWidth() < totalSlotsWidth(); }
    // Slot boundary coordinates: uniformToScreen(u) is the x offset (px) of the fractional slot position
    // u ∈ [0, slotCount]; screenToUniform is its inverse.
    float uniformToScreen(float u) const;
    float screenToUniform(float px) const;
    float totalSlotsWidth() const { return uniformToScreen(static_cast<float>(slotCount)) - uniformToScreen(0.0f); }
    ScreenRect getToolboxRectangle() const;
    ScreenRect getDropRectangle() const;
    ScreenRect getScrollRectangle() const;
    ScreenRect getToolboxButtonRectangle() const;

    // --- st::ToolboxUtils ----------------------------------------------------------------------
    // Slot centre relative to (x, y): (uniformToScreen(slot + 0.5) − ejectLength − scroll, 0).
    Vec2 getCenterForSlot(int slot) const;
    // The slot nearest to a screen point (x only) within max(width/2, 50) px, −1 when none.
    int getSlotForPos(Vec2 px) const;
    int getSlotIndexForType(ItemType type) const;
    int getItemCount() const;
    bool isOverToolbox(Vec2 px) const { return getToolboxRectangle().contains(px); }
    // AddItem: one more of `type`; a new slot is inserted at the strip position under `screenPx`.
    void addItem(ItemType type, Vec2 screenPx);
    // RemoveItem: one less of `type` (unlimited slots never empty); an emptied slot is removed.
    void removeItem(ItemType type);
    void removeSlot(int slot);
    void removeAllSlots() { slotCount = 0; }
    // ToolboxUtils::SetAll's per-type slot (FUN_000f0fc8) for a level's toolbox list: append a slot.
    void appendSlot(ItemType type, int amount);
    ToolboxStripSlot makeSlot(ItemType type, int amount) const;
};

}  // namespace aa::sim
