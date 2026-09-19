// Physics object templates (st::PhysicsObjectTemplates, docs/04-physics.md §4, §7): per item type the
// flags byte, the half-size derived from the atlas frame and the attachment points. Filled once at start-up
// from the GameItems frame table, exactly as st::PhysicsObjectsUtils::InitializePhysicsObjectTemplates.
#pragma once

#include "aa/sim/frame_table.h"
#include "aa/sim/types.h"

#include <array>
#include <cstdint>

namespace aa::sim {

// One attachment point (docs/04 §7); the per-object record adds the connection state (world_state.h).
struct AttachmentPoint {
    Vec2 pos{0.0f, 0.0f};    // local position, multiplied by the object scale at run time
    Vec2 dir{0.0f, 0.0f};    // local direction, for alignment
    int kind = 0;            // attachment_kind
    int mask = 0;            // kinds it may connect to
    int body = 0;            // index of the body owning the point
    bool positionOnly = false;   // snap by distance alone (else the directions must be within 45°)
};

// PhysicsObject+0x90..0x92: one byte per body (the first three), bit 0 = selectable by the pick query,
// bit 1 = the item shows its gizmos (rotation ring / flip button) after a tap or drop. Both set by
// default; the initialiser clears bit 1 for the fish bowl, the rope bodies, the goal star, the slingshot
// pouch body and the zip-line anchors [verified: InitializePhysicsObjectTemplates byte writes at +0x90..].
namespace body_flags {
constexpr std::uint8_t kSelectable = 0x01;
constexpr std::uint8_t kGizmos = 0x02;
constexpr std::uint8_t kDefault = 0x03;
}

struct PhysicsObjectTemplate {
    static constexpr int kMaxAttachments = 2;
    static constexpr int kBodyFlagBytes = 3;

    ItemType type = ItemType::None;
    std::uint8_t flags = object_flags::kTemplateDefault;
    float halfSize = 1.0f;        // "r" of docs/03: half-size or radius in metres (template default 1.0)
    int attachmentCount = 0;
    std::array<AttachmentPoint, kMaxAttachments> attachments{};
    std::array<std::uint8_t, kBodyFlagBytes> bodyFlags{{body_flags::kDefault, body_flags::kDefault, body_flags::kDefault}};
};

using TemplateTable = std::array<PhysicsObjectTemplate, kItemTypeCount>;

// Build the table from the GameItems atlas frames (150 frames, file order; docs/03 §4).
// halfSize = trunc(|frame extent| − 2) · 0.5 · 3.41 · (1/1024) · tweak, evaluated in float in that order.
TemplateTable initTemplates(const FrameTable& frames);

// Frame indices the initialiser reads (GameItems.plist order, docs/03 §4), for the loaders' validation.
constexpr int kTemplateFrameCount = 150;

}  // namespace aa::sim
