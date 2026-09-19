// G5 rule tests of the simulation (docs/10 §8): the substep loop, the handle table, the contact listener
// on synthetic scenarios, the goal types, stars and the completion sequence, the simulation touch, the
// no-motion auto-stop, removal / lerp alignment and the play-time set-up of the item state. The bit-exact
// behaviour is gated by G4 / G6 (tools/sim_run_conformance.py); these check the rules on levels built
// without assets.
#include "aa/sim/float_bits.h"
#include "aa/sim/goals.h"
#include "aa/sim/items/items.h"
#include "aa/sim/session.h"
#include "aa/sim/simulation.h"
#include "aa/data/frame_table_loader.h"
#include "setup_rig.h"
#include "test_support.h"

#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>

using namespace aa::sim;
using setup_test::kDt;
using setup_test::levelItem;
using setup_test::Rig;

namespace {

int countEvents(const std::vector<SessionEvent>& events, SessionEvent::Kind kind) {
    int n = 0;
    for (const SessionEvent& e : events) n += e.kind == kind ? 1 : 0;
    return n;
}

// Plays `frames` frames of 1/60 s, collecting the events.
std::vector<SessionEvent> playFrames(Session& s, int frames) {
    std::vector<SessionEvent> out;
    for (int i = 0; i < frames; ++i) {
        s.advance(kDt);
        s.drainActions();
        for (const SessionEvent& e : s.drainEvents()) out.push_back(e);
    }
    return out;
}

Level boundOnly() {
    Level level;
    level.items = {levelItem(ItemType::WorldBound, 0, Vec2(0.0f, 0.0f), false)};
    return level;
}

const PhysicsObject& objectOf(const Session& s, int handle) {
    const GameItem* item = s.state().findItem(handle);
    REQUIRE(item != nullptr);
    return s.state().objects[static_cast<std::size_t>(item->objectIndex)];
}

}  // namespace

TEST_CASE("simulation: the fixed step is the original's 0x3c088889 and the accumulator runs at 0.8 x real time") {
    CHECK(bitsFromFloat(kSimulationStep) == 0x3c088889u);
    Rig rig(boundOnly());
    rig.session.play();
    CHECK(rig.session.controllerState() == 4);
    // One frame of 1/60 s feeds 0.8/60 = 0.0133 s: one substep, 0.005 s left.
    rig.session.advance(kDt);
    CHECK(rig.session.substepCount() == 1);
    CHECK(rig.session.accumulator() == doctest::Approx(0.8f / 60.0f - kSimulationStep).epsilon(1e-6));
    // Three frames = 0.04 s → 4 substeps (the third frame carries two).
    rig.session.advance(kDt);
    rig.session.advance(kDt);
    CHECK(rig.session.substepCount() == 4);
    CHECK(rig.session.playTime() == doctest::Approx(3.0f * kDt));
    rig.session.stop();
    CHECK(rig.session.controllerState() == 2);
    CHECK(rig.session.playTime() == 0.0f);
}

TEST_CASE("handle table: per-slot generations and the LIFO free list of the original") {
    HandleManager hm;
    // A fresh table hands out slot 0 with generation 2 (the entry starts at 1, Add bumps it).
    const int h0 = hm.add(ItemType::Shelf, 0);
    CHECK(Handle::slotOf(h0) == 0);
    CHECK(Handle::generationOf(h0) == 2);
    CHECK(hm.add(ItemType::Shelf, 1) == Handle::make(ItemType::Shelf, 2, 1));
    // A level handle takes its slot out of the free list with its own generation.
    hm.addWithHandle(Handle::make(ItemType::Book, 5, 3), 2);
    CHECK(hm.lookup(Handle::make(ItemType::Book, 5, 3)) == 2);
    CHECK(hm.lookup(Handle::make(ItemType::Book, 4, 3)) == -1);   // a stale generation
    CHECK(hm.freeSlot() == 2);
    // A removed slot goes back to the head of the free list and its next handle bumps the generation.
    hm.remove(h0);
    CHECK(hm.freeSlot() == 0);
    const int h0b = hm.add(ItemType::Shelf, 0);
    CHECK(Handle::slotOf(h0b) == 0);
    CHECK(Handle::generationOf(h0b) == 3);
    CHECK(hm.lookup(h0) == -1);
    CHECK(hm.freeSlot() == 2);
}

TEST_CASE("simulation: a dropped ball rests on the floor, GetStateFromPhysics copies body 0, the lerp reports the render pose") {
    Level level = boundOnly();
    level.items.push_back(levelItem(ItemType::TennisBall, 1, Vec2(1.7f, 0.5f), false));
    Rig rig(level);
    rig.session.play();
    const int ball = Handle::make(ItemType::TennisBall, 1, 1);
    playFrames(rig.session, 120);
    const PhysicsObject& obj = objectOf(rig.session, ball);
    CHECK(obj.position.y < 0.2f);
    CHECK(obj.position.y > 0.0f);
    const b2Body* body = rig.session.world()->body(obj.bodies[0]);
    CHECK(obj.position.x == body->GetPosition().x);
    CHECK(obj.position.y == body->GetPosition().y);
    // The render pose lies between the last two physics states (here: at rest, equal to both).
    const RenderPose pose = rig.session.renderPose(obj.index);
    CHECK(pose.position.y == doctest::Approx(obj.position.y).epsilon(1e-4));
    const RenderState rs = rig.session.renderState();
    bool found = false;
    for (const RenderItem& ri : rs.items) {
        if (ri.handle == ball) {
            found = true;
            CHECK(ri.position.y == pose.position.y);
        }
    }
    CHECK(found);
}

TEST_CASE("simulation: a sharp dart pops a balloon (BeginContact) and the balloon is removed after 0.15 s") {
    Level level = boundOnly();
    // A balloon pinned under a shelf so that it cannot float away, a dart tip-down above it.
    level.items.push_back(levelItem(ItemType::Balloon, 1, Vec2(1.7f, 0.5f), false));
    LevelItem dart = levelItem(ItemType::Dart, 2, Vec2(1.7f, 1.2f), false);
    dart.angle = -1.5707963f;   // tip (+x) turned down
    level.items.push_back(dart);
    Rig rig(level);
    rig.session.play();
    const int balloon = Handle::make(ItemType::Balloon, 1, 1);
    bool popped = false;
    int removedAt = -1;
    for (int frame = 0; frame < 240 && removedAt < 0; ++frame) {
        const std::vector<SessionEvent> events = playFrames(rig.session, 1);
        for (const SessionEvent& e : events) {
            if (e.kind == SessionEvent::Kind::Sound && e.soundId == sound::kBalloonPop) popped = true;
        }
        if (rig.session.state().findItem(balloon) == nullptr) removedAt = frame;
    }
    CHECK(popped);
    CHECK(removedAt > 0);
    // The popped balloon's object is gone from the collection; the dart survives.
    CHECK(rig.session.state().findItem(balloon) == nullptr);
    CHECK(rig.session.state().findItem(Handle::make(ItemType::Dart, 1, 2)) != nullptr);
}

TEST_CASE("goals: type 8 (y at or below the height) completes, the countdown ends in the completed state, stop returns to set-up") {
    Level level = boundOnly();
    level.items.push_back(levelItem(ItemType::TennisBall, 1, Vec2(1.7f, 1.0f), false));
    level.goal.type = 8;
    level.goal.itemCount = 1;
    level.goal.itemHandles[0] = Handle::make(ItemType::TennisBall, 1, 1);
    level.goal.height = 0.5f;
    Rig rig(level);
    rig.session.play();
    std::vector<SessionEvent> events;
    int goalFrame = -1;
    for (int frame = 0; frame < 300; ++frame) {
        for (const SessionEvent& e : playFrames(rig.session, 1)) {
            if (e.kind == SessionEvent::Kind::GoalComplete && goalFrame < 0) goalFrame = frame;
            events.push_back(e);
        }
    }
    CHECK(goalFrame > 0);
    CHECK(rig.session.goalState().reached);
    CHECK(countEvents(events, SessionEvent::Kind::GoalComplete) == 1);
    CHECK(countEvents(events, SessionEvent::Kind::LevelCompleted) == 1);
    CHECK(rig.session.controllerState() == 6);
    // The physics froze in state 6: the accumulator no longer advances.
    const float acc = rig.session.accumulator();
    playFrames(rig.session, 5);
    CHECK(rig.session.accumulator() == acc);
    rig.session.stop();
    CHECK(rig.session.controllerState() == 2);
    CHECK_FALSE(rig.session.goalState().reached);
}

TEST_CASE("goal markers: a touch hides them, the set-up tail lays them out again 5 s after the last touch") {
    Level level = boundOnly();
    level.items.push_back(levelItem(ItemType::TennisBall, 1, Vec2(1.7f, 1.0f), true));
    level.goal.type = 8;
    level.goal.itemCount = 1;
    level.goal.itemHandles[0] = Handle::make(ItemType::TennisBall, 1, 1);
    level.goal.height = 0.5f;
    Rig rig(level);
    REQUIRE(rig.session.renderState().markers.size() == 2);   // the target's circle and the goal's own marker
    // A press on the fixed ball (state pending → buzz) clears the markers for the whole touch.
    const Vec2 p = rig.worldToPointer(Vec2(1.7f, 1.0f));
    rig.session.pointerDown(0, p);
    rig.advanceIds(15);
    CHECK(rig.session.renderState().markers.empty());
    rig.session.pointerUp(0, p);
    rig.advanceIds();
    CHECK(rig.session.touchState().state == touch_state::kIdle);
    // Idle for 4.9 s: still hidden; past 5 s of idle time SetGoalMarkers runs and the appear animation replays.
    rig.advanceIds(294);
    CHECK(rig.session.renderState().markers.empty());
    rig.advanceIds(8);
    REQUIRE(rig.session.renderState().markers.size() == 2);
    CHECK(rig.session.renderState().markers[0].frameStep == -1);
    rig.advanceIds(120);
    CHECK(rig.session.renderState().markers[0].frameStep == 4);
}

TEST_CASE("goals: types 6 / 9 / 10 need every target past the threshold, type 4 never completes") {
    auto run = [](int type, float width, float height, Vec2 ballAt) {
        Level level = boundOnly();
        level.items.push_back(levelItem(ItemType::TennisBall, 1, ballAt, false));
        level.goal.type = type;
        level.goal.itemCount = 1;
        level.goal.itemHandles[0] = Handle::make(ItemType::TennisBall, 1, 1);
        level.goal.width = width;
        level.goal.height = height;
        Rig rig(level);
        rig.session.play();
        const std::vector<SessionEvent> events = playFrames(rig.session, 120);
        return countEvents(events, SessionEvent::Kind::GoalComplete);
    };
    CHECK(run(6, 0.0f, 0.5f, Vec2(1.7f, 1.0f)) == 1);    // starts above 0.5 → y ≥ height at once
    CHECK(run(6, 0.0f, 5.0f, Vec2(1.7f, 1.0f)) == 0);
    CHECK(run(9, 1.0f, 0.0f, Vec2(1.7f, 1.0f)) == 1);    // x ≥ width
    CHECK(run(9, 3.0f, 0.0f, Vec2(1.7f, 1.0f)) == 0);
    CHECK(run(10, 3.0f, 0.0f, Vec2(1.7f, 1.0f)) == 1);   // x ≤ width
    CHECK(run(10, 1.0f, 0.0f, Vec2(1.7f, 1.0f)) == 0);
    CHECK(run(4, 0.0f, 0.0f, Vec2(1.7f, 1.0f)) == 0);
}

TEST_CASE("goals: type 3 needs the floor contact, type 7 needs 0.3 s in the container's sensor") {
    {
        Level level = boundOnly();
        level.items.push_back(levelItem(ItemType::TennisBall, 1, Vec2(1.7f, 0.8f), false));
        level.goal.type = 3;
        level.goal.itemCount = 1;
        level.goal.itemHandles[0] = Handle::make(ItemType::TennisBall, 1, 1);
        Rig rig(level);
        rig.session.play();
        CHECK(countEvents(playFrames(rig.session, 5), SessionEvent::Kind::GoalComplete) == 0);   // still falling
        CHECK(countEvents(playFrames(rig.session, 120), SessionEvent::Kind::GoalComplete) == 1);
    }
    {
        // The container scenario needs the real item sizes (a plain-frame bucket is as small as the ball).
        AA_REQUIRE_ASSETS();
        const FrameTable frames = aa::data::loadFrameTableFile(assetsDir() + "/atlases/GameItems.json");
        const TemplateTable templates = initTemplates(frames);
        Level level = boundOnly();
        level.items.push_back(levelItem(ItemType::Bucket, 1, Vec2(1.7f, 0.3f), true));
        level.items.push_back(levelItem(ItemType::TennisBall, 2, Vec2(1.7f, 0.9f), false));
        level.goal.type = 7;
        level.goal.itemCount = 1;
        level.goal.itemHandles[0] = Handle::make(ItemType::Bucket, 1, 1);
        level.goal.itemHandles2[0] = Handle::make(ItemType::TennisBall, 1, 2);
        Session session(templates);
        session.setViewport(ScreenLayout::compute(1024, 768));
        session.load(level);
        session.play();
        int goalFrame = -1;
        int contactFrame = -1;
        for (int frame = 0; frame < 300 && goalFrame < 0; ++frame) {
            const std::vector<SessionEvent> events = playFrames(session, 1);
            if (contactFrame < 0 && session.goalState().contactTime[0] > 0.0f) contactFrame = frame;
            if (countEvents(events, SessionEvent::Kind::GoalComplete)) goalFrame = frame;
        }
        CHECK(contactFrame > 0);
        // The timer accumulates the frame's accumulator (0.8 × wall time) and restarts whenever the ball
        // bounces out of the sensor: at least 0.3 s / 0.8 ≈ 22 frames of contact before the goal.
        CHECK(goalFrame >= contactFrame + 22);
    }
}

TEST_CASE("stars: a touched star shrinks for 0.4 s and is removed; three stars do not complete a campaign level") {
    Level level = boundOnly();
    for (int i = 0; i < 3; ++i) level.items.push_back(levelItem(ItemType::GoalStar, 1 + i, Vec2(1.0f + 0.5f * static_cast<float>(i), 0.3f), true));
    level.items.push_back(levelItem(ItemType::Shelf, 4, Vec2(1.7f, 0.2f), true));
    level.items.push_back(levelItem(ItemType::TennisBall, 5, Vec2(1.0f, 0.5f), false));
    level.goal.type = 4;
    Rig rig(level);
    rig.session.play();
    std::vector<SessionEvent> events = playFrames(rig.session, 60);
    // The ball falls through the first star's sensor.
    CHECK(countEvents(events, SessionEvent::Kind::StarCollected) == 1);
    CHECK(rig.session.goalState().collectedStars == 1);
    bool shrinking = false;
    const GameItem* star = rig.session.state().findItem(Handle::make(ItemType::GoalStar, 1, 1));
    if (star != nullptr) shrinking = star->starState != 0;
    CHECK((shrinking || star == nullptr));
    playFrames(rig.session, 60);
    CHECK(rig.session.state().findItem(Handle::make(ItemType::GoalStar, 1, 1)) == nullptr);
    CHECK(rig.session.state().findItem(Handle::make(ItemType::GoalStar, 1, 2)) != nullptr);
    // The object list stays consistent after the removal (indices, item links, render poses).
    for (std::size_t i = 0; i < rig.session.state().objects.size(); ++i) {
        const PhysicsObject& o = rig.session.state().objects[i];
        CHECK(o.index == static_cast<int>(i));
        CHECK(rig.session.state().itemOf(o).objectIndex == static_cast<int>(i));
        const RenderPose pose = rig.session.renderPose(static_cast<int>(i));
        if (!o.isDynamic()) CHECK(pose.position.x == o.position.x);
    }
    // A three-star run of a campaign level does not complete (the rule belongs to modes 2 / 4).
    CHECK_FALSE(rig.session.goalState().reached);
}

TEST_CASE("simulation touch: a press during play stops the run on the next frame") {
    Level level = boundOnly();
    level.items.push_back(levelItem(ItemType::TennisBall, 1, Vec2(1.7f, 1.0f), false));
    Rig rig(level);
    rig.session.play();
    playFrames(rig.session, 10);
    CHECK(rig.session.controllerState() == 4);
    rig.session.pointerDown(0, Vec2(500.0f, 300.0f));
    rig.session.advance(kDt);
    CHECK(rig.session.stopRequested());
    rig.session.advance(kDt);
    CHECK(rig.session.controllerState() == 2);
    CHECK(rig.session.physicsMode() == PhysicsMode::SetUp);
    rig.session.pointerUp(0, Vec2(500.0f, 300.0f));
}

TEST_CASE("simulation: five seconds without a moving body stop the run by themselves") {
    Rig rig(boundOnly());
    rig.session.play();
    int stoppedAt = -1;
    for (int frame = 0; frame < 400 && stoppedAt < 0; ++frame) {
        rig.session.advance(kDt);
        rig.session.drainActions();
        rig.session.drainEvents();
        if (rig.session.controllerState() == 2) stoppedAt = frame;
    }
    // 300 additions of 1/60 in float pass 5.0 a frame early.
    CHECK(stoppedAt >= 5 * 60 - 2);
    CHECK(stoppedAt < 5 * 60 + 3);
}

TEST_CASE("play: SetInitialState seeds the idle timers of darts and scissors from the handle, deterministically") {
    Level level = boundOnly();
    level.items.push_back(levelItem(ItemType::Dart, 1, Vec2(1.0f, 1.0f), true));
    level.items.push_back(levelItem(ItemType::Scissors, 2, Vec2(2.0f, 1.0f), true));
    Rig a(level);
    Rig b(level);
    const GameItem* dart = a.session.state().findItem(Handle::make(ItemType::Dart, 1, 1));
    const GameItem* scissors = a.session.state().findItem(Handle::make(ItemType::Scissors, 1, 2));
    REQUIRE(dart != nullptr);
    REQUIRE(scissors != nullptr);
    // The rig ran 90 set-up frames, which count the idle timers down (the set-up idle animations).
    auto expected = [](int handle) {
        Random r;
        r.setSeed(handle);
        float t = r.getFloat(GameItem::kIdleTimerMin, GameItem::kIdleTimerMax);
        for (int i = 0; i < 90; ++i) t = t - kDt;
        return t;
    };
    CHECK(dart->wobbleTimer == expected(Handle::make(ItemType::Dart, 1, 1)));
    CHECK(scissors->snipTimer == expected(Handle::make(ItemType::Scissors, 1, 2)));
    CHECK(dart->wobbleTimer == b.session.state().findItem(Handle::make(ItemType::Dart, 1, 1))->wobbleTimer);
    // The idle wobble starts after that timer and draws from the game's Random.
    const std::uint32_t seedBefore = a.session.random().seed;
    a.session.play();
    playFrames(a.session, 60 * 11);
    CHECK(a.session.random().seed != seedBefore);
}

TEST_CASE("layout: the auto-added world bound never answers a 'no pair' state word of zero") {
    // A layout without a bound gets one under the free slot with generation 1 (HandleManager's first
    // generation): a generation-0 handle would let lookup(0) — a lever's / controller's "no pair" word —
    // resolve to the bound and open its walls.
    Level level;
    level.items = {levelItem(ItemType::TrapdoorLever, 1, Vec2(1.0f, 1.0f), true)};
    Rig rig(level);
    const WorldState& state = rig.session.state();
    CHECK(state.handles.lookup(0) < 0);
    const GameItem* bound = nullptr;
    for (const GameItem& item : state.items) {
        if (item.type == ItemType::WorldBound) bound = &item;
    }
    REQUIRE(bound != nullptr);
    CHECK(Handle::generationOf(bound->handle) == 1);
    CHECK(state.handles.lookup(bound->handle) >= 0);
}

TEST_CASE("layout: a zip line with a zero-length rope end is rejected instead of seeding NaN") {
    Level level = boundOnly();
    LevelItem zip = levelItem(ItemType::ZipLine, 1, Vec2(1.0f, 1.0f), true);
    zip.ropeEnd = Vec2(0.0f, 0.0f);
    level.items.push_back(zip);
    TemplateTable templates = setup_test::plainTemplates();
    Session session{templates};
    CHECK_THROWS_AS(session.load(level), std::invalid_argument);
    zip.ropeEnd = Vec2(1.0f, -0.2f);
    level.items.back() = zip;
    CHECK_NOTHROW(session.load(level));
}
