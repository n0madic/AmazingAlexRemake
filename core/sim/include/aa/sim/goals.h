// The goal rules of a play run (docs/02 §5, docs/05 §3): st::GoalStateUtils and the floor test.
#pragma once

#include "aa/sim/goal_state.h"
#include "aa/sim/level.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/world_state.h"

namespace aa::sim {

// GamePhysicsUtils::IsFloorCollidingWith: some body of `obj` has a touching contact with the world
// bound's floor fixture (goal type 3).
bool isFloorCollidingWith(const WorldState& state, const PhysicsWorld& world, const PhysicsObject& obj);

// GoalStateUtils::Update (once per frame, with the accumulator as dt): the goal-7 sensor contact timers.
void updateGoalState(float dt, GoalState& goal, const Goal& level, const WorldState& state, const PhysicsWorld& world);

// GoalStateUtils::IsGoalComplete for the types evaluated after the physics (3, 5–10); 1 and 2 complete
// from WorldContactListener::PreSolve, 4 never.
bool isGoalComplete(const GoalState& goal, const Goal& level, const WorldState& state, const PhysicsWorld& world);

}  // namespace aa::sim
