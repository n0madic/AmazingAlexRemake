#include "aa/sim/physics_world.h"

#include "aa/sim/simulation.h"
#include "contact_listener.h"

#include <cstdint>

namespace aa::sim {

namespace {

// st::WorldContactListenerSetUp::PreSolve: the two halves of one pair of scissors never collide with each
// other in the set-up world (nothing else is filtered here; BeginContact / EndContact are empty).
class SetUpContactListener : public b2ContactListener {
public:
    void PreSolve(b2Contact* contact, const b2Manifold*) override {
        const b2Body* a = contact->GetFixtureA()->GetBody();
        const b2Body* b = contact->GetFixtureB()->GetBody();
        const int oa = PhysicsWorld::bodyObject(a);
        if (oa >= 0 && oa == PhysicsWorld::bodyObject(b) && PhysicsWorld::bodyObjectType(a) == ItemType::Scissors) {
            contact->SetEnabled(false);
        }
    }
};

constexpr std::uintptr_t kUserDataIndexMask = 0xFFFFu;
constexpr int kUserDataTypeShift = 16;

bool touchingAndEnabled(const b2Contact* c) { return c->IsTouching() && c->IsEnabled(); }

}  // namespace

const Vec2 PhysicsWorld::kGravity(0.0f, -9.8f);

PhysicsWorld::PhysicsWorld(SceneRecorder* recorder) : world_(new b2World(kGravity)), recorder_(recorder) {
    // GamePhysicsUtils::CreateWorld: b2World(gravity, doSleep = true) and m_flags &= ~e_clearForces.
    world_->SetAllowSleeping(true);
    world_->SetAutoClearForces(false);
    if (recorder_) recorder_->gravity(kGravity);
}

PhysicsWorld::~PhysicsWorld() = default;

int PhysicsWorld::createBody(const b2BodyDef& def) {
    const int slot = static_cast<int>(bodies_.size());
    if (recorder_) recorder_->body(slot, def);
    bodies_.push_back(world_->CreateBody(&def));
    return slot;
}

b2Fixture* PhysicsWorld::createFixture(int body, b2Shape& shape, const b2FixtureDef& def) {
    b2FixtureDef fd = def;
    fd.shape = &shape;
    return this->body(body)->CreateFixture(&fd);
}

b2Fixture* PhysicsWorld::addCircle(int body, float radius, Vec2 center, const b2FixtureDef& def) {
    if (recorder_) recorder_->circle(body, radius, center, def);
    b2CircleShape shape;
    shape.m_radius = radius;
    shape.m_p = center;
    return createFixture(body, shape, def);
}

b2Fixture* PhysicsWorld::addBox(int body, float hx, float hy, const b2FixtureDef& def) {
    if (recorder_) recorder_->box(body, hx, hy, def);
    b2PolygonShape shape;
    shape.SetAsBox(hx, hy);
    return createFixture(body, shape, def);
}

b2Fixture* PhysicsWorld::addBoxAt(int body, float hx, float hy, Vec2 center, float angle, const b2FixtureDef& def) {
    if (recorder_) recorder_->boxAt(body, hx, hy, center, angle, def);
    b2PolygonShape shape;
    shape.SetAsBox(hx, hy, center, angle);
    return createFixture(body, shape, def);
}

b2Fixture* PhysicsWorld::addPolygon(int body, const Vec2* vertices, int count, const b2FixtureDef& def) {
    if (recorder_) recorder_->polygon(body, vertices, count, def);
    b2PolygonShape shape;
    shape.Set(vertices, count);
    return createFixture(body, shape, def);
}

void PhysicsWorld::setMassData(int body, float mass, Vec2 center, float inertia) {
    b2MassData md;
    md.mass = mass;
    md.center = center;
    md.I = inertia;
    if (recorder_) recorder_->massData(body, md);
    this->body(body)->SetMassData(&md);
}

void PhysicsWorld::setTransform(int body, Vec2 position, float angle) {
    if (recorder_) recorder_->transform(body, position, angle);
    this->body(body)->SetTransform(position, angle);
}

void PhysicsWorld::resetBox(int body, b2Fixture* fixture, float hx, float hy) {
    static_cast<b2PolygonShape*>(fixture->GetShape())->SetAsBox(hx, hy);
    recordPolygonFix(body, fixture);
}

void PhysicsWorld::resetBoxAt(int body, b2Fixture* fixture, float hx, float hy, Vec2 center, float angle) {
    static_cast<b2PolygonShape*>(fixture->GetShape())->SetAsBox(hx, hy, center, angle);
    recordPolygonFix(body, fixture);
}

int PhysicsWorld::fixtureIndex(const b2Fixture* fixture) {
    // b2Body prepends to its fixture list, so the creation index counts from the tail.
    int behind = 0;
    for (const b2Fixture* f = fixture->GetNext(); f; f = f->GetNext()) ++behind;
    return behind;
}

void PhysicsWorld::recordPolygonFix(int body, const b2Fixture* fixture) {
    if (!recorder_) return;
    const auto* shape = static_cast<const b2PolygonShape*>(fixture->GetShape());
    recorder_->polygonFix(body, fixtureIndex(fixture), shape->m_vertices, shape->m_normals, shape->m_vertexCount,
                          shape->m_centroid);
}

int PhysicsWorld::createJoint(const b2JointDef& def) {
    const int slot = static_cast<int>(joints_.size());
    joints_.push_back(world_->CreateJoint(&def));
    return slot;
}

int PhysicsWorld::createRevolute(const b2RevoluteJointDef& def) {
    if (recorder_) recorder_->revolute(slotOf(def.bodyA), slotOf(def.bodyB), def);
    return createJoint(def);
}

int PhysicsWorld::createPrismatic(const b2PrismaticJointDef& def) {
    if (recorder_) recorder_->prismatic(slotOf(def.bodyA), slotOf(def.bodyB), def);
    return createJoint(def);
}

int PhysicsWorld::createDistance(const b2DistanceJointDef& def) {
    if (recorder_) recorder_->distance(slotOf(def.bodyA), slotOf(def.bodyB), def);
    return createJoint(def);
}

int PhysicsWorld::createWheel(const b2WheelJointDef& def) {
    if (recorder_) recorder_->wheel(slotOf(def.bodyA), slotOf(def.bodyB), def);
    return createJoint(def);
}

int PhysicsWorld::slotOf(const b2Body* body) const {
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (bodies_[i] == body && body) return static_cast<int>(i);
    }
    return -1;
}

int PhysicsWorld::slotOf(const b2Joint* joint) const {
    for (std::size_t i = 0; i < joints_.size(); ++i) {
        if (joints_[i] == joint && joint) return static_cast<int>(i);
    }
    return -1;
}

void PhysicsWorld::destroyJoint(int slot) {
    b2Joint* j = joint(slot);
    if (!j) return;
    if (recorder_) recorder_->destroyJoint(slot);
    world_->DestroyJoint(j);
    joints_[static_cast<std::size_t>(slot)] = nullptr;
}

void PhysicsWorld::destroyBody(int slot) {
    b2Body* b = body(slot);
    if (!b) return;
    if (recorder_) recorder_->destroyBody(slot);
    // b2World::DestroyBody destroys the attached joints; forget their slots first.
    for (const b2JointEdge* je = b->GetJointList(); je; je = je->next) {
        const int js = slotOf(je->joint);
        if (js >= 0) joints_[static_cast<std::size_t>(js)] = nullptr;
    }
    world_->DestroyBody(b);
    bodies_[static_cast<std::size_t>(slot)] = nullptr;
}

void PhysicsWorld::installContactListener(PhysicsMode mode, SimulationContext* context) {
    if (mode == PhysicsMode::SetUp) {
        listener_.reset(new SetUpContactListener());
    } else {
        listener_.reset(context ? makeWorldContactListener(*context).release() : nullptr);
    }
    world_->SetContactListener(listener_.get());
}

b2Fixture* PhysicsWorld::fixtureAt(int body, int creationIndex) const {
    const b2Body* b = body >= 0 && body < bodyCount() ? this->body(body) : nullptr;
    if (b == nullptr || creationIndex < 0) return nullptr;
    int count = 0;
    for (const b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext()) ++count;
    if (creationIndex >= count) return nullptr;
    // The list is prepended: the k-th created fixture is the (count - 1 - k)-th from the head.
    b2Fixture* f = const_cast<b2Body*>(b)->GetFixtureList();
    for (int i = count - 1 - creationIndex; i > 0; --i) f = f->GetNext();
    return f;
}

void PhysicsWorld::collideOnly() { world_->Step(0.0f, 1, 1); }

void PhysicsWorld::step() {
    world_->Step(kSimulationStep, kVelocityIterations, kPositionIterations);
    world_->ClearForces();
}

bool PhysicsWorld::isColliding(const PhysicsObject& obj) const {
    for (int k = 0; k < obj.bodyCount; ++k) {
        const b2Body* b = body(obj.bodies[static_cast<std::size_t>(k)]);
        if (!b) continue;
        for (const b2ContactEdge* ce = b->GetContactList(); ce; ce = ce->next) {
            if (touchingAndEnabled(ce->contact)) return true;
        }
    }
    return false;
}

bool PhysicsWorld::isCollidingWithAnother(const PhysicsObject& a, const PhysicsObject& b) const {
    for (int k = 0; k < a.bodyCount; ++k) {
        const b2Body* ba = body(a.bodies[static_cast<std::size_t>(k)]);
        if (!ba) continue;
        for (const b2ContactEdge* ce = ba->GetContactList(); ce; ce = ce->next) {
            // The original compares against the contact's fixture A body only (b2Contact+0x30 = m_fixtureA;
            // whether that is the other object's fixture depends on the pair's creation order) [verified:
            // decompile + G5a oracle, flip_collide script].
            const b2Body* other = ce->contact->GetFixtureA()->GetBody();
            for (int j = 0; j < b.bodyCount; ++j) {
                if (body(b.bodies[static_cast<std::size_t>(j)]) == other && touchingAndEnabled(ce->contact)) return true;
            }
        }
    }
    return false;
}

void PhysicsWorld::setCollisionFilter(const PhysicsObject& obj, const b2Filter& filter) {
    for (int k = 0; k < obj.bodyCount; ++k) {
        b2Body* b = body(obj.bodies[static_cast<std::size_t>(k)]);
        if (!b) continue;
        b->SetType(b2_staticBody);
        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext()) f->SetFilterData(filter);
    }
}

void PhysicsWorld::destroyPhysics(PhysicsObject& obj) {
    for (int k = 0; k < obj.bodyCount; ++k) destroyBody(obj.bodies[static_cast<std::size_t>(k)]);
    obj.bodyCount = 0;
    obj.jointCount = 0;
    for (int k = 0; k < obj.attachmentCount; ++k) obj.attachments[static_cast<std::size_t>(k)].joint = -1;
}

void* PhysicsWorld::encodeBodyUserData(int objectIndex, ItemType type) {
    const std::uintptr_t v = (static_cast<std::uintptr_t>(objectIndex + 1) & kUserDataIndexMask) |
                             (static_cast<std::uintptr_t>(static_cast<int>(type)) << kUserDataTypeShift);
    return reinterpret_cast<void*>(v);
}

int PhysicsWorld::bodyObject(const b2Body* body) {
    const std::uintptr_t v = reinterpret_cast<std::uintptr_t>(body->GetUserData());
    return static_cast<int>(v & kUserDataIndexMask) - 1;
}

ItemType PhysicsWorld::bodyObjectType(const b2Body* body) {
    const std::uintptr_t v = reinterpret_cast<std::uintptr_t>(body->GetUserData());
    return static_cast<ItemType>(static_cast<int>(v >> kUserDataTypeShift));
}

void PhysicsWorld::setBodyObject(int body, int objectIndex, ItemType type) {
    b2Body* b = this->body(body);
    if (b) b->SetUserData(encodeBodyUserData(objectIndex, type));
}

int PhysicsWorld::fixtureBodyIndex(const b2Fixture* fixture) {
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(fixture->GetUserData()));
}

b2FixtureDef fixtureDef(float density, float friction, float restitution, const b2Filter& filter, bool sensor) {
    b2FixtureDef def;
    def.density = density;
    def.friction = friction;
    def.restitution = restitution;
    def.filter = filter;
    def.isSensor = sensor;
    return def;
}

}  // namespace aa::sim
