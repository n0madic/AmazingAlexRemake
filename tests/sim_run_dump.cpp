// sim_run_dump — G4 / G6 conformance tool (docs/10-architecture.md §8): plays a level on aa_sim the way the
// viewer does (Session::load → play → advance(1/60) × N) and writes the three dumps of
// tools/uc_sim_oracle.py — the simulation world's construction (`.scene`), the body trajectory per
// substep (`.traj`) and the per-frame state / action trace (`.sim`) — so tools/sim_run_conformance.py
// can diff them against the emulated original.
//
//   sim_run_dump --frames <GameItems.json> --levels <assets/levels> --script <S.txt> <out-prefix>
//
// Exit codes: 0 ok, 2 usage, 1 other error.
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/sim/float_bits.h"
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

constexpr float kFrameDt = 1.0f / 60.0f;

struct Options {
    std::string frames;
    std::string levels;
    std::string script;
    std::string out;
};

void usage() {
    std::fprintf(stderr, "usage: sim_run_dump --frames GameItems.json --levels assets/levels --script S.txt out-prefix\n");
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
    std::vector<std::vector<std::string>> commands;
};

Script readScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read script " + path);
    Script s;
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = raw.substr(0, raw.find('#'));
        std::istringstream words(line);
        std::vector<std::string> a;
        for (std::string w; words >> w;) a.push_back(w);
        if (a.empty()) continue;
        if (a[0] == "level" && a.size() > 1) s.level = a[1];
        else s.commands.push_back(a);
    }
    if (s.level.empty()) throw std::runtime_error(path + ": no `level` line");
    return s;
}

std::string hex(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

// The per-type item fields of tools/uc_sim_oracle.py ITEM_FIELDS, in its order ("name=value").
std::string itemFields(const GameItem& item) {
    const auto b = [](bool v) { return std::string(v ? "1" : "0"); };
    const auto f = [](float v) { return SceneRecorder::bits(v); };
    const auto i = [](int v) { return std::to_string(v); };
    switch (item.type) {
    case ItemType::Balloon:
        return "popped=" + b(item.popped) + " t=" + f(item.popTimer);
    case ItemType::Scissors:
        return "state=" + i(item.scissorsState) + " cut=" + f(item.cutAngle) + " step=" + i(item.snipStep) + " timer=" +
               f(item.snipTimer) + " snipping=" + b(item.snipping) + " angle=" + f(item.snipAngle) + " phase=" +
               f(item.snipPhase) + " dir=" + f(item.snipDirection);
    case ItemType::PiggyBank:
        return "t=" + f(item.piggyTimer);
    case ItemType::BoxingGlove:
        return "state=" + i(item.gloveState) + " button=" + f(item.gloveButton.value) + " lattice=" + f(item.latticeAngle);
    case ItemType::GoalStar:
        return "state=" + i(item.starState) + " t=" + f(item.starTimer);
    case ItemType::Magnet:
        return "pulling=" + b(item.magnetPulling) + " min=" + f(item.magnetMinDist2) + " timer=" + f(item.magnetTimer) +
               " frame=" + i(item.magnetFrame);
    case ItemType::Dart:
        return "wobbling=" + b(item.wobbling) + " stuck=" + b(item.stuck) + " angle=" + f(item.wobbleAngle) + " phase=" +
               f(item.wobblePhase) + " dir=" + f(item.wobbleDirection) + " timer=" + f(item.wobbleTimer);
    case ItemType::Bumper:
        return "on=" + i(item.bumperOn) + " t=" + f(item.bumperTimer);
    case ItemType::Slingshot:
        return "fired=" + b(item.fired) + " pouchx=" + f(item.endVector.x) + " pouchy=" + f(item.endVector.y) + " velx=" +
               f(item.pouchVelocity.x) + " vely=" + f(item.pouchVelocity.y) + " loaded=" + i(item.loadedHandle);
    case ItemType::Seesaw:
        return "dir=" + i(item.seesawDirection);
    case ItemType::RCController:
        return "pressed=" + b(item.buttonPressed);
    case ItemType::TrapdoorLever:
        return "unlocked=" + b(item.leverUnlocked) + " sounded=" + b(item.leverSounded);
    case ItemType::Helicopter:
        return "on=" + b(item.heliOn) + " throttle=" + f(item.heliThrottle) + " rotor=" + f(item.rotorSpeed) + " rotorphase=" +
               f(item.rotorPhase) + " tail=" + f(item.tailSpeed) + " tailphase=" + f(item.tailPhase);
    default:
        return "";
    }
}

std::string g9(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
    return buf;
}

struct Runner {
    Session& session;
    std::vector<std::string> traj;
    std::vector<std::string> sim;
    int frame = 0;
    std::vector<int> handlesBefore;

    explicit Runner(Session& s) : session(s) {
        session.setSubstepObserver([this](int substep) { dumpBodies(substep); });
    }

    void dumpBodies(int substep) {
        const PhysicsWorld* world = session.world();
        for (int slot = 0; slot < world->bodyCount(); ++slot) {
            const b2Body* b = world->body(slot);
            if (b == nullptr) continue;
            const b2Vec2 p = b->GetPosition();
            const b2Vec2 v = b->GetLinearVelocity();
            traj.push_back(std::to_string(substep) + " " + std::to_string(slot) + " " + g9(p.x) + " " + g9(p.y) + " " +
                           g9(b->GetAngle()) + " " + g9(v.x) + " " + g9(v.y) + " " + g9(b->GetAngularVelocity()) + " " +
                           (b->IsAwake() ? "1" : "0"));
        }
    }

    void runFrame() {
        const WorldState& before = session.state();
        handlesBefore.clear();
        for (const PhysicsObject& o : before.objects) handlesBefore.push_back(o.handle);
        ++frame;
        session.advance(kFrameDt);
        for (const Session::ProcessedAction& p : session.drainProcessedActions()) {
            const Action& a = p.action;
            sim.push_back("action " + std::to_string(frame) + " " + (p.substep < 0 ? std::string("-") : std::to_string(p.substep)) +
                          " " + std::to_string(a.id) + " " + hex(static_cast<std::uint32_t>(a.handle)) + " " +
                          hex(bitsFromFloat(a.position.x)) + " " + hex(bitsFromFloat(a.position.y)) + " " +
                          hex(static_cast<std::uint32_t>(a.payload)) + " " + hex(bitsFromFloat(a.value)) + " " +
                          hex(static_cast<std::uint32_t>(a.extra)) + " " + hex(bitsFromFloat(a.amount)));
        }
        dumpFrame();
    }

    void dumpFrame() {
        const WorldState& st = session.state();
        const GoalState& gs = session.goalState();
        if (session.controllerState() == 2) {
            // The run stopped in this frame's tail (the 5 s no-motion rule): the set-up world is back.
            sim.push_back("frame " + std::to_string(frame) + " stopped");
            return;
        }
        sim.push_back("frame " + std::to_string(frame) + " " + SceneRecorder::bits(session.accumulator()) + " " +
                      SceneRecorder::bits(session.playTime()) + " " + (gs.reached ? "1" : "0") + " " +
                      std::to_string(gs.collectedStars) + " " + std::to_string(session.controllerState()));
        sim.push_back("random " + hex(session.random().seed));
        for (int h : handlesBefore) {
            bool live = false;
            for (const PhysicsObject& o : st.objects) live = live || o.handle == h;
            if (!live) sim.push_back("removed " + hex(static_cast<std::uint32_t>(h)));
        }
        for (std::size_t i = 0; i < st.objects.size(); ++i) {
            const PhysicsObject& o = st.objects[i];
            sim.push_back("obj " + std::to_string(i) + " " + std::to_string(static_cast<int>(o.type)) + " " +
                          hex(static_cast<std::uint32_t>(o.handle)) + " " + SceneRecorder::bits(o.position.x) + " " +
                          SceneRecorder::bits(o.position.y) + " " + SceneRecorder::bits(o.angle) + " " +
                          std::to_string(static_cast<int>(o.flags)) + " " + std::to_string(static_cast<int>(o.state)) + " " +
                          std::to_string(o.bodyCount));
            if (o.isDynamic()) {
                const RenderPose pose = session.renderPose(static_cast<int>(i));
                sim.push_back("lerp " + std::to_string(i) + " " + SceneRecorder::bits(pose.position.x) + " " +
                              SceneRecorder::bits(pose.position.y) + " " + SceneRecorder::bits(pose.angle));
            }
        }
        for (const GameItem& item : st.items) {
            const std::string fields = itemFields(item);
            if (!fields.empty()) {
                sim.push_back("item " + std::to_string(static_cast<int>(item.type)) + " " + hex(static_cast<std::uint32_t>(item.handle)) +
                              " " + fields);
            }
        }
        std::string goal = "goalstate " + hex(gs.reached ? 1u : 0u) + " " + hex(static_cast<std::uint32_t>(gs.collectedStars));
        for (float t : gs.contactTime) goal += " " + hex(bitsFromFloat(t));
        sim.push_back(goal);
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
        Session session(templates);
        session.load(level);
        SceneRecorder rec;
        rec.comment("amazing-alex physics scene v1 sim (aa_sim)");
        session.play(&rec);
        // The construction scene (the recorder keeps recording the run-time destruction afterwards).
        const std::string sceneText = rec.text();
        Runner runner(session);
        // The trajectory names its items like the harness does ("# item <type> <name> bodies a..b").
        std::vector<std::string> itemLines;
        for (const PhysicsObject& obj : session.state().objects) {
            if (obj.bodyCount == 0) continue;
            itemLines.push_back("# item " + std::to_string(static_cast<int>(obj.type)) + " " + itemTypeName(obj.type) + " bodies " +
                                std::to_string(obj.bodies[0]) + ".." +
                                std::to_string(obj.bodies[static_cast<std::size_t>(obj.bodyCount - 1)]));
        }
        int frames_ = 0;
        bool stopped = false;
        for (const std::vector<std::string>& cmd : script.commands) {
            if (cmd[0] == "frames") {
                const int n = std::atoi(cmd[1].c_str());
                for (int i = 0; i < n; ++i) {
                    if (session.controllerState() == 2 || session.stopRequested()) {
                        stopped = true;
                        break;
                    }
                    runner.runFrame();
                    ++frames_;
                }
            } else if (cmd[0] == "tap") {
                session.pointerDown(0, Vec2(10.0f, 10.0f));
                session.pointerUp(0, Vec2(10.0f, 10.0f));
            } else if (cmd[0] == "goal") {
                // The oracle's `goal`: action 11 queued as IsGoalComplete would (the completion path).
                session.queueAction(Action(action::kGoalComplete, 0));
            } else if (cmd[0] == "listener") {
                // The plumbing check of tools/uc_sim_oracle.py: no contact listener on either side.
                if (cmd.size() > 1 && cmd[1] == "off") session.world()->world().SetContactListener(nullptr);
            } else if (cmd[0] == "seed") {
                // Oracle-side switch (the core's Random starts at the same seed 1).
            } else {
                throw std::runtime_error("unknown sim command: " + cmd[0]);
            }
        }
        std::ofstream scene(o.out + ".scene", std::ios::binary);
        std::ofstream traj(o.out + ".core.traj", std::ios::binary);
        std::ofstream sim(o.out + ".sim", std::ios::binary);
        if (!scene || !traj || !sim) {
            std::fprintf(stderr, "cannot write %s.*\n", o.out.c_str());
            return 1;
        }
        scene << sceneText;
        scene << "step " << session.substepCount() << " " << SceneRecorder::bits(kSimulationStep) << " " << kVelocityIterations
              << " " << kPositionIterations << "\n";
        traj << "# amazing-alex trajectory v1 bodies=" << session.world()->bodyCount() << " steps=" << session.substepCount() << "\n";
        for (const std::string& l : itemLines) traj << l << "\n";
        for (const std::string& l : runner.traj) traj << l << "\n";
        sim << "# amazing-alex sim trace v1 level=" << script.level << " frames=" << frames_ << " substeps=" << session.substepCount()
            << " stopped=" << (stopped ? 1 : 0) << "\n";
        for (const std::string& l : runner.sim) sim << l << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
}
