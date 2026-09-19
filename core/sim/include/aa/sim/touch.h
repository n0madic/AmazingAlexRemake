// st::Touches / st::TouchUtils and the set-up touch state (GameState+0x57320) of docs/05 §5. The platform
// feeds native pixel events; `TouchUtils::QueueTouches*` flips y (NativeScreenHeight − y, y up), stamps the
// record with the current time (a double, `currentTimeMillis / 1000`) and appends an event to a ring that
// GameTouchHandler::Process drains once per frame [verified].
#pragma once

#include "aa/sim/types.h"

#include <array>
#include <vector>

namespace aa::sim {

// One touch record (0x38 bytes): +0 id, +4 tap count, +8 start px, +0x18 previous px, +0x20 previous time,
// +0x28 current px, +0x30 current time.
struct TouchRecord {
    int id = -1;                  // -1 = slot free
    int tapCount = 0;
    Vec2 startPx{0.0f, 0.0f};     // native px, y up
    Vec2 prevPx{0.0f, 0.0f};
    double prevTime = 0.0;
    Vec2 currentPx{0.0f, 0.0f};
    double currentTime = 0.0;
};

// The event ring entries: kind 1 began, 2 ended, 3 moved, 4 cancelled; index = record slot.
struct TouchEvent {
    static constexpr int kBegan = 1;
    static constexpr int kEnded = 2;
    static constexpr int kMoved = 3;
    static constexpr int kCancelled = 4;
    int kind = 0;
    int index = 0;
};

struct Touches {
    static constexpr int kMaxTouches = 16;
    static constexpr int kRingCapacity = 32;   // the original's ring holds `+0x380` entries (capacity field)

    std::array<TouchRecord, kMaxTouches> records{};
    std::vector<TouchEvent> events;   // drained by the handler each frame

    // TouchUtils::QueueTouchesBegan/Moved/Ended/Cancel. `px` is native, y already flipped (y up).
    void began(int id, Vec2 px, double now, int tapCount = 1);
    void moved(int id, Vec2 px, double now);
    void ended(int id, Vec2 px, double now);
    void cancelled(int id);
    // TouchUtils::GetActiveTouchCount.
    int activeCount() const;

private:
    int find(int id) const;
    void update(int slot, Vec2 px, double now);   // FUN_000f1e80: shift current → previous, stamp
};

// GameTouchHandler's state machine values (GameState+0x57320) [verified: GameTouchHandler::Process].
namespace touch_state {
constexpr int kIdle = 0;
constexpr int kPending = 1;          // finger down on an item (or empty space), hold timer running
constexpr int kDragging = 2;
constexpr int kTwoFingerPending = 3;     // two-finger rotation, waiting for the rotating finger (unreachable in the binary)
constexpr int kTwoFingerRotate = 4;      // two-finger rotation (unreachable in the binary)
constexpr int kRingRotate = 5;       // rotation gizmo ring drag
constexpr int kFlipping = 6;         // flip button hit, waiting for the animation
constexpr int kFromToolbox = 7;      // item just taken out of the strip, dragging
constexpr int kReturning = 8;        // item on its way back to the toolbox
constexpr int kBuzz = 9;             // a fixed item was touched
constexpr int kPinchZoom = 10;       // a second finger in state 1 on a phone: pinch zoom
constexpr int kPan = 11;             // empty-space drag scrolls the camera
constexpr int kButton = 12;          // an in-world button (simulation touch handler)
constexpr int kToolboxTouch = 13;    // finger down on the strip
constexpr int kToolboxButton = 14;   // finger on the toolbox open/close button
constexpr int kToolboxScroll = 15;
}  // namespace touch_state

// GameState+0x57320 (0x70 bytes), field order and semantics of the original.
struct TouchState {
    int state = touch_state::kIdle;
    int primaryTouch = -1;            // +4: record slot of the manipulating finger
    int secondaryTouch = -1;          // +8
    int rotationTouch = -1;           // +0xc
    float holdTimer = 0.0f;           // +0x10: 0.2 s from the touch; expiry starts the drag
    int tapCount = 0;                 // +0x14: fixed-item buzz counter
    int selectedObject = -1;          // +0x18: object index
    int bodyIndex = -1;               // +0x1c: the picked body of the object
    Vec2 startPos{0.0f, 0.0f};        // +0x20: world position of the picked body at the touch
    Vec2 targetPos{0.0f, 0.0f};       // +0x28: where the body should go (UpdatePos target)
    Vec2 dragVelocity{0.0f, 0.0f};    // +0x30: world units between move events (pixelSizeToWorld)
    float angleStart = 0.0f;          // +0x38: object angle when the ring drag started
    float angleCurrent = 0.0f;        // +0x3c: the angle UpdatePos / UpdateAngle apply
    float angleDelta = 0.0f;          // +0x40: signed ring angle of the current drag
    Vec2 ringPx{0.0f, 0.0f};          // +0x44: ring drag start (screen px)
    int reserved4c = 0;               // +0x4c, +0x50: unused here
    int reserved50 = 0;
    int slingshotObject = -1;         // +0x54: the slingshot whose pouch was under the finger
    Vec2 samplePos{0.0f, 0.0f};       // +0x58: velocity sample position (world) / toolbox scroll anchor (px)
    double lastTouchTime = 0.0;       // +0x60: time of the velocity sample
    int buttonTouch = -1;             // +0x68: touch held by an in-world button
    int gizmoObject = -1;             // +0x6c: the item whose gizmos are shown (last tapped dynamic item)
};

}  // namespace aa::sim
