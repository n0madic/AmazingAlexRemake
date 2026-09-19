// PhysicsWorld construction primitives added for the compound items: joint slots, destruction numbering,
// deferred polygonfix lines, and the ports' use of them (rope attachment bodies, rope rebuild).
#include "aa/sim/attachments.h"
#include "aa/sim/filters.h"
#include "aa/sim/items/items.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/scene_recorder.h"
#include "aa/sim/session.h"

#include <doctest.h>

#include <string>
#include <vector>

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

int countPrefix(const std::vector<std::string>& lines, const std::string& prefix) {
    int n = 0;
    for (const std::string& l : lines) n += l.rfind(prefix, 0) == 0 ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("physics world: joints are numbered in creation order and destruction keeps the slots") {
    SceneRecorder rec;
    PhysicsWorld world(&rec);
    b2BodyDef bd;
    bd.type = b2_dynamicBody;
    const int a = world.createBody(bd);
    const int b = world.createBody(bd);
    const int c = world.createBody(bd);
    b2RevoluteJointDef rj;
    rj.Initialize(world.body(a), world.body(b), Vec2(0.0f, 0.0f));
    b2DistanceJointDef dj;
    dj.Initialize(world.body(b), world.body(c), Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f));
    CHECK(world.createRevolute(rj) == 0);
    CHECK(world.createDistance(dj) == 1);
    CHECK(world.slotOf(world.joint(1)) == 1);
    world.destroyJoint(0);
    CHECK(world.joint(0) == nullptr);
    CHECK(world.jointCount() == 2);
    // Destroying body c takes joint 1 with it: no destroyjoint line, the slot is nulled.
    world.destroyBody(c);
    CHECK(world.body(c) == nullptr);
    CHECK(world.joint(1) == nullptr);
    CHECK(world.slotOf(static_cast<const b2Body*>(nullptr)) == -1);
    const std::vector<std::string> lines = rec.lines();
    CHECK(countPrefix(lines, "revolute 0 1 ") == 1);
    CHECK(countPrefix(lines, "distance 1 2 ") == 1);
    CHECK(countPrefix(lines, "destroyjoint 0") == 1);
    CHECK(countPrefix(lines, "destroybody 2") == 1);
    CHECK(countPrefix(lines, "destroyjoint 1") == 0);
    // A new body after the destruction takes the next slot, not the freed one.
    CHECK(world.createBody(bd) == 3);
}

TEST_CASE("physics world: in-place polygon edits are reported once, at the end, per fixture") {
    SceneRecorder rec;
    PhysicsWorld world(&rec);
    b2BodyDef bd;
    const int body = world.createBody(bd);
    const b2FixtureDef fd = fixtureDef(0.0f, 0.2f, 0.0f, filters::kStatic);
    world.addBox(body, 0.1f, 0.1f, fd);
    b2Fixture* second = world.addBox(body, 0.2f, 0.2f, fd);
    world.resetBoxAt(body, second, 0.3f, 0.1f, Vec2(0.0f, 0.0f), 0.0f);
    world.resetBox(body, second, 0.4f, 0.1f);
    rec.comment("after");
    const std::vector<std::string> lines = rec.lines();
    REQUIRE(countPrefix(lines, "polygonfix ") == 1);
    CHECK(lines.back().rfind("polygonfix 0 1 4 ", 0) == 0);   // fixture 1 in creation order, final shape
    CHECK(lines[lines.size() - 2] == "# after");
    // A destroyed body drops its pending line.
    world.destroyBody(body);
    CHECK(countPrefix(rec.lines(), "polygonfix ") == 0);
}

TEST_CASE("rope: the end points move to the two extreme link bodies; link count follows the length") {
    const TemplateTable templates = plainTemplates();
    WorldState state;
    const int i = state.addItemWithHandle(templates, Handle::make(ItemType::Rope, 0, 1), Vec2(1.0f, 1.0f), 0.0f);
    GameItem& item = state.items[static_cast<std::size_t>(i)];
    item.endVector = Vec2(0.6f, 0.0f);
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
    PhysicsWorld world;
    createPhysics(obj, item, world, PhysicsMode::Simulation);
    CHECK(obj.attachments[0].point.body == 1);
    CHECK(obj.attachments[1].point.body == 2);
    CHECK(items::ropeLinkCount(Vec2(0.6f, 0.0f)) == 9);
    CHECK(obj.bodyCount == 10);
    CHECK(obj.jointCount == 8);   // N − 1 distance joints
    CHECK(items::ropeLinkCount(Vec2(0.0f, 0.0f)) == 2);
    CHECK(items::ropeLinkCount(Vec2(5.0f, 0.0f)) == 15);
}

TEST_CASE("attachments on load: a rope hanging from a hook is rebuilt to the attached length") {
    // Hook at (1, 1.5), rope from (1, 1.5) to (1, 0.9) attached at its end A; a longer end B attachment
    // moves the rope end and changes the link count, which rebuilds the middle links.
    const TemplateTable templates = plainTemplates();
    Level level;
    LevelItem bound;
    bound.type = ItemType::WorldBound;
    bound.handle = Handle::make(ItemType::WorldBound, 0, 0);
    level.items.push_back(bound);
    LevelItem hook;
    hook.type = ItemType::Hook;
    hook.handle = Handle::make(ItemType::Hook, 0, 1);
    hook.center = Vec2(1.0f, 1.5f);
    hook.attachmentCount = 1;
    hook.attachments[0] = {attachment_state::kAttached, 2, 0};
    level.items.push_back(hook);
    LevelItem rope;
    rope.type = ItemType::Rope;
    rope.handle = Handle::make(ItemType::Rope, 0, 2);
    rope.center = Vec2(1.2f, 1.5f);
    rope.ropeEnd = Vec2(0.0f, -0.6f);
    rope.attachmentCount = 2;
    rope.attachments[0] = {attachment_state::kAttached, 1, 0};
    level.items.push_back(rope);
    SceneRecorder rec;
    Session session(templates);
    session.load(level, GameMode::Campaign, &rec);
    const PhysicsObject& ropeObj = session.state().objects[2];
    CHECK(ropeObj.attachments[0].joint >= 0);
    CHECK(session.state().objects[1].attachments[0].joint == ropeObj.attachments[0].joint);
    // End A followed the hook's point (0, −0.03 below the hook).
    CHECK(ropeObj.position.x == doctest::Approx(1.0f));
    CHECK(ropeObj.position.y == doctest::Approx(1.47f));
    const std::vector<std::string> lines = rec.lines();
    CHECK(countPrefix(lines, "revolute ") == 1);
    CHECK(countPrefix(lines, "destroybody ") == 0);   // same length, no rebuild
}
