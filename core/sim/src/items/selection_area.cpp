// SelectionArea (40): PhysicsObjectUtils::CreatePhysics case 40 (docs/03 §39) — in set-up mode only, a
// static 5.41 × 1 m box below the floor whose category is ReturnAreaBound and whose mask is Static's
// (0xFFFF): the physical toolbox return area. Nothing in simulation.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

void createSelectionArea(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    if (mode != PhysicsMode::SetUp) return;
    b2BodyDef def;
    def.type = b2_staticBody;
    def.position = Vec2(1.705f, -0.5f);
    const int body = addBody(obj, world, def);
    b2Filter filter = filters::kReturnAreaBound;
    filter.maskBits = filters::kStatic.maskBits;
    filter.groupIndex = filters::kStatic.groupIndex;
    world.addBox(body, 2.705f, 0.5f, fixtureDef(0.0f, kDefaultFriction, 0.0f, filter));
}

}  // namespace aa::sim::items
