// Dart (29): st::DartUtils::CreatePhysics — a dynamic body with linear and angular damping 0.01, the
// shaft polygon (one of two vertex tables by flip state), in simulation the sharp tip circle of group −8
// at the point, in set-up the selection box (x half-size through a double multiplication), and the mass
// override with the centre shifted toward the tip.
#include "aa/sim/animations.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kEpsilon = 0.0001f;   // st::Epsilon
constexpr float kStabDot = 0.65f;     // 0x3f266666
constexpr float kWobbleSpin = -6.283184f;   // DAT_0028317c (−2·Pi of the game)
constexpr float kWobbleStartPhase = 0.2f;
constexpr float kWobbleDuration = 0.5f;
}  // namespace

void createDart(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float s = obj.flipSign();
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    def.linearDamping = 0.01f;
    def.angularDamping = 0.01f;
    const int body = addBody(obj, world, def);
    const float tail = r * 0.2f;
    const float front = r * 0.95f;
    const float back = r * -0.95f;
    const float ntail = -tail;
    const Vec2 shaft[4] = {Vec2(front, 0.005f), Vec2(back, tail), Vec2(back, ntail), Vec2(front, -0.005f)};
    const Vec2 shaftFlipped[4] = {Vec2(back, -0.005f), Vec2(front, ntail), Vec2(front, tail), Vec2(back, 0.005f)};
    world.addPolygon(body, obj.flipped() ? shaftFlipped : shaft, 4, fixtureDef(5.0f, 0.5f, 0.0f, filters::kDynamic));
    if (mode == PhysicsMode::SetUp) {
        world.addBox(body, dmul(r, 1.1), r * 0.7f, fixtureDef(5.0f, 0.5f, 0.0f, filters::kSelection));
    } else {
        world.addCircle(body, 0.001f, Vec2(s * r, 0.0f),
                        fixtureDef(5.0f, 0.5f, 0.0f, filters::withGroup(filters::kDynamic, filters::kGroupDartTip)));
    }
    world.setMassData(body, 0.05f, Vec2(s * 0.2f * r, 0.0f), 0.001f);
}

void updateDartsSetUpMode(float dt, WorldState& state, Random& random) {
    // DartUtils::UpdateSetUpMode [verified: decompile + disassembly]: the same idle animation shape as
    // the scissors' snip (FUN_000e4314), skipped for a stuck dart.
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Dart || item.stuck) continue;
        item.wobbleTimer = item.wobbleTimer - dt;
        if (item.wobbling) {
            float step = (dt + dt) * item.wobbleDirection;
            item.wobbleAngle = item.wobbleAngle + dt * kWobbleSpin;
            step = step + step;
            item.wobblePhase = step + item.wobblePhase;
            if (item.wobbleDirection > 0.0f && item.wobblePhase >= 1.0f) item.wobbleDirection = -1.0f;
            if (item.wobbleTimer <= 0.0f) {
                item.wobbling = false;
                item.wobbleTimer = random.getFloat(GameItem::kIdleTimerMin, GameItem::kIdleTimerMax);
            }
            continue;
        }
        if (item.wobbleTimer > 0.0f) continue;
        item.wobbling = true;
        item.wobbleAngle = random.getFloat(-kPi, kPi);
        item.wobblePhase = kWobbleStartPhase;
        item.wobbleDirection = 1.0f;
        item.wobbleTimer = kWobbleDuration;
    }
}

void dartHandleStabCollision(GameItem& item, const PhysicsObject& dart, const PhysicsObject& other, int dartBody, int otherBody,
                             b2Fixture* dartFixture, Vec2 point, Vec2 relativeVelocity, ActionQueue& queue) {
    // DartUtils::HandleStabCollision [verified: decompile + disassembly]. The tip direction ignores the
    // flip state (Rotate(angle, (1, 0))).
    const float speed = length(relativeVelocity);
    if (speed < kEpsilon) return;
    const Vec2 dir = rotate(dart.angle, Vec2(1.0f, 0.0f));
    const Vec2 n = normalize(relativeVelocity);
    const float dot = n.y * dir.y + n.x * dir.x;
    if (!(dot >= kStabDot)) return;
    Action a(action::kAttachSharpObject, dart.handle, point);
    a.payload = dartBody;
    a.setValueInt(other.handle);   // +0x14: the stabbed object's handle
    a.extra = otherBody;
    a.amount = speed;
    queue.add(a);
    dartFixture->SetFilterData(filters::kNonCollidable);
    item.stuck = true;
}

void attachSharpObject(WorldState& state, PhysicsWorld& world, int dartHandle, int dartBody, int otherHandle, int otherBody,
                       Vec2 point, float speed, ActionQueue& queue) {
    // GameItemUtils::AttachSharpObject [verified]: a revolute joint (collideConnected, a motor of speed 0
    // and torque 4·v²) between the dart body and the stabbed body at the contact point; sound 0x16.
    const int dartItem = state.handles.lookup(dartHandle);
    const int otherItem = state.handles.lookup(otherHandle);
    if (dartItem < 0 || otherItem < 0) return;
    const PhysicsObject& dart = state.objects[static_cast<std::size_t>(state.items[static_cast<std::size_t>(dartItem)].objectIndex)];
    const PhysicsObject& other = state.objects[static_cast<std::size_t>(state.items[static_cast<std::size_t>(otherItem)].objectIndex)];
    float v = speed / 5.0f;
    if (1.0f - v < 0.0f) v = 1.0f;
    if (v - 0.1f < 0.0f) v = 0.1f;
    b2RevoluteJointDef jd;
    jd.Initialize(world.body(dart.bodies[static_cast<std::size_t>(dartBody)]),
                  world.body(other.bodies[static_cast<std::size_t>(otherBody)]), point);
    jd.collideConnected = true;
    jd.enableMotor = true;
    jd.motorSpeed = 0.0f;
    jd.maxMotorTorque = v * v * 4.0f;
    world.createRevolute(jd);   // not stored in any item field
    queue.add(Action::sound(sound::kDartStuck, point, v));
}

}  // namespace aa::sim::items
