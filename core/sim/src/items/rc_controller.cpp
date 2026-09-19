// RCController (36): st::RadioControllerUtils::CreatePhysics — base body and button body (0.072 m above,
// rotated with the item; both dynamic in every mode), the two boxes, a vertical prismatic joint (limits
// −0.04…0, motor force 2, speed 1) anchored at the base's centre of mass, and the set-up selection box.
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
const Vec2 kButtonOffset(0.0f, 0.072f);   // `.bss` 0x281418
constexpr double kPressTranslation = -0.03;   // 0xbf9eb851eb851eb8
constexpr float kTruckMotorSpeed = 15.0f;
}

void createRCController(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int base = addBody(obj, world, def);
    const Vec2 up = rotate(obj.angle, kButtonOffset);
    def.position = Vec2(obj.position.x + up.x, obj.position.y + up.y);
    const int button = addBody(obj, world, def);
    world.addBox(base, 0.1f, 0.04f, fixtureDef(12.0f, 0.7f, 0.4f, filters::kDynamic));
    world.addBox(button, 0.05f, 0.038f, fixtureDef(8.0f, 0.7f, 0.4f, filters::kDynamic));
    b2PrismaticJointDef jd;
    b2Body* a = world.body(base);
    jd.Initialize(a, world.body(button), a->GetWorldCenter(), rotate(a->GetAngle(), Vec2(0.0f, 1.0f)));
    jd.lowerTranslation = -0.04f;
    jd.upperTranslation = 0.0f;
    jd.enableLimit = true;
    jd.enableMotor = true;
    jd.maxMotorForce = 2.0f;
    jd.motorSpeed = 1.0f;
    obj.addJoint(world.createPrismatic(jd));
    if (mode == PhysicsMode::SetUp) {
        const float r = obj.halfSize;
        world.addBoxAt(base, r, r * 0.8f, Vec2(0.0f, r * 0.25f), 0.0f, selectionDef());
    }
}

void updateRadioControllers(float dt, WorldState& state, PhysicsWorld& world, ActionQueue& queue) {
    // RadioControllerUtils::Update [verified: decompile + disassembly]. The translation test is a
    // double comparison against −0.03.
    (void)dt;
    for (GameItem& item : state.items) {
        if (item.type != ItemType::RCController) continue;
        const int pairedItem = state.handles.lookup(item.stateWord);
        if (pairedItem < 0) continue;
        GameItem& paired = state.items[static_cast<std::size_t>(pairedItem)];
        const ItemType pairedType = Handle::typeOf(item.stateWord);
        if (pairedType != ItemType::RCTruck && pairedType != ItemType::Helicopter) continue;
        const PhysicsObject& controller = state.objects[static_cast<std::size_t>(item.objectIndex)];
        const PhysicsObject& pairedObj = state.objects[static_cast<std::size_t>(paired.objectIndex)];
        const auto* button = static_cast<const b2PrismaticJoint*>(world.joint(controller.joints[0]));
        const bool down = static_cast<double>(button->GetJointTranslation()) < kPressTranslation;
        if (pairedType == ItemType::RCTruck) {
            auto* back = static_cast<b2WheelJoint*>(world.joint(pairedObj.joints[0]));
            auto* front = static_cast<b2WheelJoint*>(world.joint(pairedObj.joints[1]));
            if (down) {
                if (item.buttonPressed) continue;
                const float dir = pairedObj.scale.x < 0.0f ? 1.0f : -1.0f;
                const float speed = dir * kTruckMotorSpeed;
                back->SetMotorSpeed(speed);
                front->SetMotorSpeed(speed);
                queue.add(Action::soundOf(controller.handle, sound::kRCButtonClick, controller.position, 1.0f));
                item.buttonPressed = true;
            } else {
                if (!item.buttonPressed) continue;
                back->SetMotorSpeed(0.0f);
                front->SetMotorSpeed(0.0f);
                item.buttonPressed = false;
            }
        } else {
            if (down) {
                if (item.buttonPressed) continue;
                helicopterTurnOn(paired, pairedObj, world);
                queue.add(Action::soundOf(controller.handle, sound::kRCButtonClick, controller.position, 1.0f));
                item.buttonPressed = true;
            } else {
                if (!item.buttonPressed) continue;
                helicopterTurnOff(paired, pairedObj, world);
                item.buttonPressed = false;
            }
        }
    }
}

}  // namespace aa::sim::items
