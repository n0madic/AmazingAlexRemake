// Cardboard boxes and the fish bowl through the original's box helper (FUN_000de380, docs/04 §9):
// dynamic body, SetAsBox(r, r · aspect), friction 0.6, filter Dynamic|0x10 with the return-area bit in the
// mask. No selection fixture: the box itself is large enough to pick.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {

constexpr float kFriction = 0.6f;

struct BoxParams {
    float aspect;    // hy = r · aspect
    float density;
};

BoxParams paramsFor(ItemType type) {
    switch (type) {
    case ItemType::CardboardBoxMedium:
    case ItemType::CardboardBoxSmall: return {0.96f, 20.0f};
    case ItemType::FishBowl: return {0.88f, 100.0f};
    default: throw NotImplemented(type);
    }
}

}  // namespace

void createBox(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode /*mode*/) {
    const BoxParams p = paramsFor(obj.type);
    const float r = obj.halfSize;
    b2BodyDef def;
    def.type = b2_dynamicBody;   // dynamic in both modes (the helper takes no mode)
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);
    b2Filter filter = filters::dynamicPlus();
    filter.maskBits = static_cast<uint16>(filter.maskBits | filters::kReturnAreaBit);
    world.addBox(body, r, r * p.aspect, fixtureDef(p.density, kFriction, 0.0f, filter));
}

}  // namespace aa::sim::items
