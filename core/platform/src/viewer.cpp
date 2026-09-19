#include "aa/platform/viewer.h"

#include "aa/data/asset_root.h"
#include "aa/ui/app_state.h"
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/platform/atlas.h"
#include "aa/platform/audio.h"
#include "aa/platform/platform.h"
#include "aa/platform/world_renderer.h"
#include "aa/sim/screen_layout.h"
#include "aa/sim/session.h"
#include "aa/sim/toolbox.h"

#include <raylib.h>
#include <rlgl.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace aa::platform {

namespace {

// The original's clear colour (0.85, 0.8, 0.8).
const Color kClearColour = {217, 204, 204, 255};
constexpr float kPanStepPx = 8.0f;        // per frame while a WASD key is down
constexpr float kZoomStep = 1.05f;
constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 4.0f;
constexpr float kWheelRotateStep = 3.14159265f / 36.0f;   // 5° per wheel notch while holding an item
constexpr int kHudFontSize = 18;
constexpr int kHudMargin = 12;
constexpr int kPointerId = 0;
constexpr float kScriptDt = 1.0f / 60.0f;

struct LevelRef {
    std::string chapter;
    std::string name;
};

std::vector<LevelRef> listLevels(const aa::data::AssetRoot& root) {
    std::vector<LevelRef> refs;
    for (const std::string& chapter : root.chapters()) {
        const aa::data::LevelIndex index = aa::data::loadLevelIndex(root.json("levels/" + chapter + "/index.json").root());
        for (const std::string& name : index.levels) refs.push_back({chapter, name});
        for (const std::string& name : index.unlisted) refs.push_back({chapter, name});   // the 4 Treehouse extras
    }
    return refs;
}

int findLevel(const std::vector<LevelRef>& refs, const std::string& ref) {
    if (ref.empty()) return 0;
    const std::size_t slash = ref.find('/');
    if (slash == std::string::npos) throw std::runtime_error("--level expects <chapter>/<name>");
    const std::string chapter = ref.substr(0, slash);
    const std::string name = ref.substr(slash + 1);
    for (std::size_t i = 0; i < refs.size(); ++i) {
        if (refs[i].chapter == chapter && refs[i].name == name) return static_cast<int>(i);
    }
    throw std::runtime_error("level not found: " + ref);
}

const char* touchStateName(int state) {
    switch (state) {
    case aa::sim::touch_state::kIdle: return "idle";
    case aa::sim::touch_state::kPending: return "pending";
    case aa::sim::touch_state::kDragging: return "drag";
    case aa::sim::touch_state::kRingRotate: return "ring";
    case aa::sim::touch_state::kFlipping: return "flip";
    case aa::sim::touch_state::kFromToolbox: return "from-toolbox";
    case aa::sim::touch_state::kReturning: return "returning";
    case aa::sim::touch_state::kBuzz: return "buzz";
    case aa::sim::touch_state::kPan: return "pan";
    case aa::sim::touch_state::kToolboxTouch: return "toolbox";
    case aa::sim::touch_state::kToolboxButton: return "toolbox-button";
    case aa::sim::touch_state::kToolboxScroll: return "toolbox-scroll";
    default: return "?";
    }
}

class Viewer {
public:
    Viewer(const aa::data::AssetRoot& root, const aa::sim::TemplateTable& templates, const aa::sim::ToolboxFrameSizes& sizes)
        : root_(root), session_(templates), levels_(listLevels(root)) {
        session_.setToolboxFrameSizes(sizes);
    }

    const std::vector<LevelRef>& levels() const { return levels_; }

    void load(int index) {
        index_ = index;
        const LevelRef& ref = levels_[static_cast<std::size_t>(index)];
        session_.load(aa::data::loadLevel(root_.json("levels/" + ref.chapter + "/" + ref.name + ".json").root()));
        if (showMarkers_) session_.visual().revealGoalMarkers();
    }

    std::string title() const {
        const LevelRef& ref = levels_[static_cast<std::size_t>(index_)];
        return ref.chapter + "/" + ref.name + " (" + std::to_string(index_ + 1) + "/" + std::to_string(levels_.size()) + ")";
    }

    void setViewport(int width, int height) {
        session_.setViewport(aa::sim::ScreenLayout::compute(width, height, aa::ui::AppState::profilePixelScaleOf(root_)));
    }

    // Pointer events in the window's native px (y down), as the original's touch layer delivers them.
    void pointerDown(aa::sim::Vec2 px) {
        pointer_ = px;
        session_.pointerDown(kPointerId, px);
        pointerHeld_ = true;
    }
    void pointerMove(aa::sim::Vec2 px) {
        pointer_ = px;
        if (pointerHeld_) session_.pointerMove(kPointerId, px);
    }
    void pointerUp(aa::sim::Vec2 px) {
        pointer_ = px;
        session_.pointerUp(kPointerId, px);
        pointerHeld_ = false;
    }
    void rotateHeld(float delta) {
        // rotateHeld only records the angle; re-sending the pointer makes the next UpdatePos apply it.
        session_.rotateHeld(delta);
        if (pointerHeld_) session_.pointerMove(kPointerId, pointer_);
    }

    void handleInput() {
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_PAGE_DOWN)) load((index_ + 1) % static_cast<int>(levels_.size()));
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_PAGE_UP)) {
            load((index_ + static_cast<int>(levels_.size()) - 1) % static_cast<int>(levels_.size()));
        }
        if (IsKeyPressed(KEY_R)) {
            // The pause menu's restart: the held item is released first (GameScene::SetPaused).
            session_.pause();
            session_.restart();
        }
        if (IsKeyPressed(KEY_S)) {
            session_.rebuildWorld(session_.physicsMode() == aa::sim::PhysicsMode::SetUp ? aa::sim::PhysicsMode::Simulation
                                                                                        : aa::sim::PhysicsMode::SetUp);
        }
        if (IsKeyPressed(KEY_M)) {
            showMarkers_ = !showMarkers_;
            if (showMarkers_) session_.visual().revealGoalMarkers();
            else session_.visual().hideGoalMarkers();
        }
        if (IsKeyPressed(KEY_Z)) session_.undo();
        if (IsKeyPressed(KEY_Y)) session_.redo();
        if (IsKeyPressed(KEY_SPACE)) {
            if (session_.physicsMode() == aa::sim::PhysicsMode::SetUp) session_.play();
            else session_.stop();
        }
        if (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE)) session_.returnHeld();
        if (IsKeyPressed(KEY_T)) session_.toggleToolbox();
        if (IsKeyPressed(KEY_P)) session_.pause();
        if (IsKeyPressed(KEY_F)) session_.flipHeld();

        // The mouse is the one finger: down / move / up in native px; empty-space drags pan through the
        // original's state 0xB, the strip scrolls and takes items through its own states.
        const Vector2 m = GetMousePosition();
        const aa::sim::Vec2 px(m.x, m.y);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) pointerDown(px);
        else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) pointerUp(px);
        else if (pointerHeld_ && (m.x != pointer_.x || m.y != pointer_.y)) pointerMove(px);
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            if (session_.heldObject() >= 0) rotateHeld(wheel > 0.0f ? kWheelRotateStep : -kWheelRotateStep);
            else {
                float zoom = session_.camera().zoom * (wheel > 0.0f ? kZoomStep : 1.0f / kZoomStep);
                zoom = zoom < kZoomMin ? kZoomMin : (zoom > kZoomMax ? kZoomMax : zoom);
                session_.setCameraZoom(zoom);
            }
        }

        aa::sim::Vec2 center = session_.camera().centerPx;
        if (IsKeyDown(KEY_A)) center.x -= kPanStepPx;
        if (IsKeyDown(KEY_D)) center.x += kPanStepPx;
        if (IsKeyDown(KEY_W)) center.y += kPanStepPx;
        if (IsKeyDown(KEY_X)) center.y -= kPanStepPx;
        if (center.x != session_.camera().centerPx.x || center.y != session_.camera().centerPx.y) session_.setCameraCenter(center);
        float zoom = session_.camera().zoom;
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) zoom *= kZoomStep;
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) zoom /= kZoomStep;
        if (IsKeyPressed(KEY_ZERO)) zoom = 1.0f;
        zoom = zoom < kZoomMin ? kZoomMin : (zoom > kZoomMax ? kZoomMax : zoom);
        if (zoom != session_.camera().zoom) session_.setCameraZoom(zoom);
    }

    void setAudio(AudioSystem* audio) {
        audio_ = audio;
        session_.setSoundSink(audio);
    }

    void advance(float dt) {
        session_.advance(dt);
        session_.drainActions();
        // The one-shot sounds go to the audio system (SoundSystemUtils::Play with the event's position and
        // volume); the HUD shows the last few events of the run.
        for (const aa::sim::SessionEvent& e : session_.drainEvents()) {
            if (e.kind == aa::sim::SessionEvent::Kind::Buzz) continue;
            if (e.kind == aa::sim::SessionEvent::Kind::Sound && audio_) audio_->play(e.soundId, e.volume, e.position);
            std::string text;
            switch (e.kind) {
            case aa::sim::SessionEvent::Kind::Sound: text = "snd " + std::to_string(e.soundId); break;
            case aa::sim::SessionEvent::Kind::GoalComplete: text = "GOAL"; break;
            case aa::sim::SessionEvent::Kind::StarCollected: text = "star " + std::to_string(e.soundId); break;
            case aa::sim::SessionEvent::Kind::LevelCompleted: text = "COMPLETED"; break;
            default: break;
            }
            recentEvents_.push_back(text);
            if (recentEvents_.size() > kRecentEvents) recentEvents_.erase(recentEvents_.begin());
        }
        if (session_.controllerState() == 2) recentEvents_.clear();
        if (audio_) audio_->update(dt);
    }

    void drawFrame(WorldRenderer& renderer, bool hud) {
        const aa::sim::ScreenLayout layout = aa::sim::ScreenLayout::compute(GetScreenWidth(), GetScreenHeight(), aa::ui::AppState::profilePixelScaleOf(root_));
        WorldRendererOptions ro;
        ro.drawMarkers = true;   // markers with frameStep < 0 are skipped by the renderer itself
        renderer.draw(session_.renderState(), layout, ro);
        if (hud) {
            const std::string mode = session_.physicsMode() == aa::sim::PhysicsMode::SetUp ? "set-up" : "simulation";
            const std::string line = title() + "  [" + mode + "]  bodies " + std::to_string(session_.world()->bodyCount()) +
                                     "  zoom " + std::to_string(session_.camera().zoom).substr(0, 4);
            DrawText(line.c_str(), kHudMargin, kHudMargin, kHudFontSize, DARKGRAY);
            const aa::sim::TouchState& ts = session_.touchState();
            std::string held = "-";
            const int obj = session_.heldObject();
            if (obj >= 0) held = std::string(aa::sim::itemTypeName(session_.state().objects[static_cast<std::size_t>(obj)].type)) + " #" + std::to_string(obj);
            const std::string line2 = std::string("touch ") + touchStateName(ts.state) + "  held " + held + "  undo " +
                                      std::to_string(session_.undoQueue().count < 0 ? 0 : session_.undoQueue().count) + "/" +
                                      std::to_string(session_.undoQueue().top < 0 ? 0 : session_.undoQueue().top) +
                                      (session_.ghost().inGhost ? "  GHOST" : "") + "  strip " +
                                      std::to_string(session_.toolbox().getItemCount());
            DrawText(line2.c_str(), kHudMargin, kHudMargin + kHudFontSize + 4, kHudFontSize, DARKGRAY);
            if (session_.controllerState() != 2) {
                // Simulation: the stopwatch (play time), the accumulator, the goal state, the stars, the
                // last events and the frame rate.
                char buf[160];
                std::snprintf(buf, sizeof buf, "time %.2f  acc %.4f  goal %s  stars %d  %s  %d fps", static_cast<double>(session_.playTime()),
                              static_cast<double>(session_.accumulator()),
                              session_.controllerState() == 6 ? "done" : (session_.goalState().reached ? "reached" : "-"),
                              session_.goalState().collectedStars, session_.completing() ? "completing" : "", GetFPS());
                std::string line3 = buf;
                for (const std::string& e : recentEvents_) line3 += "  " + e;
                DrawText(line3.c_str(), kHudMargin, kHudMargin + (kHudFontSize + 4) * 2, kHudFontSize, DARKGRAY);
            }
        }
    }

    const aa::sim::Session& session() const { return session_; }
    aa::sim::Session& session() { return session_; }

private:
    const aa::data::AssetRoot& root_;
    aa::sim::Session session_;
    std::vector<LevelRef> levels_;
    int index_ = 0;
    bool showMarkers_ = true;
    bool pointerHeld_ = false;
    aa::sim::Vec2 pointer_{0.0f, 0.0f};
    AudioSystem* audio_ = nullptr;
    static constexpr std::size_t kRecentEvents = 6;
    std::vector<std::string> recentEvents_;

public:
    void setShowMarkers(bool on) { showMarkers_ = on; }
};

// A pointer script (`--script`): one command per line, native px with y down, `#` comments.
//   down X Y | move X Y | up X Y | frames N | wheel ±1 | take SLOT | key Z|Y|SPACE|R|DELETE|T|P|F | shot <file.png> [hud]
//   play | stop | sim N (N frames of 1/60 s while the simulation runs)
struct ScriptCommand {
    std::string op;
    std::vector<std::string> args;
};

std::vector<ScriptCommand> readScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read --script " + path);
    std::vector<ScriptCommand> commands;
    std::string raw;
    while (std::getline(in, raw)) {
        const std::string line = raw.substr(0, raw.find('#'));
        std::istringstream words(line);
        ScriptCommand c;
        if (!(words >> c.op)) continue;
        for (std::string w; words >> w;) c.args.push_back(w);
        commands.push_back(c);
    }
    return commands;
}

float arg(const ScriptCommand& c, std::size_t i) {
    if (i >= c.args.size()) throw std::runtime_error("script: `" + c.op + "` needs more arguments");
    return std::strtof(c.args[i].c_str(), nullptr);
}

// Runs the script frame by frame (60 Hz), rendering every frame; returns false when the window closed.
bool runScript(Viewer& viewer, WorldRenderer& renderer, const std::vector<ScriptCommand>& commands, int& exitCode) {
    auto frame = [&]() {
        if (WindowShouldClose()) return false;
        viewer.setViewport(GetScreenWidth(), GetScreenHeight());
        viewer.advance(kScriptDt);
        BeginDrawing();
        ClearBackground(kClearColour);
        viewer.drawFrame(renderer, true);
        EndDrawing();
        return true;
    };
    for (const ScriptCommand& c : commands) {
        if (c.op == "down") viewer.pointerDown(aa::sim::Vec2(arg(c, 0), arg(c, 1)));
        else if (c.op == "move") viewer.pointerMove(aa::sim::Vec2(arg(c, 0), arg(c, 1)));
        else if (c.op == "up") viewer.pointerUp(aa::sim::Vec2(arg(c, 0), arg(c, 1)));
        else if (c.op == "wheel") viewer.rotateHeld(arg(c, 0) > 0.0f ? kWheelRotateStep : -kWheelRotateStep);
        else if (c.op == "take") viewer.session().takeFromToolbox(static_cast<int>(arg(c, 0)));
        else if (c.op == "play") viewer.session().play();
        else if (c.op == "stop") viewer.session().stop();
        else if (c.op == "sim" || c.op == "frames") {
            const int n = static_cast<int>(arg(c, 0));
            for (int i = 0; i < n; ++i) {
                if (!frame()) return false;
            }
            continue;
        } else if (c.op == "key") {
            const std::string k = c.args.empty() ? "" : c.args[0];
            aa::sim::Session& s = viewer.session();
            if (k == "Z") s.undo();
            else if (k == "Y") s.redo();
            else if (k == "SPACE") { if (s.physicsMode() == aa::sim::PhysicsMode::SetUp) s.play(); else s.stop(); }
            else if (k == "R") { s.pause(); s.restart(); }
            else if (k == "DELETE") s.returnHeld();
            else if (k == "T") s.toggleToolbox();
            else if (k == "P") s.pause();
            else if (k == "F") s.flipHeld();
            else throw std::runtime_error("script: unknown key " + k);
        } else if (c.op == "shot") {
            if (c.args.empty()) throw std::runtime_error("script: `shot` needs a file");
            if (!frame()) return false;
            BeginDrawing();
            ClearBackground(kClearColour);
            viewer.drawFrame(renderer, c.args.size() > 1 && c.args[1] == "hud");
            rlDrawRenderBatchActive();   // the HUD text is still in raylib's batch
            Image shot = LoadImageFromScreen();
            EndDrawing();
            if (!ExportImage(shot, c.args[0].c_str())) {
                std::fprintf(stderr, "amazing_alex: cannot write %s\n", c.args[0].c_str());
                exitCode = 1;
            }
            UnloadImage(shot);
            continue;
        } else {
            throw std::runtime_error("script: unknown command " + c.op);
        }
        if (!frame()) return false;
    }
    return true;
}

}  // namespace

int runViewer(const ViewerOptions& options) {
    const aa::data::AssetRoot root(options.assets, assetFileReader());
    const aa::sim::FrameTable frames = aa::data::loadFrameTable(root.json("atlases/GameItems.json").root());
    const aa::sim::FrameTable uiFrames = aa::data::loadFrameTable(root.json("atlases/UIElements.json").root());
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    Viewer viewer(root, templates, aa::sim::ToolboxFrameSizes::fromFrames(uiFrames));
    if (viewer.levels().empty()) throw std::runtime_error("no levels in " + root.dir());
    const int start = findLevel(viewer.levels(), options.level);
    viewer.setShowMarkers(options.screenshotDir.empty() ? options.markers : options.screenshotMarkers);
    viewer.setViewport(options.width, options.height);
    viewer.load(start);
    std::vector<ScriptCommand> script;
    if (!options.script.empty()) script = readScript(options.script);

    std::printf("assets: %s (%s, profile %s)\nlevels: %zu in %zu chapters\nGameItems frames: %d\nlevel %s: %zu items, %d bodies in the set-up world\n",
                root.dir().c_str(), root.manifest().sourceKind.c_str(), root.manifest().profile.c_str(), viewer.levels().size(),
                root.chapters().size(), frames.size(), viewer.title().c_str(), viewer.session().level().items.size(),
                viewer.session().world()->bodyCount());
    std::fflush(stdout);
    if (options.headless) return 0;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(options.screenshotDir.empty() ? FLAG_WINDOW_RESIZABLE : 0);
    InitWindow(options.width, options.height, "Amazing Alex");
    setWindowIconFromAssets(options.assets);
    SetTargetFPS(60);
    SetExitKey(KEY_ESCAPE);
    int exitCode = 0;
    {
        AtlasSet atlases;
        atlases.load(root);
        WorldRenderer renderer(atlases);
        AudioSystem audio;
        if (options.audio) {
            audio.load(root);
            viewer.setAudio(&audio);
        }
        if (!options.screenshotDir.empty()) {
            std::filesystem::create_directories(options.screenshotDir);
            for (std::size_t i = 0; i < viewer.levels().size() && !WindowShouldClose(); ++i) {
                viewer.load(static_cast<int>(i));
                const LevelRef& ref = viewer.levels()[i];
                BeginDrawing();
                ClearBackground(kClearColour);
                viewer.drawFrame(renderer, false);
                // Read the pixels while the frame is still the back buffer (before the swap).
                Image shot = LoadImageFromScreen();
                EndDrawing();
                const std::string path = (std::filesystem::path(options.screenshotDir) / (ref.chapter + "_" + ref.name + ".png")).string();
                if (!ExportImage(shot, path.c_str())) {
                    std::fprintf(stderr, "amazing_alex: cannot write %s\n", path.c_str());
                    exitCode = 1;
                }
                UnloadImage(shot);
            }
        } else if (!script.empty()) {
            runScript(viewer, renderer, script, exitCode);
        } else {
            for (int frame = 0; !WindowShouldClose() && (options.maxFrames <= 0 || frame < options.maxFrames); ++frame) {
                viewer.setViewport(GetScreenWidth(), GetScreenHeight());
                viewer.handleInput();
                viewer.advance(GetFrameTime());
                SetWindowTitle(("Amazing Alex - " + viewer.title()).c_str());
                BeginDrawing();
                ClearBackground(kClearColour);
                viewer.drawFrame(renderer, true);
                EndDrawing();
            }
        }
        viewer.setAudio(nullptr);
        audio.unload();
        atlases.unload();
    }
    CloseWindow();
    return exitCode;
}

}  // namespace aa::platform
