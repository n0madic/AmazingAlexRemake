// The shared fixture of the G5 set-up suites: a Session on a synthetic level (no assets), a 1024×768
// viewport, pointer helpers in the window's native px (y down) and per-frame action collection.
#pragma once

#include "aa/sim/action.h"
#include "aa/sim/session.h"
#include "aa/sim/world_state.h"

#include <vector>

namespace setup_test {

using namespace aa::sim;

constexpr float kDt = 1.0f / 60.0f;

inline TemplateTable plainTemplates() {
    FrameTable frames;
    frames.frames.resize(kTemplateFrameCount);
    for (Frame& f : frames.frames) {
        f.x1 = 100.0f;
        f.y1 = 100.0f;
    }
    return initTemplates(frames);
}

inline LevelItem levelItem(ItemType type, int slot, Vec2 center, bool fixed = true) {
    LevelItem li;
    li.type = type;
    li.handle = Handle::make(type, 1, slot);
    li.center = center;
    if (fixed) li.flags = level_flags::kFixed;
    return li;
}

// Bound + a fixed shelf at (1.7, 0.6) + `toolbox` (default: one Book).
inline Level classroomLike(std::vector<ToolboxSlot> toolbox = {{ItemType::Book, 1}}) {
    Level level;
    level.items = {levelItem(ItemType::WorldBound, 0, Vec2(0.0f, 0.0f), false), levelItem(ItemType::Shelf, 1, Vec2(1.7f, 0.6f))};
    level.toolbox = std::move(toolbox);
    return level;
}

// Bound + a fixed hook at (1.7, 1.6) + `toolbox` (default: one Rope).
inline Level hookLevel(std::vector<ToolboxSlot> toolbox = {{ItemType::Rope, 1}}) {
    Level level;
    level.items = {levelItem(ItemType::WorldBound, 0, Vec2(0.0f, 0.0f), false), levelItem(ItemType::Hook, 1, Vec2(1.7f, 1.6f))};
    level.toolbox = std::move(toolbox);
    return level;
}

struct Rig {
    TemplateTable templates = plainTemplates();
    Session session{templates};
    ScreenLayout layout = ScreenLayout::compute(1024, 768);

    explicit Rig(const Level& level = classroomLike()) {
        session.setViewport(layout);
        session.load(level);
        // Let the toolbox strip glide on screen and extend (0.6 s + the eject easing).
        for (int i = 0; i < 90; ++i) session.advance(kDt);
        session.drainActions();
        session.drainEvents();
    }
    // Native px with y down, as the window reports it, from the original's y-up px.
    Vec2 yDown(Vec2 px) const { return Vec2(px.x, static_cast<float>(layout.height) - px.y); }
    Vec2 worldToPointer(Vec2 w) const { return yDown(session.worldToScreen(w)); }
    Vec2 slotPointer(int slot) const {
        const Toolbox& tb = session.toolbox();
        const Vec2 c = tb.getCenterForSlot(slot);
        return yDown(Vec2(tb.x + c.x, tb.y + c.y));
    }
    std::vector<int> advanceIds(int frames = 1) {
        std::vector<int> ids;
        for (int i = 0; i < frames; ++i) {
            session.advance(kDt);
            for (const Action& a : session.drainActions()) ids.push_back(a.id);
        }
        return ids;
    }
    static bool has(const std::vector<int>& ids, int id) {
        for (int i : ids) {
            if (i == id) return true;
        }
        return false;
    }
    int objectOfType(ItemType type) const {
        for (const PhysicsObject& o : session.state().objects) {
            if (o.type == type && o.valid() && !o.isGhost()) return o.index;
        }
        return -1;
    }
    int countOfType(ItemType type) const {
        int n = 0;
        for (const PhysicsObject& o : session.state().objects) {
            if (o.type == type && o.valid() && !o.isGhost()) ++n;
        }
        return n;
    }
    const PhysicsObject& obj(int i) const { return session.state().objects[static_cast<std::size_t>(i)]; }
    // Takes slot 0 out of the strip, waits for the adding animation, adopts pointer 0 as its finger.
    int takeAndHold(int slot = 0) {
        session.takeFromToolbox(slot);
        advanceIds(25);
        const int held = session.heldObject();
        if (held < 0) return -1;
        session.pointerDown(0, worldToPointer(obj(held).position));
        advanceIds();
        return held;
    }
    // Drops the held item at world position `w` and runs the frames the drop needs.
    void dropAt(Vec2 w) {
        session.pointerMove(0, worldToPointer(w));
        advanceIds();
        session.pointerUp(0, worldToPointer(w));
        advanceIds(2);
    }
};

}  // namespace setup_test
