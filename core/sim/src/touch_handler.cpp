// st::GameTouchHandler::Process (667 lines in the decompile) with FUN_000cf3e8 (start manipulation),
// FUN_000cedd8 (touch ended), FUN_000ceb68 (release / return) and FUN_000cf4f0 (ring angle), plus
// st::TouchUtils and the IntersectionQueries pick. Every literal is the original's [verified]. The
// two-finger states: 10 (pinch zoom) is entered by a second finger in state 1 on a phone (the Android
// build sets DeviceParams::IsTablet unconditionally, so it never gets there); 3 / 4 (two-finger rotation)
// have no writer anywhere in the shipped binary — their Moved / Ended branches are ported as read
// (docs/05 §2) but nothing reaches them.
#include "touch_handler.h"

#include "aa/sim/animations.h"
#include "aa/sim/coords.h"
#include "aa/sim/filters.h"
#include "aa/sim/math_utils.h"

#include <cmath>

namespace aa::sim {

// --- st::Touches -------------------------------------------------------------------------------

int Touches::find(int id) const {
    for (int i = 0; i < kMaxTouches; ++i) {
        if (records[static_cast<std::size_t>(i)].id == id) return i;
    }
    return -1;
}

void Touches::update(int slot, Vec2 px, double now) {
    TouchRecord& r = records[static_cast<std::size_t>(slot)];
    r.prevTime = r.currentTime;
    r.prevPx = r.currentPx;
    r.currentPx = px;
    r.currentTime = now;
}

void Touches::began(int id, Vec2 px, double now, int tapCount) {
    int slot = 0;
    while (slot < kMaxTouches - 1 && records[static_cast<std::size_t>(slot)].id != -1) ++slot;
    TouchRecord& r = records[static_cast<std::size_t>(slot)];
    r.id = id;
    r.tapCount = tapCount;
    r.startPx = px;
    // QueueTouchesBegan copies the old start into `current` before FUN_000f1e80 shifts it into `prev`;
    // a fresh record starts with prev = start so the first velocity sample is zero.
    r.currentPx = px;
    r.currentTime = now;
    update(slot, px, now);
    if (static_cast<int>(events.size()) < kRingCapacity) events.push_back({TouchEvent::kBegan, slot});
}

void Touches::moved(int id, Vec2 px, double now) {
    const int slot = find(id);
    if (slot < 0) return;
    update(slot, px, now);
    if (static_cast<int>(events.size()) < kRingCapacity) events.push_back({TouchEvent::kMoved, slot});
}

void Touches::ended(int id, Vec2 px, double now) {
    const int slot = find(id);
    if (slot < 0) return;
    update(slot, px, now);
    if (static_cast<int>(events.size()) < kRingCapacity) events.push_back({TouchEvent::kEnded, slot});
    records[static_cast<std::size_t>(slot)].id = -1;
}

void Touches::cancelled(int id) {
    const int slot = find(id);
    if (slot < 0) return;
    if (static_cast<int>(events.size()) < kRingCapacity) events.push_back({TouchEvent::kCancelled, slot});
    records[static_cast<std::size_t>(slot)].id = -1;
}

int Touches::activeCount() const {
    int n = 0;
    for (const TouchRecord& r : records) n += r.id != -1 ? 1 : 0;
    return n;
}

// --- IntersectionQueries -----------------------------------------------------------------------

bool pickObject(const WorldState& state, const PhysicsWorld& world, Vec2 worldPos, bool skipFixed, bool includeBillboards,
                uint16 mask, PickResult& out) {
    constexpr float kHalf = 0.04f;
    struct Best {
        int object = -1;
        const b2Body* body = nullptr;
        int fixtureBodyIndex = -1;
        float dist = 1000000.0f;   // 0x49742400
        int slingshot = -1;
    } best;
    b2AABB aabb;
    aabb.lowerBound = Vec2(worldPos.x - kHalf, worldPos.y - kHalf);
    aabb.upperBound = Vec2(worldPos.x + kHalf, worldPos.y + kHalf);
    world.queryAABB(aabb, [&](b2Fixture* fixture) {
        // FUN_000d49f8
        if ((fixture->GetFilterData().categoryBits & mask) == 0) return true;
        const b2Body* body = fixture->GetBody();
        const int idx = PhysicsWorld::bodyObject(body);
        if (idx < 0) return true;
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(idx)];
        const int bi = PhysicsWorld::fixtureBodyIndex(fixture);
        if (skipFixed && obj.isFixed()) return true;
        if (bi != 0 && bi - 1 < PhysicsObject::kSelectableBytes && (obj.selectable[static_cast<std::size_t>(bi - 1)] & 1) == 0) return true;
        if (!includeBillboards && obj.type == ItemType::Billboard) return true;
        if (!fixture->TestPoint(worldPos)) return true;
        if (obj.type == ItemType::Slingshot && bi - 1 == 1 && !obj.isFixed()) best.slingshot = idx;
        bool take = false;
        if (best.object < 0) {
            take = true;
        } else if (!state.objects[static_cast<std::size_t>(best.object)].isFixed()) {
            if (obj.isFixed()) return true;   // a movable best is never replaced by a fixed one
        } else if (!obj.isFixed()) {
            take = true;
        }
        const float dy = worldPos.y - body->GetPosition().y;
        const float dx = worldPos.x - body->GetPosition().x;
        const float d2 = dy * dy + dx * dx;
        const bool bestFixed = best.object >= 0 && state.objects[static_cast<std::size_t>(best.object)].isFixed();
        if ((take || d2 < best.dist) && (best.object < 0 || idx != best.slingshot || bi != 2 || bestFixed)) {
            best.object = idx;
            best.dist = d2;
            best.body = body;
            best.fixtureBodyIndex = bi - 1;
        }
        return true;
    });
    if (best.object < 0) return false;
    const PhysicsObject& obj = state.objects[static_cast<std::size_t>(best.object)];
    out.object = best.object;
    out.bodyIndex = -1;
    for (int k = 0; k < obj.bodyCount; ++k) {
        if (world.body(obj.bodies[static_cast<std::size_t>(k)]) == best.body) {
            out.bodyIndex = k;
            break;
        }
    }
    out.fixtureBodyIndex = best.fixtureBodyIndex;
    out.bodyPosition = Vec2(best.body->GetPosition().x, best.body->GetPosition().y);
    out.slingshot = best.slingshot;
    return true;
}

// --- GameTouchHandler ------------------------------------------------------------------------------

namespace {

constexpr float kDragThresholdSquared = 25.0f;     // 5 px
constexpr float kToolboxDragSquared = 50.0f;
constexpr float kToolboxDragOutAngleDeg = 20.0f;
constexpr float kRadToDeg = 57.29578f;
constexpr float kHoldTime = 0.2f;                  // 0x3e4ccccd
constexpr float kRingInner = 0.22999999f;          // 0x3e6b851e
constexpr float kRingOuter = 0.53f;
constexpr float kFlipButtonDistance = 0.4f;
constexpr float kFlipButtonHalf = 0.1f;            // DAT_0028e178..84
constexpr float kFlipButtonAngle = 0.78539818f;    // DAT_0028e174 = π/4 (0x3f490fdb)
constexpr double kVelocitySampleInterval = 0.10000000149011612;   // the float 0.1 widened
constexpr float kPinchZoomRate = 0.003f;           // 0x3b449ba6: zoom per native px of finger distance
constexpr float kPinchZoomMin = 1.0f;
constexpr float kPinchZoomMax = 2.5f;
constexpr float kHalf = 0.5f;

bool pickForMode(const TouchContext& c, Vec2 w, PickResult& out) {
    const bool includeBillboards = c.mode == GameMode::Sandbox || c.mode == GameMode::SandboxToolbox;
    return pickObject(c.state, c.world, w, false, includeBillboards, filters::kSelectionBit, out);
}

void resetAfterTouch(TouchState& ts, bool clearGizmo) {
    ts.selectedObject = -1;
    ts.state = touch_state::kIdle;
    ts.primaryTouch = -1;
    ts.secondaryTouch = -1;
    ts.rotationTouch = -1;
    ts.slingshotObject = -1;
    if (clearGizmo) ts.gizmoObject = -1;
}

// FUN_000cf3e8: the drag threshold / hold timer passed — pick again at the touch start and start
// dragging (action 1), or pan the camera when nothing movable is there.
void startManipulation(TouchContext& c, const TouchRecord& rec) {
    TouchState& ts = c.ts;
    const Vec2 w = screenToWorld(c.layout, c.camera, rec.startPx);
    PickResult r;
    if (!pickForMode(c, w, r) || c.state.objects[static_cast<std::size_t>(r.object)].isFixed()) {
        ts.secondaryTouch = -1;
        ts.state = touch_state::kPan;
        ts.rotationTouch = -1;
        ts.slingshotObject = -1;
        return;
    }
    const PhysicsObject& obj = c.state.objects[static_cast<std::size_t>(r.object)];
    ts.angleStart = obj.angle;
    ts.angleCurrent = obj.angle;
    ts.startPos = r.bodyPosition;
    ts.bodyIndex = r.bodyIndex;
    ts.targetPos = r.bodyPosition;
    ts.samplePos = r.bodyPosition;
    ts.slingshotObject = r.slingshot;
    ts.state = touch_state::kDragging;
    ts.selectedObject = obj.index;
    c.queue.add(Action(action::kManipulationStarted, obj.handle));
}

// FUN_000cf4f0: the signed angle between the ring drag's start and current directions about the item.
void ringAngle(TouchContext& c, const TouchRecord& rec) {
    TouchState& ts = c.ts;
    const PhysicsObject& obj = c.state.objects[static_cast<std::size_t>(ts.selectedObject)];
    const Vec2 s = screenToWorld(c.layout, c.camera, rec.startPx);
    const Vec2 e = screenToWorld(c.layout, c.camera, rec.currentPx);
    const float ax = s.x - obj.position.x;
    const float ay = s.y - obj.position.y;
    const float bx = e.x - obj.position.x;
    const float by = e.y - obj.position.y;
    const float la = length(Vec2(ax, ay));
    const float dotY = ay * by;
    const float lb = length(Vec2(bx, by));
    const float cross = ax * by - ay * bx;
    float cosv = (dotY + ax * bx) / (la * lb);
    float sign;
    if (0.0f < cross) sign = 1.0f;
    else if (0.0f <= cross) sign = 0.0f;
    else sign = -1.0f;
    if (cosv < -1.0f) cosv = -1.0f;
    else if (1.0f < cosv) cosv = 1.0f;
    const float a = acosF(cosv);
    ts.angleDelta = sign * a;
    ts.angleCurrent = ts.angleStart + sign * a;
}

// FUN_000ceb68: the drop — over the drop rectangle or colliding with the SelectionArea → action 9,
// otherwise the velocity sample and action 2.
void release(TouchContext& c, const TouchRecord& rec) {
    TouchState& ts = c.ts;
    const PhysicsObject& obj = c.state.objects[static_cast<std::size_t>(ts.selectedObject)];
    const Vec2 screenPx = screenToPixelPos(c.layout, c.camera, worldPtToScreenPt(c.layout, obj.position));
    const ScreenRect drop = c.toolbox.getDropRectangle();
    bool returnIt = drop.contains(screenPx);
    if (!returnIt) {
        for (const PhysicsObject& o : c.state.objects) {
            if (o.type != ItemType::SelectionArea) continue;
            if (c.world.isCollidingWithAnother(obj, o)) returnIt = true;
            break;
        }
    }
    if (!returnIt) {
        if (rec.currentTime - ts.lastTouchTime < kVelocitySampleInterval) {
            const Vec2 d(ts.targetPos.x - ts.samplePos.x, ts.targetPos.y - ts.samplePos.y);
            ts.dragVelocity = pixelSizeToWorld(c.layout, d);
            ts.targetPos = ts.samplePos;
        }
        c.queue.add(Action(action::kManipulationEnded, obj.handle));
        ts.state = touch_state::kIdle;
        if (obj.showsGizmos()) ts.gizmoObject = ts.selectedObject;
        ts.selectedObject = -1;
    } else {
        c.queue.add(Action(action::kReturnToToolbox, obj.handle));
        ts.gizmoObject = -1;
        ts.state = touch_state::kReturning;
    }
    ts.slingshotObject = -1;
    ts.primaryTouch = -1;
    ts.secondaryTouch = -1;
    ts.rotationTouch = -1;
}

// FUN_000cedd8
void touchEnded(TouchContext& c, int slot) {
    TouchState& ts = c.ts;
    const TouchRecord& rec = c.touches.records[static_cast<std::size_t>(slot)];
    switch (ts.state) {
    case touch_state::kPending: {
        const int sel = ts.selectedObject;
        if (sel == -1 || !c.state.objects[static_cast<std::size_t>(sel)].isFixed()) {
            if (sel != -1 && c.state.objects[static_cast<std::size_t>(sel)].showsGizmos()) ts.gizmoObject = sel;
        } else {
            c.queue.add(Action(action::kFixedBuzzEnd, c.state.objects[static_cast<std::size_t>(sel)].handle));
        }
        if (slot != ts.primaryTouch || rec.tapCount != 2) c.queue.add(Action(action::kCornerLeave, 0));
        resetAfterTouch(ts, false);
        return;
    }
    case touch_state::kDragging:
        if (slot != ts.primaryTouch) return;
        if (ts.selectedObject == -1) {
            resetAfterTouch(ts, false);
            ts.samplePos = Vec2(0.0f, 0.0f);
            return;
        }
        release(c, rec);
        return;
    case touch_state::kRingRotate:
        c.queue.add(Action(action::kManipulationEnded, c.state.objects[static_cast<std::size_t>(ts.selectedObject)].handle));
        resetAfterTouch(ts, false);
        return;
    case touch_state::kFlipping:
        resetAfterTouch(ts, false);
        return;
    case touch_state::kBuzz:
        if (slot != ts.primaryTouch) return;
        c.queue.add(Action(action::kFixedBuzzEnd, 0));
        resetAfterTouch(ts, false);
        return;
    case touch_state::kToolboxScroll: {
        if (slot != ts.primaryTouch) return;
        const Vec2 d(rec.currentPx.x - ts.samplePos.x, rec.currentPx.y - ts.samplePos.y);
        c.queue.add(Action::scroll(Vec2(-d.x, -d.y)));
        if (ts.selectedObject == -1) {
            resetAfterTouch(ts, false);
            ts.samplePos = Vec2(0.0f, 0.0f);
            return;
        }
        release(c, rec);
        return;
    }
    case touch_state::kTwoFingerPending: {
        // [verified: 0xbf100] No rotating finger yet: the touch is over (the gizmo object is kept).
        if (ts.rotationTouch == -1) {
            ts.selectedObject = -1;
            ts.state = touch_state::kIdle;
            ts.primaryTouch = -1;
            ts.secondaryTouch = -1;
            ts.slingshotObject = -1;
            return;
        }
        int& lifted = slot == ts.primaryTouch ? ts.primaryTouch : ts.secondaryTouch;
        const int other = slot == ts.primaryTouch ? ts.secondaryTouch : ts.primaryTouch;
        if (slot != ts.rotationTouch) {
            lifted = -1;
            return;
        }
        if (other == -1) {
            c.queue.add(Action(action::kManipulationEnded, c.state.objects[static_cast<std::size_t>(ts.selectedObject)].handle));
            resetAfterTouch(ts, false);
            return;
        }
        // The other finger takes the ring over from where it is now.
        ts.rotationTouch = other;
        lifted = -1;
        ts.ringPx = pixelToScreenPos(c.layout, c.camera, c.touches.records[static_cast<std::size_t>(other)].currentPx);
        ts.angleStart = ts.angleCurrent;
        return;
    }
    case touch_state::kTwoFingerRotate: {
        // [verified: 0xbf19c] `other` = the finger that is not rotating (the secondary when the primary
        // rotates, else the primary).
        const int other = ts.rotationTouch == ts.primaryTouch ? ts.secondaryTouch : ts.primaryTouch;
        if (slot != ts.rotationTouch) {
            // The original clears the *primary* slot whichever finger `other` was.
            if (slot == other) ts.primaryTouch = -1;
            return;
        }
        const int primary = ts.primaryTouch;
        ts.secondaryTouch = -1;
        ts.rotationTouch = -1;
        if (primary == -1) {
            release(c, rec);
            return;
        }
        // The primary finger goes on dragging from its current position, the item from its target.
        ts.state = touch_state::kDragging;
        ts.startPos = ts.targetPos;
        TouchRecord& p = c.touches.records[static_cast<std::size_t>(primary)];
        p.startPx = p.currentPx;
        return;
    }
    case touch_state::kPinchZoom:
        // [verified: 0xbefb8] either finger lifting ends the pinch; the gizmo object is cleared too.
    default:
        resetAfterTouch(ts, true);
        return;
    }
}

// FUN_000cf4f0 on the rotating finger, then action 5 for the selected item.
void ringRotateStep(TouchContext& c, const TouchRecord& rec) {
    ringAngle(c, rec);
    c.queue.add(Action(action::kRotateSelected, c.state.objects[static_cast<std::size_t>(c.ts.selectedObject)].handle));
}

// The pinch (state 10) on a Moved event of either finger [verified: 0xc04bc–0xc06ec]: the zoom follows the
// change of the finger distance (0.003 per native px), the centre follows half of the fingers' midpoint
// motion (in virtual px, converted with the zoom *before* the update), then the zoom is clamped to
// [1, 2.5] and the centre through GetClampedCenter with the new zoom.
void pinchStep(TouchContext& c) {
    TouchState& ts = c.ts;
    const TouchRecord& a = c.touches.records[static_cast<std::size_t>(ts.primaryTouch)];
    const TouchRecord& b = c.touches.records[static_cast<std::size_t>(ts.secondaryTouch)];
    const float prevDist = length(Vec2(a.prevPx.x - b.prevPx.x, a.prevPx.y - b.prevPx.y));
    const float curDist = length(Vec2(a.currentPx.x - b.currentPx.x, a.currentPx.y - b.currentPx.y));
    const float zoom = c.camera.zoom + (curDist - prevDist) * kPinchZoomRate;
    const Vec2 prevMid(b.prevPx.x + (a.prevPx.x - b.prevPx.x) * kHalf, b.prevPx.y + (a.prevPx.y - b.prevPx.y) * kHalf);
    const Vec2 prevMidScreen = pixelToScreenPos(c.layout, c.camera, prevMid);
    const Vec2 curMid(b.currentPx.x + (a.currentPx.x - b.currentPx.x) * kHalf, b.currentPx.y + (a.currentPx.y - b.currentPx.y) * kHalf);
    const Vec2 curMidScreen = pixelToScreenPos(c.layout, c.camera, curMid);
    const Vec2 target(c.camera.centerPx.x - (curMidScreen.x - prevMidScreen.x) * kHalf,
                      c.camera.centerPx.y - (curMidScreen.y - prevMidScreen.y) * kHalf);
    float clamped = zoom;
    if (kPinchZoomMax - zoom < 0.0f) clamped = kPinchZoomMax;
    if (zoom - kPinchZoomMin < 0.0f) clamped = kPinchZoomMin;
    c.camera.zoom = clamped;
    c.camera.centerPx = clampedCameraCenter(c.camera, target);
}

}  // namespace

void processTouches(TouchContext& c) {
    TouchState& ts = c.ts;
    Touches& touches = c.touches;
    for (const TouchEvent& ev : touches.events) {
        const int slot = ev.index;
        TouchRecord& rec = touches.records[static_cast<std::size_t>(slot)];
        switch (ev.kind) {
        case TouchEvent::kBegan: {
            if (ts.state == touch_state::kIdle) {
                if (!c.toolbox.isOverToolbox(rec.startPx)) {
                    const Vec2 w = screenToWorld(c.layout, c.camera, rec.startPx);
                    int gz = ts.gizmoObject;
                    bool handled = false;
                    if (gz != -1) {
                        const PhysicsObject& gobj = c.state.objects[static_cast<std::size_t>(gz)];
                        if ((gobj.flags & object_flags::kFlippable) != 0) {
                            const float cx = cosF(kFlipButtonAngle);
                            const float sy = sinF(kFlipButtonAngle);
                            const float fx = gobj.position.x + cx * kFlipButtonDistance;
                            const float fy = sy * kFlipButtonDistance + gobj.position.y;
                            ScreenRect rect;
                            rect.top = kFlipButtonHalf + fy;
                            rect.bottom = -kFlipButtonHalf + fy;
                            rect.left = -kFlipButtonHalf + fx;
                            rect.right = kFlipButtonHalf + fx;
                            if (rect.contains(w)) {
                                c.queue.add(Action(action::kFlipSelected, gobj.handle));
                                ts.state = touch_state::kFlipping;
                                handled = true;
                            }
                            gz = ts.gizmoObject;
                        }
                        if (!handled && gz != -1) {
                            const PhysicsObject& obj = c.state.objects[static_cast<std::size_t>(gz)];
                            const float d = length(Vec2(w.x - obj.position.x, w.y - obj.position.y));
                            if (kRingInner <= d && d <= kRingOuter) {
                                ts.state = touch_state::kRingRotate;
                                ts.selectedObject = gz;
                                ts.primaryTouch = slot;
                                ts.ringPx = pixelToScreenPos(c.layout, c.camera, rec.currentPx);
                                ts.startPos = obj.position;
                                ts.angleStart = obj.angle;
                                ts.angleCurrent = obj.angle;
                                handled = true;
                            }
                        }
                    }
                    if (!handled) {
                        PickResult r;
                        if (pickForMode(c, w, r)) {
                            const PhysicsObject& obj = c.state.objects[static_cast<std::size_t>(r.object)];
                            if (obj.isFixed()) {
                                ts.state = touch_state::kPending;
                                ts.holdTimer = kHoldTime;
                                ts.gizmoObject = -1;
                                ts.selectedObject = obj.index;
                                ts.angleStart = obj.angle;
                                ts.bodyIndex = r.bodyIndex;
                                ts.angleCurrent = obj.angle;
                                ts.primaryTouch = slot;
                            } else {
                                ts.selectedObject = obj.index;
                                ts.angleStart = obj.angle;
                                ts.state = touch_state::kPending;
                                ts.bodyIndex = r.bodyIndex;
                                ts.angleCurrent = obj.angle;
                                ts.holdTimer = kHoldTime;
                                ts.tapCount = 0;
                                if (obj.index != ts.gizmoObject) ts.gizmoObject = -1;
                                ts.primaryTouch = slot;
                                c.queue.add(Action(action::kSelected, obj.handle));
                            }
                        } else {
                            ts.gizmoObject = -1;
                            ts.state = touch_state::kPending;
                            ts.tapCount = 0;
                            ts.primaryTouch = slot;
                            ts.holdTimer = kHoldTime;
                        }
                    }
                } else {
                    ts.gizmoObject = -1;
                    ts.tapCount = 0;
                    if (c.toolbox.getToolboxButtonRectangle().contains(rec.currentPx)) {
                        ts.state = touch_state::kToolboxButton;
                        c.events.push_back({SessionEvent::Kind::Sound, sound::kUIToolboxButton, Vec2(0.0f, 0.0f), 0.2f});
                    } else {
                        ts.primaryTouch = slot;
                        ts.samplePos = Vec2(0.0f, 0.0f);
                        ts.state = touch_state::kToolboxTouch;
                    }
                }
            } else if (ts.state == touch_state::kPending) {
                // A second finger [verified: 0xbfa94]: recorded; on a phone (DeviceParams::IsTablet false)
                // the pair becomes a pinch. The Android original is always a tablet here.
                ts.secondaryTouch = slot;
                if (!c.isTablet) ts.state = touch_state::kPinchZoom;
            }
            break;
        }
        case TouchEvent::kEnded: {
            if (ts.state == touch_state::kButton) break;   // in-world buttons: simulation only
            if (slot != ts.buttonTouch) {
                if (ts.state == touch_state::kToolboxButton) {
                    c.toolbox.buttonPressed = false;
                    c.events.push_back({SessionEvent::Kind::Sound, sound::kUIToolboxButtonRelease, Vec2(0.0f, 0.0f), 0.2f});
                    if (c.toolbox.buttonState == 0) c.toolbox.buttonState = 1;
                    else if (c.toolbox.buttonState == 1) c.toolbox.buttonState = 0;
                }
                touchEnded(c, slot);
            }
            break;
        }
        case TouchEvent::kMoved: {
            if (ts.state == touch_state::kButton || slot == ts.buttonTouch) break;
            switch (ts.state) {
            case touch_state::kPending:
                if (slot == ts.primaryTouch) {
                    const float dy = rec.currentPx.y - rec.startPx.y;
                    const float dx = rec.currentPx.x - rec.startPx.x;
                    if (kDragThresholdSquared < dy * dy + dx * dx) {
                        const int sel = ts.selectedObject;
                        if (sel == -1 || !c.state.objects[static_cast<std::size_t>(sel)].isFixed()) {
                            startManipulation(c, rec);
                        } else {
                            ts.state = touch_state::kBuzz;
                            ts.tapCount = ts.tapCount + 1;
                            c.queue.add(Action(action::kFixedBuzz, c.state.objects[static_cast<std::size_t>(sel)].handle));
                        }
                    }
                }
                break;
            case touch_state::kDragging:
                if (slot == ts.primaryTouch) {
                    if (kVelocitySampleInterval < rec.currentTime - rec.prevTime) {
                        ts.lastTouchTime = rec.currentTime;
                        ts.samplePos = ts.targetPos;
                    }
                    const Vec2 cur = screenToWorld(c.layout, c.camera, rec.currentPx);
                    const Vec2 start = screenToWorld(c.layout, c.camera, rec.startPx);
                    ts.targetPos = Vec2((cur.x - start.x) + ts.startPos.x, (cur.y - start.y) + ts.startPos.y);
                    ts.dragVelocity = pixelSizeToWorld(c.layout, Vec2(rec.currentPx.x - rec.prevPx.x, rec.currentPx.y - rec.prevPx.y));
                    c.queue.add(Action(action::kMoveSelected, c.state.objects[static_cast<std::size_t>(ts.selectedObject)].handle));
                }
                break;
            case touch_state::kRingRotate:
                if (slot == ts.primaryTouch) {
                    ringAngle(c, rec);
                    c.queue.add(Action(action::kRotateSelected, c.state.objects[static_cast<std::size_t>(ts.selectedObject)].handle));
                }
                break;
            case touch_state::kTwoFingerPending: {
                // Unreachable in the shipped binary (see the file comment) [verified: 0xc01ac, 0xc09a0]: the first
                // finger that moves more than 5 px becomes the rotating one, from its current position.
                if (ts.rotationTouch == -1 && (slot == ts.primaryTouch || slot == ts.secondaryTouch)) {
                    const float dy = rec.currentPx.y - rec.startPx.y;
                    const float dx = rec.currentPx.x - rec.startPx.x;
                    if (kDragThresholdSquared < dy * dy + dx * dx) {
                        ts.rotationTouch = slot;
                        ts.ringPx = pixelToScreenPos(c.layout, c.camera, rec.currentPx);
                    }
                }
                if (slot == ts.rotationTouch) ringRotateStep(c, rec);
                break;
            }
            case touch_state::kTwoFingerRotate: {
                // Unreachable in the shipped binary [verified: 0xc0208]: the secondary finger rotates; the
                // primary lets go of the item once it is farther than the item's size from it.
                if (slot == ts.secondaryTouch) {
                    ringRotateStep(c, rec);
                } else if (slot == ts.primaryTouch) {
                    const PhysicsObject& obj = c.state.objects[static_cast<std::size_t>(ts.selectedObject)];
                    const Vec2 w = screenToWorld(c.layout, c.camera, rec.currentPx);
                    const float dy = w.y - obj.position.y;
                    const float dx = w.x - obj.position.x;
                    if (obj.halfSize * obj.halfSize < dy * dy + dx * dx) ts.primaryTouch = -1;
                }
                break;
            }
            case touch_state::kPinchZoom:
                if (slot == ts.primaryTouch || slot == ts.secondaryTouch) pinchStep(c);
                break;
            case touch_state::kPan:
                if (slot == ts.primaryTouch) {
                    // The original subtracts the native pixel delta from the virtual-pixel centre as is.
                    const Vec2 target(c.camera.centerPx.x - (rec.currentPx.x - rec.prevPx.x),
                                      c.camera.centerPx.y - (rec.currentPx.y - rec.prevPx.y));
                    c.camera.centerPx = clampedCameraCenter(c.camera, target);
                }
                break;
            case touch_state::kToolboxTouch: {
                const float dy = rec.currentPx.y - rec.startPx.y;
                float dx = rec.currentPx.x - rec.startPx.x;
                if (kToolboxDragSquared < dy * dy + dx * dx && ts.samplePos.x != 0.0f) {
                    dx = std::fabs(dx);
                    if (dx == 0.0f) dx = 1.0f;
                    const float a = atan2F(std::fabs(dy), dx);
                    if (!c.toolbox.isScrollable() || kToolboxDragOutAngleDeg < a * kRadToDeg) {
                        const int slotIdx = c.toolbox.getSlotForPos(rec.startPx);
                        if (slotIdx >= 0) {
                            const ToolboxStripSlot& s = c.toolbox.slots[static_cast<std::size_t>(slotIdx)];
                            if (s.amount != 0 && !(s.heightPx + 20.0f < std::fabs(c.toolbox.y - rec.startPx.y))) {
                                Action a8(action::kNewFromToolbox, 0);
                                a8.payload = static_cast<int>(s.type);
                                c.queue.add(a8);
                                const Vec2 center = c.toolbox.getCenterForSlot(slotIdx);
                                const Vec2 px(c.toolbox.x + center.x, c.toolbox.y + center.y);
                                const Vec2 w = screenToWorld(c.layout, c.camera, px);
                                ts.state = touch_state::kFromToolbox;
                                ts.primaryTouch = slot;
                                ts.startPos = w;
                                ts.bodyIndex = 0;
                                ts.angleStart = 0.0f;
                                ts.angleCurrent = 0.0f;
                                ts.targetPos = w;
                            }
                        }
                    } else {
                        ts.state = touch_state::kToolboxScroll;
                    }
                }
                ts.samplePos = rec.currentPx;
                break;
            }
            case touch_state::kToolboxButton:
                if (!c.toolbox.getToolboxButtonRectangle().contains(rec.currentPx)) {
                    resetAfterTouch(ts, false);
                    c.toolbox.buttonPressed = false;
                } else {
                    c.toolbox.buttonPressed = true;
                }
                break;
            case touch_state::kToolboxScroll:
                if (slot == ts.primaryTouch) {
                    if (ts.samplePos.x != 0.0f) {
                        const Vec2 d(rec.currentPx.x - ts.samplePos.x, rec.currentPx.y - ts.samplePos.y);
                        c.queue.add(Action::scroll(Vec2(-d.x, -d.y)));
                    }
                    ts.samplePos = rec.currentPx;
                }
                break;
            default:
                break;
            }
            break;
        }
        case TouchEvent::kCancelled: {
            if (ts.state != touch_state::kButton && slot != ts.buttonTouch) {
                if (ts.state == touch_state::kToolboxButton) c.toolbox.buttonPressed = false;
                touchEnded(c, slot);
            }
            break;
        }
        default:
            break;
        }
    }
    touches.events.clear();
    // The hold timer (decremented in doFrame) ran out while pending: start dragging / buzz.
    if (ts.state == touch_state::kPending && ts.holdTimer <= 0.0001f && ts.primaryTouch >= 0) {
        const int sel = ts.selectedObject;
        if (sel == -1 || !c.state.objects[static_cast<std::size_t>(sel)].isFixed()) {
            startManipulation(c, touches.records[static_cast<std::size_t>(ts.primaryTouch)]);
        } else {
            ts.state = touch_state::kBuzz;
            ts.tapCount = ts.tapCount + 1;
            c.queue.add(Action(action::kFixedBuzz, c.state.objects[static_cast<std::size_t>(sel)].handle));
        }
    }
}

}  // namespace aa::sim
