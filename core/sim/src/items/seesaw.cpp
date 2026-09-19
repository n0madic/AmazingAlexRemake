// Seesaw (22): st::SeesawUtils::CreatePhysics — the fulcrum pentagon (static in simulation; density 2000
// even on the set-up selection box), the arm box on a dynamic bullet body and the pivot revolute joint
// (limits ±40°, motor on with 0.02 N·m). Both bodies are bullets.
#include "aa/sim/items/items.h"
#include "item_common.h"

#include <cmath>

namespace aa::sim::items {

namespace {
constexpr float kSeesawSoundSpeed = 4.0f;
}  // namespace

void createSeesaw(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    b2BodyDef def = itemBodyDef(obj, mode, b2_staticBody);
    def.bullet = true;
    const int fulcrum = addBody(obj, world, def);
    const float a = r * 0.2f;
    const float ax = a * 0.4f;
    const float ay = a * 0.55f;
    const float nax = a * -0.4f;
    const float na = -a;
    const Vec2 pentagon[5] = {Vec2(a, 0.0f), Vec2(ax, ay), Vec2(nax, ay), Vec2(na, 0.0f), Vec2(0.0f, na)};
    const b2FixtureDef fd = fixtureDef(2000.0f, 0.9f, 0.0f, filters::kDynamic);
    world.addPolygon(fulcrum, pentagon, 5, fd);
    if (mode == PhysicsMode::SetUp) {
        world.addBox(fulcrum, r, r * 0.3f, fixtureDef(2000.0f, 0.9f, 0.0f, filters::kSelection));
    }
    b2BodyDef armDef;
    armDef.type = b2_dynamicBody;
    armDef.position = obj.position;
    armDef.angle = obj.angle;
    armDef.bullet = true;
    const int arm = addBody(obj, world, armDef);
    world.addBoxAt(arm, r * 0.98f, r * 0.05f, Vec2(0.0f, 0.024f), 0.0f, fixtureDef(20.0f, 0.9f, 0.0f, filters::kDynamic));
    b2RevoluteJointDef jd;
    jd.Initialize(world.body(fulcrum), world.body(arm), obj.position);
    jd.lowerAngle = kDegToRad * -40.0f;
    jd.upperAngle = kDegToRad * 40.0f;
    jd.enableLimit = true;
    jd.enableMotor = true;
    jd.motorSpeed = 0.0f;
    jd.maxMotorTorque = 0.02f;
    obj.addJoint(world.createRevolute(jd));
}

void updateSeesaws(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // SeesawUtils::Update [verified].
    (void)dt;
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Seesaw) continue;
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        const auto* joint = static_cast<const b2RevoluteJoint*>(world.joint(obj.joints[0]));
        const float speed = joint->GetJointSpeed();
        const int dir = speed >= 0.0f ? 1 : -1;
        if (item.seesawDirection == dir || !(std::fabs(speed) > kSeesawSoundSpeed)) continue;
        item.seesawDirection = dir;
        queue.add(Action::soundOf(obj.handle, sound::kSeesawMove, obj.position, 0.3f));
    }
}

}  // namespace aa::sim::items
