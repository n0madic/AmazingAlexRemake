// Billboard (type 24): st::BillboardUtils::CreatePhysics. A placement hint with a selection box only
// (SetAsBox(r, r), r = 0.2) in both modes; static in simulation. The hint picture (itemData) is a render
// matter (docs/03 §24).
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

void createBillboard(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const b2BodyDef def = itemBodyDef(obj, mode, b2_staticBody);
    const int body = addBody(obj, world, def);
    world.addBox(body, obj.halfSize, obj.halfSize, selectionDef());
}

}  // namespace aa::sim::items
