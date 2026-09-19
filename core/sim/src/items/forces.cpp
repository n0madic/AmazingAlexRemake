// st::ApplyForcesUtils — the force actions of the simulation (18 ForceToItem, 19 ForceToRadius)
// [verified: decompile + disassembly]. Both write b2Body::m_force / m_torque the way b2Body::ApplyForce
// does (the same expression, the body woken first), so the port calls ApplyForce.
#include "aa/sim/items/items.h"
#include "item_common.h"

#include <vector>

namespace aa::sim::items {

namespace {
constexpr float kLn2 = 0.6931472f;   // 0x3f317218
}  // namespace

void forceToItem(WorldState& state, PhysicsWorld& world, int handle, int bodyIndex, Vec2 force, Vec2 point) {
    const int itemIndex = state.handles.lookup(handle);
    if (itemIndex < 0) return;
    const PhysicsObject& obj = state.objects[static_cast<std::size_t>(state.items[static_cast<std::size_t>(itemIndex)].objectIndex)];
    if (bodyIndex < 0 || bodyIndex >= obj.bodyCount) return;
    b2Body* body = world.body(obj.bodies[static_cast<std::size_t>(bodyIndex)]);
    if (body == nullptr || body->GetType() != b2_dynamicBody) return;
    body->ApplyForce(force, point);
}

void forceToRadius(WorldState& state, PhysicsWorld& world, Vec2 center, float radius, float force) {
    // The query callback keeps every Dynamic-category fixture whose body centre lies within the radius,
    // with its distance and direction (a body with several such fixtures is pushed once per fixture).
    struct Hit {
        b2Body* body;
        float distance;
        float dx;
        float dy;
    };
    std::vector<Hit> hits;
    const float r2 = radius * radius;
    b2AABB aabb;
    aabb.lowerBound = b2Vec2(center.x - radius, center.y - radius);
    aabb.upperBound = b2Vec2(center.x + radius, radius + center.y);
    world.queryAABB(aabb, [&](b2Fixture* f) {
        if ((f->GetFilterData().categoryBits & filters::kDynamicBit) == 0) return true;
        b2Body* body = f->GetBody();
        const float dy = body->GetPosition().y - center.y;
        const float dx = body->GetPosition().x - center.x;
        const float d2 = dy * dy + dx * dx;
        if (d2 < r2) {
            const float d = std::sqrt(d2);
            hits.push_back({body, d, dx / d, dy / d});
        }
        return true;
    });
    for (const Hit& h : hits) {
        const int oi = PhysicsWorld::bodyObject(h.body);
        const float mass = oi >= 0 ? objectMass(state.objects[static_cast<std::size_t>(oi)], world) : 0.0f;
        const float lg = logF(mass + 1.0f);
        if (h.body->GetType() != b2_dynamicBody) continue;
        const float f = (1.0f - h.distance / radius) * force * (lg / kLn2);
        h.body->ApplyForce(b2Vec2(f * h.dx, f * h.dy), h.body->GetWorldCenter());
    }
}

}  // namespace aa::sim::items
