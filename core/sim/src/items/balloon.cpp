// Balloon (5): st::BalloonUtils::CreatePhysics — one dynamic body in both modes (no mode argument): the
// envelope circle, the knot triangle and the mass override (0.1 kg, I 0.01). No selection sensor: the
// balloon is larger than the minimum selection radius.
#include "aa/sim/attachments.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kPopTime = 0.15f;      // 0x3e19999a
constexpr float kPopRadius = 0.5f;
constexpr float kPopForce = 25.0f;     // 0x41c80000
// DAT_00281144..54: lift 350, altitude weight 0.2, drag 400, load gain 3.2, drag cap 5000.
constexpr float kLift = 350.0f;
constexpr float kAltitudeWeight = 0.2f;
constexpr float kDrag = 400.0f;
constexpr float kLoadGain = 3.2f;
constexpr float kDragCap = 5000.0f;
constexpr float kLn2 = 0.6931472f;
constexpr float kKnotOffsetFactor = 0.8f;
constexpr float kAltitudeCeiling = 3.0f;
}  // namespace

void createBalloon(PhysicsObject& obj, PhysicsWorld& world) {
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);
    const b2FixtureDef fd = fixtureDef(0.01f, kDefaultFriction, 0.3f, filters::dynamicPlus());
    world.addCircle(body, obj.halfSize, Vec2(0.0f, 0.0f), fd);
    const Vec2 knot[3] = {Vec2(0.0f, -0.25f), Vec2(0.1f, -0.1f), Vec2(-0.1f, -0.1f)};
    world.addPolygon(body, knot, 3, fd);
    world.setMassData(body, 0.1f, Vec2(0.0f, 0.0f), 0.01f);
}

void popBalloon(GameItem& item, const PhysicsObject& obj, ActionQueue& queue) {
    // BalloonUtils::Pop [verified]: no "already popped" test — a second sharp contact in the same step
    // restarts the timer and queues the actions again.
    item.popped = true;
    item.popTimer = kPopTime;
    queue.add(Action::sound(sound::kBalloonPop, obj.position, 1.0f));
    Action force(action::kForceToRadius, 0, obj.position);
    force.setPayloadFloat(kPopRadius);
    force.value = kPopForce;
    queue.add(force);
}

void updateBalloons(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // BalloonUtils::Update [verified: decompile + disassembly].
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Balloon) continue;
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if (!item.popped) {
            const Vec2 knot = rotate(obj.angle, Vec2(0.0f, obj.halfSize * kKnotOffsetFactor));
            b2Body* body = world.body(obj.bodies[0]);
            const b2Vec2 v = body->GetLinearVelocity();
            const float vx400 = v.x * kDrag;
            const float vy400 = kDrag * v.y;
            float dragX = v.x * vx400;
            float dragY = v.y * vy400;
            const float signX = v.x < 0.0f ? -1.0f : 1.0f;
            if (dragX - kDragCap >= 0.0f) dragX = kDragCap;
            const float signY = v.y < 0.0f ? -1.0f : 1.0f;
            if (dragY - kDragCap >= 0.0f) dragY = kDragCap;
            // The load: the object on the far end of a rope attached to the balloon's knot.
            float load = 0.0f;
            const AttachmentRecord& rec = obj.attachments[0];
            if (rec.otherObject != -1 && state.objects[static_cast<std::size_t>(rec.otherObject)].type == ItemType::Rope) {
                const PhysicsObject& rope = state.objects[static_cast<std::size_t>(rec.otherObject)];
                const int farEnd = rope.attachments[static_cast<std::size_t>(rec.otherPoint == 0 ? 1 : 0)].otherObject;
                if (farEnd != -1) load = objectMass(state.objects[static_cast<std::size_t>(farEnd)], world);
            }
            float h = (kAltitudeCeiling - obj.position.y) / kAltitudeCeiling;
            if (1.0f - h < 0.0f) h = 1.0f;
            if (h < 0.0f) h = 0.0f;
            const float lg = logF(load + 1.0f);
            if (body->GetType() == b2_dynamicBody) {
                float weight = 1.0f - kAltitudeWeight;
                weight = weight + kAltitudeWeight * h;
                const float gain = kLoadGain * (lg / kLn2);
                float lift = 1.0f;
                lift = lift + gain * weight;
                float fx = 0.0f;
                fx = fx - signX * dragX;
                const float dy = signY * dragY;
                const float fy = lift * kLift - dy;
                const b2Vec2 p = body->GetPosition();
                body->ApplyForce(b2Vec2(dt * fx, dt * fy), b2Vec2(p.x + knot.x, p.y + knot.y));
            }
        } else {
            if (item.popTimer < 0.0f) continue;
            if (obj.bodyCount > 0) {
                removeAllAttachments(state, world, obj.index);
                world.destroyPhysics(obj);
                obj.state = static_cast<std::uint8_t>(obj.state | object_state::kActivated);
            }
            item.popTimer = item.popTimer - dt;
            if (item.popTimer < 0.0f) queue.add(Action(action::kRemoveItem, obj.handle));
        }
    }
}

}  // namespace aa::sim::items
