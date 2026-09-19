// G5 (docs/10 §8): the two-finger branches of GameTouchHandler::Process — the pinch (state 10, a second
// finger in state 1 on a phone) and the rotation states 3 / 4, which no transition of the shipped binary
// enters (docs/05 §2): those are forced here to pin the reading of their Moved / Ended branches.
#include "aa/sim/coords.h"
#include "setup_rig.h"

#include <doctest.h>

#include <cmath>
#include <vector>

using namespace aa::sim;
using setup_test::Rig;
using setup_test::kDt;

namespace {

float dist(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

}  // namespace

TEST_CASE("two-finger: a second finger in state 1 pinches on a phone and is only recorded on a tablet") {
    SUBCASE("tablet: state 1 keeps the second finger as the secondary touch") {
        Rig rig;
        rig.session.setTablet(true);
        rig.session.pointerDown(0, Vec2(300.0f, 300.0f));
        rig.session.pointerDown(1, Vec2(600.0f, 300.0f));
        rig.advanceIds();
        CHECK(rig.session.touchState().state == touch_state::kPending);
        CHECK(rig.session.touchState().primaryTouch == 0);
        CHECK(rig.session.touchState().secondaryTouch == 1);
        CHECK(rig.session.camera().zoom == 1.0f);
    }
    SUBCASE("phone: the pair enters the pinch, either finger lifting ends it") {
        Rig rig;
        rig.session.pointerDown(0, Vec2(300.0f, 300.0f));
        rig.session.pointerDown(1, Vec2(600.0f, 300.0f));
        rig.advanceIds();
        REQUIRE(rig.session.touchState().state == touch_state::kPinchZoom);
        CHECK(rig.session.touchState().secondaryTouch == 1);
        // The hold timer never fires a drag out of state 10.
        rig.advanceIds(20);
        CHECK(rig.session.touchState().state == touch_state::kPinchZoom);
        // Finger 1 moves 100 px further away: zoom += 100 · 0.003 (the distance change in native px).
        rig.session.pointerMove(1, Vec2(700.0f, 300.0f));
        rig.advanceIds();
        CHECK(std::fabs(rig.session.camera().zoom - 1.3f) < 1e-5f);
        // The centre follows half of the midpoint's motion (50 px → 25 virtual px at 1024 wide) at the
        // old zoom, then GetClampedCenter at the new zoom: the pan is possible at zoom 1.3 and stays inside
        // [512 / 1.3, 1024 − 512 / 1.3].
        const Camera& cam = rig.session.camera();
        CHECK(cam.centerPx.x < Camera::kDefaultCenterX);
        CHECK(std::fabs(cam.centerPx.x - (Camera::kDefaultCenterX - 25.0f)) < 1e-3f);
        CHECK(cam.centerPx.x >= 512.0f / cam.zoom - 1e-3f);
        CHECK(cam.centerPx.y == Camera::kDefaultCenterY);
        // Way beyond the limit: the zoom clamps at 2.5.
        rig.session.pointerMove(1, Vec2(1024.0f + 1000.0f, 300.0f));
        rig.advanceIds();
        CHECK(rig.session.camera().zoom == 2.5f);
        const Vec2 c = rig.session.camera().centerPx;
        CHECK(c.x >= 512.0f / 2.5f - 1e-3f);
        CHECK(c.x <= 1024.0f - 512.0f / 2.5f + 1e-3f);
        CHECK(c.y >= 319.0f / 2.5f - 1e-3f);
        CHECK(c.y <= 638.0f - 319.0f / 2.5f + 1e-3f);
        // Fingers together again: the zoom bottoms out at 1 and the centre clamps back to the middle.
        rig.session.pointerMove(1, Vec2(301.0f, 300.0f));
        rig.advanceIds();
        CHECK(rig.session.camera().zoom == 1.0f);
        CHECK(rig.session.camera().centerPx.x == Camera::kDefaultCenterX);
        CHECK(rig.session.camera().centerPx.y == Camera::kDefaultCenterY);
        // The first finger lifts: the touch is over for both; the second finger's lift changes nothing.
        rig.session.pointerUp(0, Vec2(300.0f, 300.0f));
        std::vector<int> ids = rig.advanceIds();
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        CHECK(rig.session.touchState().primaryTouch == -1);
        CHECK(rig.session.touchState().secondaryTouch == -1);
        CHECK(ids.empty());
        rig.session.pointerUp(1, Vec2(301.0f, 300.0f));
        ids = rig.advanceIds();
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        CHECK(ids.empty());
    }
    SUBCASE("phone: the pinch over a movable item clears the gizmo object") {
        Rig rig;
        const int book = rig.takeAndHold();
        REQUIRE(book >= 0);
        rig.dropAt(Vec2(1.0f, 1.2f));
        rig.advanceIds(5);
        REQUIRE(rig.session.touchState().gizmoObject == book);
        // A tap on the book (state 1, selected) plus a second finger.
        rig.session.pointerDown(0, rig.worldToPointer(rig.obj(book).position));
        rig.advanceIds();
        REQUIRE(rig.session.touchState().state == touch_state::kPending);
        REQUIRE(rig.session.touchState().selectedObject == book);
        rig.session.pointerDown(1, Vec2(900.0f, 200.0f));
        rig.advanceIds();
        REQUIRE(rig.session.touchState().state == touch_state::kPinchZoom);
        rig.session.pointerUp(1, Vec2(900.0f, 200.0f));
        rig.advanceIds();
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        CHECK(rig.session.touchState().selectedObject == -1);
        CHECK(rig.session.touchState().gizmoObject == -1);
    }
}

TEST_CASE("two-finger: the forced rotation states 3 / 4 turn the item with the rotating finger") {
    Rig rig;
    const int book = rig.takeAndHold();
    REQUIRE(book >= 0);
    rig.dropAt(Vec2(1.2f, 1.0f));
    rig.advanceIds(5);
    // Finger 0 on the book (state 1, the book selected), finger 1 recorded, the state forced to 3.
    const Vec2 center = rig.worldToPointer(rig.obj(book).position);
    rig.session.pointerDown(0, center);
    rig.advanceIds();
    REQUIRE(rig.session.touchState().state == touch_state::kPending);
    REQUIRE(rig.session.touchState().selectedObject == book);
    const Vec2 f1(center.x + 150.0f, center.y);   // to the right of the book
    rig.session.pointerDown(1, f1);
    rig.advanceIds();
    const float angle0 = rig.obj(book).angle;
    SUBCASE("state 3: the finger that moves > 5 px rotates; its lift hands the ring to the other finger") {
        TouchState& ts = rig.session.touchState();
        ts.state = touch_state::kTwoFingerPending;
        ts.secondaryTouch = 1;
        ts.rotationTouch = -1;
        // 3 px: below the threshold, nothing happens.
        rig.session.pointerMove(1, Vec2(f1.x, f1.y - 3.0f));
        std::vector<int> ids = rig.advanceIds();
        CHECK(rig.session.touchState().rotationTouch == -1);
        CHECK(!Rig::has(ids, action::kRotateSelected));
        // 40 px up (y down on the window = counter-clockwise about the book): the ring starts, and the
        // angle is measured from the touch's *start* (FUN_000cf4f0 reads the record's start px).
        rig.session.pointerMove(1, Vec2(f1.x, f1.y - 40.0f));
        ids = rig.advanceIds();
        CHECK(rig.session.touchState().rotationTouch == 1);
        CHECK(Rig::has(ids, action::kRotateSelected));
        CHECK(std::fabs((rig.obj(book).angle - angle0) - std::atan2(40.0f, 150.0f)) < 1e-3f);
        // Straight above the book: a quarter turn from the start direction.
        rig.session.pointerMove(1, Vec2(center.x, f1.y - 150.0f));
        ids = rig.advanceIds(2);
        CHECK(Rig::has(ids, action::kRotateSelected));
        const float turned = rig.obj(book).angle - angle0;
        CHECK(std::fabs(turned - 1.5707964f) < 1e-3f);
        // The primary finger moving does not rotate.
        rig.session.pointerMove(0, Vec2(center.x + 2.0f, center.y));
        ids = rig.advanceIds();
        CHECK(!Rig::has(ids, action::kRotateSelected));
        // The rotating finger lifts: the primary takes the ring over from its own position, the angle
        // reached becomes the new start (no jump).
        rig.session.pointerUp(1, Vec2(center.x, f1.y - 150.0f));
        ids = rig.advanceIds();
        CHECK(rig.session.touchState().state == touch_state::kTwoFingerPending);
        CHECK(rig.session.touchState().rotationTouch == 0);
        CHECK(rig.session.touchState().secondaryTouch == -1);
        CHECK(rig.session.touchState().angleStart == rig.session.touchState().angleCurrent);
        CHECK(std::fabs(rig.obj(book).angle - (angle0 + turned)) < 1e-6f);
        // The last finger lifts: the manipulation ends, the gizmo stays on the book.
        rig.session.pointerUp(0, Vec2(center.x + 2.0f, center.y));
        ids = rig.advanceIds();
        CHECK(Rig::has(ids, action::kManipulationEnded));
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        CHECK(rig.session.touchState().gizmoObject == book);
    }
    SUBCASE("state 3 without a rotating finger: a lift ends the touch and keeps the gizmo") {
        TouchState& ts = rig.session.touchState();
        ts.state = touch_state::kTwoFingerPending;
        ts.secondaryTouch = 1;
        ts.rotationTouch = -1;
        rig.session.pointerUp(1, f1);
        const std::vector<int> ids = rig.advanceIds();
        CHECK(ids.empty());
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        CHECK(rig.session.touchState().primaryTouch == -1);
        CHECK(rig.session.touchState().secondaryTouch == -1);
        CHECK(rig.session.touchState().gizmoObject == book);
    }
    SUBCASE("state 4: the secondary rotates, the primary lets go once far from the item, its lift continues the drag") {
        TouchState& ts = rig.session.touchState();
        ts.state = touch_state::kTwoFingerRotate;
        ts.secondaryTouch = 1;
        ts.rotationTouch = 1;
        ts.ringPx = pixelToScreenPos(rig.layout, rig.session.camera(), rig.yDown(f1));
        ts.angleStart = angle0;
        ts.angleCurrent = angle0;
        ts.targetPos = rig.obj(book).position;
        rig.session.pointerMove(1, Vec2(center.x, f1.y - 150.0f));
        std::vector<int> ids = rig.advanceIds(2);
        CHECK(Rig::has(ids, action::kRotateSelected));
        const float turned = rig.obj(book).angle - angle0;
        CHECK(std::fabs(turned - 1.5707964f) < 1e-3f);
        // The primary 2 px away: still holding.
        rig.session.pointerMove(0, Vec2(center.x + 2.0f, center.y));
        rig.advanceIds();
        CHECK(rig.session.touchState().primaryTouch == 0);
        SUBCASE("the rotating finger lifts while the primary holds: back to dragging from here") {
            rig.session.pointerUp(1, Vec2(center.x, f1.y - 150.0f));
            ids = rig.advanceIds();
            CHECK(rig.session.touchState().state == touch_state::kDragging);
            CHECK(rig.session.touchState().primaryTouch == 0);
            CHECK(rig.session.touchState().secondaryTouch == -1);
            CHECK(rig.session.touchState().rotationTouch == -1);
            CHECK(rig.session.touchState().startPos.x == rig.session.touchState().targetPos.x);
            // The drag continues relative to the primary's current position: a 100 px move to the right
            // moves the target by 100 px worth of metres.
            rig.session.pointerMove(0, Vec2(center.x + 102.0f, center.y));
            ids = rig.advanceIds();
            CHECK(Rig::has(ids, action::kMoveSelected));
            CHECK(std::fabs((rig.session.touchState().targetPos.x - rig.session.touchState().startPos.x) - 100.0f * kPixelToMeters) < 1e-4f);
        }
        SUBCASE("the primary far from the item lets go; the rotating finger's lift then drops the item") {
            rig.session.pointerMove(0, Vec2(center.x + 400.0f, center.y));
            rig.advanceIds();
            CHECK(rig.session.touchState().primaryTouch == -1);
            rig.session.pointerUp(1, Vec2(center.x, f1.y - 150.0f));
            ids = rig.advanceIds();
            CHECK(Rig::has(ids, action::kManipulationEnded));
            CHECK(rig.session.touchState().state == touch_state::kIdle);
        }
        SUBCASE("the non-rotating finger's lift clears the primary slot") {
            rig.session.pointerUp(0, Vec2(center.x + 2.0f, center.y));
            rig.advanceIds();
            CHECK(rig.session.touchState().state == touch_state::kTwoFingerRotate);
            CHECK(rig.session.touchState().primaryTouch == -1);
            CHECK(rig.session.touchState().secondaryTouch == 1);
        }
    }
}
