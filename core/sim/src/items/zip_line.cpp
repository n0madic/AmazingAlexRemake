// ZipLine (42): st::ZipLineUtils::CreatePhysics — anchor A at the item position, anchor B at the end
// vector pulled back 0.02 m along the line (both static in simulation, non-collidable circles of group
// 11), the trolley body 0.15 m along the line (its angle flipped by Pi when the line's down vector points
// up) with the triangle fixture of group 11; in set-up three selection circles and a fourth body with a
// thin Rope-filter box along the line; in simulation the prismatic joint along the line (limit 0…length,
// motor force 2). The line angle is the double atan2 rounded to float.
#include "aa/sim/attachments.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr int16 kGroupZipLine = 11;
constexpr float kAnchorRadius = 0.02f;
}  // namespace

void createZipLine(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const Vec2 end = item.endVector;
    const Vec2 p = obj.position;
    const float a = atan2F(end.y, end.x);
    b2BodyDef def;
    def.type = mode == PhysicsMode::Simulation ? b2_staticBody : b2_dynamicBody;
    def.position = p;
    const int anchorA = addBody(obj, world, def);
    const b2FixtureDef anchorFd =
        fixtureDef(1.0f, 0.5f, 0.4f, filters::withGroup(filters::kNonCollidable, kGroupZipLine));
    world.addCircle(anchorA, kAnchorRadius, Vec2(0.0f, 0.0f), anchorFd);
    const float ex = p.x + end.x;
    const float ey = p.y + end.y;
    const Vec2 n = normalize(end);
    def.position = Vec2(ex - n.x * kAnchorRadius, ey - n.y * kAnchorRadius);
    def.angle = a;
    const int anchorB = addBody(obj, world, def);
    world.addCircle(anchorB, kAnchorRadius, Vec2(0.0f, 0.0f), anchorFd);

    const Vec2 down = rotate(a, Vec2(0.0f, -1.0f));
    const float trolleyAngle = down.y < 0.0f ? a : a + kPi;
    const Vec2 along = rotate(a, Vec2(0.15f, 0.0f));
    b2BodyDef trolleyDef;
    trolleyDef.type = b2_dynamicBody;
    trolleyDef.position = Vec2(p.x + along.x, along.y + p.y);
    trolleyDef.angle = trolleyAngle;
    const int trolley = addBody(obj, world, trolleyDef);
    const b2FixtureDef trolleyFd = fixtureDef(100.0f, 0.5f, 0.3f, filters::withGroup(filters::kDynamic, kGroupZipLine));
    const Vec2 wedge[3] = {Vec2(0.0f, -0.15f), Vec2(0.13f, 0.04f), Vec2(-0.13f, 0.04f)};
    world.addPolygon(trolley, wedge, 3, trolleyFd);
    if (mode == PhysicsMode::SetUp) {
        const b2FixtureDef sel = selectionDef();
        world.addCircle(anchorA, kMinSelectionRadius, Vec2(-0.06f, 0.0f), sel);
        world.addCircle(anchorA, kMinSelectionRadius, Vec2(0.15f, 0.0f), sel);
        world.addCircle(anchorB, kMinSelectionRadius, Vec2(0.06f, 0.0f), sel);
        b2BodyDef lineDef;
        lineDef.type = b2_dynamicBody;
        lineDef.position = p;
        lineDef.angle = a;
        const int line = addBody(obj, world, lineDef);
        const float half = length(end) * 0.5f;
        world.addBoxAt(line, half, 0.02f, Vec2(half, 0.0f), 0.0f, fixtureDef(100.0f, 0.5f, 0.3f, filters::kRope));
    } else {
        b2PrismaticJointDef jd;
        b2Body* t = world.body(trolley);
        jd.enableLimit = true;
        jd.lowerTranslation = 0.0f;
        jd.upperTranslation = length(end);
        jd.enableMotor = true;
        jd.maxMotorForce = 2.0f;
        jd.motorSpeed = 0.0f;
        jd.Initialize(world.body(anchorA), t, t->GetWorldCenter(), normalize(end));
        obj.addJoint(world.createPrismatic(jd));
    }
}


void layoutZipLine(const GameItem& item, const PhysicsObject& obj, PhysicsWorld& world) {
    // FUN_000fcb78 [verified]: anchor A at the position with angle 0, the trolley 0.15 m along the line
    // (angle flipped by Pi when the line's down vector points up), the line body at the position with the
    // line angle, anchor B at the end vector (not pulled back) with the line angle; the line box re-shaped.
    const Vec2 end = item.endVector;
    const float a = atan2F(end.y, end.x);
    const Vec2 down = rotate(a, Vec2(0.0f, -1.0f));
    const float trolleyAngle = down.y < 0.0f ? a : a + kPi;
    const Vec2 p = obj.position;
    world.setTransform(obj.bodies[0], p, 0.0f);
    const Vec2 along = rotate(a, Vec2(0.15f, 0.0f));
    world.setTransform(obj.bodies[2], Vec2(along.x + p.x, p.y + along.y), trolleyAngle);
    world.setTransform(obj.bodies[3], p, a);
    world.setTransform(obj.bodies[1], Vec2(p.x + end.x, p.y + end.y), a);
    const float half = length(end) * 0.5f;
    b2Fixture* f = world.body(obj.bodies[3])->GetFixtureList();
    world.resetBoxAt(obj.bodies[3], f, half, 0.02f, Vec2(half, 0.0f), 0.0f);
}

void zipLineUpdatePos(WorldState& state, PhysicsWorld& world, GameItem& item, PhysicsObject& obj, int bodyIndex, Vec2 target) {
    if (bodyIndex == 0) {
        const SnapResult r = calculateSnap(state, world, obj, target, target, obj.halfSize * 1.1f);
        if (!r.found) unsnapAllNotAttached(state, obj.index);
        else snap(state, obj.index, r.point, r.otherObject, r.otherPoint);
        obj.position = r.position;
    } else {
        item.endVector.x = item.endVector.x + (target.x - (obj.position.x + item.endVector.x));
        item.endVector.y = item.endVector.y + (target.y - (obj.position.y + item.endVector.y));
    }
    layoutZipLine(item, obj, world);
}

}  // namespace aa::sim::items
