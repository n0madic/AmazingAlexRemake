// Doll / ragdoll (19): st::DollUtils::CreatePhysics — six dynamic bodies with angular damping 0.1 (torso
// box, head circle, two arms, two legs; limbs placed with the torso's world transform and tilted by
// fractions of the game's Pi global, mirrored by the flip sign) and five revolute joints from the torso
// (head and legs ±Pi/4 without motor; arms −Pi/8…Pi/1.5 — the flipped doll swaps and negates them — with
// the motor on), all with maxMotorTorque 0.001. Identical in both modes.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kAngularDamping = 0.1f;
constexpr float kMotorTorque = 0.001f;

b2BodyDef partDef(Vec2 position, float angle) {
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = position;
    def.angle = angle;
    def.angularDamping = kAngularDamping;
    return def;
}

// `part` is the world body slot of the limb.
void jointTo(PhysicsObject& obj, PhysicsWorld& world, int part, Vec2 anchor, float lower, float upper, bool motor) {
    b2RevoluteJointDef jd;
    jd.Initialize(world.body(obj.bodies[0]), world.body(part), anchor);
    jd.enableLimit = true;
    jd.lowerAngle = lower;
    jd.upperAngle = upper;
    jd.enableMotor = motor;
    jd.motorSpeed = 0.0f;
    jd.maxMotorTorque = kMotorTorque;
    obj.addJoint(world.createRevolute(jd));
}
}  // namespace

void createDoll(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode) {
    const float s = obj.flipSign();
    const b2FixtureDef fd = fixtureDef(9.0f, 0.8f, 0.3f, filters::dynamicPlus());
    const float angle = obj.angle;
    const int torso = addBody(obj, world, partDef(obj.position, angle));
    world.addBoxAt(torso, 0.06f, 0.08f, Vec2(0.0f, -0.015f), 0.0f, fd);
    const b2Body* t = world.body(torso);

    const int head = addBody(obj, world, partDef(t->GetWorldPoint(Vec2(0.0f, 0.13f)), angle));
    world.addCircle(head, 0.07f, Vec2(0.0f, 0.0f), fd);
    const int frontArm = addBody(obj, world, partDef(t->GetWorldPoint(Vec2(s * -0.06f, 0.015f)), angle - 0.125f * kPi * s));
    world.addBox(frontArm, 0.015f, 0.05f, fd);
    const int backArm = addBody(obj, world, partDef(t->GetWorldPoint(Vec2(s * 0.06f, 0.015f)), angle + (kPi / 6.0f) * s));
    world.addBox(backArm, 0.015f, 0.05f, fd);
    const float legX = s * -0.04f;
    const int frontLeg = addBody(obj, world, partDef(t->GetWorldPoint(Vec2(legX, -0.15f)), angle - (kPi / 10.0f) * s));
    world.addBox(frontLeg, 0.025f, 0.07f, fd);
    const float backLegX = s * 0.05f;
    const int backLeg = addBody(obj, world, partDef(t->GetWorldPoint(Vec2(backLegX, -0.15f)), angle));
    world.addBox(backLeg, 0.025f, 0.07f, fd);

    const float quarter = kPi * 0.25f;
    const float nquarter = -kPi * 0.25f;
    jointTo(obj, world, head, t->GetWorldPoint(Vec2(0.0f, 0.09f)), nquarter, quarter, false);
    const float armLow = -kPi * 0.125f;
    const float armHigh = kPi / 1.5f;
    const float lower = s > 0.0f ? armLow : -armHigh;
    const float upper = s > 0.0f ? armHigh : -armLow;
    jointTo(obj, world, frontArm, t->GetWorldPoint(Vec2(legX, 0.04f)), lower, upper, true);
    jointTo(obj, world, backArm, t->GetWorldPoint(Vec2(s * 0.04f, 0.04f)), lower, upper, true);
    jointTo(obj, world, frontLeg, t->GetWorldPoint(Vec2(legX, -0.05f)), nquarter, quarter, false);
    jointTo(obj, world, backLeg, t->GetWorldPoint(Vec2(backLegX, -0.05f)), nquarter, quarter, false);
}

void dollHandleCollisionSounds(const PhysicsObject& obj, int bodyIndex, float impact, ActionQueue& queue) {
    // DollUtils::HandleCollisionSounds [verified].
    if (bodyIndex == 2 || bodyIndex == 5 || bodyIndex == 1) {
        float v = impact / 5.0f;
        if (1.0f - v < 0.0f) v = 1.0f;
        if (v - 0.1f < 0.0f) v = 0.1f;
        queue.add(Action::soundOf(obj.handle, sound::kDollImpact, obj.position, v));
    }
    if (bodyIndex == 0 && 3.0f < impact) {
        float v = impact / 7.0f;
        if (1.0f - v < 0.0f) v = 1.0f;
        if (v - 0.1f < 0.0f) v = 0.1f;
        queue.add(Action::soundOf(obj.handle, sound::kDollHeadImpact, obj.position, v));
    }
}

}  // namespace aa::sim::items
