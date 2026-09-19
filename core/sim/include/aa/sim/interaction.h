// st::GameItemUtils' set-up-mode operations (docs/05 §5): what the controller calls while an item is
// picked, dragged, rotated, flipped and dropped. Ports of SetPos / UpdatePos / UpdateAngle /
// ManipulationStarted / ManipulationEnded / Flip / GetConstrainedPos / CopySetUpData / GetRelatedItem /
// RemoveRelatedItems [verified: decompile + disassembly, docs/05 §5]. Objects are addressed by index
// because several of these add items (the truck / trapdoor / helicopter controller split).
#pragma once

#include <functional>
#include <vector>

#include "aa/sim/action.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/templates.h"
#include "aa/sim/toolbox.h"
#include "aa/sim/touch.h"
#include "aa/sim/world_state.h"

namespace aa::sim {

// GameItemUtils::SetPos: every body translated by (pos − object position), angles kept.
void setItemPos(WorldState& state, PhysicsWorld& world, int object, Vec2 pos);

// GameItemUtils::UpdatePos: the drag. Non-special types: the picked body follows touch.targetPos, the
// object angle becomes touch.angleCurrent, CalculateSnap within 1.1·halfSize (pipe ends only while
// `snapping`), the snap applied, every body moved by the resulting delta, attached ropes follow or detach
// (FUN_000b8654), and a ball over a slingshot pouch drags the pouch. Ropes, slingshots and zip lines have
// their own paths (RopeUtils / SlingshotUtils / ZipLineUtils::UpdatePos).
void updateItemPos(WorldState& state, PhysicsWorld& world, int object, const TouchState& touch, bool snapping,
                   ActionQueue& queue);

// GameItemUtils::UpdateAngle: every body rotated about the object position by (angle − object angle);
// attached ropes follow. A rope only stores the angle.
void updateItemAngle(WorldState& state, PhysicsWorld& world, int object, float angle);

// GameItemUtils::ManipulationStarted: attached non-rope neighbours are detached (ropes stay attached and
// follow); RopeUtils::ManipulationStarted for ropes (by the grabbed body).
void manipulationStarted(WorldState& state, PhysicsWorld& world, int object, int bodyIndex);

// GameItemUtils::ManipulationEnded: y clamped to ≥ 0, the per-type ending (truck / trapdoor / helicopter
// controller split into its own item, zip line / rope re-laid), RopeUtils::ManipulationEnded for every
// attached rope, then AttachToNearbyItems.
void manipulationEnded(WorldState& state, PhysicsWorld& world, const TemplateTable& templates, int object);

// GameItemUtils::Flip: RemoveAllAttachments, scale.x negated, the bodies re-created in set-up mode,
// attachment sounds; scissors rotate by π instead.
void flipItem(WorldState& state, PhysicsWorld& world, int object, ActionQueue& queue);

// GameItemUtils::GetConstrainedPos: only ropes constrain (RopeUtils::GetConstrainedPos).
Vec2 constrainedPos(const WorldState& state, int object, int bodyIndex, Vec2 pos);

// GameItemUtils::CopySetUpData: the per-type set-up fields (end vectors, book colour, billboard hint).
void copySetUpData(GameItem& dst, const GameItem& src);

// GameItemUtils::GetRelatedItem: the paired controller / lever / truck / trapdoor / helicopter handle,
// 0 when none.
int relatedItemHandle(const GameItem& item);

// GameItemUtils::RemoveRelatedItems: a removed controller or lever puts its truck / trapdoor back into
// the toolbox; the related item is invalidated. Returns the related handle (0 when none).
int removeRelatedItems(WorldState& state, PhysicsWorld& world, int itemIndex, Toolbox& toolbox, Vec2 screenPx);
// FUN_000c3478 (the completion path): the object's type goes back into the toolbox strip at its screen
// position when object_flags::kReturnsToToolbox is set, then the item is invalidated.
void returnObjectToToolbox(WorldState& state, PhysicsWorld& world, int object, Toolbox& toolbox, Vec2 screenPx);
// FUN_000c9590 (the completion path, before returnObjectToToolbox): every rope attached to the object is
// detached (snapped-only records are unsnapped); a rope left with no attachment on either end is returned to
// the toolbox / invalidated the same way; `freedRopes` (when given) receives those ropes' object indices
// — the sandbox toolbox step moves them into the strip with the object (mode 5).
void detachRopesFromRemovedObject(WorldState& state, PhysicsWorld& world, int object, Toolbox& toolbox,
                                  const std::function<Vec2(Vec2)>& worldToScreen, std::vector<int>* freedRopes = nullptr);

// FUN_000b8654: attached ropes follow the object (UpdatePosFromAttachedObjects) or detach when the end
// would be constrained (over-stretched).
void updateAttachedRopes(WorldState& state, PhysicsWorld& world, int object);

// FUN_000b85d0: RopeUtils::ManipulationEnded for every attached rope.
void endAttachedRopeManipulation(WorldState& state, PhysicsWorld& world, int object);

}  // namespace aa::sim
