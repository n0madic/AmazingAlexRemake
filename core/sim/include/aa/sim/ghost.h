// st::GhostManipulationState / GhostAnimationState (docs/05 §5): the "good state" copy of the held item, the
// ghost copy that shows where it will land, and the glide back to it on release. Value-semantic like the
// original (the good state is a memcpy of the PhysicsObject and the GameItem).
#pragma once

#include "aa/sim/types.h"
#include "aa/sim/world_state.h"

#include <array>

namespace aa::sim {

// GameScreenController+0xc2900 (0x170 bytes).
struct GhostState {
    static constexpr int kMaxRopes = 2;

    bool inGhost = false;            // +0
    int ghostObject = -1;            // +4: object index of the ghost copy, -1 when none
    bool hasGoodState = false;       // +8
    PhysicsObject goodObject;        // +0xc: the last collision-free pose (position, angle, attachments…)
    GameItem goodItem;               // +0xe4: the item's set-up data at that time
    int ropeCount = 0;               // +0x164: attached ropes recorded when the ghost was entered
    std::array<int, kMaxRopes> ropes{{-1, -1}};   // +0x168: object indices of those ropes
};

// GameScreenController+0xc2a70 (0x30 bytes): the glide from the drop position to the ghost pose at 4.5 m/s
// with the CubicInterp(−1, 1, t/2 + 1/2) easing; zero-length glides finish at once.
struct GhostAnimation {
    static constexpr float kSpeed = 4.5f;
    static constexpr float kMinDistance = 0.0001f;

    int state = 0;                   // 0 idle, 1 running, 2 finished this frame
    int handle = 0;
    Vec2 from{0.0f, 0.0f};
    Vec2 to{0.0f, 0.0f};
    Vec2 direction{0.0f, 0.0f};
    float duration = 0.0f;
    float t = 0.0f;
    Vec2 position{0.0f, 0.0f};

    // GhostAnimationUtils::Start
    void start(Vec2 from_, Vec2 to_, int itemHandle);
    // GhostAnimationUtils::Update
    void update(float dt);
};

}  // namespace aa::sim
