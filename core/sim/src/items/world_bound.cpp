// st::WorldBoundUtils::CreatePhysics (docs/04-physics.md §6): floor, ceiling and walls as static boxes.
// The Treehouse variant (item state word 1, backgroundIndex 3) has a floor hole for x in [1.27875, 2.13136].
#include "aa/sim/float_bits.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {

constexpr float kFloorY = -0.5f;
constexpr float kHalfHeight = 0.5f;
constexpr float kCenterX = 1.705f;
constexpr float kFloorHalfWidth = 2.705f;
constexpr float kCeilingY = 30.5f;
constexpr float kWallY = 15.0f;
constexpr float kWallHalfHeight = 15.0f;
constexpr float kWallHalfWidth = 0.5f;
constexpr float kRightWallX = 3.91f;
constexpr float kLeftWallX = -0.5f;
constexpr float kHoleFloorHalfWidth = 1.639375f;
constexpr int kTreehouseVariant = 1;   // WorldBoundUtils::GetTypeForBackgroundIndex

void addBound(PhysicsObject& obj, PhysicsWorld& world, b2BodyDef& def, Vec2 position, float hx, float hy,
              const b2FixtureDef& fd) {
    def.position = position;
    const int body = addBody(obj, world, def);
    world.addBox(body, hx, hy, fd);
}

}  // namespace

void createWorldBound(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode /*mode*/) {
    // Static in both modes (the only item besides the selection area that is).
    b2BodyDef def;
    def.type = b2_staticBody;
    const b2FixtureDef fd = fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kStatic);
    if (item.stateWord == kTreehouseVariant) {
        // Two floor pieces around the hole; the literals are the binary's own bit patterns.
        addBound(obj, world, def, Vec2(floatFromBits(0xbeb8a3d6u), kFloorY), kHoleFloorHalfWidth, kHalfHeight, fd);
        addBound(obj, world, def, Vec2(floatFromBits(0x407151ecu), kFloorY), kHoleFloorHalfWidth, kHalfHeight, fd);
    } else {
        addBound(obj, world, def, Vec2(kCenterX, kFloorY), kFloorHalfWidth, kHalfHeight, fd);
    }
    addBound(obj, world, def, Vec2(kCenterX, kCeilingY), kCenterX, kHalfHeight, fd);
    addBound(obj, world, def, Vec2(kRightWallX, kWallY), kWallHalfWidth, kWallHalfHeight, fd);
    addBound(obj, world, def, Vec2(kLeftWallX, kWallY), kWallHalfWidth, kWallHalfHeight, fd);
}

}  // namespace aa::sim::items
