// RCTruck (35): st::TruckUtils::CreatePhysics — chassis body with two boxes (the half-width of both is a
// *double* product, the centre x of both a float product — they differ in the last bit) and the cabin
// triangle (one of two vertex tables by flip state), the set-up selection box, two wheel bodies
// (angular damping 0.3) and their b2LineJoints (motor on with 5 N·m, 25 Hz / 0.8), and the legacy
// built-in controller body when Truck+8 is set.
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kWheelRadius = 0.085f;
constexpr float kWheelY = -0.1f;
}  // namespace

void createRCTruck(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float s = obj.flipSign();
    const float w = r * 0.85f;
    const float h = (r * 0.9f * 116.0f) / 220.0f;
    const double hd = static_cast<double>(h);
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int chassis = addBody(obj, world, def);
    const b2FixtureDef body = fixtureDef(50.0f, 0.7f, 0.4f, filters::kDynamic);
    const float hx = dmul(w, 0.47);
    world.addBoxAt(chassis, hx, static_cast<float>(hd * 0.2), Vec2(w * -0.47f * s, static_cast<float>(hd * -0.2)), 0.0f, body);
    const float hy = static_cast<float>(hd * 0.4);
    world.addBoxAt(chassis, hx, hy, Vec2(w * 0.47f * s, h * 0.0f), 0.0f, body);
    const float top = h * 0.98f;
    const Vec2 cabin[3] = {Vec2(0.0f, top), Vec2(0.0f, hy), Vec2(w * 0.6f, hy)};
    const Vec2 cabinFlipped[3] = {Vec2(w * -0.6f, hy), Vec2(0.0f, hy), Vec2(0.0f, top)};
    world.addPolygon(chassis, obj.flipped() ? cabinFlipped : cabin, 3, body);
    if (mode == PhysicsMode::SetUp) world.addBox(chassis, w, h, selectionDef());

    b2BodyDef wheelDef;
    wheelDef.type = b2_dynamicBody;
    wheelDef.angle = obj.angle;
    wheelDef.angularDamping = 0.3f;
    const Vec2 back = rotate(obj.angle, Vec2(-(w * 0.6f) * s, kWheelY));
    wheelDef.position = Vec2(obj.position.x + back.x, obj.position.y + back.y);
    const int backWheel = addBody(obj, world, wheelDef);
    const Vec2 front = rotate(obj.angle, Vec2(w * 0.58f * s, kWheelY));
    wheelDef.position = Vec2(front.x + obj.position.x, obj.position.y + front.y);
    const int frontWheel = addBody(obj, world, wheelDef);
    const b2FixtureDef wheelFd = fixtureDef(8.0f, 1.0f, 0.4f, filters::kDynamic);
    world.addCircle(backWheel, kWheelRadius, Vec2(0.0f, 0.0f), wheelFd);
    world.addCircle(frontWheel, kWheelRadius, Vec2(0.0f, 0.0f), wheelFd);
    const Vec2 axis = rotate(obj.angle, Vec2(0.0f, -1.0f));
    for (const int wheel : {backWheel, frontWheel}) {
        b2WheelJointDef jd;
        b2Body* wb = world.body(wheel);
        jd.Initialize(world.body(chassis), wb, wb->GetWorldCenter(), axis);
        jd.enableMotor = true;
        jd.maxMotorTorque = 5.0f;
        jd.motorSpeed = 0.0f;
        jd.frequencyHz = 25.0f;
        jd.dampingRatio = 0.8f;
        obj.addJoint(world.createWheel(jd));
    }
    if (item.builtInController) {
        b2BodyDef cdef;
        cdef.type = b2_dynamicBody;
        cdef.position = Vec2(obj.position.x + -0.5f, static_cast<float>(hd * -0.8) + obj.position.y);
        const int controller = addBody(obj, world, cdef);
        const b2FixtureDef cfd = fixtureDef(0.0f, 0.7f, 0.4f, filters::kDynamic);
        world.addBox(controller, 0.1f, 0.04f, cfd);
        world.addBox(controller, 0.05f, 0.038f, cfd);
    }
}

}  // namespace aa::sim::items
