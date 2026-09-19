// The six balls: TennisBall, BowlingBall, SoccerBall, EightBall, Pinball, BouncyBall — all through the
// original's ball helper (FUN_000de4ec, docs/04-physics.md §9): dynamic body with angular damping 0.2,
// circle r·0.9 of density 1, a selection circle in set-up mode when the ball is small, then SetMassData.
// The per-type arguments (mass, inertia, friction, restitution, bullet) are the literals of the dispatch
// switch (PhysicsObjectUtils::CreatePhysics); the inertia of three balls is computed as (r·(r·m))·0.5.
#include "aa/sim/float_bits.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {

constexpr float kAngularDamping = 0.2f;
constexpr float kRadiusFactor = 0.9f;

struct BallParams {
    float mass;
    float inertia;
    float friction;
    float restitution;
    bool bullet;
};

float solidDiscInertia(float r, float mass) { return (r * (r * mass)) * 0.5f; }

BallParams paramsFor(ItemType type, float r) {
    switch (type) {
    case ItemType::TennisBall: return {0.057f, floatFromBits(0x387ba882u) /* 6e-5 */, 0.6f, 0.72f, true};
    case ItemType::BowlingBall: return {7.0f, solidDiscInertia(r, 7.0f), 0.5f, 0.2f, false};
    case ItemType::SoccerBall: return {0.41f, 0.004f, 0.5f, 0.78f, false};
    case ItemType::EightBall: return {0.2f, solidDiscInertia(r, 0.2f), 0.5f, 0.5f, true};
    case ItemType::Pinball: return {0.1f, solidDiscInertia(r, 0.1f), 0.5f, 0.3f, true};
    case ItemType::BouncyBall: return {0.03f, floatFromBits(0x37a7c5acu) /* 2e-5 */, 0.9f, 0.99f, true};
    default: throw NotImplemented(type);
    }
}

}  // namespace

void createBall(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const BallParams p = paramsFor(obj.type, r);
    b2BodyDef def = itemBodyDef(obj, mode, b2_dynamicBody);
    def.angularDamping = kAngularDamping;
    def.bullet = p.bullet;
    const int body = addBody(obj, world, def);
    world.addCircle(body, r * kRadiusFactor, Vec2(0.0f, 0.0f),
                    fixtureDef(1.0f, p.friction, p.restitution, filters::dynamicPlus()));
    if (mode == PhysicsMode::SetUp && r < kMinSelectionRadius) {
        world.addCircle(body, kMinSelectionRadius, Vec2(0.0f, 0.0f), selectionDef());
    }
    world.setMassData(body, p.mass, Vec2(0.0f, 0.0f), p.inertia);
}

}  // namespace aa::sim::items
