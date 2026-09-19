// st::GamePhysicsUtils — the simulation-mode whole-state helpers [verified: decompile + disassembly].
#include "aa/sim/simulation.h"

#include <cmath>

namespace aa::sim {

namespace {

constexpr float kMotionEpsilon = 0.001f;         // 0x3a83126f
constexpr float kRunawayX = 6.82f;               // 0x40da3d71
constexpr float kRunawayY = -4.24918f;           // 0xc087f948

}  // namespace

void getStateFromPhysics(WorldState& state, const PhysicsWorld& world) {
    for (PhysicsObject& obj : state.objects) {
        if (!obj.isDynamic() || obj.bodyCount == 0) continue;
        const b2Body* body = world.body(obj.bodies[0]);
        if (body == nullptr) continue;
        // m_xf.position and m_sweep.a, as the original reads them.
        obj.position = body->GetPosition();
        obj.angle = body->GetAngle();
    }
}

void lerpState(std::vector<RenderPose>& render, const WorldState& prev, const WorldState& current, float alpha) {
    render.resize(current.objects.size());
    for (std::size_t i = 0; i < current.objects.size(); ++i) {
        const PhysicsObject& cur = current.objects[i];
        RenderPose& out = render[i];
        if (!cur.isDynamic() || i >= prev.objects.size()) {
            out.position = cur.position;
            out.angle = cur.angle;
            continue;
        }
        // vmla.f32: the product is rounded before the add (two roundings, no fusion).
        const PhysicsObject& p = prev.objects[i];
        out.position.x = p.position.x + alpha * (cur.position.x - p.position.x);
        out.position.y = p.position.y + alpha * (cur.position.y - p.position.y);
        out.angle = p.angle + alpha * (cur.angle - p.angle);
    }
}

void stopRunawayObjects(const WorldState& state, PhysicsWorld& world) {
    for (const PhysicsObject& obj : state.objects) {
        for (int k = 0; k < obj.bodyCount; ++k) {
            b2Body* body = world.body(obj.bodies[static_cast<std::size_t>(k)]);
            if (body == nullptr) continue;
            const b2Vec2 v = body->GetLinearVelocity();
            if (!(std::fabs(v.x) > kMotionEpsilon) && !(std::fabs(v.y) > kMotionEpsilon)) continue;
            const b2Vec2 p = body->GetPosition();
            const bool runaway = std::fabs(p.x) > kRunawayX || p.y < kRunawayY;
            if (runaway && body->GetType() != b2_staticBody) {
                body->SetAngularVelocity(0.0f);
                body->SetLinearVelocity(b2Vec2(0.0f, 0.0f));
            }
        }
    }
}

bool hasMovingObjects(const WorldState& state, const PhysicsWorld& world) {
    for (const PhysicsObject& obj : state.objects) {
        for (int k = 0; k < obj.bodyCount; ++k) {
            if (k == 0 && obj.type == ItemType::Rope) continue;   // the rope's root body is ignored
            const b2Body* body = world.body(obj.bodies[static_cast<std::size_t>(k)]);
            if (body == nullptr) continue;
            const b2Vec2 v = body->GetLinearVelocity();
            if (std::fabs(v.x) > kMotionEpsilon) return true;
            if (std::fabs(v.y) > kMotionEpsilon) return true;
            if (std::fabs(body->GetAngularVelocity()) > kMotionEpsilon) return true;
        }
    }
    return false;
}

}  // namespace aa::sim
