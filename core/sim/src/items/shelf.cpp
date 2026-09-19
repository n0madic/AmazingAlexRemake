// Shelf (type 1): PhysicsObjectUtils::CreatePhysics case 1. Static platform; thickness = 10 % of the width,
// written as the original computes it: ((r + r) * 16) / 320, then SetAsBox(r, that * 0.5).
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

void createShelf(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const b2BodyDef def = itemBodyDef(obj, mode, b2_staticBody);
    const float thickness = ((r + r) * 16.0f) / 320.0f;
    const int body = addBody(obj, world, def);
    world.addBox(body, r, thickness * 0.5f, fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kStatic));
    if (mode == PhysicsMode::SetUp) {
        world.addBoxAt(body, r, r / 3.5f, Vec2(0.0f, 0.0f), 0.0f, selectionDef());
    }
}

}  // namespace aa::sim::items
