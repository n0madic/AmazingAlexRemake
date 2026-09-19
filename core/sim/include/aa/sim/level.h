// A parsed level file (st::LevelLayout, docs/02-level-format.md). Plain data, value semantics.
#pragma once

#include "aa/sim/types.h"

#include <array>
#include <string>
#include <vector>

namespace aa::sim {

// docs/02 §3.5: what one attachment point of the item is connected to.
struct LevelAttachment {
    int state = attachment_state::kFree;
    int objectIndex = -1;   // index into Level::items, -1 if free
    int index = -1;         // attachment-point index on the other item
};

// docs/02 §3: one pre-placed item (LevelLayout::Item).
struct LevelItem {
    static constexpr int kMaxAttachments = 2;

    ItemType type = ItemType::None;
    int handle = 0;               // st::Handle, type in the top 6 bits (docs/02 §3.1)
    Vec2 center{0.0f, 0.0f};      // world metres
    float angle = 0.0f;           // radians CCW
    int flags = 0;                // level_flags
    Vec2 ropeEnd{0.0f, 0.0f};     // second end point of Rope / Slingshot / ZipLine
    int itemData = 0;             // type-specific integer (docs/02 §3.3)
    int attachmentCount = 0;
    std::array<LevelAttachment, kMaxAttachments> attachments{};

    bool fixed() const { return (flags & level_flags::kFixed) != 0; }
    bool flipped() const { return (flags & level_flags::kFlipped) != 0; }
};

// docs/02 §4.
struct ToolboxSlot {
    ItemType type = ItemType::None;
    int amount = 0;
};

// docs/02 §5 (st::Goal): at most 9 targets; itemHandles2 holds the paired items of types 2 and 7.
struct Goal {
    static constexpr int kMaxTargets = 9;

    int type = 0;
    int itemCount = 0;
    std::array<int, kMaxTargets> itemHandles{};
    std::array<int, kMaxTargets> itemHandles2{};
    int timeLimit = 0;
    float height = 0.0f;
    float width = 0.0f;
    float angle = 0.0f;    // degrees (the one angle in degrees, docs/02 §6)
    bool negated = false;
};

// docs/02 §2. The importer normalises every level to version 7 (legacy type 40 converted, version-4 goals
// converted), so the loader has no legacy paths.
struct Level {
    int version = 7;
    std::string title;          // localisation id
    std::string description;    // localisation id of the tip
    std::string authorName;
    int backgroundIndex = 0;    // 0..3 = chapter; 3 selects the Treehouse world bound (docs/04 §6)
    std::vector<ToolboxSlot> toolbox;
    std::vector<LevelItem> items;
    Goal goal;
    int rewardId = 0;
    bool tested = false;
};

}  // namespace aa::sim
