// The Classroom tutorial scripts and the hand-state machine (docs/05 §5): the step lists of the three
// builders, the timings of a run, the dragged / oriented ghost items.
#include "aa/sim/math_utils.h"
#include "aa/sim/tutorial.h"

#include <doctest/doctest.h>

#include <cmath>
#include <utility>
#include <vector>

using namespace aa::sim;

namespace {

constexpr float kDt = 1.0f / 60.0f;

TutorialContext classroom(int level, std::vector<TutorialToolboxSlot> slots) {
    TutorialContext ctx;
    ctx.locationIndex = 0;
    ctx.levelIndex = level;
    ctx.slots = std::move(slots);
    ctx.itemCount = 0;
    for (const TutorialToolboxSlot& s : ctx.slots) ctx.itemCount += s.amount;
    return ctx;
}

TutorialToolboxSlot slot(ItemType type, int amount, Vec2 world) {
    TutorialToolboxSlot s;
    s.type = type;
    s.amount = amount;
    s.world = world;
    return s;
}

void run(TutorialState& st, float seconds) {
    const int frames = static_cast<int>(seconds / kDt + 0.5f);
    for (int i = 0; i < frames; ++i) tutorialUpdate(kDt, st);
}

}  // namespace

TEST_CASE("tutorial_should_run: location 0, a non-empty toolbox or level 0") {
    CHECK(tutorialShouldRun(classroom(0, {})));
    CHECK_FALSE(tutorialShouldRun(classroom(1, {})));
    CHECK_FALSE(tutorialShouldRun(classroom(2, {})));
    CHECK(tutorialShouldRun(classroom(3, {slot(ItemType::Shelf, 1, Vec2(2.8f, -0.3f))})));
    TutorialContext other = classroom(0, {slot(ItemType::Shelf, 1, Vec2(2.8f, -0.3f))});
    other.locationIndex = 1;
    CHECK_FALSE(tutorialShouldRun(other));
}

TEST_CASE("click_position_tutorial: the level-0 script and its timing") {
    const Vec2 from(2.5575f, 1.5934f);
    const Vec2 to(3.1f, 1.9f);
    const std::vector<TutorialStep> steps = clickPositionTutorial(from, to);
    REQUIRE(steps.size() == 12);
    CHECK(steps[0].kind == TutorialStep::Kind::SetPos);
    CHECK(steps[1].kind == TutorialStep::Kind::SetImage);
    CHECK(steps[3].kind == TutorialStep::Kind::Fade);
    CHECK(steps[3].from == 0.0f);
    CHECK(steps[3].to == 1.0f);
    CHECK(steps[5].kind == TutorialStep::Kind::MoveLinear);
    CHECK(steps[7].image == 1);
    CHECK(steps[11].kind == TutorialStep::Kind::Fade);

    TutorialContext ctx = classroom(0, {});
    ctx.hasPlayButton = true;
    ctx.playButtonWorld = to;
    TutorialState st;
    tutorialStart(st, ctx);
    REQUIRE(st.running);
    CHECK(st.itemCount == 0);
    CHECK(st.steps.size() == 12);
    // Two frames consume SetPos and SetImage; the wait then holds a little over 1 s.
    run(st, 2.0f * kDt);
    CHECK(st.step == 2);
    CHECK(st.hand.pos.x == doctest::Approx(from.x));
    CHECK(st.hand.alpha == 0.0f);
    run(st, 1.05f);
    CHECK(st.step == 3);
    // Half-way through the fade the alpha is about 0.5.
    run(st, 0.5f);
    CHECK(st.hand.alpha == doctest::Approx(0.5f).epsilon(0.05));
    run(st, 0.55f);
    CHECK(st.step == 4);
    // The wait, then the move: half-way the hand is between the two points.
    run(st, 1.05f);
    CHECK(st.step == 5);
    run(st, 0.5f);
    CHECK(st.hand.pos.x == doctest::Approx(from.x + 0.5f * (to.x - from.x)).epsilon(0.02));
    CHECK(st.hand.pos.y == doctest::Approx(from.y + 0.5f * (to.y - from.y)).epsilon(0.02));
    run(st, 0.55f);
    CHECK(st.step == 6);
    CHECK(st.hand.pos.x == doctest::Approx(to.x));
    // The script loops: after the fade-out the step wraps to 0.
    run(st, 6.0f);
    CHECK(st.step < 12);
    // Stop drops the run.
    tutorialStop(st);
    CHECK_FALSE(st.running);
    CHECK(st.steps.empty());
}

TEST_CASE("fetch_all_items_tutorial: one item per slot, targets popped from the back") {
    const Vec2 shelfSlot(2.81f, -0.30f);
    // Level 3: two shelves, two targets — the second pushed target goes first.
    TutorialContext ctx = classroom(3, {slot(ItemType::Shelf, 2, shelfSlot)});
    TutorialState st;
    tutorialStart(st, ctx);
    REQUIRE(st.running);
    CHECK(st.itemCount == 2);
    CHECK(st.items.size() == 2);
    // setPos, 2 × setDragItem(i, 0), fade, wait, 2 × 12, fade, wait(5).
    REQUIRE(st.steps.size() == 1 + 2 + 2 + 24 + 2);
    const TutorialStep& firstDrag = st.steps[5];
    CHECK(firstDrag.kind == TutorialStep::Kind::MoveLinear);
    CHECK(firstDrag.a.x == doctest::Approx(2.5575f));
    CHECK(firstDrag.a.y == doctest::Approx(0.0f));
    CHECK(firstDrag.b.x == doctest::Approx(shelfSlot.x));
    const TutorialStep& toTarget = st.steps[11];
    CHECK(toTarget.kind == TutorialStep::Kind::MoveLinear);
    CHECK(toTarget.b.x == doctest::Approx(1.329f));
    CHECK(toTarget.b.y == doctest::Approx(0.335f));
    CHECK(st.steps[17].a.x == doctest::Approx(1.329f));   // the hand continues from the first target
    const TutorialStep& secondToTarget = st.steps[23];
    CHECK(secondToTarget.a.x == doctest::Approx(shelfSlot.x));
    CHECK(secondToTarget.b.x == doctest::Approx(1.149f));
    CHECK(secondToTarget.b.y == doctest::Approx(1.077f));
    CHECK(st.steps[9].kind == TutorialStep::Kind::SetDragItem);
    CHECK(st.steps[9].slot == 0);
    CHECK(st.steps[9].type == 1);
    CHECK(st.steps.back().duration == 5.0f);

    // Run into the first drag: the ghost of item 0 follows the hand as a Shelf.
    run(st, 3.0f * kDt);   // setPos + 2 drags
    run(st, 1.05f + 1.05f + 1.05f + 1.05f + 1.05f + 1.05f);   // fade, wait, move, wait, image, wait
    CHECK(st.step >= 9);
    run(st, 0.5f);         // inside the move to the target
    CHECK(st.dragSlot == 0);
    CHECK(st.items[0].visible);
    CHECK(st.items[0].type == 1);
    CHECK(st.items[0].pos.x == doctest::Approx(st.hand.pos.x));
    CHECK(st.items[0].pos.y == doctest::Approx(st.hand.pos.y));
    CHECK(st.hand.image == 1);
}

TEST_CASE("tutorial_chap0_level4: the Book targets use key 15") {
    TutorialContext ctx = classroom(4, {slot(ItemType::Shelf, 1, Vec2(2.7f, -0.3f)), slot(ItemType::Book, 1, Vec2(2.9f, -0.3f))});
    TutorialState st;
    tutorialStart(st, ctx);
    REQUIRE(st.running);
    REQUIRE(st.steps.size() == 1 + 2 + 2 + 24 + 2);
    CHECK(st.steps[11].b.x == doctest::Approx(0.966f));
    CHECK(st.steps[11].b.y == doctest::Approx(0.871f));
    CHECK(st.steps[21].type == 15);
    CHECK(st.steps[23].b.x == doctest::Approx(2.408f));
    CHECK(st.steps[23].b.y == doctest::Approx(1.304f));
}

TEST_CASE("fetch_item_rotate_tutorial: the level-6 ring path and the orientation ghost") {
    const Vec2 slotWorld(2.81f, -0.30f);
    TutorialContext ctx = classroom(6, {slot(ItemType::Shelf, 1, slotWorld)});
    TutorialState st;
    tutorialStart(st, ctx);
    REQUIRE(st.running);
    CHECK(st.itemCount == 1);
    REQUIRE(st.steps.size() == 1 + 1 + 2 + 12 + 12);
    const float a = 1.5707964f - (-0.5425f);
    const Vec2 target(cosF(a) * 0.05f + 0.413f, sinF(a) * 0.05f + 1.369f);
    CHECK(st.steps[10].b.x == doctest::Approx(target.x));
    CHECK(st.steps[10].b.y == doctest::Approx(target.y));
    const TutorialStep& toRing = st.steps[16];
    CHECK(toRing.kind == TutorialStep::Kind::MoveLinear);
    CHECK(toRing.b.x == doctest::Approx(target.x + 0.4f));
    CHECK(toRing.b.y == doctest::Approx(target.y));
    const TutorialStep& orient = st.steps[18];
    CHECK(orient.kind == TutorialStep::Kind::SetOrientationItem);
    CHECK(orient.slot == 0);
    CHECK(orient.type == 1);
    const TutorialStep& ring = st.steps[22];
    CHECK(ring.kind == TutorialStep::Kind::MoveCircular);
    CHECK(ring.radius == doctest::Approx(0.4f));
    CHECK(ring.from == 0.0f);
    CHECK(ring.to == doctest::Approx(-0.5425f));
    CHECK(st.steps[21].duration == doctest::Approx(0.5f));
    CHECK(st.steps[23].duration == doctest::Approx(0.5f));

    // Fast-forward to the ring: the oriented ghost points from the target towards the hand.
    for (int i = 0; i < 60 * 40 && st.step < 22; ++i) tutorialUpdate(kDt, st);
    REQUIRE(st.step == 22);
    run(st, 0.5f);
    CHECK(st.orientSlot == 0);
    CHECK(st.items[0].visible);
    const float expected = atan2F(st.hand.pos.y - st.items[0].pos.y, st.hand.pos.x - st.items[0].pos.x);
    CHECK(st.items[0].angle == doctest::Approx(expected));
    CHECK(st.items[0].pos.x == doctest::Approx(target.x).epsilon(0.01));
    // At the end of the circular path the hand sits at the swept angle.
    run(st, 0.6f);
    CHECK(st.step == 23);
    CHECK(st.hand.pos.x == doctest::Approx(target.x + 0.4f * cosF(-0.5425f)).epsilon(0.01));
    CHECK(st.hand.pos.y == doctest::Approx(target.y + 0.4f * sinF(-0.5425f)).epsilon(0.01));
}
