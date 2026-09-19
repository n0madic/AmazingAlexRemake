// GoalStar (type 23): st::GoalStarUtils::CreatePhysics. A sensor circle of r·1.1 with the Dynamic filter
// (contact detection only); static in simulation; a selection circle in set-up mode. Update: the collect
// animation (docs/11 §5).
#include "aa/sim/animations.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kSensorFactor = 1.1f;
constexpr float kCollectTime = 0.4f;
constexpr float kCollectSoundVolume = 0.5f;
constexpr int kStarUncollected = 0;
constexpr int kStarCollecting = 1;
constexpr int kStarRemoved = 2;
// DAT_0028e284: the collect scale curve (.bss, read with decomp_dis.py bss).
const CurvePoint kCollectCurve[6] = {{0.0f, 1.0f}, {0.1f, 1.4f}, {0.25f, 1.6f}, {0.45f, 1.2f}, {0.7f, 0.5f}, {1.0f, 0.0f}};
}

void createGoalStar(PhysicsObject& obj, PhysicsWorld& world, PhysicsMode mode) {
    const b2BodyDef def = itemBodyDef(obj, mode, b2_staticBody);
    const int body = addBody(obj, world, def);
    const Vec2 origin(0.0f, 0.0f);
    world.addCircle(body, obj.halfSize * kSensorFactor, origin,
                    fixtureDef(0.0f, kDefaultFriction, 0.0f, filters::kDynamic, /*sensor=*/true));
    if (mode == PhysicsMode::SetUp) {
        world.addCircle(body, kMinSelectionRadius, origin, selectionDef());
    }
}

void updateGoalStars(float dt, WorldState& state, PhysicsWorld& world, GoalState& goal, ActionQueue& queue) {
    for (GameItem& item : state.items) {
        if (item.type != ItemType::GoalStar) continue;
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if (item.starState == kStarUncollected) {
            if (!world.isColliding(obj)) continue;
            item.starState = kStarCollecting;
            // SparkleEffectUtils::Start at the star: presentation (VisualState, WP6).
            Action sound = Action::sound(sound::kStarCollected + goal.collectedStars, obj.position, kCollectSoundVolume);
            queue.add(sound);
            goal.collectedStars = goal.collectedStars + 1;
        } else if (item.starState == kStarCollecting) {
            item.starTimer = dt + item.starTimer;
            float s = item.starTimer / kCollectTime;
            if (s - 1.0f >= 0.0f) s = 1.0f;
            const float scale = curveValueAt(s, kCollectCurve, 6);
            obj.scale = Vec2(scale, scale);
            if (item.starTimer < kCollectTime) continue;
            queue.add(Action(action::kRemoveItem, obj.handle));
            item.starState = kStarRemoved;
        }
    }
}

}  // namespace aa::sim::items
