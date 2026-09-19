// The three containers: Bucket (7), LaundryBasket (32), HangingLamp (30) — st::BucketUtils /
// LaundryBasketUtils / HangingLampUtils::CreatePhysics. Each is one dynamic body with wall polygons, a
// solid `Topping` box across the opening (collides only with other Topping fixtures, docs/04 §5) and, in
// simulation, an interior goal sensor of group −7 (goal type 7). Expression shapes follow the Android
// build: the bucket's and lamp's interiors use a few double multiplications (`(float)((double)x * c)`),
// everything else is float. Hidden filter writes (`strh` after the memcpy of a filter) are reproduced:
// the bucket's non-collidable interior carries group index 10.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {

// --- Bucket ---------------------------------------------------------------------------------------
namespace bucket {
constexpr float kFriction = 0.7f;
constexpr float kRestitution = 0.4f;
constexpr float kDensity = 70.0f;
constexpr float kMass = 0.5f;
constexpr float kInertia = 0.02f;
}  // namespace bucket

// --- LaundryBasket --------------------------------------------------------------------------------
namespace basket {
constexpr float kFriction = 0.7f;
constexpr float kRestitution = 0.4f;
constexpr float kDensity = 50.0f;
constexpr float kAspect = 2.1234567f;      // height = width / 2.1234567
constexpr float kWallThickness = 0.009f;
constexpr float kInset = 0.018f;
}  // namespace basket

// --- HangingLamp ----------------------------------------------------------------------------------
namespace lamp {
constexpr float kFriction = 0.7f;
constexpr float kRestitution = 0.4f;
constexpr float kShadeDensity = 100.0f;
constexpr float kSideDensity = 5.0f;
constexpr float kToppingDensity = 70.0f;
constexpr float kToppingFriction = 0.4f;
constexpr float kToppingRestitution = 0.6f;
constexpr float kAspect = 1.3333334f;      // height = width / 1.3333334
constexpr float kSideHalfWidth = 0.01f;
}  // namespace lamp

}  // namespace

void createBucket(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    using namespace bucket;
    const float r = obj.halfSize;
    const float r13 = r / 1.3f;
    const float half = r * 0.5f;
    const float k = r13 * 0.9f;
    const b2FixtureDef walls = fixtureDef(kDensity, kFriction, kRestitution, filters::kDynamic);

    b2BodyDef def;
    def.type = b2_dynamicBody;   // dynamic in both modes
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);

    const float m = 0.02f - k;
    const float nk = -k;
    const float nh = -half;
    const Vec2 bottom[4] = {Vec2(half, nk), Vec2(half, m), Vec2(nh, m), Vec2(nh, nk)};
    world.addPolygon(body, bottom, 4, walls);
    const float r75 = r * 0.75f;
    const float s85 = r13 * 0.85f;
    const Vec2 left[3] = {Vec2(nh, nk), Vec2(0.02f - half, nk), Vec2(-r75, s85)};
    world.addPolygon(body, left, 3, walls);
    const Vec2 right[3] = {Vec2(half - 0.02f, nk), Vec2(half, nk), Vec2(r75, s85)};
    world.addPolygon(body, right, 3, walls);
    world.addBoxAt(body, r75, 0.01f, Vec2(0.0f, s85), 0.0f,
                   fixtureDef(kDensity, kFriction, 0.0f, filters::kTopping));
    if (mode == PhysicsMode::SetUp) {
        const float y0 = k * -0.2f + 0.024f;
        const Vec2 interior[4] = {Vec2(half * 0.8f, y0), Vec2(r75 * 0.95f, s85 * 1.15f),
                                  Vec2(-r75 * 0.95f, s85 * 1.15f), Vec2(-half * 0.8f, y0)};
        world.addPolygon(body, interior, 4,
                         fixtureDef(kDensity, kFriction, kRestitution,
                                    filters::withGroup(filters::kNonCollidable, filters::kGroupBucketInterior)));
        const b2FixtureDef selection = fixtureDef(0.0f, kFriction, kRestitution, filters::kSelection);
        world.addBox(body, r, r13, selection);
        world.addCircle(body, r * 0.9f, Vec2(0.0f, r * 0.75f), selection);
    } else {
        const float y0 = 0.024f - k;
        const Vec2 sensor[4] = {Vec2(half * 0.8f, y0), Vec2(r75 * 0.8f, s85 * 0.7f),
                                Vec2(-r75 * 0.8f, s85 * 0.7f), Vec2(-half * 0.8f, y0)};
        world.addPolygon(body, sensor, 4,
                         fixtureDef(kDensity, kFriction, kRestitution,
                                    filters::withGroup(filters::kDynamic, filters::kGroupGoalSensor), /*sensor=*/true));
        world.addBoxAt(body, r * 0.75f, r * 0.05f, Vec2(0.0f, dmul(s85, 1.1)), 0.0f,
                       fixtureDef(kDensity, kFriction, kRestitution,
                                  filters::withGroup(filters::kNonCollidable, filters::kGroupBucketInterior)));
    }
    world.setMassData(body, kMass, Vec2(0.0f, nk * 0.3f), kInertia);
}

void createLaundryBasket(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    using namespace basket;
    const float r = obj.halfSize;
    const float r2 = r / kAspect;
    const float k = r2 * 0.9f;
    const b2FixtureDef walls = fixtureDef(kDensity, kFriction, kRestitution, filters::kDynamic);

    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);

    const float w = r * 0.75f;
    const float nk = -k;
    world.addBoxAt(body, w, kWallThickness, Vec2(0.0f, nk), 0.0f, walls);
    const float r95 = r * 0.95f;
    const float s = r2 * 0.85f;
    const Vec2 left[4] = {Vec2(-w, nk), Vec2(kInset - w, nk), Vec2(kInset - r95, s), Vec2(-r95, s)};
    world.addPolygon(body, left, 4, walls);
    const Vec2 right[4] = {Vec2(w - kInset, nk), Vec2(w, nk), Vec2(r95, s), Vec2(r95 - kInset, s)};
    world.addPolygon(body, right, 4, walls);
    world.addBoxAt(body, r95, kWallThickness, Vec2(0.0f, s), 0.0f,
                   fixtureDef(kDensity, kFriction, kRestitution, filters::kTopping));
    if (mode == PhysicsMode::Simulation) {
        const float y0 = kInset - k;
        const Vec2 sensor[4] = {Vec2(w * 0.9f, y0), Vec2(r95 * 0.85f, s * 0.75f), Vec2(r95 * -0.85f, s * 0.75f),
                                Vec2(w * -0.9f, y0)};
        world.addPolygon(body, sensor, 4,
                         fixtureDef(kDensity, kFriction, kRestitution,
                                    filters::withGroup(filters::kDynamic, filters::kGroupGoalSensor), /*sensor=*/true));
    } else {
        world.addBox(body, r, r2, fixtureDef(0.0f, kFriction, kRestitution, filters::kSelection));
    }
}

void createHangingLamp(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    using namespace lamp;
    const float r = obj.halfSize;
    const float h = r / kAspect;

    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);

    // Shade: a triangle from the top point down to the rim.
    const Vec2 shade[3] = {Vec2(0.0f, h), Vec2(dmul(r, -0.35), dmul(h, 0.2)), Vec2(dmul(r, 0.35), dmul(h, 0.2))};
    world.addPolygon(body, shade, 3, fixtureDef(kShadeDensity, kFriction, kRestitution, filters::kDynamic));
    // Four thin side boxes, ±20° and ±60°.
    const b2FixtureDef side = fixtureDef(kSideDensity, kFriction, kRestitution, filters::kDynamic);
    const float upperY = dmul(h, -0.1);
    const float upperHalf = dmul(h, 0.4);
    world.addBoxAt(body, kSideHalfWidth, upperHalf, Vec2(dmul(r, -0.4), upperY), kDegToRad * -20.0f, side);
    world.addBoxAt(body, kSideHalfWidth, upperHalf, Vec2(dmul(r, 0.4), upperY), kDegToRad * 20.0f, side);
    const float lowerHalf = dmul(h, 0.37);
    const float lowerY = dmul(h, -0.63);
    world.addBoxAt(body, kSideHalfWidth, lowerHalf, Vec2(dmul(r, -0.72), lowerY), kDegToRad * -60.0f, side);
    world.addBoxAt(body, kSideHalfWidth, lowerHalf, Vec2(dmul(r, 0.72), lowerY), kDegToRad * 60.0f, side);
    // Topping box across the opening.
    world.addBoxAt(body, r, 0.01f, Vec2(0.0f, 0.04f - h), 0.0f,
                   fixtureDef(kToppingDensity, kToppingFriction, kToppingRestitution, filters::kTopping));
    if (mode == PhysicsMode::SetUp) {
        world.addBox(body, r, h, fixtureDef(kSideDensity, kFriction, kRestitution, filters::kSelection));
    } else {
        const float x0 = dmul(r, 0.6);
        const float y0 = dmul(h, -0.8);
        const float x1 = r * 0.25f;
        const float y1 = dmul(h, 0.15);
        const Vec2 sensor[4] = {Vec2(x0, y0), Vec2(x1, y1), Vec2(-x1, y1), Vec2(-x0, y0)};
        world.addPolygon(body, sensor, 4,
                         fixtureDef(0.0f, kFriction, kRestitution,
                                    filters::withGroup(filters::kDynamic, filters::kGroupGoalSensor), /*sensor=*/true));
    }
}

}  // namespace aa::sim::items
