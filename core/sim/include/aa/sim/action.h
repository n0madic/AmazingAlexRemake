// st::Action / st::ActionQueue (docs/05-gameplay.md §7): the queued messages between the touch handler, the
// item code and the controller. The ids are the original's; the payload union carries what each id needs.
#pragma once

#include "aa/sim/types.h"

#include <cstring>
#include <vector>

namespace aa::sim {

// st::ActionType::Enum, ids as in docs/05 §7 (only the ones the set-up mode produces or consumes are named).
namespace action {
constexpr int kNoOp = 0;
constexpr int kManipulationStarted = 1;
constexpr int kManipulationEnded = 2;
constexpr int kSelected = 3;
constexpr int kMoveSelected = 4;
constexpr int kRotateSelected = 5;
constexpr int kFlipSelected = 6;
constexpr int kRemoveItem = 7;
constexpr int kNewFromToolbox = 8;
constexpr int kReturnToToolbox = 9;
constexpr int kRemovalFinished = 10;
constexpr int kGoalComplete = 11;
constexpr int kBreak = 12;
constexpr int kPlaySound = 13;
constexpr int kFixedBuzz = 14;
constexpr int kFixedBuzzEnd = 15;
constexpr int kCycleState = 16;
constexpr int kAttachSharpObject = 0x11;
constexpr int kForceToItem = 0x12;
constexpr int kForceToRadius = 0x13;
constexpr int kSimulationTouch = 0x15;
constexpr int kGameButton = 0x16;
constexpr int kSkipAnims = 0x18;
constexpr int kCornerEnter = 0x19;
constexpr int kCornerLeave = 0x1A;
constexpr int kToolboxScroll = 0x1B;
}  // namespace action

// st::AudioId values the set-up mode queues through action 13 (docs/06 §5); nothing is played by the core.
namespace sound {
constexpr int kUIToolboxButton = 3;
constexpr int kUIToolboxButtonRelease = 4;
constexpr int kUIItemAdded = 5;
constexpr int kUIItemRemoved = 6;
constexpr int kUISelectBuzz = 7;
constexpr int kUIItemSelected = 8;
constexpr int kUIItemDeselected = 9;
constexpr int kBalloonPop = 0x1f;
constexpr int kBumperImpact = 0x17;
constexpr int kBoxingGloveTriggered = 0x24;
constexpr int kBoxingGlovePunch = 0x25;
constexpr int kScissorsCut = 0x22;
constexpr int kHelicopterHit = 0x38;
constexpr int kDartStuck = 0x16;
constexpr int kPiggyBreak = 0x20;
constexpr int kDollImpact = 0x1d;
constexpr int kDollHeadImpact = 0x1e;
constexpr int kStarCollected = 0x3d;    // + the number of stars already collected (0x3d..0x3f)
constexpr int kSpring = 0x21;
constexpr int kRopeCut = 0x23;
constexpr int kRCButtonClick = 0x2c;
constexpr int kTrapdoorOpen = 0x2f;
constexpr int kTrapdoorLever = 0x30;
constexpr int kSeesawMove = 0x31;
constexpr int kSlingshotFire = 0x26;
constexpr int kSlingshotStretch = 0x27;
constexpr int kSlingshotRelease = 0x28;
constexpr int kAttachHook = 0x40;       // FUN_000a6f2c: kinds 1/2/4 → 0x40 attach, 0x41 detach
constexpr int kDetachHook = 0x41;
constexpr int kAttachPipe = 0x42;       // kind 8 → 0x42 attach, 0x43 detach
constexpr int kDetachPipe = 0x43;
}  // namespace sound

// st::Action (0x20 bytes): +0 id, +4 handle, +8 position, +0x10 payload int (item type for 8, sound id for
// 13, body index for 17), +0x14 float (sound volume), +0x18 int (body index for 18, other handle for 17),
// +0x1c float (stab speed for 17). The force actions carry a vector in +0x10/+0x14 (18: the impulse, 19:
// radius and force) — stored as float bits in `payload` / `value` (payloadFloat).
struct Action {
    int id = 0;
    int handle = 0;
    Vec2 position{0.0f, 0.0f};
    int payload = 0;
    float value = 0.0f;
    int extra = 0;
    float amount = 0.0f;

    Action() = default;
    Action(int id_, int handle_) : id(id_), handle(handle_) {}
    Action(int id_, int handle_, Vec2 pos) : id(id_), handle(handle_), position(pos) {}
    static Action sound(int soundId, Vec2 at, float volume) {
        Action a(action::kPlaySound, 0, at);
        a.payload = soundId;
        a.value = volume;
        return a;
    }
    // Action 13 carrying the object's handle (the collision sounds; HandleCollisionSounds checks it).
    static Action soundOf(int handle_, int soundId, Vec2 at, float volume) {
        Action a = sound(soundId, at, volume);
        a.handle = handle_;
        return a;
    }
    static Action scroll(Vec2 delta) { return Action(action::kToolboxScroll, 0, delta); }
    // The +0x10 word read as a float (ApplyForcesUtils: impulse.x / radius).
    float payloadFloat() const {
        float f;
        std::memcpy(&f, &payload, sizeof f);
        return f;
    }
    void setPayloadFloat(float f) { std::memcpy(&payload, &f, sizeof payload); }
    // The +0x14 word read as an int (action 17: the stabbed object's handle).
    int valueInt() const {
        int i;
        std::memcpy(&i, &value, sizeof i);
        return i;
    }
    void setValueInt(int i) { std::memcpy(&value, &i, sizeof value); }
};

// st::ActionQueue (ActionQueueUtils::Add): a bounded array in the original (64 entries); a vector here.
struct ActionQueue {
    std::vector<Action> actions;
    void add(const Action& a) { actions.push_back(a); }
    void clear() { actions.clear(); }
    bool empty() const { return actions.empty(); }
};

}  // namespace aa::sim
