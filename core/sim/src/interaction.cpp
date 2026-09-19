// st::GameItemUtils set-up operations (interaction.h) with the per-type ManipulationEnded of
// TruckUtils / TrapdoorUtils / HelicopterUtils (the built-in controller split).
#include "aa/sim/interaction.h"

#include "aa/sim/animations.h"
#include "aa/sim/attachments.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"

namespace aa::sim {

namespace {

// The 0.1 m² threshold GameItemUtils::UpdatePos uses to decide that a ball sits on a slingshot pouch
// (a double compare: 0.01 as 0x3f847ae147ae147c).
constexpr double kPouchDistanceSquared = 0.01;

// TruckUtils / TrapdoorUtils / HelicopterUtils::ManipulationEnded: the first drop of an item taken from
// the toolbox destroys the built-in controller body and creates the controller / lever item at that
// body's position, pairing the two by handle through stateWord [verified].
void splitBuiltInController(WorldState& state, PhysicsWorld& world, const TemplateTable& templates, int object,
                            int controllerBody, ItemType controllerType) {
    const int itemIndex = state.handles.lookup(state.objects[static_cast<std::size_t>(object)].handle);
    if (itemIndex < 0 || !state.items[static_cast<std::size_t>(itemIndex)].builtInController) return;
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    if (controllerBody >= obj.bodyCount) return;
    const int slot = obj.bodies[static_cast<std::size_t>(controllerBody)];
    const b2Body* body = world.body(slot);
    const Vec2 at(body->GetPosition().x, body->GetPosition().y);
    world.destroyBody(slot);
    obj.bodyCount = obj.bodyCount - 1;
    obj.bodies[static_cast<std::size_t>(controllerBody)] = -1;
    const int ctrlItem = state.addNewItem(templates, controllerType, at, 0.0f, false);
    const int ctrlObject = state.items[static_cast<std::size_t>(ctrlItem)].objectIndex;
    createPhysics(state.objects[static_cast<std::size_t>(ctrlObject)], state.items[static_cast<std::size_t>(ctrlItem)], world,
                  PhysicsMode::SetUp);
    GameItem& owner = state.items[static_cast<std::size_t>(itemIndex)];
    GameItem& ctrl = state.items[static_cast<std::size_t>(ctrlItem)];
    ctrl.stateWord = owner.handle;
    owner.builtInController = false;
    owner.stateWord = ctrl.handle;
}

}  // namespace

void setItemPos(WorldState& state, PhysicsWorld& world, int object, Vec2 pos) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    const float dx = pos.x - obj.position.x;
    const float dy = pos.y - obj.position.y;
    for (int k = 0; k < obj.bodyCount; ++k) {
        const int slot = obj.bodies[static_cast<std::size_t>(k)];
        const b2Body* b = world.body(slot);
        if (!b) continue;
        world.setTransform(slot, Vec2(dx + b->GetPosition().x, dy + b->GetPosition().y), b->GetAngle());
    }
    obj.position = pos;
}

void updateAttachedRopes(WorldState& state, PhysicsWorld& world, int object) {
    const int n = state.objects[static_cast<std::size_t>(object)].attachmentCount;
    for (int k = 0; k < n; ++k) {
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
        const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
        if (rec.state != attachment_state::kAttached) continue;
        const int ropeObject = rec.otherObject;
        PhysicsObject& rope = state.objects[static_cast<std::size_t>(ropeObject)];
        GameItem& ropeItem = state.itemOf(rope);
        const Vec2 posWS = attachmentPosWS(obj, world, k);
        const Vec2 constrained = items::ropeConstrainedPos(ropeItem, rope, rec.otherPoint + 1, posWS);
        const float d2 = (constrained.y - posWS.y) * (constrained.y - posWS.y) + (constrained.x - posWS.x) * (constrained.x - posWS.x);
        if (d2 <= 0.0001f) {   // st::Epsilon
            items::updateRopeFromAttachedObjects(state, world, ropeItem, rope);
        } else {
            detach(state, world, object, k);
        }
    }
}

void endAttachedRopeManipulation(WorldState& state, PhysicsWorld& world, int object) {
    const int n = state.objects[static_cast<std::size_t>(object)].attachmentCount;
    for (int k = 0; k < n; ++k) {
        const AttachmentRecord& rec = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)];
        if (rec.state != attachment_state::kAttached) continue;
        PhysicsObject& rope = state.objects[static_cast<std::size_t>(rec.otherObject)];
        items::ropeManipulationEnded(state, world, state.itemOf(rope), rope);
    }
}

void updateItemPos(WorldState& state, PhysicsWorld& world, int object, const TouchState& touch, bool snapping,
                   ActionQueue& queue) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    GameItem& item = state.itemOf(obj);
    if (obj.type == ItemType::Slingshot) {
        items::slingshotUpdatePos(item, obj, world, touch.bodyIndex, touch.targetPos, queue);
        return;
    }
    if (obj.type == ItemType::ZipLine) {
        items::zipLineUpdatePos(state, world, item, obj, touch.bodyIndex, touch.targetPos);
        updateAttachedRopes(state, world, object);
        return;
    }
    if (obj.type == ItemType::Rope) {
        const Vec2 v = touch.dragVelocity;
        if (v.y * v.y + v.x * v.x <= 0.0f) return;
        items::ropeUpdatePos(state, world, item, obj, touch.bodyIndex, touch.targetPos, v);
        return;
    }
    const b2Body* grabbed = world.body(obj.bodies[static_cast<std::size_t>(touch.bodyIndex)]);
    const float oldAngle = obj.angle;
    const Vec2 newPos((touch.targetPos.x - grabbed->GetPosition().x) + obj.position.x,
                      (touch.targetPos.y - grabbed->GetPosition().y) + obj.position.y);
    obj.angle = touch.angleCurrent;
    SnapResult r = calculateSnap(state, world, obj, newPos, newPos, obj.halfSize * 1.1f);
    bool applySnap = false;
    if (!snapping) {
        if (r.found) {
            if (obj.attachments[static_cast<std::size_t>(r.point)].point.kind != attachment_kind::kPipeEnd) {
                applySnap = true;
            } else {
                r.position = newPos;
                r.found = false;
                r.angle = 0.0f;
            }
        }
    } else if (r.found) {
        applySnap = true;
    }
    if (applySnap) {
        if (obj.attachments[static_cast<std::size_t>(r.point)].state == attachment_state::kSnapped) {
            unsnap(state, object, r.point);
        } else if (state.objects[static_cast<std::size_t>(r.otherObject)].attachments[static_cast<std::size_t>(r.otherPoint)].state ==
                   attachment_state::kSnapped) {
            unsnap(state, r.otherObject, r.otherPoint);
        }
        snap(state, object, r.point, r.otherObject, r.otherPoint);
    } else {
        unsnapAllNotAttached(state, object);
    }
    PhysicsObject& o = state.objects[static_cast<std::size_t>(object)];
    const Vec2 old = o.position;
    o.position = r.position;
    o.angle = o.angle + r.angle;
    const float dx = r.position.x - old.x;
    const float dy = r.position.y - old.y;
    const float dAngle = o.angle - oldAngle;
    for (int k = 0; k < o.bodyCount; ++k) {
        const int slot = o.bodies[static_cast<std::size_t>(k)];
        const b2Body* b = world.body(slot);
        if (!b) continue;
        world.setTransform(slot, Vec2(dx + b->GetPosition().x, dy + b->GetPosition().y), dAngle + b->GetAngle());
    }
    updateAttachedRopes(state, world, object);
    if (touch.slingshotObject != -1) {
        PhysicsObject& sling = state.objects[static_cast<std::size_t>(touch.slingshotObject)];
        GameItem& slingItem = state.itemOf(sling);
        const Vec2 pouch = items::slingshotPouchPosition(sling, slingItem);
        const float d2 = (pouch.y - touch.targetPos.y) * (pouch.y - touch.targetPos.y) +
                         (pouch.x - touch.targetPos.x) * (pouch.x - touch.targetPos.x);
        if (static_cast<double>(d2) < kPouchDistanceSquared) {
            items::slingshotUpdatePos(slingItem, sling, world, 1, touch.targetPos, queue);
        }
    }
}

void updateItemAngle(WorldState& state, PhysicsWorld& world, int object, float angle) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    if (obj.type == ItemType::Rope) {
        obj.angle = angle;
        return;
    }
    const float delta = angle - obj.angle;
    obj.angle = angle;
    for (int k = 0; k < obj.bodyCount; ++k) {
        const int slot = obj.bodies[static_cast<std::size_t>(k)];
        const b2Body* b = world.body(slot);
        if (!b) continue;
        const Vec2 d(b->GetPosition().x - obj.position.x, b->GetPosition().y - obj.position.y);
        const Vec2 r = rotate(delta, d);
        world.setTransform(slot, Vec2(r.x + obj.position.x, r.y + obj.position.y), delta + b->GetAngle());
    }
    updateAttachedRopes(state, world, object);
}

void manipulationStarted(WorldState& state, PhysicsWorld& world, int object, int bodyIndex) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    if (obj.type == ItemType::Rope) {
        items::ropeManipulationStarted(state, world, obj, bodyIndex);
        return;
    }
    for (int k = 0; k < obj.attachmentCount; ++k) {
        const AttachmentRecord& rec = state.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)];
        if (rec.state == attachment_state::kAttached && state.objects[static_cast<std::size_t>(rec.otherObject)].type != ItemType::Rope) {
            detach(state, world, object, k);
        }
    }
}

void manipulationEnded(WorldState& state, PhysicsWorld& world, const TemplateTable& templates, int object) {
    {
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
        if (obj.position.y < 0.0f) obj.position.y = 0.0f;
    }
    const ItemType type = state.objects[static_cast<std::size_t>(object)].type;
    switch (type) {
    case ItemType::Rope: {
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
        items::ropeManipulationEnded(state, world, state.itemOf(obj), obj);
        return;
    }
    case ItemType::RCTruck:
        splitBuiltInController(state, world, templates, object, 3, ItemType::RCController);
        endAttachedRopeManipulation(state, world, object);
        attachToNearbyItems(state, world, object);
        return;
    case ItemType::Trapdoor:
        splitBuiltInController(state, world, templates, object, 3, ItemType::TrapdoorLever);
        return;
    case ItemType::Helicopter:
        splitBuiltInController(state, world, templates, object, 1, ItemType::RCController);
        endAttachedRopeManipulation(state, world, object);
        attachToNearbyItems(state, world, object);
        return;
    case ItemType::ZipLine: {
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
        items::layoutZipLine(state.itemOf(obj), obj, world);
        attachToNearbyItems(state, world, object);
        return;
    }
    default:
        break;
    }
    endAttachedRopeManipulation(state, world, object);
    if (state.objects[static_cast<std::size_t>(object)].attachmentCount < 1) return;
    attachToNearbyItems(state, world, object);
}

void flipItem(WorldState& state, PhysicsWorld& world, int object, ActionQueue& queue) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    if (obj.type == ItemType::Scissors) {
        obj.angle = obj.angle + kGamePi;
        items::updateScissorsAngle(obj, world, state.itemOf(obj).cutAngle);
        return;
    }
    const PhysicsObject before = obj;
    removeAllAttachments(state, world, object);
    PhysicsObject& o = state.objects[static_cast<std::size_t>(object)];
    o.scale.x = -o.scale.x;
    world.destroyPhysics(o);
    createPhysics(o, state.itemOf(o), world, PhysicsMode::SetUp);
    playAttachmentSounds(o, before, queue);
}

Vec2 constrainedPos(const WorldState& state, int object, int bodyIndex, Vec2 pos) {
    const PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    if (obj.type != ItemType::Rope) return pos;
    return items::ropeConstrainedPos(state.itemOf(obj), obj, bodyIndex, pos);
}

void copySetUpData(GameItem& dst, const GameItem& src) {
    switch (src.type) {
    case ItemType::Rope:
    case ItemType::ZipLine:
    case ItemType::Slingshot:
        dst.endVector = src.endVector;
        break;
    case ItemType::Book:
    case ItemType::Billboard:
        dst.stateWord = src.stateWord;
        break;
    default:
        break;
    }
}

int relatedItemHandle(const GameItem& item) {
    switch (item.type) {
    case ItemType::RCTruck:
    case ItemType::Trapdoor:
    case ItemType::Helicopter:
    case ItemType::RCController:
    case ItemType::TrapdoorLever:
        return item.stateWord;
    default:
        return 0;
    }
}

int removeRelatedItems(WorldState& state, PhysicsWorld& world, int itemIndex, Toolbox& toolbox, Vec2 screenPx) {
    const GameItem& item = state.items[static_cast<std::size_t>(itemIndex)];
    const int related = relatedItemHandle(item);
    // A half without its counterpart (stateWord 0, a hand-edited level) has nothing to return to the strip.
    if (related != 0 && (item.type == ItemType::RCController || item.type == ItemType::TrapdoorLever)) {
        toolbox.addItem(Handle::typeOf(related), screenPx);
    }
    if (related != 0) {
        const int relatedIndex = state.handles.lookup(related);
        if (relatedIndex >= 0) invalidateItem(state, world, relatedIndex);
    }
    return related;
}

void returnObjectToToolbox(WorldState& state, PhysicsWorld& world, int object, Toolbox& toolbox, Vec2 screenPx) {
    const PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    const int itemIndex = state.handles.lookup(obj.handle);
    if (obj.flags & object_flags::kReturnsToToolbox) toolbox.addItem(obj.type, screenPx);
    if (itemIndex >= 0) invalidateItem(state, world, itemIndex);
}

void detachRopesFromRemovedObject(WorldState& state, PhysicsWorld& world, int object, Toolbox& toolbox,
                                  const std::function<Vec2(Vec2)>& worldToScreen, std::vector<int>* freedRopes) {
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(object)];
    for (int i = 0; i < obj.attachmentCount; ++i) {
        const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(i)];
        if (rec.state == attachment_state::kSnapped) {
            unsnap(state, object, i);
        } else if (rec.state == attachment_state::kAttached) {
            const int other = rec.otherObject;
            if (state.objects[static_cast<std::size_t>(other)].type != ItemType::Rope) continue;
            detach(state, world, object, i);
            const PhysicsObject& rope = state.objects[static_cast<std::size_t>(other)];
            if (rope.attachments[0].state == attachment_state::kFree && rope.attachments[1].state == attachment_state::kFree) {
                returnObjectToToolbox(state, world, other, toolbox, worldToScreen(rope.position));
                if (freedRopes) freedRopes->push_back(other);
            }
        }
    }
}

}  // namespace aa::sim
