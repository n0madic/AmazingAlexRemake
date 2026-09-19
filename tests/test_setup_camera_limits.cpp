// G5 (docs/10 §8): the camera edge auto-scroll (WP7), the toolbox / item limits (WP5), the undo queue
// over real edits, play / stop / restart and the pause-menu release (WP6) — on synthetic levels.
#include "aa/sim/attachments.h"
#include "aa/sim/items/items.h"
#include "setup_rig.h"

#include <doctest.h>

#include <cmath>
#include <vector>

using namespace aa::sim;
using setup_test::Rig;
using setup_test::classroomLike;
using setup_test::hookLevel;
using setup_test::kDt;

namespace {
int liveJoints(const PhysicsWorld& world) {
    int n = 0;
    for (int i = 0; i < world.jointCount(); ++i) n += world.joint(i) != nullptr ? 1 : 0;
    return n;
}
}  // namespace

TEST_CASE("setup: an item held 50 px from the right edge scrolls the camera at 300 px/s after 0.4 s (phones only)") {
    Rig rig;
    rig.session.setCameraZoom(2.0f);
    const Vec2 before = rig.session.camera().centerPx;
    const int book = rig.takeAndHold();
    REQUIRE(book >= 0);
    // 50 px from the right edge, mid-height (well above the strip).
    const Vec2 edge(static_cast<float>(rig.layout.width) - 50.0f, 300.0f);
    rig.session.pointerMove(0, edge);
    std::vector<int> ids = rig.advanceIds(20);   // 0.33 s: still inside the 0.4 s delay
    CHECK(rig.session.camera().centerPx.x == before.x);
    CHECK(rig.session.camera().edgeTimer > 0.0f);
    const Vec2 target = rig.session.touchState().targetPos;
    ids = rig.advanceIds(12);   // past 0.4 s: the centre moves right by 300·dt per frame
    const float moved = rig.session.camera().centerPx.x - before.x;
    CHECK(moved > 0.0f);
    CHECK(moved <= 300.0f * kDt * 12.0f + 1e-3f);
    CHECK(rig.session.camera().centerPx.y == before.y);
    // The item follows: its target moved by the same amount in world units, through action 4.
    CHECK(Rig::has(ids, action::kMoveSelected));
    CHECK(rig.session.touchState().targetPos.x > target.x);
    CHECK(std::fabs((rig.session.touchState().targetPos.x - target.x) - moved * kPixelToMeters) < 1e-4f);
    // Moving the finger back into the middle stops the scroll and resets the timer.
    rig.session.pointerMove(0, Vec2(500.0f, 300.0f));
    rig.advanceIds(2);
    CHECK(rig.session.camera().edgeTimer == 0.0f);
}

TEST_CASE("setup: at zoom 1 the camera clamp holds and a tablet never edge-scrolls") {
    SUBCASE("zoom 1") {
        Rig rig;
        const Vec2 before = rig.session.camera().centerPx;
        REQUIRE(rig.takeAndHold() >= 0);
        rig.session.pointerMove(0, Vec2(static_cast<float>(rig.layout.width) - 50.0f, 300.0f));
        rig.advanceIds(40);
        CHECK(rig.session.camera().centerPx.x == before.x);
        CHECK(rig.session.camera().centerPx.y == before.y);
    }
    SUBCASE("tablet") {
        Rig rig;
        rig.session.setTablet(true);
        rig.session.setCameraZoom(2.0f);
        const Vec2 before = rig.session.camera().centerPx;
        REQUIRE(rig.takeAndHold() >= 0);
        rig.session.pointerMove(0, Vec2(static_cast<float>(rig.layout.width) - 50.0f, 300.0f));
        rig.advanceIds(40);
        CHECK(rig.session.camera().centerPx.x == before.x);
        CHECK(rig.session.camera().edgeTimer == 0.0f);
    }
}

TEST_CASE("setup: an unlimited toolbox slot (amount -1) never empties") {
    Rig rig(classroomLike({{ItemType::Book, -1}}));
    for (int i = 0; i < 3; ++i) {
        REQUIRE(rig.takeAndHold() >= 0);
        rig.dropAt(Vec2(0.6f + 0.4f * static_cast<float>(i), 1.6f));
        CHECK(rig.session.toolbox().slotCount == 1);
        CHECK(rig.session.toolbox().slots[0].amount == -1);
    }
    CHECK(rig.countOfType(ItemType::Book) == 3);
}

TEST_CASE("setup: the per-type limit refuses the 33rd item of a type (count > 0x1f)") {
    Rig rig(classroomLike({{ItemType::Book, -1}}));
    for (int i = 0; i < 32; ++i) {
        REQUIRE(rig.takeAndHold() >= 0);
        rig.dropAt(Vec2(0.4f + 0.08f * static_cast<float>(i), 2.0f));
    }
    CHECK(rig.countOfType(ItemType::Book) == 32);
    rig.session.takeFromToolbox(0);
    rig.advanceIds(3);
    CHECK(rig.countOfType(ItemType::Book) == 32);
    CHECK(rig.session.heldObject() == -1);
    CHECK(rig.session.touchState().state == touch_state::kIdle);
}

TEST_CASE("setup: 31 drops undo back to the base; the 32nd push drops the oldest snapshot") {
    Rig rig(classroomLike({{ItemType::Book, -1}}));
    for (int i = 0; i < 31; ++i) {
        REQUIRE(rig.takeAndHold() >= 0);
        rig.dropAt(Vec2(0.4f + 0.08f * static_cast<float>(i), 2.0f));
        CHECK(rig.session.undoQueue().count == i + 1);
    }
    CHECK(rig.session.undoQueue().count == UndoQueue::kMaxCount);   // base + 31 = 32 layouts, count 0x1f
    for (int i = 0; i < 31; ++i) rig.session.undo();
    CHECK_FALSE(rig.session.canUndo());
    CHECK(rig.countOfType(ItemType::Book) == 0);
    CHECK(rig.session.canRedo());
    // 32 drops: the push at count 0x1f moves the oldest layout (the base) out; the undos stop one book short.
    for (int i = 0; i < 32; ++i) {
        REQUIRE(rig.takeAndHold() >= 0);
        rig.dropAt(Vec2(0.4f + 0.08f * static_cast<float>(i), 2.0f));
    }
    CHECK(rig.session.undoQueue().count == UndoQueue::kMaxCount);
    while (rig.session.canUndo()) rig.session.undo();
    CHECK(rig.countOfType(ItemType::Book) == 1);
}

TEST_CASE("setup: undo after a snap removes the joint; play / stop keep the poses; no undo while holding") {
    Rig rig(hookLevel());
    const int hook = rig.objectOfType(ItemType::Hook);
    const int rope = rig.takeAndHold();
    REQUIRE(rope >= 0);
    CHECK_FALSE(rig.session.canUndo());
    rig.session.undo();   // ignored while holding
    CHECK(rig.session.heldObject() == rope);
    // End A onto the hook point (1.7, 1.57), slowly, then the drop attaches.
    const Vec2 grabbed = rig.session.world()->body(rig.obj(rope).bodies[0])->GetPosition();
    const Vec2 endA = rig.obj(rope).position;
    const Vec2 fast(grabbed.x + (1.68f - endA.x), grabbed.y + (1.53f - endA.y));
    rig.session.pointerMove(0, rig.worldToPointer(fast));
    rig.advanceIds();
    const Vec2 px = rig.worldToPointer(fast);
    rig.session.pointerMove(0, Vec2(px.x + 1.0f, px.y));
    rig.advanceIds();
    rig.session.pointerUp(0, Vec2(px.x + 1.0f, px.y));
    rig.advanceIds(2);
    REQUIRE(rig.obj(rope).attachments[0].state == attachment_state::kAttached);
    const int jointsAttached = liveJoints(*rig.session.world());
    CHECK(rig.session.undoQueue().count == 1);
    // play / stop rebuild the world from the same layout: same poses, same records.
    const Vec2 ropePos = rig.obj(rope).position;
    rig.session.play();
    CHECK(rig.session.physicsMode() == PhysicsMode::Simulation);
    rig.session.stop();
    CHECK(rig.session.physicsMode() == PhysicsMode::SetUp);
    const int ropeAgain = rig.objectOfType(ItemType::Rope);
    REQUIRE(ropeAgain >= 0);
    CHECK(rig.obj(ropeAgain).position.x == ropePos.x);
    CHECK(rig.obj(ropeAgain).position.y == ropePos.y);
    CHECK(rig.obj(ropeAgain).attachments[0].state == attachment_state::kAttached);
    CHECK(liveJoints(*rig.session.world()) == jointsAttached);
    // Undo: the rope is back in the strip, the hook record is free, the attachment joint is gone.
    rig.session.undo();
    CHECK(rig.objectOfType(ItemType::Rope) == -1);
    CHECK(rig.obj(hook).attachments[0].state == attachment_state::kFree);
    CHECK(rig.session.toolbox().getItemCount() == 1);
    CHECK(liveJoints(*rig.session.world()) < jointsAttached);
}

TEST_CASE("setup: restart restores the base layout without the SelectionArea (original quirk) and clears the undo count") {
    Rig rig;
    REQUIRE(rig.takeAndHold() >= 0);
    rig.dropAt(Vec2(0.8f, 1.6f));
    CHECK(rig.countOfType(ItemType::Book) == 1);
    CHECK(rig.objectOfType(ItemType::SelectionArea) >= 0);
    rig.session.restart();
    CHECK(rig.countOfType(ItemType::Book) == 0);
    CHECK(rig.session.toolbox().getItemCount() == 1);
    CHECK(rig.session.undoQueue().count == 0);
    CHECK_FALSE(rig.session.canUndo());
    // restartLevel restores the layout captured before CreateSelectionAreaObject and never re-adds it.
    CHECK(rig.objectOfType(ItemType::SelectionArea) == -1);
    // A later edit snapshots the world as it is; undo brings that world back.
    REQUIRE(rig.takeAndHold() >= 0);
    rig.dropAt(Vec2(0.8f, 1.6f));
    CHECK(rig.session.undoQueue().count == 1);
    rig.session.undo();
    CHECK(rig.countOfType(ItemType::Book) == 0);
}

TEST_CASE("setup: pause() releases the held item where it is; a fresh toolbox item goes back to the strip") {
    Rig rig;
    SUBCASE("a placed item picked up again is dropped in place") {
        REQUIRE(rig.takeAndHold() >= 0);
        rig.dropAt(Vec2(0.8f, 1.6f));
        const int book = rig.objectOfType(ItemType::Book);
        REQUIRE(book >= 0);
        rig.session.pointerDown(0, rig.worldToPointer(rig.obj(book).position));
        rig.advanceIds(14);   // the hold timer starts the drag
        rig.session.pointerMove(0, rig.worldToPointer(Vec2(0.9f, 1.7f)));
        rig.advanceIds();
        REQUIRE(rig.session.heldObject() == book);
        rig.session.pause();
        CHECK(rig.session.heldObject() == -1);
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        CHECK(rig.countOfType(ItemType::Book) == 1);
        CHECK(rig.session.undoQueue().count == 2);   // the cancelled drag ends like a drop: one more push
    }
    SUBCASE("an item still being added returns to the toolbox") {
        REQUIRE(rig.takeAndHold() >= 0);
        rig.session.pause();
        // releaseHeldItems queues action 9 while `adding`; the removal animation then takes 0.15 s.
        rig.advanceIds(40);
        CHECK(rig.countOfType(ItemType::Book) == 0);
        CHECK(rig.session.toolbox().getItemCount() == 1);
    }
}

TEST_CASE("setup: returning an item with a rope hanging on it detaches the rope (FUN_000c9590)") {
    // Hook + toolbox rope and bucket: the rope hangs on the hook, the bucket on the rope; returning the
    // bucket to the strip detaches the rope end (the rope stays, still on the hook).
    Rig rig(hookLevel({{ItemType::Rope, 1}, {ItemType::Bucket, 1}}));
    const int hook = rig.objectOfType(ItemType::Hook);
    const int rope = rig.takeAndHold(0);
    REQUIRE(rope >= 0);
    {
        const Vec2 grabbed = rig.session.world()->body(rig.obj(rope).bodies[0])->GetPosition();
        const Vec2 endA = rig.obj(rope).position;
        const Vec2 fast(grabbed.x + (1.68f - endA.x), grabbed.y + (1.53f - endA.y));
        rig.session.pointerMove(0, rig.worldToPointer(fast));
        rig.advanceIds();
        const Vec2 px = rig.worldToPointer(fast);
        rig.session.pointerMove(0, Vec2(px.x + 1.0f, px.y));
        rig.advanceIds();
        rig.session.pointerUp(0, Vec2(px.x + 1.0f, px.y));
        rig.advanceIds(2);
    }
    REQUIRE(rig.obj(rope).attachments[0].state == attachment_state::kAttached);
    REQUIRE(rig.obj(rope).attachments[0].otherObject == hook);
    // The bucket's hanging point onto the rope's free end B (0.6 m below end A).
    const int bucket = rig.takeAndHold(0);
    REQUIRE(bucket >= 0);
    REQUIRE(rig.obj(bucket).type == ItemType::Bucket);
    const Vec2 endB = attachmentPosWS(rig.obj(rope), *rig.session.world(), 1);
    const Vec2 bucketPoint = attachmentPosWS(rig.obj(bucket), *rig.session.world(), 0);
    const Vec2 grabbed = rig.session.world()->body(rig.obj(bucket).bodies[0])->GetPosition();
    const Vec2 fast(grabbed.x + (endB.x - bucketPoint.x) + 0.02f, grabbed.y + (endB.y - bucketPoint.y) - 0.03f);
    rig.session.pointerMove(0, rig.worldToPointer(fast));
    rig.advanceIds();
    const Vec2 px = rig.worldToPointer(fast);
    rig.session.pointerMove(0, Vec2(px.x + 1.0f, px.y));
    rig.advanceIds();
    rig.session.pointerUp(0, Vec2(px.x + 1.0f, px.y));
    rig.advanceIds(2);
    REQUIRE(rig.obj(bucket).attachments[0].state == attachment_state::kAttached);
    REQUIRE(rig.obj(bucket).attachments[0].otherObject == rope);
    CHECK(rig.obj(rope).attachments[1].state == attachment_state::kAttached);
    // Pick the bucket up again and return it to the strip.
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(bucket).position));
    rig.advanceIds(14);
    REQUIRE(rig.session.heldObject() == bucket);
    rig.session.returnHeld();
    rig.advanceIds(40);
    CHECK(rig.objectOfType(ItemType::Bucket) == -1);
    CHECK(rig.session.toolbox().getItemCount() == 1);
    const int ropeNow = rig.objectOfType(ItemType::Rope);
    REQUIRE(ropeNow >= 0);
    CHECK(rig.obj(ropeNow).attachments[0].state == attachment_state::kAttached);   // still on the hook
    CHECK(rig.obj(ropeNow).attachments[1].state == attachment_state::kFree);        // the bucket end let go
}

TEST_CASE("setup: restoreGameState drops every record that indexed the old collection") {
    SUBCASE("a lingering gizmo after undo") {
        Rig rig;
        const int book = rig.takeAndHold();
        REQUIRE(book >= 0);
        rig.dropAt(Vec2(1.0f, 1.6f));
        REQUIRE(rig.session.touchState().gizmoObject == book);
        REQUIRE(rig.session.undoQueue().count == 1);
        rig.session.undo();
        CHECK(rig.session.touchState().gizmoObject == -1);
        CHECK(rig.objectOfType(ItemType::Book) == -1);
        // Nothing to flip: no action, no flipping state, the next tap still works.
        rig.session.flipHeld();
        std::vector<int> ids = rig.advanceIds();
        CHECK_FALSE(Rig::has(ids, action::kFlipSelected));
        CHECK(rig.session.touchState().state == touch_state::kIdle);
        rig.session.pointerDown(0, rig.worldToPointer(Vec2(1.0f, 1.6f)));
        rig.advanceIds();
        CHECK(rig.session.touchState().state != touch_state::kFlipping);
        rig.session.pointerUp(0, rig.worldToPointer(Vec2(1.0f, 1.6f)));
        rig.advanceIds(2);
    }
    SUBCASE("a running ghost glide after undo and after play") {
        Rig rig(classroomLike({{ItemType::Book, 3}}));
        for (int i = 0; i < 2; ++i) {
            REQUIRE(rig.takeAndHold() >= 0);
            rig.dropAt(Vec2(0.6f + 0.3f * static_cast<float>(i), 1.6f));
        }
        REQUIRE(rig.session.undoQueue().count == 2);
        const int book = rig.takeAndHold();
        REQUIRE(book >= 0);
        rig.session.pointerMove(0, rig.worldToPointer(Vec2(1.7f, 1.6f)));
        rig.advanceIds(2);
        rig.session.pointerMove(0, rig.worldToPointer(Vec2(1.7f, 0.6f)));   // into the shelf
        rig.advanceIds(2);
        REQUIRE(rig.session.ghost().inGhost);
        rig.session.pointerUp(0, rig.worldToPointer(Vec2(1.7f, 0.6f)));
        rig.advanceIds();
        REQUIRE(rig.session.ghost().inGhost);   // the glide is running, the touch is idle
        REQUIRE(rig.session.touchState().state == touch_state::kIdle);
        SUBCASE("undo") {
            rig.session.undo();
            CHECK_FALSE(rig.session.ghost().inGhost);
            CHECK(rig.session.ghost().ghostObject == -1);
            CHECK(rig.countOfType(ItemType::Book) == 1);
            rig.advanceIds(40);   // no glide end, no spurious undo push, no teleport
            CHECK(rig.session.undoQueue().count == 1);
            CHECK(rig.session.undoQueue().top == 2);
            CHECK(rig.countOfType(ItemType::Book) == 1);
            CHECK(rig.obj(rig.objectOfType(ItemType::Book)).position.y > 1.0f);
        }
        SUBCASE("play waits for the glide") {
            // The ghost copy must never reach the simulation layout as a real item: play refuses while the
            // glide runs, then works once the good pose is back.
            rig.session.play();
            CHECK(rig.session.physicsMode() == PhysicsMode::SetUp);
            rig.advanceIds(40);
            REQUIRE_FALSE(rig.session.ghost().inGhost);
            rig.session.play();
            CHECK(rig.session.physicsMode() == PhysicsMode::Simulation);
            CHECK(rig.countOfType(ItemType::Book) == 3);
            rig.session.stop();
            CHECK(rig.countOfType(ItemType::Book) == 3);
            rig.advanceIds(40);
            CHECK(rig.countOfType(ItemType::Book) == 3);
        }
    }
}

TEST_CASE("setup: a ball dragged over a movable slingshot's pouch takes the pouch along (fixture user data 2)") {
    Rig rig(classroomLike({{ItemType::Slingshot, 1}, {ItemType::TennisBall, 1}}));
    const int sling = rig.takeAndHold(0);
    REQUIRE(sling >= 0);
    rig.dropAt(Vec2(0.8f, 1.2f));
    // (References into the collections are re-taken after the ball is added: the vectors may reallocate.)
    auto slingItem = [&]() -> const GameItem& { return rig.session.state().itemOf(rig.obj(sling)); };
    // Stretch the pouch 0.4 m to the left first (a fresh pouch sits 0.11 m from the frame, too close for a
    // ball): the finger on the pouch picks the slingshot with body index 1.
    Vec2 pouch = items::slingshotPouchPosition(rig.obj(sling), slingItem());
    rig.session.pointerDown(0, rig.worldToPointer(pouch));
    rig.advanceIds(14);
    REQUIRE(rig.session.heldObject() == sling);
    REQUIRE(rig.session.touchState().bodyIndex == 1);
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(0.4f, 1.2f)));
    rig.advanceIds(2);
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(0.4f, 1.2f)));
    rig.advanceIds(2);
    CHECK(slingItem().endVector.x < -0.3f);
    pouch = items::slingshotPouchPosition(rig.obj(sling), slingItem());
    const int ball = rig.takeAndHold(0);   // the ball is slot 0 once the slingshot left the strip
    REQUIRE(ball >= 0);
    REQUIRE(rig.obj(ball).type == ItemType::TennisBall);
    rig.dropAt(pouch);
    REQUIRE_FALSE(rig.session.ghost().inGhost);
    // Pick the ball up again: the pick query sees the pouch circle (user data 2) under the finger.
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(ball).position));
    rig.advanceIds(14);
    REQUIRE(rig.session.heldObject() == ball);
    CHECK(rig.session.touchState().slingshotObject == sling);
    const Vec2 before = slingItem().endVector;
    const Vec2 to(pouch.x - 0.05f, pouch.y - 0.05f);
    rig.session.pointerMove(0, rig.worldToPointer(to));
    rig.advanceIds(2);
    CHECK(slingItem().endVector.x != before.x);
    CHECK(slingItem().endVector.y != before.y);
    rig.session.pointerUp(0, rig.worldToPointer(to));
    rig.advanceIds(2);
}
