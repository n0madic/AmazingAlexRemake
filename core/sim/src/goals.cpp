// st::GoalStateUtils::Update / IsGoalComplete and GamePhysicsUtils::IsFloorCollidingWith (docs/02 §5,
// docs/05 §3) [verified: decompile + disassembly].
#include "aa/sim/goals.h"

#include "aa/sim/filters.h"

namespace aa::sim {

namespace {

constexpr float kContainerTime = 0.3f;

// The object of a goal handle, or null when the handle is dead (a removed item).
const PhysicsObject* goalObject(const WorldState& state, int handle) {
    const int item = state.handles.lookup(handle);
    if (item < 0) return nullptr;
    return &state.objects[static_cast<std::size_t>(state.items[static_cast<std::size_t>(item)].objectIndex)];
}

}  // namespace

bool isFloorCollidingWith(const WorldState& state, const PhysicsWorld& world, const PhysicsObject& obj) {
    // The floor fixture: the head of the world bound's body-0 fixture list (the floor box).
    const b2Fixture* floor = nullptr;
    for (const GameItem& item : state.items) {
        if (item.type != ItemType::WorldBound) continue;
        const PhysicsObject& bound = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if (bound.bodyCount > 0) {
            if (const b2Body* b = world.body(bound.bodies[0])) floor = b->GetFixtureList();
        }
        break;
    }
    for (int k = 0; k < obj.bodyCount; ++k) {
        const b2Body* body = world.body(obj.bodies[static_cast<std::size_t>(k)]);
        if (body == nullptr) continue;
        for (const b2ContactEdge* ce = body->GetContactList(); ce; ce = ce->next) {
            const b2Contact* c = ce->contact;
            if (!c->IsTouching()) continue;
            if (c->GetFixtureA() == floor || c->GetFixtureB() == floor) return true;
        }
    }
    return false;
}

void updateGoalState(float dt, GoalState& goal, const Goal& level, const WorldState& state, const PhysicsWorld& world) {
    // GoalStateUtils::Update: goal type 7 only — per pair, the seconds the target has been touching the
    // container's goal sensor fixture (group −7); reset to 0 when it is not.
    if (level.type != 7 || level.itemCount <= 0) return;
    for (int i = 0; i < level.itemCount && i < Goal::kMaxTargets; ++i) {
        float& timer = goal.contactTime[static_cast<std::size_t>(i)];
        const PhysicsObject* container = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
        if (container == nullptr) {
            timer = 0.0f;
            continue;
        }
        const b2Fixture* sensor = nullptr;
        for (int k = 0; k < container->bodyCount && sensor == nullptr; ++k) {
            const b2Body* body = world.body(container->bodies[static_cast<std::size_t>(k)]);
            if (body == nullptr) continue;
            for (const b2Fixture* f = body->GetFixtureList(); f; f = f->GetNext()) {
                if (f->GetFilterData().groupIndex == filters::kGroupGoalSensor) {
                    sensor = f;
                    break;
                }
            }
        }
        const PhysicsObject* target = goalObject(state, level.itemHandles2[static_cast<std::size_t>(i)]);
        if (target == nullptr) {
            timer = 0.0f;
            continue;
        }
        bool touching = false;
        for (int k = 0; k < target->bodyCount && !touching; ++k) {
            const b2Body* body = world.body(target->bodies[static_cast<std::size_t>(k)]);
            if (body == nullptr) continue;
            for (const b2ContactEdge* ce = body->GetContactList(); ce; ce = ce->next) {
                const b2Contact* c = ce->contact;
                if (c->IsTouching() && (c->GetFixtureA() == sensor || c->GetFixtureB() == sensor)) {
                    touching = true;
                    break;
                }
            }
        }
        timer = touching ? timer + dt : 0.0f;
    }
}

bool isGoalComplete(const GoalState& goal, const Goal& level, const WorldState& state, const PhysicsWorld& world) {
    const int n = level.itemCount < Goal::kMaxTargets ? level.itemCount : Goal::kMaxTargets;
    switch (level.type) {
    case 3:
        for (int i = 0; i < n; ++i) {
            const PhysicsObject* obj = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
            if (obj == nullptr || !isFloorCollidingWith(state, world, *obj)) return false;
        }
        return true;
    case 4:
        return false;
    case 5:
        // A dead target (a popped balloon already removed) counts as activated.
        for (int i = 0; i < n; ++i) {
            const PhysicsObject* obj = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
            if (obj != nullptr && (obj->state & object_state::kActivated) == 0) return false;
        }
        return true;
    case 6:
        for (int i = 0; i < n; ++i) {
            const PhysicsObject* obj = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
            if (obj == nullptr || obj->position.y < level.height) return false;
        }
        return true;
    case 7:
        if (level.itemCount > 0) {
            if (goal.contactTime[0] < kContainerTime) return false;
            for (int i = 1; i < n; ++i) {
                if (!(goal.contactTime[static_cast<std::size_t>(i)] >= kContainerTime)) return false;
            }
        }
        return true;
    case 8:
        for (int i = 0; i < n; ++i) {
            const PhysicsObject* obj = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
            if (obj == nullptr || level.height < obj->position.y) return false;
        }
        return true;
    case 9:
        for (int i = 0; i < n; ++i) {
            const PhysicsObject* obj = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
            if (obj == nullptr || obj->position.x < level.width) return false;
        }
        return true;
    case 10:
        for (int i = 0; i < n; ++i) {
            const PhysicsObject* obj = goalObject(state, level.itemHandles[static_cast<std::size_t>(i)]);
            if (obj == nullptr || level.width < obj->position.x) return false;
        }
        return true;
    default:
        return false;   // types 1 and 2 complete from the contact listener
    }
}

}  // namespace aa::sim
