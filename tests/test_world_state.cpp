// WorldState value semantics, the handle table and the level → state application.
#include "aa/sim/items/items.h"
#include "aa/sim/session.h"
#include "aa/sim/world_state.h"

#include <doctest.h>

using namespace aa::sim;

namespace {
TemplateTable plainTemplates() {
    FrameTable frames;
    frames.frames.resize(kTemplateFrameCount);
    for (Frame& f : frames.frames) {
        f.x1 = 100.0f;
        f.y1 = 100.0f;
    }
    return initTemplates(frames);
}
}  // namespace

TEST_CASE("handles: bit layout of st::Handle") {
    // 1543524377 is a GoalStar handle of 00_Classroom/Playtime (docs/02 §3.1).
    CHECK(Handle::typeOf(1543524377) == ItemType::GoalStar);
    CHECK(Handle::typeOf(-1677709310) == ItemType::Helicopter);   // types >= 32 give negative handles
    CHECK(Handle::slotOf(1543524377) == (1543524377 & 0xFFF));
    const int h = Handle::make(ItemType::ZipLine, 5, 7);
    CHECK(Handle::typeOf(h) == ItemType::ZipLine);
    CHECK(Handle::generationOf(h) == 5);
    CHECK(Handle::slotOf(h) == 7);
    CHECK(h < 0);
}

TEST_CASE("handle manager: lookup, generations, removal") {
    HandleManager hm;
    hm.addWithHandle(1543524377, 3);
    CHECK(hm.lookup(1543524377) == 3);
    CHECK(hm.lookup(Handle::make(ItemType::GoalStar, 0, Handle::slotOf(1543524377))) == -1);   // stale generation
    const int fresh = hm.add(ItemType::Shelf, 4);
    CHECK(Handle::typeOf(fresh) == ItemType::Shelf);
    CHECK(hm.lookup(fresh) == 4);
    hm.remove(fresh);
    CHECK(hm.lookup(fresh) == -1);
    CHECK_THROWS(hm.addWithHandle(1543524377, 9));   // slot already live
    // a stale generation must not evict the slot's current occupant
    hm.remove(Handle::make(ItemType::GoalStar, 0, Handle::slotOf(1543524377)));
    CHECK(hm.lookup(1543524377) == 3);
    CHECK(hm.freeSlot() == Handle::slotOf(fresh));
}

TEST_CASE("world state: a rejected handle leaves no orphan item or object") {
    const TemplateTable templates = plainTemplates();
    WorldState s;
    s.addItemWithHandle(templates, 1543524377, Vec2(0.0f, 0.0f), 0.0f);
    CHECK_THROWS(s.addItemWithHandle(templates, 1543524377, Vec2(1.0f, 1.0f), 0.0f));
    CHECK(s.items.size() == 1);
    CHECK(s.objects.size() == 1);
}

TEST_CASE("world state: copies are independent and keep handle lookups") {
    const TemplateTable templates = plainTemplates();
    WorldState a;
    const int i = a.addItemWithHandle(templates, 1543524377, Vec2(1.0f, 2.0f), 0.5f);
    REQUIRE(i == 0);
    CHECK(a.objects[0].type == ItemType::GoalStar);
    CHECK(a.objects[0].position.x == 1.0f);
    CHECK(a.items[0].objectIndex == 0);
    WorldState b = a;                      // the original snapshots whole states by memcpy (docs/10 §5.1)
    b.objects[0].position.x = 9.0f;
    b.items[0].stateWord = 3;
    CHECK(a.objects[0].position.x == 1.0f);
    CHECK(a.items[0].stateWord == 0);
    CHECK(b.findItem(1543524377) == &b.items[0]);
    CHECK(a.findItem(1543524377) == &a.items[0]);
    CHECK(a.findItem(12345) == nullptr);
}

TEST_CASE("apply layout: flags, flip scale and per-type state") {
    const TemplateTable templates = plainTemplates();
    Level level;
    level.backgroundIndex = kTreehouseBackground;
    LevelItem bound;
    bound.type = ItemType::WorldBound;
    bound.handle = Handle::make(ItemType::WorldBound, 1, 0);
    LevelItem book;
    book.type = ItemType::Book;
    book.handle = Handle::make(ItemType::Book, 1, 1);
    book.flags = level_flags::kFixed | level_flags::kFlipped;
    book.itemData = 2;
    book.center = Vec2(1.5f, 0.7f);
    level.items = {bound, book};
    WorldState state;
    applyLayout(level, templates, state);
    REQUIRE(state.objects.size() == 2);
    // The item collection is sorted by type (GameItemCollectionUtils::Insert): the book (15) precedes the
    // world bound (31); the objects keep the layout order.
    CHECK(state.items[0].type == ItemType::Book);
    CHECK(state.findItem(bound.handle)->stateWord == 1);        // Treehouse floor variant
    CHECK(state.findItem(book.handle)->stateWord == 2);         // book colour
    CHECK(state.objects[1].isFixed());
    CHECK(state.objects[1].flipSign() == -1.0f);
    CHECK(state.objects[1].position.x == 1.5f);

    // The set-up world for the two objects: 4 + 1 bodies, none for an unimplemented type.
    PhysicsWorld world;
    createWorldPhysics(state, world, PhysicsMode::SetUp);
    CHECK(world.bodyCount() == 5 + 1);   // Treehouse floor is two pieces
    CHECK(state.objects[0].bodyCount == 5);
    CHECK(state.objects[1].bodyCount == 1);
    CHECK(state.objects[1].bodies[0] == 5);

    // Every type has a port now; the guard stays for the invalid value.
    for (int t = kFirstItemType; t <= kLastItemType; ++t) CHECK(isItemImplemented(static_cast<ItemType>(t)));
    CHECK_FALSE(isItemImplemented(ItemType::None));
}

TEST_CASE("apply layout: a level without a world bound gets one under an unused slot") {
    const TemplateTable templates = plainTemplates();
    Level level;
    level.backgroundIndex = kTreehouseBackground;
    LevelItem ball;
    ball.type = ItemType::TennisBall;
    ball.handle = Handle::make(ItemType::TennisBall, 1, 0);   // occupies slot 0, the naive choice
    level.items.push_back(ball);
    WorldState state;
    applyLayout(level, templates, state);
    REQUIRE(state.objects.size() == 2);
    CHECK(state.objects[0].type == ItemType::WorldBound);
    CHECK(state.itemOf(state.objects[0]).stateWord == 1);
    CHECK(Handle::slotOf(state.objects[0].handle) == 1);
    CHECK(state.findItem(ball.handle) == &state.items[0]);      // type 2 sorts before type 31
}

TEST_CASE("apply layout: an attached record must name an existing attachment point") {
    const TemplateTable templates = plainTemplates();
    Level level;
    LevelItem bound;
    bound.type = ItemType::WorldBound;
    bound.handle = Handle::make(ItemType::WorldBound, 1, 0);
    LevelItem balloon;   // one hangable point
    balloon.type = ItemType::Balloon;
    balloon.handle = Handle::make(ItemType::Balloon, 1, 1);
    balloon.attachments[0].state = attachment_state::kAttached;
    balloon.attachments[0].objectIndex = 7;   // no such item in the layout
    balloon.attachments[0].index = 0;
    level.items = {bound, balloon};
    WorldState state;
    CHECK_THROWS_AS(applyLayout(level, templates, state), std::invalid_argument);

    level.items[1].attachments[0].objectIndex = 0;   // the world bound has no attachment points
    state = WorldState{};
    CHECK_THROWS_AS(applyLayout(level, templates, state), std::invalid_argument);
}

TEST_CASE("session: load builds the set-up world with the SelectionArea helper after the level bodies") {
    const TemplateTable templates = plainTemplates();
    Level level;
    LevelItem bound;
    bound.type = ItemType::WorldBound;
    bound.handle = Handle::make(ItemType::WorldBound, 1, 0);
    level.items = {bound};
    Session session(templates);
    session.load(level);
    CHECK(session.physicsMode() == PhysicsMode::SetUp);
    // 4 world-bound bodies + the SelectionArea's return-area box (prepareForNewLevel adds it last).
    CHECK(session.world()->bodyCount() == 5);
    REQUIRE(session.state().objects.size() == 2);
    CHECK(session.state().objects[1].type == ItemType::SelectionArea);
    CHECK(session.state().objects[0].isFixed());   // MarkAllObjectsFixed in the campaign
    // The undo base snapshot exists (count 0: nothing to undo yet).
    CHECK_FALSE(session.canUndo());
    CHECK(session.undoQueue().count == 0);
    // Play / stop rebuild the world through the layout snapshot; the SelectionArea has no simulation body.
    session.play();
    CHECK(session.physicsMode() == PhysicsMode::Simulation);
    CHECK(session.world()->bodyCount() == 4);
    session.stop();
    CHECK(session.physicsMode() == PhysicsMode::SetUp);
    CHECK(session.world()->bodyCount() == 5);
}
