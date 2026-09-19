// st::GoalState (GameState+0x808, 0x2c bytes): the goal / star progress of one play run, reset by
// LevelLayoutUtils::Apply on every restoreGameState (docs/05 §3) [verified: GoalState::GoalState,
// GoalStarUtils::Update, GoalStateUtils::Update].
#pragma once

#include "aa/sim/level.h"

#include <array>

namespace aa::sim {

struct GoalState {
    bool reached = false;                                    // +0: set by action 11 or by three stars (doFrame)
    int collectedStars = 0;                                  // +4: GoalStarUtils::Update
    std::array<float, Goal::kMaxTargets> contactTime{};      // +8: goal type 7, seconds of sensor contact per pair
};

}  // namespace aa::sim
