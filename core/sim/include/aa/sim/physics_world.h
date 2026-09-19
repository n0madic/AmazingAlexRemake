// The Box2D world behind a WorldState (st::GamePhysicsUtils, docs/04-physics.md §2) and the only
// construction primitives the item code may call. Every primitive forwards to an optional SceneRecorder,
// so what is recorded is exactly what Box2D received.
#pragma once

#include "aa/sim/scene_recorder.h"
#include "aa/sim/types.h"
#include "aa/sim/world_state.h"

#include <Box2D/Box2D.h>

#include <memory>
#include <vector>

namespace aa::sim {

struct SimulationContext;

class PhysicsWorld {
public:
    // GamePhysicsUtils::CreateWorld: gravity (0, -9.8), sleeping allowed, automatic ClearForces off.
    explicit PhysicsWorld(SceneRecorder* recorder = nullptr);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    static const Vec2 kGravity;

    b2World& world() { return *world_; }
    SceneRecorder* recorder() const { return recorder_; }

    // --- construction primitives (the recorded calls) ------------------------------------------------
    // Returns the body slot; bodies are numbered in creation order across the whole world.
    int createBody(const b2BodyDef& def);
    b2Body* body(int slot) const { return bodies_.at(static_cast<std::size_t>(slot)); }
    int bodyCount() const { return static_cast<int>(bodies_.size()); }

    b2Fixture* addCircle(int body, float radius, Vec2 center, const b2FixtureDef& def);
    // b2PolygonShape::SetAsBox(hx, hy)
    b2Fixture* addBox(int body, float hx, float hy, const b2FixtureDef& def);
    // b2PolygonShape::SetAsBox(hx, hy, center, angle)
    b2Fixture* addBoxAt(int body, float hx, float hy, Vec2 center, float angle, const b2FixtureDef& def);
    // b2PolygonShape::Set(vertices, count)
    b2Fixture* addPolygon(int body, const Vec2* vertices, int count, const b2FixtureDef& def);
    void setMassData(int body, float mass, Vec2 center, float inertia);
    void setTransform(int body, Vec2 position, float angle);
    // The fixture created `creationIndex`-th on a body (the original keeps b2Fixture pointers in the item
    // blocks — the glove's trigger circle, the helicopter's rotor and tail boxes; here they are named by
    // their creation order, docs/08). Null when the body or the index does not exist.
    b2Fixture* fixtureAt(int body, int creationIndex) const;

    // In-place edits of an existing polygon fixture's shape (b2PolygonShape::SetAsBox on the fixture's own
    // shape, as the glove arm and the rope root box do after CreateFixture). Recorded as a `polygonfix`
    // line at the end of the scene, the way the harness detects them.
    void resetBox(int body, b2Fixture* fixture, float hx, float hy);
    void resetBoxAt(int body, b2Fixture* fixture, float hx, float hy, Vec2 center, float angle);

    // Joints (docs/04 §1): slots are numbered in creation order across the world, like the bodies. The
    // defs must carry body pointers of this world (use body(slot)).
    int createRevolute(const b2RevoluteJointDef& def);
    int createPrismatic(const b2PrismaticJointDef& def);
    int createDistance(const b2DistanceJointDef& def);
    int createWheel(const b2WheelJointDef& def);
    b2Joint* joint(int slot) const { return joints_.at(static_cast<std::size_t>(slot)); }
    int jointCount() const { return static_cast<int>(joints_.size()); }
    // Slot of a body / joint pointer of this world, -1 when unknown (destroyed slots are never found).
    int slotOf(const b2Body* body) const;
    int slotOf(const b2Joint* joint) const;

    // Destruction keeps the slot numbering: the slot's pointer is nulled. Destroying a body destroys its
    // joints implicitly (their slots are nulled too, without a `destroyjoint` line — the harness convention).
    void destroyJoint(int slot);
    void destroyBody(int slot);

    // --- set-up interaction primitives (docs/05 §5, M3) --------------------------------------------
    // GamePhysicsUtils::CreateWorld installs the mode's contact listener: set-up = WorldContactListenerSetUp
    // (PreSolve disables the contact between two bodies of one Scissors object) [verified]; simulation =
    // WorldContactListener (contact_listener.cpp) over the session's state through `context`.
    void installContactListener(PhysicsMode mode, SimulationContext* context = nullptr);
    // b2World::Step(0, 1, 1): refreshes the contacts without moving anything (the ghost bisection). Not
    // recorded: the G3 scenes never step.
    void collideOnly();
    // GameScreen::UpdateSimulation's physics substep: Step(1/120, 10, 10) then ClearForces (auto-clear is
    // off, docs/04 §3). Not recorded per call — the dump tools write the harness's single `step` line.
    void step();
    // PhysicsObjectUtils::IsColliding: some contact of some body of the object is touching and enabled.
    bool isColliding(const PhysicsObject& obj) const;
    // PhysicsObjectUtils::IsCollidingWithAnother: such a contact between a body of `a` and a body of `b`.
    bool isCollidingWithAnother(const PhysicsObject& a, const PhysicsObject& b) const;
    // PhysicsObjectUtils::SetCollisionFilter: every body becomes static and every fixture gets the filter.
    void setCollisionFilter(const PhysicsObject& obj, const b2Filter& filter);
    // PhysicsObjectUtils::DestroyPhysics: destroys the bodies (joints implicitly) and forgets the slots.
    void destroyPhysics(PhysicsObject& obj);
    // The b2Body::userData back-reference of the original (object pointer): here the object index and its
    // type, so the pick query and the contact listener can read them without the WorldState.
    static void* encodeBodyUserData(int objectIndex, ItemType type);
    static int bodyObject(const b2Body* body);        // -1 when the body carries no object
    static ItemType bodyObjectType(const b2Body* body);
    void setBodyObject(int body, int objectIndex, ItemType type);
    // b2Fixture::userData: rope end circles carry their body index + 1 (0 on every other fixture).
    static int fixtureBodyIndex(const b2Fixture* fixture);
    // b2World::QueryAABB with a callable `bool(b2Fixture*)` (return false to stop).
    template <class F>
    void queryAABB(const b2AABB& aabb, F&& report) const {
        struct Adapter : b2QueryCallback {
            F* fn;
            bool ReportFixture(b2Fixture* fixture) override { return (*fn)(fixture); }
        } adapter;
        adapter.fn = &report;
        world_->QueryAABB(&adapter, aabb);
    }

private:
    // The fixture def is used as given except for the shape pointer.
    b2Fixture* createFixture(int body, b2Shape& shape, const b2FixtureDef& def);
    int createJoint(const b2JointDef& def);
    // Index of a fixture in its body's creation order (the harness numbers fixtures that way).
    static int fixtureIndex(const b2Fixture* fixture);
    void recordPolygonFix(int body, const b2Fixture* fixture);

    std::unique_ptr<b2World> world_;
    std::unique_ptr<b2ContactListener> listener_;
    std::vector<b2Body*> bodies_;
    std::vector<b2Joint*> joints_;
    SceneRecorder* recorder_;
};

// The b2FixtureDef the original code starts from (Box2D defaults: friction 0.2, filter Static) with the
// fields every item sets; helps keep the call sites short and explicit.
b2FixtureDef fixtureDef(float density, float friction, float restitution, const b2Filter& filter, bool sensor = false);

}  // namespace aa::sim
