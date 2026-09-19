// sim_scene_dump — G3 conformance tool (docs/10-architecture.md §8): builds a scene through aa_sim with
// the SceneRecorder attached and writes it in the `.scene` format of tools/uc_trace.py, so
// tools/sim_conformance.py can diff it textually against the scene recorded from the emulated original.
//
//   sim_scene_dump --frames <GameItems.json> --type T --mode simulation|setup [--flip] [--background N]
//                  [--state K] [--x 1.7] [--y 1.0] <out.scene>
//   sim_scene_dump --frames <GameItems.json> --level <level.json> --mode M <out.scene>
//   sim_scene_dump --list-implemented          (prints the implemented item type ids, one per line)
//
// Like the harness's drop scene, the world bound (background 0) is built first as body 0, then the item;
// a level is built the way Session::load does (applyLayout + createWorldPhysics + attachments). No `step` line is
// written: construction only. Exit codes: 0 ok, 2 usage, 3 unimplemented item type, 1 other error.
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/sim/attachments.h"
#include "aa/sim/items/items.h"
#include "aa/sim/session.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

using namespace aa::sim;

struct Options {
    std::string frames;
    std::string level;
    std::string out;
    int type = 0;
    PhysicsMode mode = PhysicsMode::Simulation;
    bool flip = false;
    int background = 0;
    int state = -1;
    float x = 1.7f;
    float y = 1.0f;
};

void usage() {
    std::fprintf(stderr,
                 "usage: sim_scene_dump --frames GameItems.json (--type T | --level L.json) --mode simulation|setup\n"
                 "       [--flip] [--background N] [--state K] [--x X] [--y Y] out.scene\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--frames") { if (!value(o.frames)) return false; }
        else if (a == "--level") { if (!value(o.level)) return false; }
        else if (a == "--type") { if (!value(v)) return false; o.type = std::atoi(v.c_str()); }
        else if (a == "--mode") {
            if (!value(v)) return false;
            if (v == "simulation") o.mode = PhysicsMode::Simulation;
            else if (v == "setup") o.mode = PhysicsMode::SetUp;
            else return false;
        }
        else if (a == "--flip") o.flip = true;
        else if (a == "--background") { if (!value(v)) return false; o.background = std::atoi(v.c_str()); }
        else if (a == "--state") { if (!value(v)) return false; o.state = std::atoi(v.c_str()); }
        else if (a == "--x") { if (!value(v)) return false; o.x = std::strtof(v.c_str(), nullptr); }
        else if (a == "--y") { if (!value(v)) return false; o.y = std::strtof(v.c_str(), nullptr); }
        else if (!a.empty() && a[0] == '-') return false;
        else o.out = a;
    }
    if (o.type != 0 && !isValidItemType(o.type)) {
        std::fprintf(stderr, "sim_scene_dump: --type %d is not an item type (1..42)\n", o.type);
        return false;
    }
    return !o.frames.empty() && !o.out.empty() && (o.type != 0) != (!o.level.empty());
}

// The harness's drop scene: world bound (state 0) at the origin, then one item at (x, y).
void dropScene(const Options& o, const TemplateTable& templates, PhysicsWorld& world, SceneRecorder& rec) {
    WorldState state;
    const int bound = state.addItemWithHandle(templates, Handle::make(ItemType::WorldBound, 0, 0), Vec2(0.0f, 0.0f), 0.0f);
    state.items[static_cast<std::size_t>(bound)].stateWord = 0;
    const ItemType type = static_cast<ItemType>(o.type);
    const int i = state.addItemWithHandle(templates, Handle::make(type, 0, 1), Vec2(o.x, o.y), 0.0f);
    GameItem& item = state.items[static_cast<std::size_t>(i)];
    PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
    if (o.flip) obj.scale.x = -obj.scale.x;
    if (type == ItemType::WorldBound) item.stateWord = o.background == kTreehouseBackground ? 1 : 0;
    else if (o.state >= 0) item.stateWord = o.state;
    else if (type == ItemType::Book) item.stateWord = 0;
    // The harness's default end vectors (uc_trace.drop_scene): rope end, slingshot pouch, zip-line end.
    if (type == ItemType::Rope) item.endVector = Vec2(0.6f, 0.0f);
    else if (type == ItemType::Slingshot) item.endVector = Vec2(0.0f, 0.5f);
    else if (type == ItemType::ZipLine) item.endVector = Vec2(0.8f, -0.3f);
    for (PhysicsObject& po : state.objects) {
        const GameItem& gi = state.items[static_cast<std::size_t>(state.handles.lookup(po.handle))];
        const int first = world.bodyCount();
        createPhysics(po, gi, world, o.mode);
        rec.comment("item " + std::to_string(static_cast<int>(po.type)) + " " + itemTypeName(po.type) + " bodies " +
                    std::to_string(first) + ".." + std::to_string(world.bodyCount() - 1));
    }
}

// A level the way Session::load builds it (applyLayout + createWorldPhysics), with the harness's per-item
// body comments in between.
void levelScene(const Options& o, const TemplateTable& templates, PhysicsWorld& world, SceneRecorder& rec) {
    const Level level = aa::data::loadLevelFile(o.level);
    WorldState state;
    applyLayout(level, templates, state);
    for (PhysicsObject& po : state.objects) {
        const GameItem& gi = state.items[static_cast<std::size_t>(state.handles.lookup(po.handle))];
        const int first = world.bodyCount();
        createPhysics(po, gi, world, o.mode);
        rec.comment("item " + std::to_string(static_cast<int>(po.type)) + " " + itemTypeName(po.type) + " bodies " +
                    std::to_string(first) + ".." + std::to_string(world.bodyCount() - 1));
    }
    createAttachments(state, world);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--list-implemented") == 0) {
        for (int t = kFirstItemType; t <= kLastItemType; ++t) {
            if (isItemImplemented(static_cast<ItemType>(t))) std::printf("%d %s\n", t, itemTypeName(static_cast<ItemType>(t)));
        }
        return 0;
    }
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 2;
    }
    try {
        const FrameTable frames = aa::data::loadFrameTableFile(o.frames);
        const TemplateTable templates = initTemplates(frames);
        SceneRecorder rec;
        rec.comment("amazing-alex physics scene v1 (aa_sim)");
        PhysicsWorld world(&rec);
        if (!o.level.empty()) levelScene(o, templates, world, rec);
        else dropScene(o, templates, world, rec);
        std::ofstream out(o.out, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "cannot write %s\n", o.out.c_str());
            return 1;
        }
        out << rec.text();
        return 0;
    } catch (const NotImplemented& ex) {
        std::fprintf(stderr, "%s\n", ex.what());
        return 3;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
