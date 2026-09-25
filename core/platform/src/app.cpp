#include "aa/platform/app.h"

#include "aa/data/asset_root.h"
#include "aa/data/frame_table_loader.h"
#include "aa/data/level_loader.h"
#include "aa/game/localization.h"
#include "aa/game/save_store.h"
#include "aa/platform/atlas.h"
#include "aa/platform/audio.h"
#include "aa/platform/platform.h"
#include "aa/platform/ui_renderer.h"
#include "aa/platform/world_renderer.h"
#include "aa/sim/coords.h"
#include "aa/sim/render_state.h"
#include "aa/sim/screen_layout.h"
#include "aa/sim/session.h"
#include "aa/sim/templates.h"
#include "aa/ui/animator.h"
#include "aa/ui/app_state.h"
#include "aa/ui/extra_scenes.h"
#include "aa/ui/game_scene.h"
#include "aa/ui/menu_scenes.h"
#include "aa/ui/sandbox_scene.h"
#include "aa/ui/scene.h"

#include <raylib.h>
#include <rlgl.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aa::platform {

namespace {

const Color kClearColour = {217, 204, 204, 255};   // the original's clear colour (0.85, 0.8, 0.8)
constexpr float kFixedDt = 1.0f / 60.0f;
constexpr int kPointerId = 0;

#if defined(__EMSCRIPTEN__)
void webFrame();
#endif

// AudioSystem seen by the scenes: GameApp::playMusic / stopMusic, the mute toggle, the UI clips.
class AppAudioAdapter : public aa::ui::AppAudio, public aa::ui::SoundPlayer {
public:
    explicit AppAudioAdapter(AudioSystem& audio) : audio_(&audio) {}
    void playMusic(int audioId) override { audio_->playMusic(audioId); }
    void stopMusic() override { audio_->stopMusic(); }
    void setMuted(bool muted) override {
        if (muted) audio_->mute();
        else audio_->unmute();
    }
    void setMusicEnabled(bool on) override { audio_->setMusicEnabled(on); }
    void playUiSound(int audioId, float volume) override { audio_->playUi(audioId, volume); }

private:
    AudioSystem* audio_;
};

// The GameScene with the world drawn by the platform renderer between the clear and the HUD.
class PlatformGameScene : public aa::ui::GameScene {
public:
    PlatformGameScene(aa::ui::UiContext& ctx, aa::ui::AppState& app, const aa::sim::TemplateTable& templates, WorldRenderer* world)
        : aa::ui::GameScene(ctx, app, templates), world_(world) {}
    void drawWorld() override {
        if (!world_) return;
        rlDrawRenderBatchActive();   // the UI's backdrop batch, before the world's matrices
        WorldRendererOptions ro;
        ro.drawMarkers = true;
        world_->draw(session().renderState(), session().viewport(), ro);
        // The world renderer leaves raylib's default 2D matrices; the UI draws in screen px from here.
        rlDrawRenderBatchActive();
    }

private:
    WorldRenderer* world_;
};

// The SandboxScene with the world drawn by the platform renderer.
class PlatformSandboxScene : public aa::ui::SandboxScene {
public:
    PlatformSandboxScene(aa::ui::UiContext& ctx, aa::ui::AppState& app, const aa::sim::TemplateTable& templates, WorldRenderer* world)
        : aa::ui::SandboxScene(ctx, app, templates), world_(world) {}
    void drawWorld() override {
        if (!world_) return;
        rlDrawRenderBatchActive();   // the UI's backdrop batch, before the world's matrices
        WorldRendererOptions ro;
        ro.drawMarkers = true;
        world_->draw(session().renderState(), session().viewport(), ro);
        rlDrawRenderBatchActive();
    }

private:
    WorldRenderer* world_;
};

// ScreenshotUtils::CreateLevelThumbnail [verified: 0xe5674, saveSandboxLevelAndThumb 0xb4f14]: the world
// rendered with the default camera (GameRenderer::RenderWorldForScreenshot, clear colour 0.1 / 0.1 / 0.3),
// the square of side min(0.6308594 · viewport height, viewport width) at the viewport's left, vertically
// centred, scaled to the profile's thumbnail size (350 px for 2048X1536). The original wrote a JPEG at
// quality 100 next to the level; the remake writes a PNG (a lossless remake choice).
void writeThumbnail(WorldRenderer& world, const aa::sim::ScreenLayout& layout, const aa::sim::Session& session, int sizePx, const std::string& path) {
    constexpr float kCropFraction = 0.6308594f;
    const Color kClear = {26, 26, 77, 255};
    RenderTexture2D target = LoadRenderTexture(layout.width, layout.height);
    if (target.id == 0) return;
    BeginTextureMode(target);
    ClearBackground(kClear);
    world.drawForThumbnail(session.renderState(), layout);
    rlDrawRenderBatchActive();
    EndTextureMode();
    Image image = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&image);   // render textures come out upside down
    const float h = static_cast<float>(layout.height);
    const float w = static_cast<float>(layout.width);
    float side = h * kCropFraction;
    if (side > w) side = w;
    const float top = (h - side) * 0.5f;
    ImageCrop(&image, Rectangle{0.0f, top, side, side});
    ImageResize(&image, sizePx, sizePx);
    if (!ExportImage(image, path.c_str())) std::fprintf(stderr, "amazing_alex: cannot write the thumbnail %s\n", path.c_str());
    UnloadImage(image);
    UnloadRenderTexture(target);
}

// The scripted walk modes (--headless, --ui-screenshots) mark levels done and flip the audio setting; without
// an explicit --save-dir they get a fresh temporary directory so the player's real progress is never touched.
// A mobile build keeps its saves in the app's private data directory (the original's LocationStateUtils
// files under the activity's files dir); the desktop builds use the OS user-data directory.
std::string saveDirFor(const AppOptions& o) {
    if (!o.saveDir.empty()) return o.saveDir;
    if (!o.headless && o.screenshotDir.empty()) {
        // Android's dataDir is the app's whole private directory, so saves get their own subfolder; Web's
        // dataDir (platform_web.cpp) is already the dedicated IDBFS mount, matching SaveStore::defaultDir.
        if (!o.dataDir.empty()) return o.mobile ? o.dataDir + "/saves" : o.dataDir;
        return aa::game::SaveStore::defaultDir();
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("amazing_alex_walk_" + std::to_string(static_cast<long long>(std::time(nullptr))));
    std::filesystem::remove_all(dir);
    return dir.string();
}

struct Game {
    const AppOptions* options;
    aa::data::AssetRoot root;
    aa::sim::FrameTable frames;
    aa::sim::FrameTable uiFrames;
    aa::sim::TemplateTable templates;
    aa::sim::ScreenLayout layout;
    aa::game::SaveStore saves;
    aa::game::Localization localization;
    aa::ui::ResourceProxy resources;
    aa::ui::Animator animator;
    aa::ui::UiContext ctx;
    aa::ui::AppState app;
    AudioSystem audio;
    UiRenderer* uiRenderer = nullptr;   // set by runApp for the thumbnail texture purge
    std::unique_ptr<AppAudioAdapter> audioAdapter;
    std::unique_ptr<aa::ui::SceneManager> scenes;
    PlatformGameScene* gameScene = nullptr;
    PlatformSandboxScene* sandboxScene = nullptr;
    aa::ui::SplashScene* splash = nullptr;
    int appState = 0;   // GameApp+0x280: 1 splash pushed, 4 waiting for the splash, 5 main menu
    double clock = 0.0;

    explicit Game(const AppOptions& o)
        : options(&o), root(o.assets, assetFileReader()), frames(aa::data::loadFrameTable(root.json("atlases/GameItems.json").root())),
          uiFrames(aa::data::loadFrameTable(root.json("atlases/UIElements.json").root())), templates(aa::sim::initTemplates(frames)),
          layout(aa::sim::ScreenLayout::compute(o.width, o.height, aa::ui::AppState::profilePixelScaleOf(root))), saves(saveDirFor(o)) {
        applyScreenLayout();
        // The sprite / font resolution ResourceProxy picks for ctx.screen.uiScale is loaded once here and
        // never reloaded on a live resize (unsafe on a live frame: synchronous reads + JSON parsing of
        // every container) — only position / size / padding / text-wrap reflow live.
        resources.load(root, ctx.screen.uiScale);
        app.assets = &root;
        app.saves = &saves;
        app.localization = &localization;
        app.settings = saves.loadSettings();
        app.progress = saves.loadProgress();
        app.toolboxSizes = aa::sim::ToolboxFrameSizes::fromFrames(uiFrames);
        app.unlockAllLevels = o.unlockAll;
        app.loadCatalogue();
        // The locale: --locale, else the settings file, else the OS language (docs/06 §3).
        std::vector<std::string> preferred;
        if (!o.locale.empty()) preferred.push_back(o.locale);
        else if (!app.settings.locale.empty()) preferred.push_back(app.settings.locale);
        else if (!o.languages.empty()) preferred = o.languages;
        else preferred = aa::game::systemPreferredLanguages();
        const std::string locale = aa::game::chooseLocale(preferred);
        localization.setLocale(locale, aa::data::loadTextTable(root.json("texts/" + locale + ".json").root()));
        ctx.resources = &resources;
        ctx.localization = &localization;
        ctx.animator = &animator;
        for (const std::string& aside : saves.asideFiles()) std::fprintf(stderr, "amazing_alex: corrupt save set aside: %s\n", aside.c_str());
    }

    // The ctx.screen.* fields `layout` (already current — the caller recomputes it first on a resize)
    // drives: uiScale / letterbox / anchor-correction / widescreen-scaling. Split out of the ctor so a
    // resize can redo it without redoing everything else the ctor does once (asset loading, locale, ...).
    void applyScreenLayout() {
        ctx.screen.nativeWidth = static_cast<float>(layout.width);
        ctx.screen.nativeHeight = static_cast<float>(layout.height);
        ctx.screen.anchorCorrectionX = layout.anchorAspectCorrectionX;
        ctx.screen.anchorCorrectionY = layout.anchorAspectCorrectionY;
        ctx.screen.pixelScale = layout.pixelScale;
        ctx.screen.uiScale = layout.uiScale();
        ctx.screen.letterBox = layout.letterBox;
        ctx.screen.letterBoxFrameWidth = layout.playFieldNativeX();
        ctx.screen.widescreenScaling = layout.pixelScale < 1.0f;   // docs/11 §1: the AssetScalingForWidescreen rule
        // The loaded sprite sheet / font data stays at whatever resolution tier was picked at startup (a
        // live resize never re-scans the asset directory) — but the *scale multiplier* ImageView::AutoResize
        // and BitmapFont metrics use is cheap, in-memory arithmetic, so it tracks the live uiScale here;
        // View::recomputeAutoSizeRecursive() (part of View::relayout()) re-derives each AutoResize frame
        // from it afterwards.
        resources.setUiScale(ctx.screen.uiScale);
    }

    void initAudio(bool device) {
        audio.load(root, device);
        if (!app.settings.soundEffectsOn) audio.mute();
        audio.setMusicEnabled(app.settings.musicOn);
        audioAdapter = std::make_unique<AppAudioAdapter>(audio);
        app.audio = audioAdapter.get();
        app.soundSink = &audio;
        ctx.sounds = audioAdapter.get();
    }

    void initScenes(WorldRenderer* world) {
        scenes = std::make_unique<aa::ui::SceneManager>(ctx);
        auto splashScene = std::make_unique<aa::ui::SplashScene>(ctx, app);
        splash = splashScene.get();
        scenes->registerScene(std::move(splashScene));
        scenes->registerScene(std::make_unique<aa::ui::MainMenuScene>(ctx, app));
        scenes->registerScene(std::make_unique<aa::ui::ChapterSelectionScene>(ctx, app));
        scenes->registerScene(std::make_unique<aa::ui::LevelSelectionScene>(ctx, app));
        scenes->registerScene(std::make_unique<aa::ui::LevelLoadingScene>(ctx, app));
        auto game = std::make_unique<PlatformGameScene>(ctx, app, templates, world);
        gameScene = game.get();
        scenes->registerScene(std::move(game));
        scenes->registerScene(std::make_unique<aa::ui::ChapterCompleteScene>(ctx, app, false));
        scenes->registerScene(std::make_unique<aa::ui::ChapterCompleteScene>(ctx, app, true));
        scenes->registerScene(std::make_unique<aa::ui::ComicScene>(ctx, app));
        scenes->registerScene(std::make_unique<aa::ui::CreditsScene>(ctx, app));
        scenes->registerScene(std::make_unique<aa::ui::MyContraptionsScene>(ctx, app));
        auto sandbox = std::make_unique<PlatformSandboxScene>(ctx, app, templates, world);
        sandboxScene = sandbox.get();
        scenes->registerScene(std::move(sandbox));
        for (const char* name : {aa::ui::scene_names::kSplash, aa::ui::scene_names::kMainMenu, aa::ui::scene_names::kChapterSelection,
                                 aa::ui::scene_names::kLevelSelection, aa::ui::scene_names::kLevelLoading, aa::ui::scene_names::kGame,
                                 aa::ui::scene_names::kChapterComplete, aa::ui::scene_names::kChapterComplete3Stars, aa::ui::scene_names::kComic,
                                 aa::ui::scene_names::kCredits, aa::ui::scene_names::kMyContraptions, aa::ui::scene_names::kSandbox}) {
            scenes->scene(name)->init();
        }
        gameScene->setViewport(options->width, options->height);
        sandboxScene->setViewport(options->width, options->height);
        // DeviceParams::IsTablet (docs/10 §6): the platform's answer unless --tablet / --phone said otherwise.
        const bool tablet = options->tablet >= 0 ? options->tablet != 0 : options->mobile;
        gameScene->session().setTablet(tablet);
        sandboxScene->session().setTablet(tablet);
        if (world) {
            WorldRenderer* w = world;
            sandboxScene->setThumbnailer([this, w](const aa::sim::Session& session, const std::string& path) {
                writeThumbnail(*w, layout, session, resources.thumbnailSize(), path);
                resources.dropThumbnail(aa::ui::ResourceProxy::thumbnailNameForFile(path));
                if (uiRenderer) uiRenderer->purgeThumbnails();
            });
        }
    }

    // GameApp::update's start-up states [verified]: the splash scene, the main menu as the root scene once
    // the splash's second page completed.
    void updateFlow() {
        if (appState == 0) {
            if (options->noIntro) {
                scenes->setRootScene(aa::ui::scene_names::kMainMenu);
                appState = 5;
            } else {
                scenes->setRootScene(aa::ui::scene_names::kSplash);
                appState = 1;
            }
        } else if (appState == 1 && splash->isPageCompleted(2)) {
            scenes->setRootScene(aa::ui::scene_names::kMainMenu);
            appState = 5;
        }
    }

    void frame(float dt) {
        clock += static_cast<double>(dt);
        updateFlow();
        scenes->update(dt);
        audio.update(dt);
    }

    aa::ui::TouchEvent touch(int id, float x, float y) const {
        aa::ui::TouchEvent e;
        e.id = id;
        e.position = aa::ui::Point{x, y};
        e.previous = e.position;
        e.time = clock;
        return e;
    }

    // A tap at a screen point: down, one frame, up.
    void tap(float x, float y) {
        scenes->touchesStarted(touch(kPointerId, x, y));
        frame(kFixedDt);
        scenes->touchesFinished(touch(kPointerId, x, y));
        frame(kFixedDt);
    }

    void run(int frames) {
        for (int i = 0; i < frames; ++i) frame(kFixedDt);
    }

    // A drag: down at (x0, y0), `steps` moves of one frame each towards (x1, y1), a short hold, up.
    void drag(float x0, float y0, float x1, float y1, int steps = 24) {
        scenes->touchesStarted(touch(kPointerId, x0, y0));
        run(3);
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            scenes->touchesMoved(touch(kPointerId, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t));
            frame(kFixedDt);
        }
        run(6);
        scenes->touchesFinished(touch(kPointerId, x1, y1));
        frame(kFixedDt);
    }

    // A drag that leaves a strip slot: down, 80 px straight up over four frames, then towards (x1, y1).
    void dragOut(float x0, float y0, float x1, float y1, int steps = 24) {
        scenes->touchesStarted(touch(kPointerId, x0, y0));
        run(3);
        const float lift = 80.0f;
        for (int i = 1; i <= 4; ++i) {
            scenes->touchesMoved(touch(kPointerId, x0, y0 - lift * static_cast<float>(i) / 4.0f));
            frame(kFixedDt);
        }
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            scenes->touchesMoved(touch(kPointerId, x0 + (x1 - x0) * t, (y0 - lift) + (y1 - (y0 - lift)) * t));
            frame(kFixedDt);
        }
        run(6);
        scenes->touchesFinished(touch(kPointerId, x1, y1));
        frame(kFixedDt);
    }

    // The centre of a named view of the active scene, in screen px.
    bool viewCenter(const std::string& name, float& x, float& y) const {
        aa::ui::Scene* s = scenes->activeScene();
        if (!s) return false;
        aa::ui::View* v = s->findView(name);
        if (!v) return false;
        const aa::ui::Point g = v->globalPosition();
        x = g.x + v->size().w * 0.5f;
        y = g.y + v->size().h * 0.5f;
        return true;
    }
};

// The scripted campaign walk (the headless smoke and the screenshot run): Splash → MainMenu →
// ChapterSelection → LevelSelection → Playtime (self-completing) → the result panel → next level →
// the pause menu (audio toggle) → back to the level list; with a save on the chapter's last level the
// chapter-complete panels instead. AA_WALK_DEBUG=1 traces every tap (the hit view and its ancestors).
struct Walker {
    Game& game;
    std::function<void(const std::string&)> shot;   // called at every scene stop
    std::string failure;

    bool expectScene(const char* name, int maxFrames) {
        for (int i = 0; i < maxFrames; ++i) {
            aa::ui::Scene* s = game.scenes->activeScene();
            if (s && std::string(s->name()) == name && !game.scenes->inTransition() && s->state() == aa::ui::scene_state::kActive) return true;
            game.frame(kFixedDt);
        }
        aa::ui::Scene* s = game.scenes->activeScene();
        failure = std::string("expected scene ") + name + ", got " + (s ? s->name() : "none") + " (stack:";
        for (aa::ui::Scene* e : game.scenes->stack()) failure += std::string(" ") + e->name() + "/" + std::to_string(e->state());
        failure += game.scenes->inTransition() ? ", in transition)" : ")";
        return false;
    }

    bool isChapterPanel() const {
        aa::ui::Scene* s = game.scenes->activeScene();
        if (!s) return false;
        const std::string n = s->name();
        return n == aa::ui::scene_names::kChapterComplete || n == aa::ui::scene_names::kChapterComplete3Stars;
    }

    // The touch-path position (native px, y up) of a world point: the inverse of st::screenToWorld — the
    // original's WorldPtToScreenPt + ScreenToPixelPos pair ignores the letterbox and is not its inverse on
    // 16:9 windows.
    aa::sim::Vec2 worldToTouch(const aa::sim::Session& session, aa::sim::Vec2 world) const {
        const aa::sim::Camera& cam = session.camera();
        const aa::sim::ScreenLayout& lay = game.layout;
        return aa::sim::Vec2((((world.x / aa::sim::kPixelToMeters - cam.centerPx.x) * cam.zoom + 512.0f) + lay.letterBoxFrameWidth) / lay.worldScaleWithFloor,
                             (((world.y / aa::sim::kPixelToMeters - cam.centerPx.y) * cam.zoom + 319.0f) + lay.floor) / lay.worldScaleWithFloor + lay.worldBandNative());
    }

    // The screen x (native px) of the strip slot holding `type` with items left; false when none.
    bool stripSlot(const aa::sim::Session& session, int type, float& sx, float& sy) const {
        const aa::sim::RenderState rs = session.renderState();
        const aa::sim::RenderToolbox& tb = rs.toolbox;
        float left = 0.0f;
        for (const aa::sim::RenderToolboxSlot& slot : tb.slots) {
            const float cx = (left + slot.widthPx * 0.5f) - tb.ejectLength - tb.scroll;
            left += slot.widthPx;
            if (static_cast<int>(slot.type) == type && slot.amount != 0) {
                sx = tb.x + cx;
                sy = tb.y;
                return true;
            }
        }
        return false;
    }

    static int stripItems(const aa::sim::Session& session) {
        int n = 0;
        for (const aa::sim::RenderToolboxSlot& slot : session.renderState().toolbox.slots) n += slot.amount > 0 ? slot.amount : 0;
        return n;
    }

    // Runs frames until the strip's glide and eject animations have settled (up to 5 s).
    void waitForStrip(const aa::sim::Session& session) {
        float lastX = -1.0f, lastEject = -1.0f;
        int stable = 0;
        for (int i = 0; i < 60 * 5 && stable < 5; ++i) {
            game.frame(kFixedDt);
            const aa::sim::RenderToolbox& tb = session.renderState().toolbox;
            stable = (tb.x == lastX && tb.ejectLength == lastEject) ? stable + 1 : 0;
            lastX = tb.x;
            lastEject = tb.ejectLength;
        }
    }

    // Drags one item of `type` out of the strip to the world point `target` and waits for the drop.
    bool dragFromStrip(aa::sim::Session& session, const char* tag, int type, aa::sim::Vec2 target) {
        const float h = static_cast<float>(game.options->height);
        waitForStrip(session);
        float sx, sy;
        if (!stripSlot(session, type, sx, sy)) {
            failure = std::string(tag) + ": no strip slot of type " + std::to_string(type);
            return false;
        }
        const int before = stripItems(session);
        const aa::sim::Vec2 t = worldToTouch(session, target);
        if (std::getenv("AA_WALK_DEBUG")) {
            const aa::sim::Vec2 back = session.screenToWorld(t);
            std::fprintf(stderr, "drag type %d: slot %.1f,%.1f -> target world %.4f,%.4f screen %.1f,%.1f (back %.4f,%.4f)\n", type, sx, sy, target.x,
                         target.y, t.x, t.y, back.x, back.y);
        }
        // Straight up out of the strip first (as a finger does): a long, flat drag towards a distant target
        // would read as a scroll along the strip (< 20° from its axis, 05 §5) on a wide window.
        game.dragOut(sx, h - sy, t.x, h - t.y);
        game.run(30);   // the drop settles (snap / ghost glide)
        if (std::getenv("AA_WALK_DEBUG")) {
            std::fprintf(stderr, "  after the drop: %d of type %d in the world, touch state %d, held %d, strip items %d\n",
                         session.state().typeCount(static_cast<aa::sim::ItemType>(type)), type, session.touchState().state, session.heldObject(),
                         stripItems(session));
        }
        const int after = stripItems(session);
        if (after != before - 1) {
            failure = std::string(tag) + ": the drag did not take the item out of the strip (" + std::to_string(before) + " -> " + std::to_string(after) + ")";
            return false;
        }
        return true;
    }

    // Drags the world object at `world` onto the strip (the toolbox step) and waits for its removal.
    bool dragToStrip(aa::sim::Session& session, const char* tag, aa::sim::Vec2 world) {
        const float h = static_cast<float>(game.options->height);
        const aa::sim::RenderToolbox& tb = session.renderState().toolbox;
        const aa::sim::Vec2 from = worldToTouch(session, world);
        const int before = static_cast<int>(session.state().objects.size());
        game.drag(from.x, h - from.y, tb.x - 60.0f, h - tb.y, 30);
        game.run(60);
        if (static_cast<int>(session.state().objects.size()) >= before) {
            failure = std::string(tag) + ": the drag did not move the item into the strip";
            return false;
        }
        return true;
    }

    bool tapButton(aa::ui::View* v, const char* what) {
        if (!v) {
            failure = std::string("view missing: ") + what;
            return false;
        }
        const aa::ui::Point g = v->globalPosition();
        game.tap(g.x + v->size().w * 0.5f, g.y + v->size().h * 0.5f);
        return true;
    }

    // A comic page on top: every frame revealed by a tap, then the next button.
    bool passComic(const char* tag) {
        auto* comic = dynamic_cast<aa::ui::ComicScene*>(game.scenes->activeScene());
        if (!comic || !comic->view()) return true;
        if (!expectScene(aa::ui::scene_names::kComic, 120)) return false;
        game.run(10);
        shot(std::string(tag) + "_comic");
        for (int i = 0; i < comic->view()->frameCount() + 1; ++i) {
            if (!tapButton(comic->view()->tapArea(), "TapAreaButton")) return false;
            game.run(15);   // a press completes after the zoom-out (0.1 s) and zoom-in (0.05 s) animations
        }
        game.run(70);   // the next button shows a second after the last frame
        if (std::getenv("AA_WALK_DEBUG")) {
            std::fprintf(stderr, "comic: shown %d of %d, next visible %d, scene state %d, tap area %.0fx%.0f interactable %d\n", comic->view()->shownFrames(),
                         comic->view()->frameCount(), comic->view()->nextButton()->isVisible(), comic->state(), comic->view()->tapArea()->size().w,
                         comic->view()->tapArea()->size().h, comic->view()->tapArea()->isInteractable());
            aa::ui::View* hit = comic->hitTest(aa::ui::Point{500.0f, 400.0f});
            std::fprintf(stderr, "comic: hit at 500,400 = %s\n", hit ? hit->viewName().c_str() : "nothing");
        }
        if (!comic->view()->nextButton()->isVisible()) {
            failure = std::string(tag) + ": the comic's next button did not show";
            return false;
        }
        shot(std::string(tag) + "_comic_end");
        return tapButton(comic->view()->nextButton(), "ButtonNext");
    }

    // Plays the current Classroom level with the tutorial's own script: every drag the hand demonstrates
    // (SetDragItem(type) followed by the move to the target) is performed on the strip's slot of that type,
    // then the play button; the level must complete within `maxSeconds`.
    bool solveWithTutorial(const char* tag, float maxSeconds) {
        aa::sim::Session& session = game.gameScene->session();
        for (int i = 0; i < 60 * 8 && !session.tutorial().running; ++i) game.frame(kFixedDt);
        const aa::sim::TutorialState& st = session.tutorial();
        if (!st.running) {
            failure = std::string(tag) + ": the tutorial did not start";
            return false;
        }
        struct Drag { int type; aa::sim::Vec2 target; };
        std::vector<Drag> drags;
        // The fetch script: SetDragItem(item, type), a wait, then the move from the slot to the target.
        for (std::size_t i = 0; i + 2 < st.steps.size(); ++i) {
            const aa::sim::TutorialStep& a = st.steps[i];
            const aa::sim::TutorialStep& b = st.steps[i + 2];
            if (a.kind == aa::sim::TutorialStep::Kind::SetDragItem && a.slot >= 0 && a.type != 0 && b.kind == aa::sim::TutorialStep::Kind::MoveLinear)
                drags.push_back({a.type, b.b});
        }
        if (drags.empty()) {
            failure = std::string(tag) + ": the tutorial script has no drag";
            return false;
        }
        for (const Drag& d : drags) {
            if (!dragFromStrip(session, tag, d.type, d.target)) return false;
        }
        shot(std::string(tag) + "_solved");
        if (!tapView("ButtonPlay")) return false;
        const int maxFrames = static_cast<int>(maxSeconds * 60.0f);
        for (int i = 0; i < maxFrames && !game.gameScene->completedView()->isVisible(); ++i) game.frame(kFixedDt);
        if (!game.gameScene->completedView()->isVisible()) {
            failure = std::string(tag) + ": the level did not complete with the tutorial's solution";
            return false;
        }
        game.run(60 * 5);
        shot(std::string(tag) + "_completed");
        return true;
    }

    bool tapView(const char* view) {
        float x, y;
        game.run(2);
        if (!game.viewCenter(view, x, y)) {
            failure = std::string("view not found: ") + view;
            return false;
        }
        if (std::getenv("AA_WALK_DEBUG")) {
            aa::ui::View* hit = game.scenes->activeScene()->hitTest(aa::ui::Point{x, y});
            std::fprintf(stderr, "tap %s at %.0f,%.0f hits %s\n", view, x, y, hit ? hit->viewName().c_str() : "nothing");
            for (aa::ui::View* v = game.scenes->activeScene()->findView(view); v; v = v->parent())
                std::fprintf(stderr, "  %s vis=%d inter=%d pos=%.0f,%.0f size=%.0f,%.0f scale=%.2f\n", v->viewName().c_str(), v->isVisible(),
                             v->isInteractable(), v->position().x, v->position().y, v->size().w, v->size().h, v->scale());
            if (aa::ui::View* v = game.scenes->activeScene()->findView(view)) {
                for (aa::ui::View* c : v->subviews())
                    std::fprintf(stderr, "    > %s vis=%d pos=%.0f,%.0f size=%.0f,%.0f pivot=%.0f,%.0f\n", c->viewName().c_str(), c->isVisible(), c->position().x,
                                 c->position().y, c->size().w, c->size().h, c->pivot().x, c->pivot().y);
            }
        }
        game.tap(x, y);
        return true;
    }

    bool run() {
        if (!expectScene(aa::ui::scene_names::kSplash, 10)) return false;
        shot("01_splash");
        game.run(75);   // 1.25 s: the loading page
        shot("02_splash_loading");
        if (!expectScene(aa::ui::scene_names::kMainMenu, 200)) return false;
        game.run(30);
        shot("03_main_menu");
        // The settings slider: the menu slides down from under the gear, the audio button nearest to it
        // (SlidingButton::LayoutMenuButtons, DOWN: the last-added button first), centred under the gear.
        if (!tapView("SettingsSlider")) return false;
        game.run(40);
        shot("03b_main_menu_settings");
        {
            float gx, gy, ax, ay, mx, my, cx, cy;
            if (!game.viewCenter("SettingsSlider", gx, gy) || !game.viewCenter("ButtonAudio", ax, ay) || !game.viewCenter("ButtonMusic", mx, my) ||
                !game.viewCenter("ButtonCredits", cx, cy)) {
                failure = "the settings menu did not open";
                return false;
            }
            const aa::ui::View* audio = game.scenes->activeScene()->findView("ButtonAudio");
            const aa::ui::View* music = game.scenes->activeScene()->findView("ButtonMusic");
            if (!audio->isVisible() || !music->isVisible() || std::fabs(ax - gx) > 0.5f || std::fabs(mx - gx) > 0.5f || !(ay > gy) || !(my > ay) || !(cy > my)) {
                failure = "the settings menu layout is off (gear " + std::to_string(gx) + "," + std::to_string(gy) + " audio " + std::to_string(ax) + "," +
                          std::to_string(ay) + " music " + std::to_string(mx) + "," + std::to_string(my) + " credits " + std::to_string(cx) + "," +
                          std::to_string(cy) + ")";
                return false;
            }
        }
        if (!tapView("SettingsSlider")) return false;
        game.run(40);
        aa::ui::Scene* mainMenu = game.scenes->activeScene();
        if (!tapView("ButtonPlay")) return false;
        // A fresh install opens the first level directly (MainMenuView::ButtonPressed) — under the
        // Classroom's begin comic the first time; otherwise the books.
        for (int i = 0; i < 60 && (game.scenes->inTransition() || game.scenes->activeScene() == mainMenu); ++i) game.frame(kFixedDt);
        if (!passComic("03c_begin")) return false;
        aa::ui::Scene* s = game.scenes->activeScene();
        if (s && std::string(s->name()) == aa::ui::scene_names::kChapterSelection) {
            if (!expectScene(aa::ui::scene_names::kChapterSelection, 60)) return false;
            game.run(30);
            // A save that opens on a later chapter (a mastered Classroom scrolls to the Backyard): the walk
            // plays the Classroom, so the books are scrolled back to its page first.
            if (auto* chapters = dynamic_cast<aa::ui::ChapterSelectionScene*>(s); chapters && chapters->view()->panel()->activePage() != 0) {
                chapters->view()->scrollToPage(0);
                game.run(60);
            }
            shot("04_chapter_selection");
            if (!tapView("Button_0")) return false;
            game.run(20);
            if (!passComic("04b_begin")) return false;   // the Classroom's begin comic over its level list, once
            if (!expectScene(aa::ui::scene_names::kLevelSelection, 60)) return false;
            game.run(30);
            // Every level played (the list opens on its last page): back to Playtime's page.
            if (auto* levels = dynamic_cast<aa::ui::LevelSelectionScene*>(game.scenes->activeScene());
                levels && levels->view()->panel()->activePage() != 0) {
                levels->view()->panel()->setContentOffset(aa::ui::Point{0.0f, 0.0f}, false);
                game.run(30);
            }
            shot("05_level_selection");
            if (!tapView("Button_0")) return false;
        }
        if (!expectScene(aa::ui::scene_names::kLevelLoading, 60)) return false;
        shot("06_level_loading");
        game.scenes->keyPressed(aa::ui::SceneManager::kKeyBack);   // consumed by the loading view: no pop
        if (!expectScene(aa::ui::scene_names::kGame, 120)) return false;
        game.run(40);
        shot("07_game");
        game.run(60 * 3);   // the Classroom tutorial hand has faded in and moved to the play button
        shot("07b_game_tutorial");
        // The level tip (remake, docs/06 §3): up on entering the level; the info button hides it and shows it
        // again, the tutorial left running.
        {
            aa::ui::GameView* view = game.gameScene->gameView();
            if (!view->isTipShown() || !view->tipButton()->isVisible()) {
                failure = "the level tip is not shown on entering the level";
                return false;
            }
            if (!tapView("ButtonTip")) return false;
            game.run(30);   // the button's press animation, then ButtonPressed
            const bool hidden = !view->isTipShown();
            if (!tapView("ButtonTip")) return false;
            game.run(30);   // the button's press animation, then ButtonPressed
            if (!hidden || !view->isTipShown() || !game.gameScene->session().tutorial().running) {
                failure = std::string("the info button did not toggle the tip (or stopped the tutorial): hidden ") + (hidden ? "1" : "0") +
                          ", shown again " + (view->isTipShown() ? "1" : "0") + ", tutorial " +
                          (game.gameScene->session().tutorial().running ? "1" : "0");
                return false;
            }
        }
        // Play: Playtime completes on its own (docs/10 §8, the G4 `playtime` script: ~6.3 s + 2.05 s).
        if (!tapView("ButtonPlay")) return false;
        for (int i = 0; i < 60 * 14 && !game.gameScene->completedView()->isVisible(); ++i) game.frame(kFixedDt);
        if (!game.gameScene->completedView()->isVisible()) {
            failure = "the level did not complete";
            return false;
        }
        game.run(60 * 5);
        shot("08_level_completed");
        if (game.app.locationState.status(0) < 3) {
            failure = "the level was not marked done";
            return false;
        }
        if (!tapView("ButtonForward")) return false;
        game.run(60);
        // The chapter's last level with the panels already shown (a mastered chapter): straight back to
        // the books through the loading scene (LevelCompletedView::ButtonPressed, location 8).
        if (aa::ui::Scene* top = game.scenes->activeScene();
            top && std::string(top->name()) != aa::ui::scene_names::kGame && !isChapterPanel()) {
            if (!expectScene(aa::ui::scene_names::kChapterSelection, 120)) return false;
            game.run(30);
            shot("10_chapter_selection_back");
            return runExtras();
        }
        // The chapter's last level (a save edited to it): the chapter-complete panels, then the books.
        if (isChapterPanel()) {
            shot("09_chapter_complete");
            if (!tapView("ButtonNext")) return false;
            game.run(60);
            if (isChapterPanel()) {
                shot("09c_chapter_complete_3stars");
                if (!tapView("ButtonNext")) return false;
            }
            game.run(20);
            if (!passComic("09d_end")) return false;   // the Classroom's end comic over the books, once
            if (!expectScene(aa::ui::scene_names::kChapterSelection, 120)) return false;
            game.run(30);
            shot("10_chapter_selection_back");
            return runExtras();
        }
        shot("09_next_level");
        game.run(60 * 8 - 30);   // level 1's tutorial: the hand drags the shelf ghost towards its target
        shot("09b_next_level_tutorial");
        if (game.app.currentLevel != 1) {
            failure = "the next level did not open";
            return false;
        }
        // Levels 1 and 2 solved the way the tutorial hand shows: with three of the first four done the
        // second page of four unlocks (LocationStateUtils::MarkLevelAsDone, docs/05 §4).
        for (int level = 1; level <= 2; ++level) {
            const std::string tag = "09" + std::string(level == 1 ? "c" : "d") + "_level" + std::to_string(level);
            if (!solveWithTutorial(tag.c_str(), 20.0f)) return false;
            if (game.app.locationState.status(level) < 3) {
                failure = tag + ": the level was not marked done";
                return false;
            }
            if (!tapView("ButtonForward")) return false;
            game.run(60);
            if (game.app.currentLevel != level + 1) {
                failure = tag + ": the next level did not open";
                return false;
            }
        }
        if (game.app.locationState.status(4) < 2) {
            failure = "the second page did not unlock after three of four levels";
            return false;
        }
        game.run(60);
        shot("09e_level3");
        // The pause menu and back to the level list.
        if (!tapView("ButtonPause")) return false;
        game.run(30);
        shot("10_pause_menu");
        // The audio toggle writes the settings file at once (SettingsUtils::SetAudioState + Save); the
        // remake's note button flips the music alone and leaves the master mute as it is.
        const bool audioBefore = game.app.settings.soundEffectsOn;
        const bool musicBefore = game.app.settings.musicOn;
        if (!tapView("ButtonAudio")) return false;
        game.run(10);
        if (game.app.settings.soundEffectsOn == audioBefore || game.saves.loadSettings().soundEffectsOn != game.app.settings.soundEffectsOn ||
            game.audio.muted() == game.app.settings.soundEffectsOn || game.app.settings.musicOn != musicBefore) {
            failure = "the audio toggle did not persist";
            return false;
        }
        if (!tapView("ButtonAudio")) return false;
        game.run(10);
        if (game.app.settings.soundEffectsOn != audioBefore || game.saves.loadSettings().soundEffectsOn != audioBefore) {
            failure = "the audio toggle did not restore";
            return false;
        }
        if (!tapView("ButtonMusic")) return false;
        game.run(10);
        if (game.app.settings.musicOn == musicBefore || game.saves.loadSettings().musicOn != game.app.settings.musicOn ||
            game.audio.musicEnabled() != game.app.settings.musicOn || game.app.settings.soundEffectsOn != audioBefore || game.audio.muted() == game.app.settings.soundEffectsOn) {
            failure = "the music toggle did not persist";
            return false;
        }
        if (!tapView("ButtonMusic")) return false;
        game.run(10);
        if (game.app.settings.musicOn != musicBefore || game.saves.loadSettings().musicOn != musicBefore) {
            failure = "the music toggle did not restore";
            return false;
        }
        if (!tapView("ButtonMenu")) return false;
        if (!expectScene(aa::ui::scene_names::kLevelSelection, 120)) return false;
        game.run(30);
        shot("11_level_selection_back");
        if (!tapView("ButtonBack")) return false;
        if (!expectScene(aa::ui::scene_names::kChapterSelection, 120)) return false;
        return runExtras();
    }

    // The M6 tail from the chapter books: My Contraptions (the legal prompt, a new contraption built,
    // its background changed, test-played to completion, the toolbox step, saved, re-opened, deleted),
    // the credits, the exit dialog.
    bool runExtras() {
        auto* chapters = dynamic_cast<aa::ui::ChapterSelectionScene*>(game.scenes->activeScene());
        if (!chapters) {
            failure = "the books are not on top";
            return false;
        }
        // The My Contraptions book opens once the Classroom's chapter panel was shown, and the editor
        // strip lists the unlocked items; the walk grants the flag and the Classroom set as a finished
        // chapter would have (GameProgressUtils::CheckForNewLocationUnlocks / UnlockItems).
        if (!game.app.progress.myContraptions || !game.app.progress.itemUnlocked[static_cast<std::size_t>(aa::sim::ItemType::TennisBall)]) {
            game.app.progress.myContraptions = true;
            game.app.progress.unlockItems(0, false);
            game.app.saveProgress();
        }
        chapters->view()->refresh();
        chapters->view()->scrollToPage(aa::ui::ChapterSelectionView::kBookCount - 1);
        game.run(60);
        shot("12_books_my_contraptions");
        if (!tapView(("Button_" + std::to_string(aa::ui::ChapterSelectionView::kBookCount - 1)).c_str())) return false;
        if (!expectScene(aa::ui::scene_names::kMyContraptions, 120)) return false;
        auto* list = dynamic_cast<aa::ui::MyContraptionsScene*>(game.scenes->activeScene());
        game.run(20);
        // The legal prompt on the first visit only (the setting persists across runs).
        const bool legalSeen = game.app.settings.sandboxLegalAccepted;
        if (list->view()->legalDialog()->isVisible() == legalSeen) {
            failure = legalSeen ? "the legal prompt showed again after its acceptance" : "the legal prompt did not show on the first visit";
            return false;
        }
        if (!legalSeen) {
            shot("13_my_contraptions_legal");
            if (!tapButton(list->view()->legalDialog()->confirmButton(), "the legal prompt's confirm")) return false;
            game.run(20);
            if (list->view()->legalDialog()->isVisible() || !game.app.settings.sandboxLegalAccepted || !game.saves.loadSettings().sandboxLegalAccepted) {
                failure = "the legal prompt was not accepted / saved";
                return false;
            }
        }
        shot("13b_my_contraptions_empty");
        const int levelsBefore = game.app.locations[aa::ui::AppState::kSandboxLocation].levelCount();
        // A new contraption.
        if (!tapView("ButtonAdd")) return false;
        if (!expectScene(aa::ui::scene_names::kLevelLoading, 60)) return false;
        if (!expectScene(aa::ui::scene_names::kSandbox, 120)) return false;
        aa::sim::Session& session = game.sandboxScene->session();
        if (session.gameMode() != aa::sim::GameMode::Sandbox || !session.editorToolboxActive()) {
            failure = "the editor did not open in mode 1 with the editor strip";
            return false;
        }
        const std::string levelName = game.sandboxScene->levelName();
        if (levelName.empty() || game.app.locations[aa::ui::AppState::kSandboxLocation].levelCount() != levelsBefore + 1 ||
            !std::filesystem::exists(game.saves.sandboxLevelPath(levelName))) {
            failure = "the new level was not added to the index / saved on open";
            return false;
        }
        game.run(60);
        shot("14_editor_empty");
        // Three stars in a column under a tennis ball: the test play collects them all.
        const int star = static_cast<int>(aa::sim::ItemType::GoalStar);
        const int ball = static_cast<int>(aa::sim::ItemType::TennisBall);
        if (!dragFromStrip(session, "14_editor", star, aa::sim::Vec2(1.7f, 0.5f))) return false;
        if (!dragFromStrip(session, "14_editor", star, aa::sim::Vec2(1.7f, 1.1f))) return false;
        if (!dragFromStrip(session, "14_editor", star, aa::sim::Vec2(1.7f, 1.7f))) return false;
        if (!dragFromStrip(session, "14_editor", ball, aa::sim::Vec2(1.7f, 2.0f))) return false;
        if (session.state().typeCount(aa::sim::ItemType::GoalStar) != 3 || session.state().typeCount(aa::sim::ItemType::TennisBall) != 1) {
            failure = "the editor did not place the four items (stars " + std::to_string(session.state().typeCount(aa::sim::ItemType::GoalStar)) + ", balls " +
                      std::to_string(session.state().typeCount(aa::sim::ItemType::TennisBall)) + ")";
            return false;
        }
        float unusedX, unusedY;
        if (stripSlot(session, star, unusedX, unusedY)) {
            failure = "the star slot did not leave the strip after three stars";
            return false;
        }
        shot("14b_editor_built");
        // The background button cycles the backgrounds (the slide runs half a second).
        if (!tapView("ButtonBackground")) return false;
        game.run(15);
        shot("14c_editor_background_slide");
        game.run(30);
        if (session.level().backgroundIndex != 1) {
            failure = "the background button did not cycle the background";
            return false;
        }
        // Test play: the ball falls through the stars, the level completes (1.5 s countdown) and the
        // editor is back with the level marked tested.
        if (!tapView("ButtonPlay")) return false;
        game.run(10);
        if (session.gameMode() != aa::sim::GameMode::TestPlay || session.controllerState() != 4) {
            failure = "the play button did not start the test play";
            return false;
        }
        shot("15_test_play");
        for (int i = 0; i < 60 * 8 && !(session.controllerState() == 2 && session.tested()); ++i) game.frame(kFixedDt);
        if (session.gameMode() != aa::sim::GameMode::Sandbox || session.controllerState() != 2 || !session.tested()) {
            failure = "the test play did not complete back into the editor";
            return false;
        }
        game.run(30);
        shot("15b_test_play_completed");
        if (game.sandboxScene->view()->readyButton()->state() != aa::ui::button_state::kNormal) {
            failure = "the ready button is not enabled after the successful test";
            return false;
        }
        // The toolbox step: the ball moved into the (empty) level strip, taken out again, moved back in.
        if (!tapView("ButtonReady")) return false;
        game.run(30);
        if (session.gameMode() != aa::sim::GameMode::SandboxToolbox || session.editorToolboxActive()) {
            failure = "the ready button did not start the toolbox step";
            return false;
        }
        shot("16_toolbox_step");
        if (!dragToStrip(session, "16_toolbox_step", aa::sim::Vec2(1.7f, 2.0f))) return false;
        if (session.removedHandles().size() != 1 || session.toolbox().slotCount != 1) {
            failure = "the ball did not move into the strip";
            return false;
        }
        shot("16b_toolbox_step_ball_in_strip");
        if (!dragFromStrip(session, "16_toolbox_step", ball, aa::sim::Vec2(0.5f, 0.5f))) return false;
        if (!session.removedHandles().empty() || session.state().typeCount(aa::sim::ItemType::TennisBall) != 1) {
            failure = "the ball did not come back out of the strip";
            return false;
        }
        // Back from the step reloads the file: the ball is where it was saved.
        if (!tapView("ButtonBack")) return false;
        game.run(30);
        if (session.gameMode() != aa::sim::GameMode::Sandbox || !session.editorToolboxActive()) {
            failure = "the toolbox step's back button did not return to the editor";
            return false;
        }
        // The 5 → 1 transition un-fixes everything the step's file holds fixed (the stars in particular).
        for (const aa::sim::PhysicsObject& o : session.state().objects) {
            if (o.type != aa::sim::ItemType::WorldBound && o.type != aa::sim::ItemType::SelectionArea && (o.flags & aa::sim::object_flags::kFixed) != 0) {
                failure = "an object is still fixed after the toolbox step's back button";
                return false;
            }
        }
        // Back from the editor: the level and its thumbnail saved, the list on top.
        const std::filesystem::path thumb = game.saves.sandboxThumbPath(levelName);
        std::filesystem::file_time_type thumbBefore{};
        if (std::filesystem::exists(thumb)) thumbBefore = std::filesystem::last_write_time(thumb);
        game.run(60);   // a second apart from the open-time thumbnail (the file time's resolution)
        if (!tapView("ButtonBack")) return false;
        if (!expectScene(aa::ui::scene_names::kMyContraptions, 120)) return false;
        game.run(30);
        {
            const aa::sim::Level saved = aa::data::loadLevelFile(game.saves.sandboxLevelPath(levelName));
            int stars = 0, balls = 0;
            for (const aa::sim::LevelItem& li : saved.items) {
                stars += li.type == aa::sim::ItemType::GoalStar ? 1 : 0;
                balls += li.type == aa::sim::ItemType::TennisBall ? 1 : 0;
            }
            if (stars != 3 || balls != 1 || saved.backgroundIndex != 1 || !saved.tested) {
                failure = "the saved level does not hold the built contraption (stars " + std::to_string(stars) + ", balls " + std::to_string(balls) +
                          ", background " + std::to_string(saved.backgroundIndex) + ", tested " + std::to_string(saved.tested) + ")";
                return false;
            }
            for (const aa::sim::LevelItem& li : saved.items) {
                if (li.type != aa::sim::ItemType::WorldBound && li.type != aa::sim::ItemType::SelectionArea && !li.fixed()) {
                    failure = "the saved level's items are not all fixed";
                    return false;
                }
            }
            if (game.uiRenderer) {
                if (!std::filesystem::exists(thumb) || std::filesystem::last_write_time(thumb) <= thumbBefore) {
                    failure = "the thumbnail was not (re)written on save";
                    return false;
                }
            }
        }
        shot("17_my_contraptions_one_level");
        if (list->view()->button(0)->type() != aa::ui::LevelSelectorButton::kTypeUserLevel ||
            list->view()->button(1)->type() != aa::ui::LevelSelectorButton::kTypeAddLevel) {
            failure = "the list does not show the level and the add tile (types " + std::to_string(list->view()->button(0)->type()) + " / " +
                      std::to_string(list->view()->button(1)->type()) + ", " + std::to_string(game.app.locations[aa::ui::AppState::kSandboxLocation].levelCount()) +
                      " levels, title '" + game.app.meta(aa::ui::AppState::kSandboxLocation, 0).titleId + "')";
            return false;
        }
        // Re-open it: the contraption is back.
        if (!tapView("Button_0")) return false;
        if (!expectScene(aa::ui::scene_names::kLevelLoading, 60)) return false;
        if (!expectScene(aa::ui::scene_names::kSandbox, 120)) return false;
        game.run(60);
        if (session.state().typeCount(aa::sim::ItemType::GoalStar) != 3 || session.level().backgroundIndex != 1 || !session.tested()) {
            failure = "the re-opened level did not restore the contraption";
            return false;
        }
        shot("18_editor_reopened");
        // Esc = the back button (deferred to the next frame).
        game.scenes->keyPressed(aa::ui::SceneManager::kKeyBack);
        if (!expectScene(aa::ui::scene_names::kMyContraptions, 120)) return false;
        game.run(30);
        // The trash toggle, then the level: gone from the index and the disk.
        if (!tapView("ButtonTrash")) return false;
        game.run(10);
        if (!list->view()->trashButton()->isChecked() || !list->view()->button(0)->trashCan()->isVisible()) {
            failure = "the trash toggle did not show the trash cans";
            return false;
        }
        shot("19_my_contraptions_trash");
        if (!tapView("Button_0")) return false;
        game.run(30);
        if (game.app.locations[aa::ui::AppState::kSandboxLocation].levelCount() != levelsBefore || std::filesystem::exists(game.saves.sandboxLevelPath(levelName)) ||
            std::filesystem::exists(thumb) || game.saves.loadSandboxLocation().levelCount() != levelsBefore) {
            failure = "the level was not deleted";
            return false;
        }
        if (list->view()->button(levelsBefore)->type() != aa::ui::LevelSelectorButton::kTypeAddLevel) {
            failure = "the list did not refresh after the deletion";
            return false;
        }
        shot("19b_my_contraptions_deleted");
        // Back to the books and the main menu; the credits from the settings slider.
        if (!tapView("ButtonBack")) return false;
        if (!expectScene(aa::ui::scene_names::kChapterSelection, 120)) return false;
        game.run(40);   // the books' slide-in ends, the view becomes interactive
        if (!tapView("ButtonBack")) return false;
        if (!expectScene(aa::ui::scene_names::kMainMenu, 120)) return false;
        game.run(30);
        if (!tapView("SettingsSlider")) return false;
        game.run(40);
        if (!tapView("ButtonCredits")) return false;
        if (!expectScene(aa::ui::scene_names::kCredits, 120)) return false;
        auto* credits = dynamic_cast<aa::ui::CreditsScene*>(game.scenes->activeScene());
        game.run(60);
        const float creditsOffset = credits->view()->panel()->contentOffset().y;
        game.run(60);
        if (!credits->view()->autoScrolling() || !(credits->view()->panel()->contentOffset().y > creditsOffset)) {
            failure = "the credits do not scroll by themselves";
            return false;
        }
        if (credits->view()->versionLabel()->text() != aa::ui::CreditsView::kVersionText) {
            failure = "the version label reads " + credits->view()->versionLabel()->text();
            return false;
        }
        shot("20_credits");
        {
            // The groups after PostProductionLead (the original's overlap, fixed here): the list scrolled
            // so that the Operations title sits a tenth of the screen down.
            const aa::ui::View* content = credits->view()->panel()->contentView();
            for (const aa::ui::View* v : content->subviews()) {
                if (v->viewName() != "LabelTitleOperations") continue;
                credits->view()->panel()->setContentOffset(aa::ui::Point{0.0f, v->position().y - game.ctx.screen.nativeHeight * 0.1f}, false);
                game.run(2);
                shot("20b_credits_operations");
                break;
            }
        }
        if (!tapView("ButtonBack")) return false;
        if (!expectScene(aa::ui::scene_names::kMainMenu, 120)) return false;
        game.run(30);
        // The exit dialog: Esc shows it, Esc again dismisses it (the remake's cancel), its confirmation
        // asks the app to quit.
        game.scenes->keyPressed(aa::ui::SceneManager::kKeyBack);
        game.run(5);
        auto* menu = dynamic_cast<aa::ui::MainMenuScene*>(game.scenes->activeScene());
        if (!menu || !menu->view()->exitDialog()->isVisible()) {
            failure = "the exit dialog did not show";
            return false;
        }
        shot("21_exit_dialog");
        game.scenes->keyPressed(aa::ui::SceneManager::kKeyBack);
        game.run(5);
        if (menu->view()->exitDialog()->isVisible() || game.app.quitRequested) {
            failure = "the exit dialog did not dismiss on the back key";
            return false;
        }
        game.scenes->keyPressed(aa::ui::SceneManager::kKeyBack);
        game.run(5);
        if (!tapButton(menu->view()->exitDialog()->confirmButton(), "the exit dialog's confirm")) return false;
        game.run(20);   // the press completes after the button's zoom animations
        if (!game.app.quitRequested) {
            failure = "the exit dialog's confirmation did not ask to quit";
            return false;
        }
        return true;
    }
};

int runHeadless(Game& game) {
    game.initAudio(false);
    game.initScenes(nullptr);
    Walker walker{game, [](const std::string&) {}, ""};
    const bool ok = walker.run();
    if (!ok) {
        std::fprintf(stderr, "amazing_alex: headless walk failed: %s\n", walker.failure.c_str());
        return 1;
    }
    // The Classroom's saved state (the walk ends in the sandbox location).
    const aa::game::LocationState classroom = game.saves.loadLocation(game.app.locations[0]);
    int done = 0;
    for (int i = 0; i < 4; ++i) done += classroom.status(i) >= 3 ? 1 : 0;
    std::printf("headless: campaign + sandbox walk ok (level 0 done with %d stars, %d of the first four levels done, page 2 %s, comic %s, audio %s, "
                "%d sandbox levels left, saves in %s)\n",
                classroom.levelStarCount(0), done, classroom.status(4) >= 2 ? "unlocked" : "locked", classroom.visited ? "seen" : "unseen",
                game.audio.muted() ? "muted" : "on", game.saves.loadSandboxLocation().levelCount(), game.saves.dir().c_str());
    return 0;
}

// The fingers raylib reports each frame (GetTouchPointCount / Id / Position), turned into the Began /
// Moved / Ended events the scene stack takes (the original's nativeInput → TouchUtils::QueueTouches*
// model): a new id begins, a moved id moves, a vanished id ends at its last position — or is cancelled
// when the system took the stream (nativeInput action 3 → TouchesCancelled; takeTouchCancel). A pause
// cancels every finger (GameApp::touchCancel → SceneManager::TouchesCancel).
struct TouchTracker {
    struct Finger {
        int id;
        Vector2 pos;
        bool seen;
    };
    std::vector<Finger> fingers;

    void poll(Game& game) {
        if (takeTouchCancel()) cancelAll(game);
        for (Finger& f : fingers) f.seen = false;
        const int count = GetTouchPointCount();
        for (int i = 0; i < count; ++i) {
            const int id = GetTouchPointId(i);
            const Vector2 pos = GetTouchPosition(i);
            Finger* f = find(id);
            if (!f) {
                fingers.push_back(Finger{id, pos, true});
                game.scenes->touchesStarted(game.touch(id, pos.x, pos.y));
                continue;
            }
            f->seen = true;
            if (pos.x != f->pos.x || pos.y != f->pos.y) {
                aa::ui::TouchEvent e = game.touch(id, pos.x, pos.y);
                e.previous = aa::ui::Point{f->pos.x, f->pos.y};
                game.scenes->touchesMoved(e);
                f->pos = pos;
            }
        }
        for (std::size_t i = 0; i < fingers.size();) {
            if (fingers[i].seen) {
                ++i;
                continue;
            }
            game.scenes->touchesFinished(game.touch(fingers[i].id, fingers[i].pos.x, fingers[i].pos.y));
            fingers.erase(fingers.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }

    void cancelAll(Game& game) {
        for (const Finger& f : fingers) game.scenes->touchesCancel(game.touch(f.id, f.pos.x, f.pos.y));
        fingers.clear();
    }

private:
    Finger* find(int id) {
        for (Finger& f : fingers) {
            if (f.id == id) return &f;
        }
        return nullptr;
    }
};

// The frame clock of the real-time loop. The frames are paced by vsync (FLAG_VSYNC_HINT): raylib's own
// limiter sleeps through nanosleep, which overshoots by ~2 ms on macOS, so SetTargetFPS(60) alone gave
// ~55 fps presented unevenly on a 60 / 120 Hz display — a visible judder in every fast motion (the result
// panel's Alex). With vsync the swap blocks on every other frame instead (macOS queues one frame), so the
// measured frame time alternates (12 / 4 ms at 120 Hz) while the presentation is even: the step handed
// to the scenes is the mean of the last kWindow frames, which is the presentation interval.
class FrameClock {
public:
    float step(float measured) {
        if (measured > kMaxDt) measured = kMaxDt;
        sum_ += measured - samples_[next_];
        samples_[next_] = measured;
        next_ = (next_ + 1) % kWindow;
        if (count_ < kWindow) ++count_;
        return sum_ / static_cast<float>(count_);
    }

private:
    static constexpr int kWindow = 8;
    static constexpr float kMaxDt = 0.1f;   // a stall (a debugger, a window drag) is not replayed
    float samples_[kWindow] = {};
    float sum_ = 0.0f;
    int next_ = 0;
    int count_ = 0;
};

// The mouse-as-one-finger state carried between frames of the desktop and Web loops below.
struct PointerState {
    Vector2 last{0.0f, 0.0f};
    bool down = false;
};

// The desktop window's minimum size (raylib's SetWindowMinSize → GLFW: the OS itself refuses a smaller
// window). Not applied on Web: there raylib's SetWindowMinSize clamps only the canvas *backing* size in its
// resize callback while web/index.html keeps the CSS size at 100 % of the viewport, so a phone-sized
// viewport (390×844) would be laid out for 480 px and squashed non-uniformly into 390 — the layout follows
// the real viewport instead (ScreenLayout::compute handles any aspect), handleResize only guarding the
// degenerate 0 px case.
constexpr int kMinWindowWidth = 480;
constexpr int kMinWindowHeight = 360;

// The one per-frame live-resize check shared by desktop, Web and Android: GLFW and Emscripten keep their
// dimensions current through their own callbacks; the bundled Android backend does the equivalent from
// the EGL surface and NativeActivity window events (a Fold transition can retain the same Activity). Not called
// on the --headless / --ui-screenshots scripted-walk paths.
void handleResize(Game& game) {
    if (!IsWindowResized()) return;
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    if (w < 1 || h < 1) return;   // a collapsed browser viewport: ScreenLayout::compute divides by both
    game.layout = aa::sim::ScreenLayout::compute(w, h, game.app.profilePixelScale);
    game.applyScreenLayout();
    if (game.scenes) game.scenes->relayoutAll(w, h);
}

// The per-frame touch / mouse / wheel / key handling shared by the desktop loop (runApp's advanceFrame)
// and WebRuntime::frame(): rotate/flip/undo/redo on the held item, otherwise scroll the scene under the
// pointer. `touchInput` selects TouchTracker over the single mouse-as-finger pointer; `wheelEnabled` is
// false on a mobile display, where there is no wheel to read.
void handleFrameInput(Game& game, TouchTracker& touches, bool touchInput, bool wheelEnabled, PointerState& pointer) {
    const Vector2 mouse = GetMousePosition();
    if (touchInput) {
        touches.poll(game);
    } else {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            game.scenes->touchesStarted(game.touch(kPointerId, mouse.x, mouse.y));
            pointer.down = true;
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            game.scenes->touchesFinished(game.touch(kPointerId, mouse.x, mouse.y));
            pointer.down = false;
        } else if (pointer.down && (mouse.x != pointer.last.x || mouse.y != pointer.last.y)) {
            aa::ui::TouchEvent event = game.touch(kPointerId, mouse.x, mouse.y);
            event.previous = aa::ui::Point{pointer.last.x, pointer.last.y};
            game.scenes->touchesMoved(event);
        }
        pointer.last = mouse;
    }
    // Esc / the Android BACK key (nativeKeyInput's 0x56, the same path as 0x28): the main menu answers it
    // with its exit dialog (MainMenuView::KeyDown). The press is read from the pressed-key queue rather
    // than IsKeyPressed: Android's back gesture delivers its down and up events in one batch, which
    // raylib's poll drains within a single frame, so the key's state is never seen down at a frame
    // boundary — only the queue records it.
    bool backPressed = false;
    for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) {
        if (key == KEY_ESCAPE || key == KEY_BACK) backPressed = true;
    }
    if (backPressed) game.scenes->keyPressed(aa::ui::SceneManager::kKeyBack);
    aa::sim::Session* session = nullptr;
    if (game.gameScene && game.scenes->activeScene() == game.gameScene) session = &game.gameScene->session();
    else if (game.sandboxScene && game.scenes->activeScene() == game.sandboxScene) session = &game.sandboxScene->session();
    // The wheel / trackpad: rotates the held item in a level, otherwise scrolls the list under the
    // pointer (the level, chapter and My Contraptions pages, the credits).
    const Vector2 wheel = wheelEnabled ? GetMouseWheelMoveV() : Vector2{0.0f, 0.0f};
    bool wheelUsed = false;
    if (session) {
        if (wheel.y != 0.0f && session->heldObject() >= 0) {
            session->rotateHeld(wheel.y > 0.0f ? 3.14159265f / 36.0f : -3.14159265f / 36.0f);
            if (pointer.down) session->pointerMove(kPointerId, aa::sim::Vec2(mouse.x, mouse.y));
            wheelUsed = true;
        }
        if (IsKeyPressed(KEY_F)) session->flipHeld();
        if (IsKeyPressed(KEY_Z)) session->undo();
        if (IsKeyPressed(KEY_Y)) session->redo();
    }
    if (!wheelUsed && (wheel.x != 0.0f || wheel.y != 0.0f)) {
        const bool taken = game.scenes->wheelScrolled(aa::ui::Point{mouse.x, mouse.y}, aa::ui::Point{wheel.x, wheel.y});
        // AA_WHEEL_DEBUG=1: every wheel sample with its time (tuning the trackpad gesture split).
        static const bool wheelDebug = std::getenv("AA_WHEEL_DEBUG") != nullptr;
        if (wheelDebug) std::fprintf(stderr, "wheel t=%.3f dx=%.4f dy=%.4f %s\n", GetTime(), wheel.x, wheel.y, taken ? "taken" : "-");
    }
}

#if defined(__EMSCRIPTEN__)
// emscripten_set_main_loop(..., true) deliberately throws to abandon the caller's stack. That clashes
// with the game's C++ top-level error boundary, so Web owns all frame state here and lets main return.
class WebRuntime {
public:
    explicit WebRuntime(AppOptions options) : options_(std::move(options)) {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
        InitWindow(options_.width, options_.height, "Amazing Alex");   // no SetWindowMinSize: see kMinWindowWidth
        SetTargetFPS(60);
        SetExitKey(KEY_NULL);
        try {
            game_ = std::make_unique<Game>(options_);
            atlases_ = std::make_unique<AtlasSet>();
            atlases_->load(game_->root);
            world_ = std::make_unique<WorldRenderer>(*atlases_);
            ui_ = std::make_unique<UiRenderer>(game_->resources);
            game_->uiRenderer = ui_.get();
            game_->initAudio(options_.audio);
            game_->initScenes(world_.get());
        } catch (...) {
            CloseWindow();
            throw;
        }
    }

    bool frame() {
        if (options_.maxFrames > 0 && frameIndex_++ >= options_.maxFrames) return false;
        const float dt = clock_.step(GetFrameTime());
        game_->clock = GetTime();
        // Keep polling for the release frame too: raylib removes the last browser touch before
        // reporting it, and TouchTracker turns that disappearance into touchesFinished().
        const bool touchInput = !touches_.fingers.empty() || GetTouchPointCount() > 0;
        handleResize(*game_);
        handleFrameInput(*game_, touches_, touchInput, !options_.mobile, pointer_);
        game_->frame(dt);
        if (game_->app.quitRequested) return false;
        BeginDrawing();
        ClearBackground(kClearColour);
        game_->scenes->draw(*ui_);
        ui_->endFrame();
        EndDrawing();
        return true;
    }

private:
    AppOptions options_;
    std::unique_ptr<Game> game_;
    std::unique_ptr<AtlasSet> atlases_;
    std::unique_ptr<WorldRenderer> world_;
    std::unique_ptr<UiRenderer> ui_;
    TouchTracker touches_;
    FrameClock clock_;
    PointerState pointer_;
    int frameIndex_ = 0;
};

std::unique_ptr<WebRuntime> g_webRuntime;
void webFrame() {
    if (!g_webRuntime->frame()) emscripten_cancel_main_loop();
}

int runWebApp(AppOptions options) {
    g_webRuntime = std::make_unique<WebRuntime>(std::move(options));
    emscripten_set_main_loop(webFrame, 0, false);
    return 0;
}
#endif

}  // namespace

int runApp(const AppOptions& optionsIn) {
    AppOptions options = optionsIn;
#if AA_DEFAULT_NOINTRO
    options.noIntro = true;
#endif
    // The scripted headless/screenshot walk is an explicit coverage of the normal launch sequence,
    // including both splash pages. Keep it stable even when a distributable build defaults to nointro.
    if (options.headless || !options.screenshotDir.empty()) options.noIntro = false;
#if defined(__EMSCRIPTEN__)
    return runWebApp(std::move(options));
#endif
    if (options.headless) {
        // No window (a mobile build still reads its APK through raylib's file reader).
        Game game(options);
        std::printf("assets: %s (profile %s)  saves: %s  locale: %s\n", game.root.dir().c_str(), game.root.manifest().profile.c_str(),
                    game.saves.dir().c_str(), game.localization.locale().c_str());
        std::fflush(stdout);
        return runHeadless(game);
    }

    SetTraceLogLevel(LOG_WARNING);
    // Android's NativeActivity surface resizes itself on a configuration change; the Android backend
    // reports that through IsWindowResized(), while FLAG_WINDOW_RESIZABLE remains desktop / Web only.
    SetConfigFlags((options.fullscreen ? FLAG_FULLSCREEN_MODE : 0) | FLAG_VSYNC_HINT | (options.mobile ? 0 : FLAG_WINDOW_RESIZABLE));
    // A mobile build's window is the display: a zero size takes it whole (a fixed one would add raylib's
    // own framebuffer letterbox on top of ours), and the layout is computed from what the surface is —
    // so the Game (and its ScreenLayout) is built after InitWindow.
    InitWindow(options.mobile ? 0 : options.width, options.mobile ? 0 : options.height, "Amazing Alex");
    setWindowIconFromAssets(options.assets);
    if (!options.mobile) SetWindowMinSize(kMinWindowWidth, kMinWindowHeight);
    // Vsync paces the frames (FrameClock); the cap at twice the refresh rate only bites when the swap does
    // not block (an occluded window), so the sleep's overshoot never lands between two refreshes.
    // (A mobile display's rate is not reported by raylib; its swap blocks at the display's rate anyway.)
    const int refreshRate = options.mobile ? 0 : GetMonitorRefreshRate(GetCurrentMonitor());
    SetTargetFPS((refreshRate > 0 ? refreshRate : 60) * 2);
    SetExitKey(KEY_NULL);
    if (options.mobile) {
        options.width = GetScreenWidth();
        options.height = GetScreenHeight();
    }
    std::unique_ptr<Game> gamePtr;
    try {
        gamePtr = std::make_unique<Game>(options);
    } catch (...) {
        CloseWindow();
        throw;
    }
    Game& game = *gamePtr;
    std::printf("assets: %s (profile %s)  saves: %s  locale: %s  window: %dx%d%s\n", game.root.dir().c_str(), game.root.manifest().profile.c_str(),
                game.saves.dir().c_str(), game.localization.locale().c_str(), options.width, options.height, options.mobile ? " (display)" : "");
    std::fflush(stdout);
    int exitCode = 0;
    {
        AtlasSet atlases;
        atlases.load(game.root);
        WorldRenderer world(atlases);
        UiRenderer ui(game.resources);
        game.uiRenderer = &ui;
        game.initAudio(options.audio);
        game.initScenes(&world);
        TouchTracker touches;
        // nativePause / nativeResume: GameApp::activate(false) → SceneManager::Pause(true) (the game scene
        // releases the held item and opens its pause menu), the fingers cancelled, the audio output
        // stopped; activate(true) → Pause(false) (a no-op for the scenes) and the audio restarted.
        // The hooks capture this scope's objects: the guard clears them on every way out, including an
        // exception unwinding to main (Android keeps pumping commands to the finished activity afterwards).
        struct HookGuard {
            ~HookGuard() { installLifecycleHooks(LifecycleHooks{}); }
        } hookGuard;
        installLifecycleHooks(LifecycleHooks{[&] {
                                                 touches.cancelAll(game);
                                                 game.scenes->pause(true);
                                                 game.audio.setSuspended(true);
                                             },
                                             [&] {
                                                 game.scenes->pause(false);
                                                 game.audio.setSuspended(false);
                                             }});

        auto drawFrame = [&]() {
            BeginDrawing();
            ClearBackground(kClearColour);
            game.scenes->draw(ui);
            ui.endFrame();
            EndDrawing();
        };

        if (!options.screenshotDir.empty()) {
            std::filesystem::create_directories(options.screenshotDir);
            Walker walker{game, [&](const std::string& name) {
                              BeginDrawing();
                              ClearBackground(kClearColour);
                              game.scenes->draw(ui);
                              ui.endFrame();
                              Image shot = LoadImageFromScreen();
                              EndDrawing();
                              const std::string path = (std::filesystem::path(options.screenshotDir) / (name + ".png")).string();
                              if (!ExportImage(shot, path.c_str())) {
                                  std::fprintf(stderr, "amazing_alex: cannot write %s\n", path.c_str());
                                  exitCode = 1;
                              }
                              UnloadImage(shot);
                          },
                          ""};
            if (!walker.run()) {
                std::fprintf(stderr, "amazing_alex: scripted walk failed: %s\n", walker.failure.c_str());
                exitCode = 1;
            }
        } else {
            PointerState pointer;
            FrameClock frameClock;
            int frameIndex = 0;
            auto advanceFrame = [&]() -> bool {
                if (WindowShouldClose() || (options.maxFrames > 0 && frameIndex++ >= options.maxFrames)) return false;
                const float dt = frameClock.step(GetFrameTime());
                game.clock = GetTime();
                // Before input handling, so this frame's hit-testing sees current geometry, not last frame's.
                // Android reports live surface/window changes through the same raylib flag as desktop/Web.
                handleResize(game);
                handleFrameInput(game, touches, options.mobile, !options.mobile, pointer);
                game.frame(dt);
                if (game.app.quitRequested) return false;
                drawFrame();
                return true;
            };
            while (advanceFrame()) {}
        }
        game.scenes.reset();
        ui.unload();
        game.audio.unload();
        atlases.unload();
    }
    gamePtr.reset();
    CloseWindow();
    return exitCode;
}

}  // namespace aa::platform
