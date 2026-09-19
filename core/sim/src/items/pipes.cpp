// Pipe (17) and Pipe90 (18): st::PipeUtils::CreatePhysicsStraight / CreatePhysics90. Static walls, a
// Topping fixture across each mouth (docs/04 §5) and, in set-up mode, a PipeFilling fixture plus the
// selection box. The bent pipe's vertices are pixel coordinates of the 107-px sprite divided by
// `107 / (2r)`, all in float.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kToppingDensity = 50.0f;
constexpr float kToppingFriction = 0.7f;
constexpr float kToppingRestitution = 0.4f;
}  // namespace

void createPipe(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float h = (r * 46.0f) / 195.0f;
    const int body = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    const b2FixtureDef wall = fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kStatic);
    world.addBoxAt(body, r, 0.005f, Vec2(0.0f, h), 0.0f, wall);
    world.addBoxAt(body, r, 0.005f, Vec2(0.0f, -h), 0.0f, wall);
    world.addBoxAt(body, r, h, Vec2(0.0f, 0.0f), 0.0f,
                   fixtureDef(kToppingDensity, kToppingFriction, kToppingRestitution, filters::kTopping));
    if (mode == PhysicsMode::SetUp) {
        world.addBox(body, r, h, fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kPipeFilling));
        world.addBox(body, r, h * 1.3f, selectionDef());
    }
}

void createPipe90(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const float r = obj.halfSize;
    const float k = 107.0f / (r + r);
    auto px = [k](float v) { return v / k; };
    const int body = addBody(obj, world, itemBodyDef(obj, mode, b2_staticBody));
    const b2FixtureDef wall = fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kStatic);
    const Vec2 wall1[4] = {Vec2(px(-50.0f), px(0.0f)), Vec2(px(-17.0f), px(14.0f)), Vec2(px(-19.5f), px(17.5f)),
                           Vec2(px(-50.0f), px(3.0f))};
    const Vec2 wall2[4] = {Vec2(px(-1.0f), px(50.0f)), Vec2(px(-19.5f), px(17.0f)), Vec2(px(-16.5f), px(14.1f)),
                           Vec2(px(2.0f), px(50.0f))};
    const Vec2 wall3[4] = {Vec2(px(36.5f), px(1.0f)), Vec2(px(49.0f), px(50.0f)), Vec2(px(46.0f), px(50.0f)),
                           Vec2(px(33.5f), px(3.5f))};
    const Vec2 wall4[4] = {Vec2(px(1.0f), px(-36.5f)), Vec2(px(36.5f), px(0.0f)), Vec2(px(34.0f), px(2.0f)),
                           Vec2(px(-1.5f), px(-34.0f))};
    const Vec2 wall5[4] = {Vec2(px(-50.0f), px(-47.0f)), Vec2(px(-0.5f), px(-37.5f)), Vec2(px(-2.5f), px(-33.5f)),
                           Vec2(px(-50.0f), px(-44.0f))};
    world.addPolygon(body, wall1, 4, wall);
    world.addPolygon(body, wall2, 4, wall);
    world.addPolygon(body, wall3, 4, wall);
    world.addPolygon(body, wall4, 4, wall);
    world.addPolygon(body, wall5, 4, wall);
    const b2FixtureDef topping = fixtureDef(kToppingDensity, kToppingFriction, kToppingRestitution, filters::kTopping);
    const Vec2 mouth1[4] = {Vec2(px(-50.0f), px(-44.0f)), Vec2(px(-2.5f), px(-33.5f)), Vec2(px(-16.5f), px(14.1f)),
                            Vec2(px(-50.0f), px(0.0f))};
    const Vec2 mouth2[4] = {Vec2(px(34.0f), px(2.0f)), Vec2(px(46.0f), px(50.0f)), Vec2(px(2.0f), px(50.0f)),
                            Vec2(px(-16.5f), px(14.1f))};
    world.addPolygon(body, mouth1, 4, topping);
    world.addPolygon(body, mouth2, 4, topping);
    if (mode == PhysicsMode::SetUp) {
        world.addBox(body, r, r, selectionDef());
        const b2FixtureDef filling = fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kPipeFilling);
        world.addCircle(body, 0.07f, Vec2(-0.09f, -0.06f), filling);
        world.addCircle(body, 0.07f, Vec2(0.06f, 0.09f), filling);
    }
}

}  // namespace aa::sim::items
