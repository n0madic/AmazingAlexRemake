// The sandbox editor's core (docs/05 §1): the editor toolbox (ToolboxUtils::SetFull), the empty level of
// CreateNewSandbox, the 1 ↔ 5 toolbox step (UpdateSandboxToolboxLayout), the 1 ↔ 4 test play with its
// completion, the background change and the `tested` flag.
#include "setup_rig.h"

#include <doctest.h>

#include <stdexcept>

using namespace setup_test;

namespace {

Level starsLevel() {
    // A tennis ball above three stars: the falling ball collects them all (the test-play "solved" rule).
    Level level;
    level.items = {levelItem(ItemType::WorldBound, 0, Vec2(0.0f, 0.0f), false),
                   levelItem(ItemType::TennisBall, 1, Vec2(1.7f, 2.0f), false),
                   levelItem(ItemType::GoalStar, 2, Vec2(1.7f, 1.5f), false),
                   levelItem(ItemType::GoalStar, 3, Vec2(1.7f, 1.1f), false),
                   levelItem(ItemType::GoalStar, 4, Vec2(1.7f, 0.7f), false)};
    return level;
}

int slotOf(const Toolbox& tb, ItemType type) { return tb.getSlotIndexForType(type); }

bool allFixed(const WorldState& st, bool fixed) {
    for (const PhysicsObject& o : st.objects) {
        if (!o.valid() || o.type == ItemType::WorldBound || o.type == ItemType::SelectionArea) continue;
        if (o.isFixed() != fixed) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("sandbox: the editor toolbox lists the unlocked types in SetFull's order, minus what the level holds") {
    TemplateTable templates = plainTemplates();
    Session session{templates};
    session.setViewport(ScreenLayout::compute(1024, 768));
    // Everything unlocked, an empty level: the star slot (3) first, then the 34 placeable types at 0x20.
    session.load(Level{}, GameMode::Sandbox);
    CHECK(session.editorToolboxActive());
    const Toolbox& tb = session.toolbox();
    REQUIRE(tb.slotCount == 35);
    CHECK(tb.slots[0].type == ItemType::GoalStar);
    CHECK(tb.slots[0].amount == 3);
    CHECK(tb.slots[1].type == ItemType::Shelf);
    CHECK(tb.slots[1].amount == 0x20);
    CHECK(tb.slots[2].type == ItemType::TennisBall);
    CHECK(tb.slots[3].type == ItemType::SoccerBall);
    CHECK(tb.slots[4].type == ItemType::Book);
    CHECK(tb.slots[34].type == ItemType::ZipLine);
    CHECK(slotOf(tb, ItemType::FishBowl) == -1);      // never in the editor (not in SetFull's list)
    CHECK(slotOf(tb, ItemType::Pulley) == -1);
    CHECK(slotOf(tb, ItemType::RCController) == -1);   // the controller comes with the truck
    CHECK(slotOf(tb, ItemType::TrapdoorLever) == -1);
    CHECK(slotOf(tb, ItemType::Billboard) == -1);
    // The empty level: CreateNewSandbox = the world bound (+ the selection area), nothing fixed, no goal.
    CHECK(session.state().objects.size() == 2);
    CHECK(session.state().objects[0].type == ItemType::WorldBound);
    CHECK(session.level().goal.type == 0);
    CHECK(session.level().backgroundIndex == 0);

    // Only the Classroom set unlocked, a level with a shelf and three stars: no star slot, the shelf at 0x1f.
    UnlockedItems few{};
    few[static_cast<std::size_t>(ItemType::Shelf)] = true;
    few[static_cast<std::size_t>(ItemType::Book)] = true;
    few[static_cast<std::size_t>(ItemType::Rope)] = true;
    session.setUnlockedItems(few);
    Level level = starsLevel();
    level.items.push_back(levelItem(ItemType::Shelf, 5, Vec2(0.8f, 0.5f), false));
    session.load(level, GameMode::Sandbox);
    const Toolbox& t2 = session.toolbox();
    REQUIRE(t2.slotCount == 3);
    CHECK(t2.slots[0].type == ItemType::Shelf);
    CHECK(t2.slots[0].amount == 0x1f);
    CHECK(t2.slots[1].type == ItemType::Book);
    CHECK(t2.slots[2].type == ItemType::Rope);
    CHECK(slotOf(t2, ItemType::GoalStar) == -1);
    // The level's own strip (the file's toolbox list) stays empty behind the editor strip.
    CHECK(session.sandboxLevel().toolbox.empty());
    // The editor never fixes the level's items (SandboxView::Show un-fixes them anyway).
    CHECK(allFixed(session.state(), false));
}

TEST_CASE("sandbox: the toolbox step moves items into the strip and back, and 5 → 1 restores everything") {
    Rig rig(Level{});
    rig.session.setMode(GameMode::Sandbox);   // load() set it; the transitions start from here
    rig.session.load(Level{}, GameMode::Sandbox);
    for (int i = 0; i < 90; ++i) rig.session.advance(kDt);
    rig.session.drainActions();
    // Two shelves and a book placed from the editor strip.
    const int shelfSlot = slotOf(rig.session.toolbox(), ItemType::Shelf);
    const int bookSlot = slotOf(rig.session.toolbox(), ItemType::Book);
    REQUIRE(shelfSlot >= 0);
    REQUIRE(bookSlot >= 0);
    REQUIRE(rig.takeAndHold(shelfSlot) >= 0);
    rig.dropAt(Vec2(0.8f, 1.2f));
    REQUIRE(rig.takeAndHold(shelfSlot) >= 0);
    rig.dropAt(Vec2(2.4f, 1.2f));
    REQUIRE(rig.takeAndHold(bookSlot) >= 0);
    rig.dropAt(Vec2(1.6f, 1.6f));
    rig.advanceIds(30);
    REQUIRE(rig.countOfType(ItemType::Shelf) == 2);
    REQUIRE(rig.countOfType(ItemType::Book) == 1);
    CHECK(rig.session.toolbox().slots[static_cast<std::size_t>(shelfSlot)].amount == 0x1e);
    // Edits in the editor push no undo snapshots (doFrame skips modes 1 / 5) and clear `tested`.
    CHECK(rig.session.undoQueue().count == 0);
    CHECK_FALSE(rig.session.tested());
    const Vec2 bookAt = rig.obj(rig.objectOfType(ItemType::Book)).position;

    // 1 → 5: the level strip is the active one and empty; the stars (none here) fixed; the handles noted.
    rig.session.setMode(GameMode::SandboxToolbox);
    CHECK_FALSE(rig.session.editorToolboxActive());
    CHECK(rig.session.toolbox().slotCount == 0);
    CHECK(rig.session.undoQueue().count == 0);
    CHECK(rig.session.removedHandles().empty());
    rig.advanceIds(30);   // the empty strip retracts to its button

    // The book dragged onto the strip: action 9 → removal → the layout loses it, the strip gains a Book.
    const int book = rig.objectOfType(ItemType::Book);
    REQUIRE(book >= 0);
    rig.session.pointerDown(0, rig.worldToPointer(bookAt));
    rig.advanceIds(2);
    const Toolbox& strip = rig.session.toolbox();
    const Vec2 dropPx = rig.yDown(Vec2(strip.x - 60.0f, strip.y));
    rig.session.pointerMove(0, rig.yDown(Vec2(bookAt.x * 300.0f, 300.0f)));
    rig.advanceIds(2);
    rig.session.pointerMove(0, dropPx);
    rig.advanceIds(2);
    rig.session.pointerUp(0, dropPx);
    std::vector<int> ids = rig.advanceIds(40);
    REQUIRE(Rig::has(ids, action::kReturnToToolbox));
    REQUIRE(Rig::has(ids, action::kRemovalFinished));
    CHECK(rig.countOfType(ItemType::Book) == 0);
    CHECK(rig.countOfType(ItemType::Shelf) == 2);
    REQUIRE(rig.session.removedHandles().size() == 1);
    CHECK(Handle::typeOf(rig.session.removedHandles()[0]) == ItemType::Book);
    REQUIRE(rig.session.toolbox().slotCount == 1);
    CHECK(rig.session.toolbox().slots[0].type == ItemType::Book);
    CHECK(rig.session.toolbox().slots[0].amount == 1);
    CHECK(rig.session.touchState().state == touch_state::kIdle);

    // Taken out of the strip again, the book reappears where it was — not under the finger.
    rig.advanceIds(30);
    rig.session.takeFromToolbox(0);
    ids = rig.advanceIds(2);
    REQUIRE(Rig::has(ids, action::kNewFromToolbox));
    const int back = rig.objectOfType(ItemType::Book);
    REQUIRE(back >= 0);
    CHECK(rig.obj(back).position.x == doctest::Approx(bookAt.x));
    CHECK(rig.obj(back).position.y == doctest::Approx(bookAt.y));
    CHECK(rig.session.removedHandles().empty());
    CHECK(rig.session.toolbox().slotCount == 0);
    rig.advanceIds(30);

    // Into the strip once more, then 5 → 1. The original's 5 → x branch undoes while isActionEnabled(0)
    // holds — but nothing in the toolbox step pushes undo snapshots (doFrame skips modes 1 / 5), so the
    // step's only snapshot is its base and the loop never runs: the book stays in the level's strip and
    // the layout keeps the two shelves. (SandboxView's back button reloads the file instead; play is
    // disabled in mode 5, so this transition is only ever reached that way.) [verified]
    rig.session.pointerDown(0, rig.worldToPointer(bookAt));
    rig.advanceIds(2);
    rig.session.pointerMove(0, rig.yDown(Vec2(bookAt.x * 300.0f, 300.0f)));
    rig.advanceIds(2);
    rig.session.pointerMove(0, dropPx);
    rig.advanceIds(2);
    rig.session.pointerUp(0, dropPx);
    ids = rig.advanceIds(40);
    REQUIRE(Rig::has(ids, action::kRemovalFinished));
    CHECK(rig.countOfType(ItemType::Book) == 0);
    rig.session.setMode(GameMode::Sandbox);
    CHECK(rig.session.editorToolboxActive());
    CHECK(rig.countOfType(ItemType::Book) == 0);
    CHECK(rig.countOfType(ItemType::Shelf) == 2);
    CHECK(rig.session.undoQueue().count == -1);   // UndoQueueUtils::Reset
    CHECK(allFixed(rig.session.state(), false));
    CHECK(rig.session.toolbox().slots[static_cast<std::size_t>(shelfSlot)].amount == 0x1e);
    // The level strip (the file's toolbox list) lists what was moved into it.
    const Level saved = rig.session.sandboxLevel();
    REQUIRE(saved.toolbox.size() == 1);
    CHECK(saved.toolbox[0].type == ItemType::Book);
    CHECK(saved.toolbox[0].amount == 1);
}

TEST_CASE("sandbox: the toolbox step keeps its stars fixed across a strip move (a remake deviation)") {
    // The original takes the ready layout before MarkAllStarsFixed and never re-fixes after the rebuild of
    // UpdateSandboxToolboxLayout, so from the first strip move on its stars are movable in the step (and a
    // star dragged onto the strip sets its removing flag before action 9 refuses it); the port re-fixes the
    // stars after every rebuild and refuses the star before touching the flag (docs/10 §11 item 14 (o)).
    Rig rig(Level{});
    rig.session.setMode(GameMode::Sandbox);
    rig.session.load(Level{}, GameMode::Sandbox);
    for (int i = 0; i < 90; ++i) rig.session.advance(kDt);
    rig.session.drainActions();
    const int starSlot = slotOf(rig.session.toolbox(), ItemType::GoalStar);
    const int bookSlot = slotOf(rig.session.toolbox(), ItemType::Book);
    REQUIRE(starSlot >= 0);
    REQUIRE(bookSlot >= 0);
    REQUIRE(rig.takeAndHold(starSlot) >= 0);
    rig.dropAt(Vec2(0.8f, 1.6f));
    REQUIRE(rig.takeAndHold(bookSlot) >= 0);
    rig.dropAt(Vec2(2.4f, 1.6f));
    rig.advanceIds(30);
    REQUIRE(rig.countOfType(ItemType::GoalStar) == 1);
    REQUIRE(rig.countOfType(ItemType::Book) == 1);
    const Vec2 starAt = rig.obj(rig.objectOfType(ItemType::GoalStar)).position;
    const Vec2 bookAt = rig.obj(rig.objectOfType(ItemType::Book)).position;

    rig.session.setMode(GameMode::SandboxToolbox);
    rig.advanceIds(30);
    CHECK(rig.obj(rig.objectOfType(ItemType::GoalStar)).isFixed());
    CHECK_FALSE(rig.obj(rig.objectOfType(ItemType::Book)).isFixed());

    // The book into the strip: the world is rebuilt from the ready layout — the star stays fixed.
    const Toolbox& strip = rig.session.toolbox();
    const Vec2 dropPx = rig.yDown(Vec2(strip.x - 60.0f, strip.y));
    rig.session.pointerDown(0, rig.worldToPointer(bookAt));
    rig.advanceIds(2);
    rig.session.pointerMove(0, rig.yDown(Vec2(bookAt.x * 300.0f, 300.0f)));
    rig.advanceIds(2);
    rig.session.pointerMove(0, dropPx);
    rig.advanceIds(2);
    rig.session.pointerUp(0, dropPx);
    std::vector<int> ids = rig.advanceIds(40);
    REQUIRE(Rig::has(ids, action::kRemovalFinished));
    REQUIRE(rig.countOfType(ItemType::Book) == 0);
    REQUIRE(rig.countOfType(ItemType::GoalStar) == 1);
    CHECK(rig.obj(rig.objectOfType(ItemType::GoalStar)).isFixed());

    // The star cannot be dragged at all: the touch buzzes, no action 9, nothing pending afterwards.
    rig.session.pointerDown(0, rig.worldToPointer(starAt));
    rig.advanceIds(2);
    rig.session.pointerMove(0, rig.yDown(Vec2(starAt.x * 300.0f, 300.0f)));
    rig.advanceIds(2);
    rig.session.pointerMove(0, dropPx);
    rig.advanceIds(2);
    rig.session.pointerUp(0, dropPx);
    ids = rig.advanceIds(40);
    CHECK_FALSE(Rig::has(ids, action::kReturnToToolbox));
    CHECK(rig.countOfType(ItemType::GoalStar) == 1);
    CHECK(rig.session.toolbox().slotCount == 1);
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    CHECK_FALSE(rig.session.isManipulationActive());
    const Vec2 still = rig.obj(rig.objectOfType(ItemType::GoalStar)).position;
    CHECK(still.x == doctest::Approx(starAt.x));
    CHECK(still.y == doctest::Approx(starAt.y));
    // The strip still works: the book comes back out.
    rig.session.takeFromToolbox(0);
    ids = rig.advanceIds(2);
    CHECK(Rig::has(ids, action::kNewFromToolbox));
    CHECK(rig.countOfType(ItemType::Book) == 1);
}

TEST_CASE("sandbox: restartLevel in the editor empties the level (the world bound, a fresh strip, the default header)") {
    // GameScreenController::restartLevel's mode 1 / 5 branch [verified: 0xb9884..0xb9a1c]; no editor
    // button reaches it in the original either.
    Rig rig(Level{});
    rig.session.setMode(GameMode::Sandbox);
    Level data;
    data.title = "custom";
    data.authorName = "me";
    data.backgroundIndex = kTreehouseBackground;
    rig.session.load(data, GameMode::Sandbox);
    for (int i = 0; i < 90; ++i) rig.session.advance(kDt);
    rig.session.drainActions();
    const int shelfSlot = slotOf(rig.session.toolbox(), ItemType::Shelf);
    REQUIRE(shelfSlot >= 0);
    REQUIRE(rig.takeAndHold(shelfSlot) >= 0);
    rig.dropAt(Vec2(0.8f, 1.2f));
    rig.advanceIds(30);
    REQUIRE(rig.countOfType(ItemType::Shelf) == 1);
    REQUIRE(rig.session.state().objects[0].bodyCount == 5);   // the Treehouse floor pieces
    const int shelfAmount = rig.session.toolbox().slots[static_cast<std::size_t>(shelfSlot)].amount;

    rig.session.restart();
    REQUIRE(rig.session.state().objects.size() == 1);
    CHECK(rig.session.state().objects[0].type == ItemType::WorldBound);
    CHECK(rig.session.state().objects[0].bodyCount == 4);   // state 0: the plain floor
    CHECK(rig.session.level().title == kDefaultSandboxTitleId);
    CHECK(rig.session.level().authorName.empty());
    CHECK(rig.session.level().backgroundIndex == 0);
    CHECK_FALSE(rig.session.tested());
    CHECK(rig.session.controllerState() == 2);
    CHECK(rig.session.gameMode() == GameMode::Sandbox);
    // The level's strip is a fresh Toolbox; the editor strip stays active with its amounts as they were.
    CHECK(rig.session.sandboxLevel().toolbox.empty());
    CHECK(rig.session.editorToolboxActive());
    CHECK(rig.session.toolbox().slots[static_cast<std::size_t>(shelfSlot)].amount == shelfAmount);
    // Editing goes on: a shelf placed again.
    for (int i = 0; i < 30; ++i) rig.session.advance(kDt);
    rig.session.drainActions();
    REQUIRE(rig.takeAndHold(slotOf(rig.session.toolbox(), ItemType::Shelf)) >= 0);
    rig.dropAt(Vec2(1.6f, 1.2f));
    rig.advanceIds(30);
    CHECK(rig.countOfType(ItemType::Shelf) == 1);
}

TEST_CASE("sandbox: test play fixes the layout, its completion marks the level tested and returns to the editor") {
    Rig rig(Level{});
    rig.session.load(starsLevel(), GameMode::Sandbox);
    for (int i = 0; i < 90; ++i) rig.session.advance(kDt);
    rig.session.drainActions();
    CHECK_FALSE(rig.session.tested());
    CHECK(allFixed(rig.session.state(), false));
    // 1 → 4: everything fixed for the run; the editor strip stays the active one.
    rig.session.setMode(GameMode::TestPlay);
    CHECK(allFixed(rig.session.state(), true));
    CHECK(rig.session.editorToolboxActive());
    rig.session.play();
    CHECK(rig.session.controllerState() == 4);
    int completedFrame = -1;
    bool sawGoal = false;
    for (int frame = 0; frame < 400 && completedFrame < 0; ++frame) {
        rig.session.advance(kDt);
        for (const SessionEvent& e : rig.session.drainEvents()) {
            if (e.kind == SessionEvent::Kind::GoalComplete) sawGoal = true;
            if (e.kind == SessionEvent::Kind::LevelCompleted) completedFrame = frame;   // never in test play
        }
        if (rig.session.controllerState() == 2 && sawGoal) completedFrame = frame;
    }
    CHECK(sawGoal);
    CHECK(completedFrame > 0);
    // setCompletedState's test-play branch: mode 1, state 2, the pre-run layout, tested, nothing fixed.
    CHECK(rig.session.gameMode() == GameMode::Sandbox);
    CHECK(rig.session.controllerState() == 2);
    CHECK(rig.session.tested());
    CHECK(allFixed(rig.session.state(), false));
    CHECK(rig.countOfType(ItemType::GoalStar) == 3);
    CHECK(rig.session.sandboxLevel().tested);
    // A stopped run without completion: 4 → 1 by the stop touch, the layout back, still untested.
    rig.session.setMode(GameMode::TestPlay);
    rig.session.play();
    rig.advanceIds(10);
    rig.session.pointerDown(0, Vec2(300.0f, 300.0f));
    rig.session.pointerUp(0, Vec2(300.0f, 300.0f));
    rig.advanceIds(5);
    CHECK(rig.session.controllerState() == 2);
    CHECK(rig.session.gameMode() == GameMode::Sandbox);
    CHECK(rig.countOfType(ItemType::TennisBall) == 1);
    // An edit after a successful test clears the flag again; the background button too.
    CHECK(rig.session.tested());
    rig.session.setBackground(2);
    CHECK_FALSE(rig.session.tested());
    CHECK(rig.session.level().backgroundIndex == 2);
    RenderState rs = rig.session.renderState();
    CHECK(rs.backgroundIndex == 2);
    CHECK(rs.previousBackground == 0);
    CHECK(rs.backgroundSlide == doctest::Approx(3.41f));
    rig.advanceIds(20);
    rs = rig.session.renderState();
    CHECK(rs.backgroundSlide < 3.41f);
    CHECK(rs.backgroundSlide > 0.0f);
    rig.advanceIds(20);
    rs = rig.session.renderState();
    CHECK(rs.backgroundSlide == 0.0f);
    CHECK(rs.previousBackground == -1);

    // The world bound follows the background at once (a remake deviation, docs/10 §11 item 14 (k)): the
    // Treehouse variant has two floor pieces + ceiling + two walls, every other background one floor.
    auto bound = [&]() -> const PhysicsObject& {
        for (const PhysicsObject& o : rig.session.state().objects) {
            if (o.type == ItemType::WorldBound) return o;
        }
        throw std::logic_error("no world bound");
    };
    CHECK(bound().bodyCount == 4);
    CHECK(rig.session.state().itemOf(bound()).stateWord == 0);
    rig.session.setBackground(kTreehouseBackground);
    CHECK(bound().bodyCount == 5);
    CHECK(rig.session.state().itemOf(bound()).stateWord == 1);
    rig.session.setBackground(0);
    CHECK(bound().bodyCount == 4);
    CHECK(rig.session.state().itemOf(bound()).stateWord == 0);
}

TEST_CASE("sandbox: a rejected layout leaves the loaded level intact (load commits on success only)") {
    Rig rig;
    const std::size_t objects = rig.session.state().objects.size();
    // Two items under the same handle slot: HandleManager rejects the duplicate.
    Level duplicate = classroomLike();
    duplicate.items.push_back(levelItem(ItemType::Shelf, 1, Vec2(2.5f, 0.6f)));
    CHECK_THROWS_AS(rig.session.load(duplicate), std::exception);
    // An attached record pointing outside the layout is rejected too.
    Level dangling = hookLevel();   // the hook carries attachment points; a shelf copies no records
    dangling.items[1].attachments[0].state = attachment_state::kAttached;
    dangling.items[1].attachments[0].objectIndex = 57;
    dangling.items[1].attachments[0].index = 0;
    CHECK_THROWS_AS(rig.session.load(dangling), std::invalid_argument);
    // The previous level, its world included, still advances.
    CHECK(rig.session.state().objects.size() == objects);
    rig.advanceIds(5);
    CHECK(rig.session.renderState().items.size() == objects);
}
