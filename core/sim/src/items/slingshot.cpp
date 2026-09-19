// Slingshot (34): st::SlingshotUtils::CreatePhysics — the frame body (static in simulation) with one box
// at (0.05·s, −0.05) and, in set-up, the selection box plus a second body at the pouch (frame position +
// pouch vector rotated by the item angle) carrying a selection circle, so the pouch can be dragged.
#include "aa/sim/action.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"
#include "../touch_handler.h"

namespace aa::sim::items {

namespace {
constexpr float kPouchRestX = -0.01f;    // DAT_00293600 (0) − 0.01
constexpr float kPouchRestY = 0.076f;    // DAT_00293604 (0x3d9ba5e3) + 0
constexpr float kPouchSpring = 2000.0f;
constexpr float kPouchDamping = -20.0f;
constexpr float kLaunchGain = 0.6f;
constexpr float kLaunchForce = 2500.0f;
constexpr float kLn2 = 0.6931472f;
constexpr float kEpsilon = 0.0001f;       // st::Epsilon
constexpr uint16 kLaunchQueryMask = 0xff;  // GetNearestIntersectingObjectWithFilter(…, 0xff)
}  // namespace

void createSlingshot(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const float s = obj.flipSign();
    const int frame = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    // Every fixture carries its body index + 1 as user data (docs/08 item 34): the pick query recognises
    // the pouch body (2) so a ball dragged over the pouch takes the pouch along [verified: FUN_000d49f8].
    b2FixtureDef frameDef = fixtureDef(1.0f, 0.7f, 0.4f, filters::kDynamic);
    frameDef.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
    world.addBoxAt(frame, 0.02f, 0.06f, Vec2(s * 0.05f, -0.05f), 0.0f, frameDef);
    if (mode == PhysicsMode::SetUp) {
        b2FixtureDef selection = selectionDef();
        selection.userData = frameDef.userData;
        world.addBox(frame, 0.07f, 0.12f, selection);
        const Vec2 pouch = slingshotPouchPosition(obj, item);
        b2BodyDef def;
        def.type = b2_dynamicBody;
        def.position = pouch;
        def.angle = obj.angle;
        const int pouchBody = addBody(obj, world, def);
        b2FixtureDef pouchDef = fixtureDef(1.0f, 0.7f, 0.4f, filters::kSelection);
        pouchDef.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(2));
        world.addCircle(pouchBody, kMinSelectionRadius, Vec2(0.0f, 0.0f), pouchDef);
    }
}

Vec2 slingshotPouchPosition(const PhysicsObject& obj, const GameItem& item) {
    // FUN_000ebe98: the pouch vector (Slingshot+0xC) with x mirrored by the flip sign (a hidden VFP
    // argument), rotated by the item angle and added to the position.
    const Vec2 v = rotate(obj.angle, Vec2(obj.flipSign() * item.endVector.x, item.endVector.y));
    return Vec2(obj.position.x + v.x, obj.position.y + v.y);
}


void slingshotUpdatePos(GameItem& item, PhysicsObject& obj, PhysicsWorld& world, int bodyIndex, Vec2 target, ActionQueue& queue) {
    // SlingshotUtils::UpdatePos [verified]. Slingshot+0xC = pouch vector, +0x1C = the set-up timer
    // (SlingshotUtils::UpdateSetUpMode adds dt to it; the sounds wait until it passed 0.5 s).
    if (bodyIndex == 0) {
        obj.position = target;
        world.setTransform(obj.bodies[0], obj.position, obj.angle);
    } else {
        const Vec2 prev = item.endVector;
        const Vec2 d(target.x - obj.position.x, target.y - obj.position.y);
        const Vec2 r = rotate(-obj.angle, d);
        Vec2 v(obj.scale.x * r.x, r.y * obj.scale.y);
        const float len = length(v);
        if (0.5f <= len) v = Vec2((v.x / len) * 0.5f, (v.y / len) * 0.5f);
        item.endVector = v;
        if (0.5f < item.setUpTimer) {
            const float before = prev.y * prev.y + prev.x * prev.x;
            const float after = v.y * v.y + v.x * v.x;
            int id = -1;
            if (before + 0.002f < after) id = sound::kSlingshotStretch;
            else if (after < before - 0.002f) id = sound::kSlingshotRelease;
            if (id >= 0) {
                queue.add(Action::sound(id, obj.position, 1.0f));
                item.setUpTimer = 0.0f;
            }
        }
    }
    if (obj.bodyCount > 1) {
        b2Body* pouch = world.body(obj.bodies[1]);
        world.setTransform(obj.bodies[1], slingshotPouchPosition(obj, item), pouch->GetAngle());
    }
}

void updateSlingshots(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // SlingshotUtils::Update [verified: decompile + disassembly]. The rest point is
    // (DAT_00293600 − 0.01, DAT_00293604 + 0) = (−0.01, 0.076) in the item's local frame.
    const Vec2 rest(kPouchRestX, kPouchRestY);
    for (GameItem& item : state.items) {
        if (item.type != ItemType::Slingshot) continue;
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if (!item.fired) {
            item.fired = true;
            queue.add(Action::sound(sound::kSlingshotFire, obj.position, 1.0f));
            const Vec2 rotatedRest = rotate(obj.angle, rest);
            const Vec2 pouch = slingshotPouchPosition(obj, item);
            PickResult hit;
            if (pickObject(state, world, pouch, false, false, kLaunchQueryMask, hit)) {
                const PhysicsObject& target = state.objects[static_cast<std::size_t>(hit.object)];
                const float lg = logF(objectMass(target, world) + 1.0f);
                b2Body* body = world.body(target.bodies[static_cast<std::size_t>(hit.bodyIndex)]);
                if (body->GetType() == b2_dynamicBody) {
                    const float f = (lg / kLn2) * kLaunchGain;
                    const float tx = obj.position.x + rotatedRest.x;
                    const float ty = obj.position.y + rotatedRest.y;
                    const float dx = tx - pouch.x;
                    const float dy = ty - pouch.y;
                    body->ApplyForce(b2Vec2(f * (dx * kLaunchForce), f * (dy * kLaunchForce)), pouch);
                }
                item.loadedHandle = target.handle;
            }
        }
        // The pouch spring: a critically damped pull toward the rest point, integrated explicitly.
        const float dx = rest.x - item.endVector.x;
        const float dy = rest.y - item.endVector.y;
        const float len = length(Vec2(dx, dy));
        float dirX = 1.0f;
        float dirY = 0.0f;
        if (len > kEpsilon) {
            dirX = dx / len;
            dirY = dy / len;
        }
        const float pull = len * kPouchSpring;
        float ax = item.pouchVelocity.x * kPouchDamping;
        float ay = item.pouchVelocity.y * kPouchDamping;
        ax = ax + pull * dirX;
        ay = ay + pull * dirY;
        item.pouchVelocity.x = item.pouchVelocity.x + dt * ax;
        item.pouchVelocity.y = item.pouchVelocity.y + dt * ay;
        item.endVector.x = item.endVector.x + dt * item.pouchVelocity.x;
        item.endVector.y = item.endVector.y + dt * item.pouchVelocity.y;
    }
}

bool slingshotShouldCollide(const GameItem& item, int otherHandle) {
    // SlingshotUtils::ShouldCollide [verified]: an unfired slingshot collides with everything.
    if (!item.fired) return true;
    return item.loadedHandle != otherHandle;
}

}  // namespace aa::sim::items
