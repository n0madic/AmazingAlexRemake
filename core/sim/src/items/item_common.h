// Helpers shared by the item ports (internal to aa_sim).
#pragma once

#include "aa/sim/filters.h"
#include "aa/sim/math_utils.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/world_state.h"

namespace aa::sim::items {

constexpr float kDefaultFriction = 0.2f;            // b2FixtureDef default the original leaves in place
constexpr float kMinSelectionRadius = 0.12f;        // GameParams::MinSelectionRadius (docs/04 §9)
constexpr float kBoxRadius = 0.005f;                // b2_polygonRadius of the shipped build (docs/04 §1)
using aa::sim::kDegToRad;
constexpr float kPi = kGamePi;                      // the game's Pi global (0x40490fd8, a DegToRad product)

// The body definition every item starts from: the object's position and angle, Box2D defaults otherwise.
// In set-up mode every body is dynamic (docs/04 §2); `simulationType` is the type used in simulation.
inline b2BodyDef itemBodyDef(const PhysicsObject& obj, PhysicsMode mode, b2BodyType simulationType) {
    b2BodyDef def;
    def.type = mode == PhysicsMode::SetUp ? b2_dynamicBody : simulationType;
    def.position = obj.position;
    def.angle = obj.angle;
    return def;
}

// CreateBody + register the slot in the object (obj+0x94 count, obj+0x98.. bodies). The original stores the
// object pointer in b2Body::userData (the `+0x98` write of every CreatePhysics); here the object index and
// type (PhysicsWorld::encodeBodyUserData) — the pick query and the contact listeners read it back.
inline int addBody(PhysicsObject& obj, PhysicsWorld& world, const b2BodyDef& def) {
    b2BodyDef d = def;
    d.userData = PhysicsWorld::encodeBodyUserData(obj.index, obj.type);
    const int slot = world.createBody(d);
    obj.addBody(slot);
    return slot;
}

// The original's double multiplications (`vcvt.f64.f32` / `vmul.f64` / `vcvt.f32.f64`): written out so that
// -Wdouble-promotion catches every accidental one (docs/10 §11 item 7).
inline float dmul(float a, double b) { return static_cast<float>(static_cast<double>(a) * b); }
inline float dmul(double a, float b) { return static_cast<float>(a * static_cast<double>(b)); }

// PhysicsObjectUtils::GetMass: the sum of the object's body masses (b2Body::m_mass).
inline float objectMass(const PhysicsObject& obj, const PhysicsWorld& world) {
    float m = 0.0f;
    for (int k = 0; k < obj.bodyCount; ++k) {
        const b2Body* b = world.body(obj.bodies[static_cast<std::size_t>(k)]);
        if (b) m = m + b->GetMass();
    }
    return m;
}

inline b2FixtureDef selectionDef() {
    return fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kSelection);
}

}  // namespace aa::sim::items
