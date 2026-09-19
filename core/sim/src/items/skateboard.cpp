// Skateboard (20): st::SkateboardUtils::CreatePhysics — the deck body with three boxes (centre, nose and
// tail, the ends tilted by Pi/5.5 and Pi/8.5 with the game's Pi global; x offsets through double
// multiplications), the set-up selection box, two wheel bodies (angular damping 0.3) placed by rotating
// their offsets with the item angle, and one b2LineJoint (= vendored b2WheelJoint) per wheel: axis
// (0, −1) rotated, collideConnected, no motor, 15 Hz / 0.8.
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kDeckHalfThickness = 0.014f;
constexpr float kWheelRadius = 0.04f;
constexpr float kWheelY = -0.08f;
}  // namespace

void createSkateboard(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float s = obj.flipSign();
    const double sd = static_cast<double>(s);
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int deck = addBody(obj, world, def);
    const b2FixtureDef deckFd = fixtureDef(40.0f, 0.6f, 0.0f, filters::dynamicPlus());
    world.addBoxAt(deck, r * 0.67f, kDeckHalfThickness, Vec2(static_cast<float>(sd * -0.02), 0.0f), 0.0f, deckFd);
    const float noseX = static_cast<float>(sd * (static_cast<double>(-r) * 0.81));
    world.addBoxAt(deck, r * 0.15f, kDeckHalfThickness, Vec2(noseX, 0.025f), (-kPi / 5.5f) * s, deckFd);
    const float tailX = static_cast<float>(sd * (static_cast<double>(r) * 0.78));
    world.addBoxAt(deck, r * 0.19f, kDeckHalfThickness, Vec2(tailX, 0.025f), (kPi / 8.5f) * s, deckFd);
    if (mode == PhysicsMode::SetUp) {
        world.addBoxAt(deck, r, r * 0.28f, Vec2(0.0f, -0.02f), 0.0f, selectionDef());
    }
    b2BodyDef wheelDef;
    wheelDef.type = b2_dynamicBody;
    wheelDef.angle = obj.angle;
    wheelDef.angularDamping = 0.3f;
    const Vec2 back = rotate(obj.angle, Vec2(-(r * 0.52f) * s, kWheelY));
    wheelDef.position = Vec2(obj.position.x + back.x, obj.position.y + back.y);
    const int backWheel = addBody(obj, world, wheelDef);
    const Vec2 front = rotate(obj.angle, Vec2(r * 0.56f * s, kWheelY));
    wheelDef.position = Vec2(obj.position.x + front.x, front.y + obj.position.y);
    const int frontWheel = addBody(obj, world, wheelDef);
    const b2FixtureDef wheelFd = fixtureDef(40.0f, kDefaultFriction, 0.0f, filters::dynamicPlus());
    world.addCircle(backWheel, kWheelRadius, Vec2(0.0f, 0.0f), wheelFd);
    world.addCircle(frontWheel, kWheelRadius, Vec2(0.0f, 0.0f), wheelFd);
    const Vec2 axis = rotate(obj.angle, Vec2(0.0f, -1.0f));
    for (const int wheel : {backWheel, frontWheel}) {
        b2WheelJointDef jd;
        jd.collideConnected = true;
        b2Body* w = world.body(wheel);
        jd.Initialize(world.body(deck), w, w->GetWorldCenter(), axis);
        jd.enableMotor = false;
        jd.maxMotorTorque = 0.0f;
        jd.motorSpeed = 0.0f;
        jd.frequencyHz = 15.0f;
        jd.dampingRatio = 0.8f;
        obj.addJoint(world.createWheel(jd));
    }
}

}  // namespace aa::sim::items
