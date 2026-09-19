// Hook (type 8): PhysicsObjectUtils::CreatePhysics case 8. A body created at the origin and moved by
// SetTransform; in simulation one non-collidable circle (rope ends attach to the body), in set-up a
// selection circle plus a second selection-category circle of the hook's own radius that also carries the
// return-area mask bit.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

void createHook(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    b2BodyDef def;
    def.type = mode == PhysicsMode::SetUp ? b2_dynamicBody : b2_staticBody;
    const int body = addBody(obj, world, def);
    world.setTransform(body, obj.position, obj.angle);
    const Vec2 origin(0.0f, 0.0f);
    if (mode == PhysicsMode::Simulation) {
        world.addCircle(body, r, origin, fixtureDef(1.0f, kDefaultFriction, 0.0f, filters::kNonCollidable));
    } else {
        world.addCircle(body, kMinSelectionRadius, origin, selectionDef());
        b2Filter filter = filters::kSelection;
        filter.maskBits = filters::kReturnAreaBound.maskBits;
        world.addCircle(body, r, origin, fixtureDef(0.0f, kDefaultFriction, 0.0f, filter));
    }
}

}  // namespace aa::sim::items
