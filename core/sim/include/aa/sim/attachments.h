// st::AttachmentUtils (docs/04 §7): the world position of an attachment point, the joint `Attach` /
// `GamePhysicsUtils::CreateAttachments` create between two attached points, and the set-up-mode snapping
// (CalculateSnap / Snap / Unsnap / Attach / Detach / AttachToNearbyItems) [verified against the decompile
// and the disassembly, docs/05 §5]. Attachment records live in the PhysicsObjects; joints are PhysicsWorld
// slots; `GameItemUtils::AttachmentChanged` keeps the rope items in step.
#pragma once

#include "aa/sim/action.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/world_state.h"

namespace aa::sim {

// AttachmentUtils::GetPosWS: the point's local position (scaled by the object scale) rotated by its
// body's angle and added to the body position.
Vec2 attachmentPosWS(const PhysicsObject& obj, const PhysicsWorld& world, int point);

// AttachmentUtils::CreateJoint: a revolute joint between the point's body and the other record's body
// (anchors at the scaled point positions, motor on with 0.01 N·m), stored in both records; then
// GameItemUtils::AttachmentChanged for both items (rope end-to-end joint, selectable bytes, masses).
void createAttachmentJoint(WorldState& state, PhysicsWorld& world, int object, int point);

// GamePhysicsUtils::CreateAttachments: joints for every attached record without one (collection order),
// then RopeUtils::UpdatePosFromAttachedObjects for every rope (item order).
void createAttachments(WorldState& state, PhysicsWorld& world);

// AttachmentUtils::CalculateSnap's result (st::SnapResult, 0x1c bytes).
struct SnapResult {
    bool found = false;
    Vec2 position{0.0f, 0.0f};   // where the object goes (the input position when nothing snaps)
    float angle = 0.0f;          // rotation delta for aligned kinds
    int point = -1;              // the object's attachment point
    int otherObject = -1;
    int otherPoint = -1;
};

// AttachmentUtils::CalculateSnap: for each free point of `obj` (in order), an AABB query of `radius`
// around `queryCenter` for objects with a free, kind-compatible point within 0.08 m of where the point
// would be with the object at `position`; the first hit wins. Ghost copies and fully attached objects are
// skipped; a point snapped to another object is not a candidate. A position-only hit moves the object by
// the world-space point offset; an aligned hit (pipe ends) also carries the rotation delta and places the
// object by the point's local position rotated to the new angle.
SnapResult calculateSnap(const WorldState& state, const PhysicsWorld& world, const PhysicsObject& obj,
                         Vec2 position, Vec2 queryCenter, float radius);

// AttachmentUtils::Snap: both records → snapped (state 1) unless either already is.
void snap(WorldState& state, int object, int point, int otherObject, int otherPoint);
// AttachmentUtils::Unsnap: both records → free (whatever their state).
void unsnap(WorldState& state, int object, int point);
// AttachmentUtils::UnsnapAllNotAttached: Unsnap every record that is not attached (state 2).
void unsnapAllNotAttached(WorldState& state, int object);
// AttachmentUtils::Attach: Unsnap, both records → attached (state 2), CreateJoint.
void attach(WorldState& state, PhysicsWorld& world, int object, int point, int otherObject, int otherPoint);
// AttachmentUtils::Detach: destroy the joint, both records → free, AttachmentChanged for both items.
void detach(WorldState& state, PhysicsWorld& world, int object, int point);
// AttachmentUtils::RemoveAllAttachments: Detach the attached records, Unsnap the snapped ones.
void removeAllAttachments(WorldState& state, PhysicsWorld& world, int object);
// AttachmentUtils::AttachToNearbyItems: for every not-attached, position-only point: Unsnap, then a
// 0.07 m query around the point's world position; the first candidate gets Attach (one per call).
void attachToNearbyItems(WorldState& state, PhysicsWorld& world, int object);
// AttachmentUtils::PlayAttachmentSounds: queues sound actions (kind 1/2/4: 0x40 attach / 0x41 detach,
// kind 8: 0x42 / 0x43) at the object position for every record whose state changed between `before`
// (a copy taken earlier in the frame) and `now` from/to free.
void playAttachmentSounds(const PhysicsObject& now, const PhysicsObject& before, ActionQueue& queue);
// GameItemUtils::AttachmentChanged: only ropes react (RopeUtils::AttachmentChanged).
void attachmentChanged(WorldState& state, PhysicsWorld& world, int object, int point);

// WorldStateUtils::InvalidateItem: RemoveAllAttachments, DestroyPhysics, base flag cleared — the object
// stays in the collection until WorldState::removeInvalidItems runs at the end of the frame.
void invalidateItem(WorldState& state, PhysicsWorld& world, int itemIndex);

}  // namespace aa::sim
