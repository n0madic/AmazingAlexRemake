// st::GameTouchHandler::Process and its helpers (docs/05 §5), internal to aa_sim: the set-up-mode touch
// state machine over the queued touch events. `IntersectionQueries` (the pick) is here too.
#pragma once

#include "aa/sim/action.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/render_state.h"
#include "aa/sim/screen_layout.h"
#include "aa/sim/session.h"
#include "aa/sim/toolbox.h"
#include "aa/sim/touch.h"
#include "aa/sim/world_state.h"

#include <vector>

namespace aa::sim {

// st::IntersectionQueries::Result (0x18 bytes).
struct PickResult {
    int object = -1;
    int bodyIndex = -1;          // PhysicsObjectUtils::GetBodyIndex of the hit body
    int fixtureBodyIndex = -1;   // the rope end circles' user index − 1, else −1
    Vec2 bodyPosition{0.0f, 0.0f};
    int slingshot = -1;          // a slingshot whose pouch body was under the point
};

// FUN_000d48f4: the nearest object under `worldPos` by selection fixture (AABB ± 0.04 m, TestPoint),
// nearest body position by squared distance, a movable object beating a fixed one; `skipFixed` drops
// fixed objects entirely, `includeBillboards` keeps type 24. The campaign uses
// GetNearestIntersectingObjectMinusBillboards (fixed kept, billboards dropped) [verified].
bool pickObject(const WorldState& state, const PhysicsWorld& world, Vec2 worldPos, bool skipFixed, bool includeBillboards,
                uint16 mask, PickResult& out);

struct TouchContext {
    Touches& touches;
    TouchState& ts;
    WorldState& state;
    PhysicsWorld& world;
    Camera& camera;
    Toolbox& toolbox;
    ActionQueue& queue;
    std::vector<SessionEvent>& events;
    const ScreenLayout& layout;
    GameMode mode;
    bool isTablet;
};

// GameTouchHandler::Process: drains the event ring and runs the hold-timer check.
void processTouches(TouchContext& c);

}  // namespace aa::sim
