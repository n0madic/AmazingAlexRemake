// Trapdoor (37) and TrapdoorLever (38).
//
// TrapdoorLever: st::TrapdoorLeverUtils::CreatePhysics — base body (static in simulation) and lever
// body created first, then the base box, the lever box and the revolute joint with limits ±Pi/5 (the game's
// Pi global divided by 5 at run time) and a 0.2 N·m motor; in set-up the selection box on the base.
// The box sizes are `.bss` constants (0.08, 0.04, 0.02, 0.14) read under emulation.
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

#include <cmath>

namespace aa::sim::items {

namespace {
constexpr float kLeverSoundSpeed = 5.0f;
constexpr float kLeverUnlockDivisor = 10.0f;   // |angle| > Pi / 10 opens the trapdoor
}  // namespace

void createTrapdoorLever(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const int base = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    b2BodyDef leverDef = itemBodyDef(obj, mode, b2_dynamicBody);
    const int lever = addBody(obj, world, leverDef);
    const b2FixtureDef fd = fixtureDef(8.0f, 0.7f, 0.4f, filters::kDynamic);
    world.addBox(base, 0.08f, 0.04f, fd);
    world.addBoxAt(lever, 0.02f, 0.14f, Vec2(0.0f, 0.14f), 0.0f, fd);
    b2RevoluteJointDef jd;
    jd.Initialize(world.body(base), world.body(lever), obj.position);
    jd.enableLimit = true;
    jd.lowerAngle = -kPi / 5.0f;
    jd.upperAngle = kPi / 5.0f;
    jd.enableMotor = true;
    jd.motorSpeed = 0.0f;
    jd.maxMotorTorque = 0.2f;
    obj.addJoint(world.createRevolute(jd));
    if (mode == PhysicsMode::SetUp) {
        world.addBoxAt(base, 0.12f, dmul(r, 1.2), Vec2(0.0f, r * 0.85f), 0.0f, selectionDef());
    }
}

void unlockTrapdoor(const PhysicsObject& trapdoor, PhysicsWorld& world, ActionQueue& queue) {
    // TrapdoorUtils::Unlock [verified]: the door bodies (1 and 2) become dynamic.
    world.body(trapdoor.bodies[1])->SetType(b2_dynamicBody);
    world.body(trapdoor.bodies[2])->SetType(b2_dynamicBody);
    queue.add(Action::soundOf(trapdoor.handle, sound::kTrapdoorOpen, trapdoor.position, 0.5f));
}

void updateTrapdoorLevers(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // TrapdoorLeverUtils::Update [verified: decompile + disassembly].
    (void)dt;
    for (GameItem& item : state.items) {
        if (item.type != ItemType::TrapdoorLever || item.leverUnlocked) continue;
        const PhysicsObject& lever = state.objects[static_cast<std::size_t>(item.objectIndex)];
        const auto* joint = static_cast<const b2RevoluteJoint*>(world.joint(lever.joints[0]));
        if (!item.leverSounded && std::fabs(joint->GetJointSpeed()) > kLeverSoundSpeed) {
            item.leverSounded = true;
            queue.add(Action::soundOf(lever.handle, sound::kTrapdoorLever, lever.position, 0.5f));
        }
        if (std::fabs(joint->GetJointAngle()) <= kPi / kLeverUnlockDivisor) continue;
        const int trapdoorItem = state.handles.lookup(item.stateWord);
        if (trapdoorItem < 0) continue;   // the original would dereference a null item
        // A lever without a pair (itemData 0) must not open whatever a generation-0 handle resolves to —
        // sandbox files carry the auto-added bound under exactly that handle (cf. rc_controller.cpp).
        if (Handle::typeOf(item.stateWord) != ItemType::Trapdoor) continue;
        const PhysicsObject& trapdoor = state.objects[static_cast<std::size_t>(state.items[static_cast<std::size_t>(trapdoorItem)].objectIndex)];
        unlockTrapdoor(trapdoor, world, queue);
        item.leverUnlocked = true;
    }
}

}  // namespace aa::sim::items

// Trapdoor (37): st::TrapdoorUtils::CreatePhysics — the frame body (static in simulation) with the two
// shelf boxes (double products), the set-up selection box, then each door: a body 2.09·d beside the
// centre (d = 0.3·r), a box from the hinge edge, and a revolute joint at the hinge (2d + 0.025, −0.03)
// with limits 0…±Pi·1.2·0.5 (the game's Pi global), motor 0.005 N·m. The doors are static until
// TrapdoorUtils::Unlock. Legacy layouts add the built-in lever body (Trapdoor+8).
namespace aa::sim::items {

void createTrapdoor(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const double rd = static_cast<double>(r);
    const int frame = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    const b2FixtureDef fd = fixtureDef(50.0f, 0.5f, 0.3f, filters::kStatic);
    const float shelfHx = static_cast<float>(rd * 0.16);
    const float shelfHy = r * 0.12f;
    world.addBoxAt(frame, shelfHx, shelfHy, Vec2(static_cast<float>(rd * -0.8), 0.01f), 0.0f, fd);
    world.addBoxAt(frame, shelfHx, shelfHy, Vec2(static_cast<float>(rd * 0.8), 0.01f), 0.0f, fd);
    if (mode == PhysicsMode::SetUp) world.addBox(frame, r, static_cast<float>(rd * 0.2), selectionDef());

    const float d = r * 0.3f;
    const float nd = -d;
    const float doorHy = r * 0.04f;
    const float hinge = d + d + 0.025f;
    const float swing = (kPi * 1.2f) * 0.5f;
    const Vec2 p = obj.position;
    auto at = [&](Vec2 local) {
        const Vec2 v = rotate(obj.angle, local);
        return Vec2(p.x + v.x, p.y + v.y);
    };
    b2BodyDef doorDef = itemBodyDef(obj, mode, b2_staticBody);
    // Left door.
    doorDef.position = at(Vec2(nd * 2.09f, 0.0f));
    const int left = addBody(obj, world, doorDef);
    world.addBoxAt(left, d, doorHy, Vec2(d, 0.0f), 0.0f, fd);
    b2RevoluteJointDef jd;
    jd.Initialize(world.body(frame), world.body(left), at(Vec2(-hinge, -0.03f)));
    jd.enableLimit = true;
    jd.lowerAngle = -swing;
    jd.upperAngle = 0.0f;
    jd.enableMotor = true;
    jd.motorSpeed = 0.0f;
    jd.maxMotorTorque = 0.005f;
    obj.addJoint(world.createRevolute(jd));
    // Right door.
    doorDef.position = at(Vec2(d * 2.09f, 0.0f));
    const int right = addBody(obj, world, doorDef);
    world.addBoxAt(right, d, doorHy, Vec2(nd, 0.0f), 0.0f, fd);
    b2RevoluteJointDef jd2;
    jd2.Initialize(world.body(frame), world.body(right), at(Vec2(hinge, -0.03f)));
    jd2.enableLimit = true;
    jd2.lowerAngle = 0.0f;
    jd2.upperAngle = swing;
    jd2.enableMotor = true;
    jd2.motorSpeed = 0.0f;
    jd2.maxMotorTorque = 0.005f;
    obj.addJoint(world.createRevolute(jd2));
    if (item.builtInController) {
        b2BodyDef ldef;
        ldef.type = b2_dynamicBody;
        ldef.position = Vec2(p.x + -0.5f, p.y + 0.0f);
        const int lever = addBody(obj, world, ldef);
        const b2FixtureDef lfd = fixtureDef(0.0f, 0.7f, 0.4f, filters::kDynamic);
        world.addBox(lever, 0.08f, 0.04f, lfd);
        world.addBoxAt(lever, 0.02f, 0.14f, Vec2(0.0f, 0.14f), 0.0f, lfd);
    }
}

}  // namespace aa::sim::items
