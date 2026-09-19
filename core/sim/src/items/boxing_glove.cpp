// BoxingGlove (14): st::BoxingGloveUtils::CreatePhysics and the arm geometry update (FUN_000a9578,
// docs/03 §14). Stand body (static in simulation) with the trigger circle (group −4), the plate and head
// boxes (group −5) and, in set-up, the Topping and selection boxes; fist body 0.28·s to the side with the
// glove circle, the wrist box and a 10 kg mass override; in simulation the prismatic joint along
// (1, −0.15) with the 10 000 N holding motor. UpdateArmGeometry then re-shapes the stand's *last created*
// fixture in place (the head box in simulation, the selection box in set-up — the original's own quirk).
#include "aa/sim/float_bits.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

#include <cmath>

namespace aa::sim::items {

namespace {
// `.bss` DAT_00282f58 (ELF 0x272f58): (0.27999997, 0.01) — the x is one ulp below 0.28f, as the static initialiser left it.
const Vec2 kFistOffset(floatFromBits(0x3e8f5c28u), 0.01f);
constexpr float kFistRadius = 0.1f;
constexpr float kLatticeLength = 0.2973f;
constexpr float kLatticeHalfHeight = 0.14865f;
constexpr float kTriggerImpulse = 0.2f;        // |v_n · m| that releases the punch
constexpr int kTriggerFixture = 0;             // BoxingGlove+0x2c: the stand's first fixture (the button circle)
constexpr float kPressedButtonHeightPx = 8.0f;
constexpr float kButtonPressTime = 0.06f;
constexpr float kPunchSpring = -5850.0f;       // 0xc5b6d000
constexpr float kRetractSpring = -1170.0f;     // 0xc4924000
constexpr float kPunchRest = 0.75f;
constexpr float kRetractRest = 0.6f;
constexpr float kFistDamping = -40.0f;
constexpr float kArmOffset = -0.2f;
constexpr float kEpsilon = 0.0001f;            // st::Epsilon
}  // namespace

void createBoxingGlove(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float s = obj.flipSign();
    b2BodyDef def;
    def.type = mode == PhysicsMode::Simulation ? b2_staticBody : b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int stand = addBody(obj, world, def);
    const float standX = s * -0.2f;
    b2Filter standFilter = filters::kStatic;
    standFilter.categoryBits = static_cast<uint16>(standFilter.categoryBits | filters::kSelectionBit);
    world.addCircle(stand, 0.05f, Vec2(standX, floatFromBits(0x3e699ae2u) /* ≈ 0.22813 */),
                    fixtureDef(0.0f, 0.6f, 0.0f, filters::withGroup(standFilter, filters::kGroupGloveTrigger)));
    const b2FixtureDef plate = fixtureDef(0.0f, 0.6f, 0.0f, filters::withGroup(standFilter, filters::kGroupGloveStand));
    world.addBoxAt(stand, 0.049951173f, 0.20812988f, Vec2(standX, 0.01f), 0.0f, plate);
    world.addBox(stand, 0.1f, 0.1f, plate);
    if (mode == PhysicsMode::SetUp) {
        world.addBoxAt(stand, 0.18f, 0.18f, Vec2(s * -0.06f, 0.0f), 0.0f,
                       fixtureDef(50.0f, kDefaultFriction, 0.0f, filters::kTopping));
        world.addBoxAt(stand, 0.08325195f, 0.24975586f, Vec2(standX, 0.05f), 0.0f,
                       fixtureDef(0.0f, 0.6f, 0.0f, filters::kSelection));
    }
    const Vec2 off = rotate(obj.angle, Vec2(kFistOffset.x * s, kFistOffset.y));
    b2BodyDef fistDef;
    fistDef.type = b2_dynamicBody;
    fistDef.position = Vec2(off.x + obj.position.x, off.y + obj.position.y);
    fistDef.angle = obj.angle;
    const int fist = addBody(obj, world, fistDef);
    world.addCircle(fist, kFistRadius, Vec2(0.0f, 0.0f), fixtureDef(0.01f, 0.7f, 0.3f, filters::dynamicPlus()));
    world.addBoxAt(fist, 0.06f, 0.05f, Vec2((-0.03f - kFistRadius) * s, 0.0f), 0.0f,
                   fixtureDef(0.01f, 0.7f, 0.3f, filters::withGroup(filters::dynamicPlus(), filters::kGroupGloveStand)));
    world.setMassData(fist, 10.0f, Vec2(0.0f, 0.0f), 0.01f);
    if (mode == PhysicsMode::Simulation) {
        b2PrismaticJointDef jd;
        b2Body* a = world.body(stand);
        jd.Initialize(a, world.body(fist), a->GetWorldCenter(), rotate(obj.angle, Vec2(s, -0.15f)));
        jd.enableLimit = false;
        jd.enableMotor = true;
        jd.maxMotorForce = 10000.0f;
        jd.motorSpeed = 0.0f;
        obj.addJoint(world.createPrismatic(jd));
    }
    updateGloveArmGeometry(obj, world);
}

float gloveLatticeAngle(Vec2 standPos, Vec2 fistPos) {
    const float len = length(Vec2(fistPos.x - standPos.x, fistPos.y - standPos.y)) + 0.2f;
    float t = (len - 0.21f) / 5.0f;
    t = (t + t) / kLatticeLength;
    float clamped = t;
    if (1.0f - t < 0.0f) clamped = 1.0f;
    if (t + 1.0f < 0.0f) clamped = -1.0f;
    return asinF(clamped);
}

float updateGloveArmGeometry(PhysicsObject& obj, PhysicsWorld& world) {
    b2Body* stand = world.body(obj.bodies[0]);
    b2Body* fist = world.body(obj.bodies[1]);
    const Vec2 standPos(stand->GetPosition().x, stand->GetPosition().y);
    const Vec2 fistPos(fist->GetPosition().x, fist->GetPosition().y);
    const float len = length(Vec2(fistPos.x - standPos.x, fistPos.y - standPos.y)) + 0.2f;
    const float phi = gloveLatticeAngle(standPos, fistPos);
    b2Fixture* head = stand->GetFixtureList();   // the most recently created fixture
    const float hx = ((len - 0.1f) - 0.1f) * 0.5f;
    const float hy = cosF(phi) * kLatticeHalfHeight;
    world.resetBoxAt(obj.bodies[0], head, hx, hy, Vec2((hx - 0.2f) * obj.flipSign(), hy - 0.14f), 0.0f);
    return phi;
}

void updateBoxingGloves(float dt, WorldState& state, PhysicsWorld& world) {
    // BoxingGloveUtils::Update [verified: decompile + disassembly].
    for (GameItem& item : state.items) {
        if (item.type != ItemType::BoxingGlove || item.gloveState == 0) continue;
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        item.gloveButton.update(dt);
        const Vec2 offset = rotate(obj.angle, Vec2(obj.scale.x * kArmOffset, obj.scale.y * 0.0f));
        b2Body* fist = world.body(obj.bodies[1]);
        const b2Body* stand = world.body(obj.bodies[0]);
        float dx = fist->GetPosition().x - stand->GetPosition().x;
        float dy = fist->GetPosition().y - stand->GetPosition().y;
        dx = dx - offset.x;
        dy = dy - offset.y;
        const float len = length(Vec2(dx, dy));
        float dirX = 1.0f;
        float dirY = 0.0f;
        if (len > kEpsilon) {
            dirX = dx / len;
            dirY = dy / len;
        }
        const bool punching = item.gloveState == 1;
        const float k = punching ? kPunchSpring : kRetractSpring;
        const float rest = punching ? kPunchRest : kRetractRest;
        if (fist->GetType() == b2_dynamicBody) {
            const float pull = (len - rest) * k;
            const b2Vec2 v = fist->GetLinearVelocity();
            float fx = v.x * kFistDamping;
            float fy = v.y * kFistDamping;
            fx = fx + pull * dirX;
            fy = fy + pull * dirY;
            fist->ApplyForce(b2Vec2(fx, fy), fist->GetWorldCenter());
        }
        item.latticeAngle = updateGloveArmGeometry(obj, world);
    }
}

void gloveHandleCollision(GameItem& item, PhysicsObject& glove, const PhysicsObject& soundAt, const PhysicsObject& other,
                          int otherBody, float impact, ActionQueue& queue, PhysicsWorld& world) {
    // BoxingGloveUtils::HandleCollision [verified: decompile + disassembly (impact in s0)].
    if (item.gloveState != 0) return;
    const b2Body* body = world.body(other.bodies[static_cast<std::size_t>(otherBody)]);
    if (!(kTriggerImpulse <= std::fabs(impact * body->GetMass()))) return;
    item.gloveState = 1;
    if (glove.jointCount > 0) {
        if (b2Joint* j = world.joint(glove.joints[0])) static_cast<b2PrismaticJoint*>(j)->EnableMotor(false);
    }
    if (b2Fixture* trigger = world.fixtureAt(glove.bodies[0], kTriggerFixture)) trigger->SetFilterData(filters::kNonCollidable);
    item.gloveButton.start(GameItem::kGloveButtonHeightPx, kPressedButtonHeightPx, kButtonPressTime);
    queue.add(Action::sound(sound::kBoxingGloveTriggered, soundAt.position, 0.4f));
}

void gloveHandleCollisionSounds(const GameItem& item, const PhysicsObject& obj, int bodyIndex, float impact,
                                ActionQueue& queue, const PhysicsWorld& world) {
    // BoxingGloveUtils::HandleCollisionSounds [verified].
    if (bodyIndex != 1 || !(impact > 3.0f) || item.gloveState != 1) return;
    const b2Body* fist = world.body(obj.bodies[1]);
    if (!(length(fist->GetLinearVelocity()) > 3.0f)) return;
    float v = impact / 7.0f;
    if (1.0f - v < 0.0f) v = 1.0f;
    if (v - 0.1f < 0.0f) v = 0.1f;
    queue.add(Action::soundOf(obj.handle, sound::kBoxingGlovePunch, obj.position, v));
}

}  // namespace aa::sim::items
