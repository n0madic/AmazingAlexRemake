#include "aa/sim/tutorial.h"

#include "aa/sim/math_utils.h"

namespace aa::sim {

namespace {

constexpr float kOne = 1.0f;
constexpr float kHalf = 0.5f;
constexpr float kFadeOutWait = 5.0f;        // fetch_all_items_tutorial: the pause after the fade-out
constexpr float kRingRadius = 0.4f;         // fetch_item_rotate_tutorial: the circular hand path
constexpr int kShelfKey = 1;                // the Hashtable keys of the target stacks
constexpr int kBookKey = 0xf;
constexpr int kImagePointer = 0;
constexpr int kImagePressed = 1;
// The seven Classroom scripts' constants (tutorial_chap0_level0..6) [verified: the literal pools].
const Vec2 kHandStartAbove(2.5575f, 1.5934f);    // 0x4023ae15, 0x3fcbf5ec (level 0: over the play field)
const Vec2 kHandStartBelow(2.5575f, 0.0f);       // levels 1..6: below the play field (the toolbox)
constexpr float kLevel6Angle = -0.5425f;         // 0xbf0ae148
constexpr float kLevel6Offset = 0.05f;
constexpr float kLevel6X = 0.413f;
constexpr float kLevel6Y = 1.369f;
constexpr float kHalfPi = 1.5707964f;

TutorialStep setPos(Vec2 p) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::SetPos;
    s.a = p;
    return s;
}
TutorialStep setImage(int image) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::SetImage;
    s.image = image;
    return s;
}
TutorialStep wait(float t) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::Wait;
    s.duration = t;
    return s;
}
TutorialStep fade(float duration, float from, float to) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::Fade;
    s.duration = duration;
    s.from = from;
    s.to = to;
    return s;
}
TutorialStep moveLinear(float duration, Vec2 from, Vec2 to) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::MoveLinear;
    s.duration = duration;
    s.a = from;
    s.b = to;
    return s;
}
TutorialStep moveCircular(float duration, Vec2 center, float radius, float startAngle, float sweep) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::MoveCircular;
    s.duration = duration;
    s.a = center;
    s.radius = radius;
    s.from = startAngle;
    s.to = sweep;
    return s;
}
TutorialStep setDragItem(int slot, int type) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::SetDragItem;
    s.slot = slot;
    s.type = type;
    return s;
}
TutorialStep setOrientationItem(int slot, int type) {
    TutorialStep s;
    s.kind = TutorialStep::Kind::SetOrientationItem;
    s.slot = slot;
    s.type = type;
    return s;
}

// Movestate::update's path evaluation.
Vec2 pathAt(const TutorialStep& s, float t) {
    if (s.kind == TutorialStep::Kind::MoveLinear) {
        // LinearPath::path(t): a + (t / duration) · (b − a).
        const float k = t / s.duration;
        return Vec2(s.a.x + k * (s.b.x - s.a.x), s.a.y + k * (s.b.y - s.a.y));
    }
    // CircularPath::path(t): centre + r · (cos, sin)(start + sweep · t / duration).
    const float angle = s.from + s.to * (t / s.duration);
    return Vec2(s.a.x + s.radius * cosF(angle), s.a.y + s.radius * sinF(angle));
}

// TutorialHandState::update of one state; the step advances inside (Movestate / Waitstate / Fadestate
// only once their time is over).
void runStep(const TutorialStep& s, TutorialState& st) {
    switch (s.kind) {
    case TutorialStep::Kind::SetPos:
        st.hand.pos = s.a;
        st.time = 0.0f;
        st.step = st.step + 1;
        break;
    case TutorialStep::Kind::SetImage:
        st.time = 0.0f;
        st.hand.image = s.image;
        st.step = st.step + 1;
        break;
    case TutorialStep::Kind::Wait:
        if (s.duration < st.time) {
            st.time = 0.0f;
            st.step = st.step + 1;
        }
        break;
    case TutorialStep::Kind::Fade:
        if (st.time <= s.duration) {
            st.hand.alpha = s.from + (s.to - s.from) * (st.time / s.duration);
        } else {
            st.hand.alpha = s.to;
            st.step = st.step + 1;
            st.time = 0.0f;
        }
        break;
    case TutorialStep::Kind::MoveLinear:
    case TutorialStep::Kind::MoveCircular:
        if (s.duration < st.time) {
            st.hand.pos = pathAt(s, s.duration);
            st.time = 0.0f;
            st.step = st.step + 1;
        } else {
            st.hand.pos = pathAt(s, st.time);
        }
        break;
    case TutorialStep::Kind::SetDragItem:
        st.time = 0.0f;
        st.dragSlot = s.slot;
        st.dragType = s.type;
        st.step = st.step + 1;
        break;
    case TutorialStep::Kind::SetOrientationItem:
        st.time = 0.0f;
        st.orientSlot = s.slot;
        st.orientType = s.type;
        st.step = st.step + 1;
        break;
    }
}

// One hand-drag of one item: to the slot, press, pick, drag to the target, release, drop (every wait
// 1 s) — the loop body shared by fetch_all_items_tutorial and fetch_item_rotate_tutorial.
void appendFetch(std::vector<TutorialStep>& out, Vec2& current, Vec2 slotWorld, Vec2 target, int item, int type) {
    out.push_back(moveLinear(kOne, current, slotWorld));
    out.push_back(wait(kOne));
    out.push_back(setImage(kImagePressed));
    out.push_back(wait(kOne));
    out.push_back(setDragItem(item, type));
    out.push_back(wait(kOne));
    out.push_back(moveLinear(kOne, slotWorld, target));
    out.push_back(wait(kOne));
    out.push_back(setImage(kImagePointer));
    out.push_back(wait(kOne));
    out.push_back(setDragItem(-1, 0));
    out.push_back(wait(kOne));
    current = target;
}

}  // namespace

bool tutorialShouldRun(const TutorialContext& ctx) {
    if (ctx.locationIndex != 0) return false;
    if (ctx.itemCount < 1) {
        // An empty toolbox: level 0 only (1 − level, clamped at 0 for level > 1).
        return ctx.levelIndex == 0;
    }
    return true;
}

std::vector<TutorialStep> clickPositionTutorial(Vec2 from, Vec2 to) {
    std::vector<TutorialStep> s;
    s.push_back(setPos(from));
    s.push_back(setImage(kImagePointer));
    s.push_back(wait(kOne));
    s.push_back(fade(kOne, 0.0f, kOne));
    s.push_back(wait(kOne));
    s.push_back(moveLinear(kOne, from, to));
    s.push_back(wait(kOne));
    s.push_back(setImage(kImagePressed));
    s.push_back(wait(kOne));
    s.push_back(setImage(kImagePointer));
    s.push_back(wait(kOne));
    s.push_back(fade(kOne, kOne, 0.0f));
    return s;
}

std::vector<TutorialStep> fetchAllItemsTutorial(Vec2 start, const TutorialTargets& targets, const TutorialContext& ctx) {
    std::vector<TutorialStep> s;
    s.push_back(setPos(start));
    for (int i = 0; i < ctx.itemCount; ++i) s.push_back(setDragItem(i, 0));
    s.push_back(fade(kOne, 0.0f, kOne));
    s.push_back(wait(kOne));
    Vec2 current = start;
    // The target stacks are copied per slot and popped from the back; a type without targets pops from
    // an empty array (the original reads before the array — undefined; the shipped scripts never do).
    std::vector<Vec2> shelf = targets.shelf;
    std::vector<Vec2> book = targets.book;
    int item = 0;
    for (const TutorialToolboxSlot& slot : ctx.slots) {
        std::vector<Vec2>* stack = nullptr;
        if (static_cast<int>(slot.type) == kShelfKey) stack = &shelf;
        else if (static_cast<int>(slot.type) == kBookKey) stack = &book;
        std::vector<Vec2> local = stack ? *stack : std::vector<Vec2>{};
        for (int n = 0; n < slot.amount; ++n) {
            Vec2 target(0.0f, 0.0f);
            if (!local.empty()) {
                target = local.back();
                local.pop_back();
            }
            appendFetch(s, current, slot.world, target, item, static_cast<int>(slot.type));
            ++item;
        }
    }
    s.push_back(fade(kOne, kOne, 0.0f));
    s.push_back(wait(kFadeOutWait));
    return s;
}

std::vector<TutorialStep> fetchItemRotateTutorial(Vec2 start, Vec2 target, float angle, const TutorialContext& ctx) {
    std::vector<TutorialStep> s;
    const Vec2 ring(target.x + kRingRadius, target.y + 0.0f);
    s.push_back(setPos(start));
    s.push_back(setDragItem(0, 0));
    s.push_back(fade(kOne, 0.0f, kOne));
    s.push_back(wait(kOne));
    const Vec2 slotWorld = ctx.slots.empty() ? Vec2(0.0f, 0.0f) : ctx.slots[0].world;
    const int type = ctx.slots.empty() ? 0 : static_cast<int>(ctx.slots[0].type);
    Vec2 current = start;
    appendFetch(s, current, slotWorld, target, 0, type);
    s.push_back(moveLinear(kOne, target, ring));
    s.push_back(wait(kOne));
    s.push_back(setOrientationItem(0, type));
    s.push_back(wait(kOne));
    s.push_back(setImage(kImagePressed));
    s.push_back(wait(kHalf));
    s.push_back(moveCircular(kOne, target, kRingRadius, 0.0f, angle));
    s.push_back(wait(kHalf));
    s.push_back(setImage(kImagePointer));
    s.push_back(setOrientationItem(-1, 0));
    s.push_back(wait(kOne));
    s.push_back(fade(kOne, kOne, 0.0f));
    return s;
}

void tutorialStop(TutorialState& state) {
    if (!state.running) return;
    state.running = false;
    state.step = 0;
    state.steps.clear();
    state.items.clear();
}

void tutorialStart(TutorialState& state, const TutorialContext& ctx) {
    tutorialStop(state);
    if (!tutorialShouldRun(ctx)) return;
    state = TutorialState{};
    if (ctx.locationIndex != 0) return;
    switch (ctx.levelIndex) {
    case 0:
        if (!ctx.hasPlayButton) return;
        state.steps = clickPositionTutorial(kHandStartAbove, ctx.playButtonWorld);
        state.itemCount = 0;
        break;
    case 1: {
        TutorialTargets t;
        t.shelf = {Vec2(0.943f, 0.754f)};
        state.steps = fetchAllItemsTutorial(kHandStartBelow, t, ctx);
        state.itemCount = ctx.itemCount;
        break;
    }
    case 2: {
        TutorialTargets t;
        t.shelf = {Vec2(1.575f, 0.303f)};
        state.steps = fetchAllItemsTutorial(kHandStartBelow, t, ctx);
        state.itemCount = ctx.itemCount;
        break;
    }
    case 3: {
        TutorialTargets t;
        t.shelf = {Vec2(1.149f, 1.077f), Vec2(1.329f, 0.335f)};
        state.steps = fetchAllItemsTutorial(kHandStartBelow, t, ctx);
        state.itemCount = ctx.itemCount;
        break;
    }
    case 4: {
        TutorialTargets t;
        t.shelf = {Vec2(0.966f, 0.871f)};
        t.book = {Vec2(2.408f, 1.304f)};
        state.steps = fetchAllItemsTutorial(kHandStartBelow, t, ctx);
        state.itemCount = ctx.itemCount;
        break;
    }
    case 5: {
        TutorialTargets t;
        t.shelf = {Vec2(2.647f, 1.48f)};
        t.book = {Vec2(1.608f, 1.693f)};
        state.steps = fetchAllItemsTutorial(kHandStartBelow, t, ctx);
        state.itemCount = ctx.itemCount;
        break;
    }
    case 6: {
        const float a = kHalfPi - kLevel6Angle;
        const Vec2 target(cosF(a) * kLevel6Offset + kLevel6X, sinF(a) * kLevel6Offset + kLevel6Y);
        state.steps = fetchItemRotateTutorial(kHandStartBelow, target, kLevel6Angle, ctx);
        state.itemCount = 1;
        break;
    }
    default: return;
    }
    state.running = true;
    state.items.assign(static_cast<std::size_t>(state.itemCount), TutorialControlledItem{});
}

void tutorialUpdate(float dt, TutorialState& st) {
    if (!st.running) return;
    const int count = static_cast<int>(st.steps.size());
    if (count <= 0) return;
    st.step = st.step % count;
    runStep(st.steps[static_cast<std::size_t>(st.step)], st);
    if (st.dragSlot >= 0 && st.dragSlot < static_cast<int>(st.items.size())) {
        TutorialControlledItem& it = st.items[static_cast<std::size_t>(st.dragSlot)];
        it.pos = st.hand.pos;
        it.type = st.dragType;
        it.visible = true;
    }
    if (st.orientSlot >= 0 && st.orientSlot < static_cast<int>(st.items.size())) {
        TutorialControlledItem& it = st.items[static_cast<std::size_t>(st.orientSlot)];
        const float angle = atan2F(st.hand.pos.y - it.pos.y, st.hand.pos.x - it.pos.x);
        it.type = st.orientType;
        it.visible = true;
        it.angle = angle;
    }
    // The ghost render entries (GameState+0x31a0c) are derived from `items` by the renderer.
    if (st.step == count) {
        for (TutorialControlledItem& it : st.items) it = TutorialControlledItem{};
    }
    st.time = st.time + dt;
}

}  // namespace aa::sim
