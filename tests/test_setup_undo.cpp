// G5 (docs/10 §8): the layout snapshot round trip (LevelLayoutUtils::Get → Apply) and the undo queue.
// The WP0 gate of M3: `applyLayout(levelFromState(state))` rebuilds the same world for every shipped
// level — same bodies, fixtures, joints and mass overrides (as a multiset of scene lines: the SelectionArea
// body moves from the end of the load scene into object order, nothing else may change).
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/sim/scene_recorder.h"
#include "aa/sim/session.h"
#include "test_support.h"

#include <doctest.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

using namespace aa::sim;

namespace {

struct LevelRef {
    std::string chapter;
    std::string name;
};

std::vector<LevelRef> allLevels() {
    std::vector<LevelRef> refs;
    for (const char* chapter : {"00_Classroom", "01_Backyard", "02_Room", "03_Treehouse"}) {
        const aa::data::LevelIndex index = aa::data::loadLevelIndexFile(assetsDir() + "/levels/" + chapter + "/index.json");
        for (const std::string& n : index.levels) refs.push_back({chapter, n});
        for (const std::string& n : index.unlisted) refs.push_back({chapter, n});
    }
    return refs;
}

std::vector<std::string> sortedScene(const SceneRecorder& rec) {
    std::vector<std::string> lines;
    for (const std::string& l : rec.lines()) {
        if (!l.empty() && l[0] != '#' && l.rfind("gravity", 0) != 0) lines.push_back(l);
    }
    std::sort(lines.begin(), lines.end());
    return lines;
}

}  // namespace

TEST_CASE("setup: layout snapshot round trip rebuilds every shipped level identically") {
    AA_REQUIRE_ASSETS();
    const FrameTable frames = aa::data::loadFrameTableFile(assetsDir() + "/atlases/GameItems.json");
    const TemplateTable templates = initTemplates(frames);
    int checked = 0;
    for (const LevelRef& ref : allLevels()) {
        INFO(ref.chapter << "/" << ref.name);
        const Level level = aa::data::loadLevelFile(assetsDir() + "/levels/" + ref.chapter + "/" + ref.name + ".json");
        SceneRecorder loadRec;
        Session session(templates);
        session.load(level, GameMode::Campaign, &loadRec);
        const WorldState before = session.state();
        const Level snapshot = levelFromState(session.state(), session.level(), session.toolbox());
        REQUIRE(snapshot.items.size() == before.objects.size());
        REQUIRE(snapshot.toolbox.size() == level.toolbox.size());
        // play() and stop() go through the snapshot twice (Get → Apply → CreateWorld…).
        SceneRecorder rebuildRec;
        session.rebuildWorld(PhysicsMode::SetUp, &rebuildRec);
        session.play();
        session.stop();
        const WorldState after = session.state();
        REQUIRE(after.objects.size() == before.objects.size());
        REQUIRE(after.items.size() == before.items.size());
        for (std::size_t i = 0; i < before.objects.size(); ++i) {
            const PhysicsObject& a = before.objects[i];
            const PhysicsObject& b = after.objects[i];
            CHECK(a.type == b.type);
            CHECK(a.handle == b.handle);
            CHECK(a.flags == b.flags);
            CHECK(a.position.x == b.position.x);
            CHECK(a.position.y == b.position.y);
            CHECK(a.angle == b.angle);
            CHECK(a.scale.x == b.scale.x);
            CHECK(a.bodyCount == b.bodyCount);
            for (int k = 0; k < a.attachmentCount; ++k) {
                CHECK(a.attachments[static_cast<std::size_t>(k)].state == b.attachments[static_cast<std::size_t>(k)].state);
                CHECK(a.attachments[static_cast<std::size_t>(k)].otherObject == b.attachments[static_cast<std::size_t>(k)].otherObject);
                CHECK(a.attachments[static_cast<std::size_t>(k)].otherPoint == b.attachments[static_cast<std::size_t>(k)].otherPoint);
            }
        }
        for (std::size_t i = 0; i < before.items.size(); ++i) {
            CHECK(before.items[i].handle == after.items[i].handle);
            CHECK(before.items[i].objectIndex == after.items[i].objectIndex);
            CHECK(before.items[i].stateWord == after.items[i].stateWord);
            CHECK(before.items[i].endVector.x == after.items[i].endVector.x);
            CHECK(before.items[i].endVector.y == after.items[i].endVector.y);
        }
        // The restored set-up world, recorded on its own, equals the original construction (multiset) —
        // except for ropes: RopeUtils::UpdatePosFromAttachedObjects moves a rope's ends onto the attached
        // objects at load, so the snapshot stores the moved end vector and the restored rope gets its link
        // lengths from that (the original does the same: LevelLayoutUtils::Get reads the live item). For
        // those levels the round trip must be idempotent from the first snapshot on.
        SceneRecorder restoredRec;
        session.rebuildWorld(PhysicsMode::SetUp, &restoredRec);
        bool hasRope = false;
        for (const LevelItem& li : level.items) hasRope = hasRope || li.type == ItemType::Rope;
        std::vector<std::string> reference = sortedScene(loadRec);
        if (hasRope) {
            session.play();
            session.stop();
            SceneRecorder againRec;
            session.rebuildWorld(PhysicsMode::SetUp, &againRec);
            reference = sortedScene(againRec);
        }
        const std::vector<std::string> restored = sortedScene(restoredRec);
        if (restored != reference) {
            std::vector<std::string> onlyRestored, onlyReference;
            std::set_difference(restored.begin(), restored.end(), reference.begin(), reference.end(), std::back_inserter(onlyRestored));
            std::set_difference(reference.begin(), reference.end(), restored.begin(), restored.end(), std::back_inserter(onlyReference));
            for (const std::string& l : onlyRestored) MESSAGE("only after restore: " << l);
            for (const std::string& l : onlyReference) MESSAGE("only in reference: " << l);
        }
        CHECK(restored == reference);
        ++checked;
    }
    CHECK(checked == 116);
}

TEST_CASE("setup: undo queue keeps 31 snapshots and drops the oldest") {
    UndoQueue q;
    q.reset();
    CHECK(q.count == -1);
    Level layout;
    for (int i = 0; i < 40; ++i) {
        layout.rewardId = i;
        q.add(layout);
        CHECK(q.count == q.top);
        CHECK(q.count <= UndoQueue::kMaxCount);
    }
    CHECK(q.count == UndoQueue::kMaxCount);
    CHECK(q.layouts[static_cast<std::size_t>(q.count)].rewardId == 39);
    CHECK(q.layouts[0].rewardId == 39 - UndoQueue::kMaxCount);
}
