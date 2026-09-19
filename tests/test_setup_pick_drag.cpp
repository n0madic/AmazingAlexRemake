// G5 (docs/10 §8): the set-up touch state machine — toolbox take-out, drag, drop, the fixed-item buzz,
// the empty-space pan and the undo push per drop — on a synthetic level (no assets needed).
#include "setup_rig.h"

#include <doctest.h>

#include <vector>

using namespace aa::sim;
using setup_test::Rig;
using setup_test::classroomLike;
using setup_test::kDt;

TEST_CASE("setup: the toolbox strip is on screen after load and its slot is where the strip says") {
    Rig rig;
    const Toolbox& tb = rig.session.toolbox();
    CHECK(tb.slotCount == 1);
    CHECK(tb.slots[0].type == ItemType::Book);
    CHECK(tb.slots[0].amount == 1);
    CHECK(tb.ejectLength > 0.0f);
    CHECK(tb.x < 1024.0f);
    CHECK(tb.isOverToolbox(Vec2(tb.x + tb.getCenterForSlot(0).x, tb.y)));
}

TEST_CASE("setup: dragging out of the toolbox spawns the item, moves it and drops it with one undo push") {
    Rig rig;
    const Vec2 slot = rig.slotPointer(0);
    rig.session.pointerDown(0, slot);
    std::vector<int> ids = rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kToolboxTouch);
    // Two moves: the first only records the scroll anchor, the second passes the 50 px² / 20° test.
    rig.session.pointerMove(0, Vec2(slot.x, slot.y - 4.0f));
    ids = rig.advanceIds();
    rig.session.pointerMove(0, Vec2(slot.x + 2.0f, slot.y - 40.0f));
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kNewFromToolbox));
    CHECK(rig.session.touchState().state == touch_state::kDragging);
    const int book = rig.objectOfType(ItemType::Book);
    REQUIRE(book >= 0);
    CHECK(rig.session.heldObject() == book);
    CHECK(rig.session.toolbox().slotCount == 0);   // the only book left the strip
    CHECK_FALSE(rig.session.state().objects[static_cast<std::size_t>(book)].isFixed());
    // The adding animation grows the item from 0.6 to 1 in 0.3 s.
    CHECK(rig.session.state().objects[static_cast<std::size_t>(book)].scale.x < 1.0f);
    rig.advanceIds(30);
    CHECK(rig.session.state().objects[static_cast<std::size_t>(book)].scale.x == 1.0f);
    // A move: action 4, the item's body follows the target.
    const Vec2 target(1.0f, 1.4f);
    rig.session.pointerMove(0, rig.worldToPointer(target));
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kMoveSelected));
    const Vec2 pos = rig.session.state().objects[static_cast<std::size_t>(book)].position;
    // The pointer moved from the slot's world position by the same delta as the item (drag = delta).
    CHECK(pos.y > 0.9f);
    CHECK(pos.y < 1.9f);
    CHECK(rig.session.undoQueue().count == 0);
    // Drop: action 2, ManipulationEnded, the undo push.
    rig.session.pointerUp(0, rig.worldToPointer(target));
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kManipulationEnded));
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    CHECK(rig.session.heldObject() == -1);
    CHECK(rig.session.touchState().gizmoObject == book);   // a dynamic item keeps its gizmos after the drop
    CHECK(rig.session.undoQueue().count == 1);
    CHECK(rig.session.canUndo());
    // Undo: the book is gone, the toolbox has it back.
    rig.session.undo();
    CHECK(rig.objectOfType(ItemType::Book) == -1);
    CHECK(rig.session.toolbox().slotCount == 1);
    CHECK_FALSE(rig.session.canUndo());
    CHECK(rig.session.canRedo());
    rig.session.redo();
    CHECK(rig.objectOfType(ItemType::Book) >= 0);
}

TEST_CASE("setup: touching a fixed item buzzes after 0.2 s or a 6 px move, and releases with 0xF") {
    Rig rig;
    const int shelf = rig.objectOfType(ItemType::Shelf);
    REQUIRE(shelf >= 0);
    const Vec2 p = rig.worldToPointer(Vec2(1.7f, 0.6f));
    rig.session.pointerDown(0, p);
    std::vector<int> ids = rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kPending);
    CHECK(rig.session.touchState().selectedObject == shelf);
    CHECK_FALSE(Rig::has(ids, action::kSelected));   // a fixed item is not "selected"
    // Hold: 0.2 s later the buzz.
    ids = rig.advanceIds(14);
    CHECK(Rig::has(ids, action::kFixedBuzz));
    CHECK(rig.session.touchState().state == touch_state::kBuzz);
    rig.session.pointerUp(0, p);
    ids = rig.advanceIds();
    CHECK(Rig::has(ids, action::kFixedBuzzEnd));
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    // Move: a 6 px drag buzzes at once.
    rig.session.pointerDown(0, p);
    rig.advanceIds();
    rig.session.pointerMove(0, Vec2(p.x + 6.0f, p.y));
    ids = rig.advanceIds();
    CHECK(Rig::has(ids, action::kFixedBuzz));
    rig.session.pointerUp(0, Vec2(p.x + 6.0f, p.y));
    rig.advanceIds();
    CHECK(rig.session.undoQueue().count == 0);   // nothing was edited
}

TEST_CASE("setup: dragging empty space pans the camera (state 0xB)") {
    Rig rig;
    rig.session.setCameraZoom(2.0f);
    const Vec2 before = rig.session.camera().centerPx;
    const Vec2 p(300.0f, 300.0f);
    rig.session.pointerDown(0, p);
    rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kPending);
    rig.session.pointerMove(0, Vec2(p.x + 20.0f, p.y));
    rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kPan);
    rig.session.pointerMove(0, Vec2(p.x + 40.0f, p.y));
    rig.advanceIds();
    CHECK(rig.session.camera().centerPx.x < before.x);
    rig.session.pointerUp(0, Vec2(p.x + 40.0f, p.y));
    rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kIdle);
}

TEST_CASE("setup: takeFromToolbox / returnHeld put an item out and back") {
    Rig rig;
    rig.session.takeFromToolbox(0);
    std::vector<int> ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kNewFromToolbox));
    const int book = rig.objectOfType(ItemType::Book);
    REQUIRE(book >= 0);
    CHECK(rig.session.toolbox().getItemCount() == 0);
    rig.session.returnHeld();
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kReturnToToolbox));
    CHECK(rig.session.touchState().state == touch_state::kReturning);
    // The adding animation (0.3 s) finishes first, then the removal (0.15 s), then action 10 removes the
    // item and refills the strip.
    ids = rig.advanceIds(40);
    REQUIRE(Rig::has(ids, action::kRemovalFinished));
    CHECK(rig.objectOfType(ItemType::Book) == -1);
    CHECK(rig.session.toolbox().slotCount == 1);
    CHECK(rig.session.toolbox().slots[0].amount == 1);
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    CHECK(rig.session.undoQueue().count == 1);
}
