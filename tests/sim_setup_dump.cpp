// sim_setup_dump — G5a conformance tool (docs/10-architecture.md §8): runs a set-up interaction script
// (the format of tools/uc_setup_oracle.py) on aa_sim and writes the same `.setup` dump, so
// tools/setup_conformance.py can diff it against the dump recorded from the emulated original.
//
//   sim_setup_dump --frames <GameItems.json> --levels <assets/levels> --script <S.txt> <out.setup>
//
// The level is built the way Session::load does (applyLayout + createPhysics per object + createAttachments)
// in set-up mode under the set-up contact listener; every command then calls the ported function directly
// (setItemPos, updateItemPos, updateItemAngle, flipItem, manipulationStarted / Ended, calculateSnap, detach,
// unsnapAllNotAttached, attachToNearbyItems, collideOnly, isColliding[WithAnother]) and dumps every object.
// Exit codes: 0 ok, 2 usage, 1 other error.
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/sim/attachments.h"
#include "aa/sim/interaction.h"
#include "aa/sim/items/items.h"
#include "aa/sim/scene_recorder.h"
#include "aa/sim/session.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace aa::sim;

struct Options {
    std::string frames;
    std::string levels;
    std::string script;
    std::string out;
};

void usage() {
    std::fprintf(stderr, "usage: sim_setup_dump --frames GameItems.json --levels assets/levels --script S.txt out.setup\n");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        if (a == "--frames") { if (!value(o.frames)) return false; }
        else if (a == "--levels") { if (!value(o.levels)) return false; }
        else if (a == "--script") { if (!value(o.script)) return false; }
        else if (!a.empty() && a[0] == '-') return false;
        else o.out = a;
    }
    return !o.frames.empty() && !o.levels.empty() && !o.script.empty() && !o.out.empty();
}

struct Script {
    std::string level;
    std::vector<std::string> commands;
};

Script readScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read script " + path);
    Script s;
    std::string raw;
    while (std::getline(in, raw)) {
        std::string line = raw.substr(0, raw.find('#'));
        const std::size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos) continue;
        const std::size_t e = line.find_last_not_of(" \t\r");
        line = line.substr(b, e - b + 1);
        if (line.rfind("level ", 0) == 0) s.level = line.substr(6);
        else s.commands.push_back(line);
    }
    if (s.level.empty()) throw std::runtime_error(path + ": no `level` line");
    return s;
}

struct Runner {
    const TemplateTable& templates;
    SceneRecorder rec;
    PhysicsWorld world{&rec};
    WorldState state;
    ActionQueue queue;
    std::vector<std::string> out;
    std::size_t recorded = 0;   // recorder lines already copied into `out`

    explicit Runner(const TemplateTable& t) : templates(t) {}

    // The comparable recorder lines: no polygonfix lines (the oracle emits those only at the end of a
    // trajectory run) and no comments.
    void copyRecorder() {
        // lines() appends the pending polygonfix lines after the recorded ones: count only the latter.
        std::vector<std::string> lines;
        for (const std::string& l : rec.lines()) {
            if (l.rfind("polygonfix", 0) != 0) lines.push_back(l);
        }
        for (std::size_t i = recorded; i < lines.size(); ++i) {
            if (!lines[i].empty() && lines[i][0] != '#') out.push_back(lines[i]);
        }
        recorded = lines.size();
    }

    void build(const Level& level) {
        applyLayout(level, templates, state);
        world.installContactListener(PhysicsMode::SetUp);
        for (PhysicsObject& po : state.objects) {
            const GameItem& gi = state.itemOf(po);
            const int first = world.bodyCount();
            createPhysics(po, gi, world, PhysicsMode::SetUp);
            rec.comment("item " + std::to_string(static_cast<int>(po.type)) + " " + itemTypeName(po.type) + " bodies " +
                        std::to_string(first) + ".." + std::to_string(world.bodyCount() - 1));
        }
        createAttachments(state, world);
        copyRecorder();
    }

    void dumpState() {
        for (std::size_t i = 0; i < state.objects.size(); ++i) {
            const PhysicsObject& o = state.objects[i];
            out.push_back("obj " + std::to_string(i) + " " + std::to_string(static_cast<int>(o.type)) + " " +
                          SceneRecorder::bits(o.position.x) + " " + SceneRecorder::bits(o.position.y) + " " +
                          SceneRecorder::bits(o.angle) + " " + SceneRecorder::bits(o.scale.x) + " " +
                          std::to_string(static_cast<int>(o.state)) + " " + std::to_string(o.bodyCount));
            for (int k = 0; k < o.attachmentCount; ++k) {
                const AttachmentRecord& r = o.attachments[static_cast<std::size_t>(k)];
                out.push_back("att " + std::to_string(i) + " " + std::to_string(k) + " " + std::to_string(r.state) + " " +
                              std::to_string(r.otherObject) + " " + std::to_string(r.otherPoint) + " " +
                              (r.joint >= 0 ? "1" : "0"));
            }
            if (o.type == ItemType::Rope || o.type == ItemType::ZipLine || o.type == ItemType::Slingshot) {
                const GameItem& item = state.itemOf(o);
                out.push_back("item " + std::to_string(i) + " " + SceneRecorder::bits(item.endVector.x) + " " +
                              SceneRecorder::bits(item.endVector.y));
            }
        }
    }

    static float f(const std::vector<std::string>& a, std::size_t i) { return std::strtof(a[i].c_str(), nullptr); }
    static int n(const std::vector<std::string>& a, std::size_t i) { return std::atoi(a[i].c_str()); }

    void run(const std::string& line) {
        std::istringstream in(line);
        std::vector<std::string> a;
        for (std::string w; in >> w;) a.push_back(w);
        const std::string& cmd = a[0];
        queue.clear();
        out.push_back("> " + line);
        std::vector<std::string> extra;
        if (cmd == "setpos") {
            setItemPos(state, world, n(a, 1), Vec2(f(a, 2), f(a, 3)));
        } else if (cmd == "updatepos") {
            TouchState t;
            t.state = touch_state::kDragging;
            t.selectedObject = n(a, 1);
            t.bodyIndex = n(a, 2);
            t.targetPos = Vec2(f(a, 3), f(a, 4));
            t.angleCurrent = a.size() > 5 ? f(a, 5) : 0.0f;
            t.dragVelocity = a.size() > 7 ? Vec2(f(a, 6), f(a, 7)) : Vec2(0.0f, 0.0f);
            updateItemPos(state, world, t.selectedObject, t, true, queue);
        } else if (cmd == "updateangle") {
            updateItemAngle(state, world, n(a, 1), f(a, 2));
        } else if (cmd == "flip") {
            flipItem(state, world, n(a, 1), queue);
        } else if (cmd == "started") {
            manipulationStarted(state, world, n(a, 1), n(a, 2));
        } else if (cmd == "ended") {
            manipulationEnded(state, world, templates, n(a, 1));
        } else if (cmd == "snap") {
            const SnapResult r = calculateSnap(state, world, state.objects[static_cast<std::size_t>(n(a, 1))],
                                               Vec2(f(a, 2), f(a, 3)), Vec2(f(a, 4), f(a, 5)), f(a, 6));
            extra.push_back("snap " + std::string(r.found ? "1" : "0") + " " + SceneRecorder::bits(r.position.x) + " " +
                            SceneRecorder::bits(r.position.y) + " " + SceneRecorder::bits(r.angle) + " " +
                            std::to_string(r.found ? r.point : -1) + " " + std::to_string(r.found ? r.otherObject : -1) + " " +
                            std::to_string(r.found ? r.otherPoint : -1));
        } else if (cmd == "detach") {
            detach(state, world, n(a, 1), n(a, 2));
        } else if (cmd == "unsnapall") {
            unsnapAllNotAttached(state, n(a, 1));
        } else if (cmd == "attachnearby") {
            attachToNearbyItems(state, world, n(a, 1));
        } else if (cmd == "step0") {
            world.collideOnly();
        } else if (cmd == "colliding") {
            extra.push_back(std::string("colliding ") + (world.isColliding(state.objects[static_cast<std::size_t>(n(a, 1))]) ? "1" : "0"));
        } else if (cmd == "collidingwith") {
            const bool hit = world.isCollidingWithAnother(state.objects[static_cast<std::size_t>(n(a, 1))],
                                                          state.objects[static_cast<std::size_t>(n(a, 2))]);
            extra.push_back(std::string("colliding ") + (hit ? "1" : "0"));
        } else {
            throw std::runtime_error("unknown setup command: " + line);
        }
        dumpState();
        copyRecorder();
        for (const std::string& e : extra) out.push_back(e);
        for (const Action& act : queue.actions) {
            const int item = state.handles.lookup(act.handle);
            const int object = item >= 0 ? state.items[static_cast<std::size_t>(item)].objectIndex : -1;
            out.push_back("action " + std::to_string(act.id) + " " + std::to_string(object));
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 2;
    }
    try {
        const FrameTable frames = aa::data::loadFrameTableFile(o.frames);
        const TemplateTable templates = initTemplates(frames);
        const Script script = readScript(o.script);
        const Level level = aa::data::loadLevelFile(o.levels + "/" + script.level + ".json");
        Runner runner(templates);
        runner.rec.comment("amazing-alex physics scene v1 (aa_sim)");
        runner.build(level);
        for (const std::string& cmd : script.commands) runner.run(cmd);
        std::ofstream outFile(o.out, std::ios::binary);
        if (!outFile) {
            std::fprintf(stderr, "cannot write %s\n", o.out.c_str());
            return 1;
        }
        outFile << "# amazing-alex setup trace v1 level=" << script.level << " commands=" << script.commands.size() << "\n";
        for (const std::string& l : runner.out) outFile << l << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
