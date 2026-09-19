// Spring / trampoline (28): st::SpringUtils::CreatePhysics — base body (kinematic in simulation) with a
// thin box and the block box (plus the set-up selection box), plate and seat bodies ±(r − 0.007) along
// the item's up axis with one box each, and in simulation the prismatic joint (limits ±0.16, collide
// connected) plus the 12 Hz / 0.1 distance joint between their centres.
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kSpringFrequency = 12.0f;   // DAT_002811ac (90 Hz at DAT_002811b0 is the release value)
constexpr float kTravel = 0.16f;
constexpr float kReleaseFrequency = 90.0f;      // DAT_002811b0
constexpr float kSpringDamping = 0.1f;          // DAT_002811b4
constexpr float kReleaseDamping = 0.1f;         // DAT_002811b8
constexpr float kCompressionSlack = 0.01f;
constexpr double kExpandingVelocity = 0.0001;   // 0x3f1a36e2eb1c432d
constexpr double kReleasedSlack = 0.1;          // 0x3fb999999999999a
constexpr float kSoundFloor = -1.0f;            // no Spring sound once a part fell below y = −1
constexpr float kBlockMargin = 0.06f;
}  // namespace

void createSpring(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    b2BodyDef def;
    def.type = mode == PhysicsMode::Simulation ? b2_kinematicBody : b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int base = addBody(obj, world, def);
    // Fixture user data 1 / 2 / 1: WorldContactListener::PreSolve disables the contacts of the block box
    // (user data 2) with the spring's own plate and seat [verified].
    b2FixtureDef thin = fixtureDef(0.5f, 1.0f, 0.0f, filters::dynamicPlus());
    thin.userData = reinterpret_cast<void*>(1);
    world.addBox(base, r * 0.6f, 0.001f, thin);
    b2FixtureDef block = fixtureDef(0.5f, 1.0f, 0.0f, filters::kDynamic);
    block.userData = reinterpret_cast<void*>(2);
    world.addBox(base, r * 0.6f, r * 0.5f, block);
    if (mode == PhysicsMode::SetUp) {
        b2FixtureDef sel = fixtureDef(0.5f, 1.0f, 0.0f, filters::kSelection);
        sel.userData = reinterpret_cast<void*>(1);
        world.addBox(base, r * 0.8f, r * 1.3f, sel);
    }
    const Vec2 up = rotate(obj.angle, Vec2(0.0f, r - 0.007f));
    b2BodyDef endDef;
    endDef.type = b2_dynamicBody;
    endDef.angle = obj.angle;
    endDef.position = Vec2(obj.position.x - up.x, obj.position.y - up.y);
    const int plate = addBody(obj, world, endDef);
    endDef.position = Vec2(up.x + obj.position.x, up.y + obj.position.y);
    const int seat = addBody(obj, world, endDef);
    b2FixtureDef endFd = fixtureDef(10.0f, 0.6f, 0.0f, filters::kDynamic);
    endFd.userData = reinterpret_cast<void*>(1);
    world.addBox(plate, r * 0.9f, 0.03f, endFd);
    world.addBox(seat, r * 0.9f, 0.03f, endFd);
    if (mode == PhysicsMode::Simulation) {
        b2Body* a = world.body(plate);
        b2Body* b = world.body(seat);
        b2PrismaticJointDef pj;
        pj.collideConnected = true;
        pj.Initialize(a, b, a->GetWorldCenter(), rotate(obj.angle, Vec2(0.0f, 1.0f)));
        pj.enableLimit = true;
        pj.lowerTranslation = -kTravel;
        pj.upperTranslation = kTravel;
        pj.enableMotor = false;
        obj.addJoint(world.createPrismatic(pj));
        b2DistanceJointDef dj;
        dj.collideConnected = true;
        dj.Initialize(a, b, a->GetWorldCenter(), b->GetWorldCenter());
        dj.frequencyHz = kSpringFrequency;
        dj.dampingRatio = 0.1f;
        dj.resistCompression = true;
        obj.addJoint(world.createDistance(dj));
    }
}

void updateSprings(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // SpringUtils::Update [verified: decompile + disassembly]. The expansion test and the "already
    // released" test are made in double (vcmpe.f64 against 0.0001 and 12 + 0.1).
    (void)dt;
    for (const GameItem& item : state.items) {
        if (item.type != ItemType::Spring) continue;
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        b2Body* plate = world.body(obj.bodies[1]);
        const b2Body* seat = world.body(obj.bodies[2]);
        const float dx = seat->GetPosition().x - plate->GetPosition().x;
        const float dy = seat->GetPosition().y - plate->GetPosition().y;
        const float len = length(Vec2(dx, dy));
        auto* joint = static_cast<b2DistanceJoint*>(world.joint(obj.joints[1]));
        const float restLen = joint->GetLength();
        bool reset = false;
        if (!(len < restLen - kCompressionSlack)) {
            reset = !(len < restLen);
        } else {
            float rel = dy * (seat->GetLinearVelocity().y - plate->GetLinearVelocity().y);
            rel = rel + dx * (seat->GetLinearVelocity().x - plate->GetLinearVelocity().x);
            if (static_cast<double>(rel) > kExpandingVelocity) {
                if (!(static_cast<double>(joint->GetFrequency()) >= static_cast<double>(kSpringFrequency) + kReleasedSlack)) {
                    const float bottom = restLen - kTravel;
                    joint->SetDampingRatio(kReleaseDamping);
                    float t = len - bottom;
                    t = t / (restLen - bottom);
                    const float u = 1.0f - t;
                    float freq = kSpringFrequency;
                    freq = freq + u * (kReleaseFrequency - kSpringFrequency);
                    joint->SetFrequency(freq);
                    bool below = false;
                    for (int k = 0; k < obj.bodyCount; ++k) {
                        if (world.body(obj.bodies[static_cast<std::size_t>(k)])->GetPosition().y < kSoundFloor) below = true;
                    }
                    if (!below) {
                        float v = u + u;
                        if (0.0f <= v - 1.0f) v = 1.0f;
                        queue.add(Action::sound(sound::kSpring, obj.position, v));
                    }
                } else {
                    reset = !(len < restLen);
                }
            } else {
                reset = !(len < restLen);
            }
        }
        if (reset) {
            joint->SetFrequency(kSpringFrequency);
            joint->SetDampingRatio(kSpringDamping);
        }
        float bx = plate->GetPosition().x;
        bx = bx + dx * 0.5f;
        float by = plate->GetPosition().y;
        by = by + dy * 0.5f;
        b2Body* base = world.body(obj.bodies[0]);
        base->SetTransform(b2Vec2(bx, by), plate->GetAngle());
        const float gap = len - kBlockMargin;
        world.resetBox(obj.bodies[0], base->GetFixtureList(), obj.halfSize * 0.6f, gap * 0.5f);
    }
}

}  // namespace aa::sim::items
