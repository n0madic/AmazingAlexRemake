// G5 (docs/10 §8): snapping and attachments, rotation (ring + rotateHeld), the flip animation and the
// ghost state on synthetic levels (no assets needed).
#include "aa/sim/attachments.h"
#include "setup_rig.h"

#include <doctest.h>

#include <cmath>
#include <vector>

using namespace aa::sim;
using setup_test::Rig;
using setup_test::classroomLike;
using setup_test::hookLevel;
using setup_test::kDt;
using setup_test::levelItem;

namespace {

Level shelfLevel(ItemType toolboxType) {
    Level level;
    level.items = {levelItem(ItemType::WorldBound, 0, Vec2(0.0f, 0.0f), false), levelItem(ItemType::Shelf, 1, Vec2(1.7f, 1.0f))};
    level.toolbox.push_back({toolboxType, 1});
    return level;
}

}  // namespace

TEST_CASE("setup: a rope end dragged slowly onto a hook snaps, attaches on drop and detaches on pick-up") {
    Rig rig(hookLevel());
    const int hook = rig.objectOfType(ItemType::Hook);
    REQUIRE(hook >= 0);
    // A hangable (bucket) never snaps to a hook directly — their masks only accept rope ends (docs/04
    // §7); the Classroom "bucket on hook" goes through a rope. Take the rope out; a pointer down while it
    // hangs on no finger adopts that pointer as the finger.
    rig.session.takeFromToolbox(0);
    rig.advanceIds(25);
    const int rope = rig.session.heldObject();
    REQUIRE(rope >= 0);
    CHECK(rig.obj(rope).type == ItemType::Rope);
    CHECK(rig.session.touchState().bodyIndex == 0);   // the root body: the whole rope moves
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(rope).position));
    rig.advanceIds();
    // A fast move (no snapping while the finger moves faster than 0.005 m per event) brings end A within
    // the 0.08 m point radius of the hook point at (1.7, 1.57) (the AABB query spans 1.1·rope length).
    // The drag moves the item by (target − grabbed body position): aim the root body accordingly.
    const Vec2 grabbed = rig.session.world()->body(rig.obj(rope).bodies[0])->GetPosition();
    const Vec2 endA = rig.obj(rope).position;
    const Vec2 wantEndA(1.68f, 1.53f);
    const Vec2 fast(grabbed.x + (wantEndA.x - endA.x), grabbed.y + (wantEndA.y - endA.y));
    rig.session.pointerMove(0, rig.worldToPointer(fast));
    std::vector<int> ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kMoveSelected));
    CHECK(rig.obj(rope).attachments[0].state == attachment_state::kFree);
    // A one-pixel move: slow enough for CalculateSnap.
    const Vec2 px = rig.worldToPointer(fast);
    rig.session.pointerMove(0, Vec2(px.x + 1.0f, px.y));
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kMoveSelected));
    const PhysicsObject& r = rig.obj(rope);
    CHECK(r.attachments[0].state == attachment_state::kSnapped);
    CHECK(r.attachments[0].otherObject == hook);
    CHECK(rig.obj(hook).attachments[0].state == attachment_state::kSnapped);
    // Snapped: end A sits on the hook point.
    const Vec2 ap = attachmentPosWS(r, *rig.session.world(), 0);
    const Vec2 hp = attachmentPosWS(rig.obj(hook), *rig.session.world(), 0);
    CHECK(std::fabs(ap.x - hp.x) < 1e-3f);
    CHECK(std::fabs(ap.y - hp.y) < 1e-3f);
    // Drop: Attach → a revolute joint between the two.
    rig.session.pointerUp(0, Vec2(px.x + 1.0f, px.y));
    ids = rig.advanceIds(2);
    REQUIRE(Rig::has(ids, action::kManipulationEnded));
    CHECK(rig.obj(rope).attachments[0].state == attachment_state::kAttached);
    CHECK(rig.obj(rope).attachments[0].joint >= 0);
    CHECK(rig.session.world()->joint(rig.obj(rope).attachments[0].joint) != nullptr);
    CHECK((rig.obj(rope).selectable[0] & body_flags::kSelectable) == 0);   // an attached end is not pickable
    CHECK(rig.session.undoQueue().count == 1);
    // Pick up the whole rope again (its root body): RopeUtils::ManipulationStarted detaches everything.
    const b2Body* root = rig.session.world()->body(rig.obj(rope).bodies[0]);
    const Vec2 rootPos(root->GetPosition().x, root->GetPosition().y);
    rig.session.pointerDown(0, rig.worldToPointer(rootPos));
    ids = rig.advanceIds(15);
    REQUIRE(Rig::has(ids, action::kManipulationStarted));
    CHECK(rig.obj(rope).attachments[0].state == attachment_state::kFree);
    CHECK(rig.obj(hook).attachments[0].state == attachment_state::kFree);
    CHECK((rig.obj(rope).selectable[0] & body_flags::kSelectable) != 0);
    rig.session.pointerUp(0, rig.worldToPointer(rootPos));
    rig.advanceIds(2);
    CHECK(rig.session.undoQueue().count == 2);
}

TEST_CASE("setup: rotateHeld turns the held item and survives the next move; the ring drag rotates too") {
    Rig rig(shelfLevel(ItemType::Book));
    rig.session.takeFromToolbox(0);
    rig.advanceIds(25);
    const int book = rig.session.heldObject();
    REQUIRE(book >= 0);
    rig.session.rotateHeld(0.5f);
    std::vector<int> ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kRotateSelected));
    // While a toolbox item is being added (+0xc99b9) the original ignores action 5 — the angle lives in
    // the touch state and the next UpdatePos applies it.
    CHECK(rig.session.touchState().angleCurrent == doctest::Approx(0.5f));
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(book).position));
    rig.advanceIds();
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(0.9f, 1.5f)));
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kMoveSelected));
    CHECK(rig.obj(book).angle == doctest::Approx(0.5f));
    CHECK(rig.session.world()->body(rig.obj(book).bodies[0])->GetAngle() == doctest::Approx(0.5f));
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(0.9f, 1.5f)));
    rig.advanceIds(2);
    CHECK(rig.session.touchState().gizmoObject == book);
    // The rotation ring: a touch 0.35 m from the item, dragged a quarter turn around it.
    const Vec2 c = rig.obj(book).position;
    const float a0 = rig.obj(book).angle;
    rig.session.pointerDown(0, rig.worldToPointer(Vec2(c.x + 0.35f, c.y)));
    rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kRingRotate);
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(c.x, c.y + 0.35f)));
    ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kRotateSelected));
    CHECK(rig.obj(book).angle == doctest::Approx(a0 + 1.5707964f).epsilon(0.01));
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(c.x, c.y + 0.35f)));
    ids = rig.advanceIds(2);
    CHECK(Rig::has(ids, action::kManipulationEnded));
    CHECK(rig.session.touchState().state == touch_state::kIdle);
}

TEST_CASE("setup: the flip button mirrors a flippable item and re-creates its bodies") {
    Rig rig(shelfLevel(ItemType::Scissors));
    rig.session.takeFromToolbox(0);
    rig.advanceIds(25);
    const int scissors = rig.session.heldObject();
    REQUIRE(scissors >= 0);
    CHECK((rig.obj(scissors).flags & object_flags::kFlippable) != 0);
    // Drop it in free space, then press the flip button 0.4 m away at 45°.
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(scissors).position));
    rig.advanceIds();
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(0.8f, 1.7f)));
    rig.advanceIds();
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(0.8f, 1.7f)));
    rig.advanceIds(2);
    CHECK(rig.session.touchState().gizmoObject == scissors);
    const Vec2 c = rig.obj(scissors).position;
    const float before = rig.obj(scissors).angle;
    const Vec2 button(c.x + 0.4f * 0.70710677f, c.y + 0.4f * 0.70710677f);
    rig.session.pointerDown(0, rig.worldToPointer(button));
    std::vector<int> ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kFlipSelected));
    CHECK(rig.session.touchState().state == touch_state::kFlipping);
    rig.session.pointerUp(0, rig.worldToPointer(button));
    rig.advanceIds();
    // The flip tap's release resets the touch state at once (FUN_000cedd8 case 6), but undo / restart stay
    // disabled with the input until the animation ends.
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    const int undoBefore = rig.session.undoQueue().count;
    REQUIRE(undoBefore > 0);
    rig.session.undo();
    rig.session.restart();
    CHECK(rig.session.undoQueue().count == undoBefore);
    CHECK(rig.objectOfType(ItemType::Scissors) == scissors);
    rig.advanceIds(11);   // 0.15 s in all
    // Scissors flip by a half turn (GameItemUtils::Flip case 6), scale.x back to +1 after the animation.
    CHECK(rig.obj(scissors).angle == doctest::Approx(before + 3.1415920f));
    CHECK(rig.obj(scissors).scale.x == doctest::Approx(1.0f));
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    // The flipped item can be picked up and dragged again.
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(scissors).position));
    rig.advanceIds(14);
    REQUIRE(rig.session.heldObject() == scissors);
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(1.2f, 1.9f)));
    rig.advanceIds(3);
    CHECK(rig.obj(scissors).position.x == doctest::Approx(1.2f).epsilon(0.05));
    CHECK(rig.obj(scissors).position.y == doctest::Approx(1.9f).epsilon(0.05));
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(1.2f, 1.9f)));
    rig.advanceIds(3);
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    CHECK(rig.session.heldObject() < 0);
}

TEST_CASE("setup: flipHeld on a truck taken from the toolbox negates the scale and rebuilds the bodies") {
    Rig rig(shelfLevel(ItemType::RCTruck));
    rig.session.takeFromToolbox(0);
    rig.advanceIds(25);
    const int truck = rig.session.heldObject();
    REQUIRE(truck >= 0);
    CHECK(rig.obj(truck).bodyCount == 4);   // chassis, two wheels, the built-in controller
    CHECK(rig.session.state().itemOf(rig.obj(truck)).builtInController);
    // Drop it: the first ManipulationEnded splits the controller off into an RCController item.
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(truck).position));
    rig.advanceIds();
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(0.8f, 1.7f)));
    rig.advanceIds();
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(0.8f, 1.7f)));
    rig.advanceIds(2);
    CHECK(rig.obj(truck).bodyCount == 3);
    CHECK_FALSE(rig.session.state().itemOf(rig.obj(truck)).builtInController);
    const int controller = rig.objectOfType(ItemType::RCController);
    REQUIRE(controller >= 0);
    CHECK(rig.session.state().itemOf(rig.obj(controller)).stateWord == rig.obj(truck).handle);
    CHECK(rig.session.state().itemOf(rig.obj(truck)).stateWord == rig.obj(controller).handle);
    CHECK(rig.session.touchState().gizmoObject == truck);
    const int firstBody = rig.obj(truck).bodies[0];
    rig.session.flipHeld();
    std::vector<int> ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kFlipSelected));
    rig.advanceIds(12);
    CHECK(rig.obj(truck).scale.x == doctest::Approx(-1.0f));
    CHECK(rig.obj(truck).bodies[0] != firstBody);   // DestroyPhysics + CreatePhysics
    CHECK(rig.session.world()->body(firstBody) == nullptr);
}

TEST_CASE("setup: dragging into a shelf enters the ghost state and the drop glides back to the good pose") {
    Rig rig(shelfLevel(ItemType::Book));
    rig.session.takeFromToolbox(0);
    rig.advanceIds(25);
    const int book = rig.session.heldObject();
    REQUIRE(book >= 0);
    // A finger on the book, then a move into free space (the good state), then into the shelf.
    rig.session.pointerDown(0, rig.worldToPointer(rig.obj(book).position));
    rig.advanceIds();
    REQUIRE(rig.session.touchState().state == touch_state::kDragging);
    const Vec2 free(1.7f, 1.6f);
    rig.session.pointerMove(0, rig.worldToPointer(free));
    rig.advanceIds(2);
    CHECK_FALSE(rig.session.ghost().inGhost);
    CHECK(rig.session.ghost().hasGoodState);
    const Vec2 inShelf(1.7f, 1.0f);
    rig.session.pointerMove(0, rig.worldToPointer(inShelf));
    rig.advanceIds(2);
    REQUIRE(rig.session.ghost().inGhost);
    REQUIRE(rig.session.ghost().ghostObject >= 0);
    const PhysicsObject& ghost = rig.obj(rig.session.ghost().ghostObject);
    CHECK(ghost.isGhost());
    CHECK((rig.obj(book).state & object_state::kGhostTint) != 0);
    // The ghost copy sits between the good pose and the shelf, above it (the bisection result).
    CHECK(ghost.position.y > inShelf.y);
    CHECK(ghost.position.y <= free.y);
    CHECK(rig.session.world()->body(ghost.bodies[0])->GetType() == b2_staticBody);
    // Release: action 2 at once (the release always queues it); its handler only starts the glide (4.5
    // m/s) while in ghost. When the glide finishes, RevertGhostState + ManipulationEnded run and a no-op
    // action 0 is queued because the last processed action already was a 2 [verified].
    rig.session.pointerUp(0, rig.worldToPointer(inShelf));
    std::vector<int> ids = rig.advanceIds(1);
    CHECK(Rig::has(ids, action::kManipulationEnded));
    CHECK(rig.session.ghost().inGhost);
    CHECK(rig.session.undoQueue().count == 0);
    ids = rig.advanceIds(30);
    REQUIRE(Rig::has(ids, action::kNoOp));
    CHECK_FALSE(Rig::has(ids, action::kManipulationEnded));
    CHECK_FALSE(rig.session.ghost().inGhost);
    CHECK(rig.session.ghost().ghostObject == -1);
    CHECK(rig.objectOfType(ItemType::Book) == book);
    CHECK(rig.obj(book).position.y > inShelf.y);
    CHECK((rig.obj(book).state & object_state::kGhostTint) == 0);
    // The ghost copy is gone from the collection.
    for (const PhysicsObject& o : rig.session.state().objects) CHECK_FALSE(o.isGhost());
    CHECK(rig.session.undoQueue().count == 1);
}

TEST_CASE("setup: a toolbox item dropped straight into the shelf with no good state returns (action 9)") {
    Rig rig(shelfLevel(ItemType::Book));
    // The strip's slot centre, then a drag straight into the shelf without ever being collision-free.
    const Toolbox& tb = rig.session.toolbox();
    const Vec2 c = tb.getCenterForSlot(0);
    const Vec2 slot = rig.yDown(Vec2(tb.x + c.x, tb.y + c.y));
    rig.session.pointerDown(0, slot);
    rig.advanceIds();
    rig.session.pointerMove(0, Vec2(slot.x, slot.y - 4.0f));
    rig.advanceIds();
    rig.session.pointerMove(0, Vec2(slot.x + 2.0f, slot.y - 40.0f));
    std::vector<int> ids = rig.advanceIds();
    REQUIRE(Rig::has(ids, action::kNewFromToolbox));
    const int book = rig.session.heldObject();
    REQUIRE(book >= 0);
    // Teleport the finger into the shelf: every frame from now on collides.
    rig.session.pointerMove(0, rig.worldToPointer(Vec2(1.7f, 1.0f)));
    rig.advanceIds(2);
    CHECK(rig.session.ghost().inGhost);
    CHECK(rig.session.ghost().ghostObject == -1);
    rig.session.pointerUp(0, rig.worldToPointer(Vec2(1.7f, 1.0f)));
    ids = rig.advanceIds(40);
    CHECK(Rig::has(ids, action::kReturnToToolbox));
    CHECK(Rig::has(ids, action::kRemovalFinished));
    CHECK(rig.objectOfType(ItemType::Book) == -1);
    CHECK(rig.session.toolbox().getItemCount() == 1);
}

// docs/04 §7: an aligned snap (pipe ends) places the object by its point's *local* position rotated to
// the new angle. Rotating the world-space offset instead only works at angle 0 — a Pipe90 at 45° then
// landed ~0.14 m off its neighbour's mouth (visible as a gap or an overlap in level 2-2).
TEST_CASE("setup: pipe ends snap end-to-end at every angle of the fixed neighbour") {
    struct Case {
        ItemType type;
        float fixedAngle;
        float misalign;   // the dragged pipe's angular error, radians
        bool expectSnap;
    };
    const Case cases[] = {
        {ItemType::Pipe90, 0.0f, 0.17f, true},
        {ItemType::Pipe90, kGamePi * 0.25f, 0.17f, true},
        {ItemType::Pipe90, kGamePi * 0.5f, -0.17f, true},
        {ItemType::Pipe90, -kGamePi * 0.25f, 0.0f, true},
        {ItemType::Pipe90, kGamePi * 0.25f, 0.8f, false},   // beyond the 45° alignment window
        {ItemType::Pipe, 0.0f, 0.17f, true},
        {ItemType::Pipe, kGamePi * 0.25f, -0.17f, true},
    };
    for (const Case& c : cases) {
        CAPTURE(static_cast<int>(c.type));
        CAPTURE(c.fixedAngle);
        CAPTURE(c.misalign);
        // The dragged pipe's mouth 0 faces the fixed pipe's mouth 1 (dir left) when its angle is
        // fixedAngle − π/2 for the elbow (mouth 0 dir up) and fixedAngle for the straight pipe (dir right).
        const float facing = c.type == ItemType::Pipe90 ? kGamePi * 0.5f : 0.0f;
        const float dragAngle = c.fixedAngle - facing + c.misalign;
        Level level;
        LevelItem fixedPipe = levelItem(c.type, 1, Vec2(1.7f, 1.0f));
        fixedPipe.angle = c.fixedAngle;
        LevelItem dragged = levelItem(c.type, 2, Vec2(0.6f, 1.0f), false);
        dragged.angle = dragAngle;
        level.items = {levelItem(ItemType::WorldBound, 0, Vec2(0.0f, 0.0f), false), fixedPipe, dragged};
        Rig rig(level);
        // Every level item is fixed in the campaign; tell the two pipes apart by position (the drag
        // itself is a direct CalculateSnap call, which does not care).
        int fixedIdx = -1;
        int dragIdx = -1;
        for (const PhysicsObject& o : rig.session.state().objects) {
            if (o.type != c.type) continue;
            (o.position.x > 1.0f ? fixedIdx : dragIdx) = o.index;
        }
        REQUIRE(fixedIdx >= 0);
        REQUIRE(dragIdx >= 0);
        const PhysicsWorld& world = *rig.session.world();
        const PhysicsObject& fixedObj = rig.obj(fixedIdx);
        const PhysicsObject& dragObj = rig.obj(dragIdx);
        REQUIRE(fixedObj.angle == doctest::Approx(c.fixedAngle));
        REQUIRE(dragObj.angle == doctest::Approx(dragAngle));
        // Aim mouth 0 at 0.04 m from the fixed pipe's mouth 1.
        const Vec2 mouth = attachmentPosWS(fixedObj, world, 1);
        const Vec2 local0 = rotate(dragAngle, dragObj.attachments[0].point.pos);
        const Vec2 want(mouth.x + 0.03f - local0.x, mouth.y - 0.025f - local0.y);
        const SnapResult r = calculateSnap(rig.session.state(), world, dragObj, want, want, dragObj.halfSize * 1.1f);
        CHECK(r.found == c.expectSnap);
        if (!c.expectSnap) continue;
        CHECK(r.point == 0);
        CHECK(r.otherObject == fixedIdx);
        CHECK(r.otherPoint == 1);
        CHECK(r.angle == doctest::Approx(-c.misalign).epsilon(1e-3));
        // Applied, the two mouths coincide and face each other.
        const float newAngle = dragAngle + r.angle;
        const Vec2 p0 = rotate(newAngle, dragObj.attachments[0].point.pos);
        CHECK(r.position.x + p0.x == doctest::Approx(mouth.x).epsilon(1e-4));
        CHECK(r.position.y + p0.y == doctest::Approx(mouth.y).epsilon(1e-4));
        const Vec2 d0 = rotate(newAngle, dragObj.attachments[0].point.dir);
        const Vec2 d1 = rotate(c.fixedAngle, fixedObj.attachments[1].point.dir);
        CHECK(d0.x * d1.x + d0.y * d1.y == doctest::Approx(-1.0f).epsilon(1e-4));
    }
}
