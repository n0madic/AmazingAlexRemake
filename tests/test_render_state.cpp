// Session::renderState() over every shipped level in both physics modes (docs/10 §5.4, §10 M2 exit).
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/sim/items/items.h"
#include "aa/sim/session.h"
#include "test_support.h"

#include <doctest.h>

#include <cmath>
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

// Slots of destroyed bodies keep their numbers (docs/09): count the live ones.
int liveBodies(const PhysicsWorld& world) {
    int n = 0;
    for (int i = 0; i < world.bodyCount(); ++i) n += world.body(i) != nullptr ? 1 : 0;
    return n;
}

void checkState(const RenderState& rs, const Session& session, int frameCount) {
    CHECK(rs.items.size() == session.state().objects.size());
    for (const RenderItem& ri : rs.items) {
        INFO("item " << static_cast<int>(ri.type) << " #" << ri.objectIndex);
        CHECK(isItemImplemented(ri.type));
        CHECK(ri.bodyCount == session.state().objects[static_cast<std::size_t>(ri.objectIndex)].bodyCount);
        for (int k = 0; k < ri.bodyCount; ++k) {
            CHECK(std::isfinite(ri.bodies[static_cast<std::size_t>(k)].position.x));
            CHECK(std::isfinite(ri.bodies[static_cast<std::size_t>(k)].angle));
        }
        if (ri.type == ItemType::Book) CHECK(7 + ri.stateWord < frameCount);   // Book colour frame
        CHECK_FALSE(ri.builtInController);   // every imported level is version 7: no legacy controller bodies
        if (ri.type == ItemType::BoxingGlove) {
            CHECK(std::isfinite(ri.latticeAngle));
            CHECK(ri.buttonHeightPx == GameItem::kGloveButtonHeightPx);
        }
    }
    for (const RenderMarker& m : rs.markers) {
        CHECK(m.kind >= 1);
        CHECK(m.kind <= 8);
        CHECK(m.frameStep == -1);   // not revealed: the appear animation has not run
    }
}

}  // namespace

TEST_CASE("render state: every shipped level in both modes") {
    AA_REQUIRE_ASSETS();
    const FrameTable frames = aa::data::loadFrameTableFile(assetsDir() + "/atlases/GameItems.json");
    const TemplateTable templates = initTemplates(frames);
    const std::vector<LevelRef> refs = allLevels();
    REQUIRE(refs.size() == 116);
    Session session(templates);
    for (const LevelRef& ref : refs) {
        INFO(ref.chapter << "/" << ref.name);
        session.load(aa::data::loadLevelFile(assetsDir() + "/levels/" + ref.chapter + "/" + ref.name + ".json"));
        checkState(session.renderState(), session, frames.size());
        // A rope rebuilt on load leaves destroyed slots behind; the rebuilt worlds start from the moved
        // rope, so only the live body count is comparable.
        const int setUpBodies = liveBodies(*session.world());
        session.rebuildWorld(PhysicsMode::Simulation);
        CHECK(session.physicsMode() == PhysicsMode::Simulation);
        checkState(session.renderState(), session, frames.size());
        CHECK(liveBodies(*session.world()) <= setUpBodies);   // set-up adds pouch / selection-area bodies
        session.rebuildWorld(PhysicsMode::SetUp);
        CHECK(liveBodies(*session.world()) == setUpBodies);
    }
}

TEST_CASE("render state: goal markers of the goal types") {
    AA_REQUIRE_ASSETS();
    const FrameTable frames = aa::data::loadFrameTableFile(assetsDir() + "/atlases/GameItems.json");
    const TemplateTable templates = initTemplates(frames);
    Session session(templates);
    // Playtime: goal type 2 (put the ball into the basket) → one circle per target + the arrow marker.
    session.load(aa::data::loadLevelFile(assetsDir() + "/levels/00_Classroom/Playtime.json"));
    const Level& level = session.level();
    const RenderState rs = session.renderState();
    const int expected = level.goal.itemCount + ((level.goal.type >= 2 && level.goal.type <= 10 && level.goal.type != 4 && level.goal.type != 5) ? 1 : 0);
    CHECK(static_cast<int>(rs.markers.size()) == expected);
    session.visual().revealGoalMarkers();
    for (const RenderMarker& m : session.renderState().markers) CHECK(m.frameStep == 4);
}

TEST_CASE("camera: clamped centre and zoom") {
    Camera cam;
    CHECK(clampedCameraCenter(cam, Vec2(0.0f, 0.0f)).x == 512.0f);
    CHECK(clampedCameraCenter(cam, Vec2(0.0f, 0.0f)).y == 319.0f);
    cam.zoom = 2.0f;
    CHECK(clampedCameraCenter(cam, Vec2(0.0f, 0.0f)).x == 256.0f);
    CHECK(clampedCameraCenter(cam, Vec2(2000.0f, 2000.0f)).x == 768.0f);
    CHECK(clampedCameraCenter(cam, Vec2(2000.0f, 2000.0f)).y == doctest::Approx(638.0f - 159.5f));
}
