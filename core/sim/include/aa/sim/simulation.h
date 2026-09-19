// The simulation-mode pieces of st::GamePhysicsUtils that act on the whole WorldState (docs/04 §3): the
// state copy-out after a step, the render interpolation, the runaway stop and the idle test of
// GameScreenController::doFrame's tail. All ported from the decompile + disassembly [verified].
#pragma once

#include "aa/sim/action.h"
#include "aa/sim/goal_state.h"
#include "aa/sim/level.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/world_state.h"

#include <vector>

namespace aa::sim {

// The fixed physics step of GameScreen::UpdateSimulation (0x3c088889) and its settings.
constexpr float kSimulationStep = 0.008333334f;
constexpr int kVelocityIterations = 10;
constexpr int kPositionIterations = 10;
// doFrame: the accumulator is fed with the frame dt × 0.8 — the game runs at 0.8 × real time (docs/04 §3).
constexpr float kSimulationSpeed = 0.8f;

// What the simulation contact listener (st::WorldContactListener{GameState*, ActionQueue*}) reaches through
// the GameState: the live world state, the action queue, the level goal and the goal progress. Transient —
// set by the Session for its world, never stored in a snapshot.
struct SimulationContext {
    WorldState* state = nullptr;
    PhysicsWorld* world = nullptr;
    ActionQueue* queue = nullptr;
    const Goal* goal = nullptr;
    GoalState* goalState = nullptr;
    // TimeUtils::GetAbsoluteTime as an int: the seed of the bouncy ball's random impact sound
    // (GameItemUtils::HandleCollisionSounds). 0 under the harness (its clock returns 0).
    int timeSeed = 0;
};

// The interpolated pose of one object (what LerpState writes into the render copy).
struct RenderPose {
    Vec2 position{0.0f, 0.0f};
    float angle = 0.0f;
};

// GamePhysicsUtils::GetStateFromPhysics: for every object with the dynamic flag and at least one body,
// the object position / angle become body 0's transform position and sweep angle.
void getStateFromPhysics(WorldState& state, const PhysicsWorld& world);

// GamePhysicsUtils::LerpState(render, prev, current, alpha): the dynamic-flag objects of the render copy get
// `prev + alpha · (current − prev)` for the position and the angle (unfused, no angle wrap). `prev` and
// `current` are index-aligned copies of the same object list (the substep loop copies `prev` before each
// step and nothing removes objects until the frame tail). Writes one RenderPose per object index.
void lerpState(std::vector<RenderPose>& render, const WorldState& prev, const WorldState& current, float alpha);

// GamePhysicsUtils::StopRunawayObjects: a moving body (|vx| > 0.001 or |vy| > 0.001) whose |x| exceeds
// 6.82 m or whose y is below −4.24918 m has its velocities zeroed (non-static bodies only).
void stopRunawayObjects(const WorldState& state, PhysicsWorld& world);

// GamePhysicsUtils::HasMovingObjects: some body (a rope's root body excepted) has |v| or |w| above 0.001.
bool hasMovingObjects(const WorldState& state, const PhysicsWorld& world);

}  // namespace aa::sim
