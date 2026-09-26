// The UI engine's layout (docs/06 §1, docs/11 §1): frames computed by the ported UpdateViewAnchors for
// views of the shipped scenes at 1024×768 and 1280×720, against values derived by hand from the
// verified formulas; WrapText; the hit test of a scaled button. No window: a recording renderer.
#include "aa/data/frame_table_loader.h"
#include "aa/game/localization.h"
#include "aa/sim/screen_layout.h"
#include "aa/ui/animator.h"
#include "aa/ui/app_state.h"
#include "aa/ui/dialogs.h"
#include "aa/ui/extra_scenes.h"
#include "aa/ui/game_scene.h"
#include "aa/ui/menu_scenes.h"
#include "aa/ui/renderer.h"
#include "aa/ui/sandbox_scene.h"
#include "aa/ui/scene.h"
#include "test_support.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aa::ui;

namespace {

struct RecordingRenderer : Renderer {
    int sprites = 0;
    int rects = 0;
    void setState(const DrawState&) override {}
    void drawSprite(const SpriteRef&, float, float, float, float) override { ++sprites; }
    void drawColorRect(const Rect&, Color) override { ++rects; }
};

// The live-resize path (app.cpp's handleResize): recomputes ctx.screen.* from a fresh ScreenLayout, the
// way Game::applyScreenLayout() does. What a live resize deliberately never reloads is the sprite sheet /
// font *data* itself (a directory scan + JSON parse, unsafe mid-frame) — the scale multiplier applied to
// that already-loaded data (ResourceProxy::uiScale_, which ImageView::AutoResize and BitmapFont metrics
// read) is cheap, in-memory arithmetic and must track the live value, via ResourceProxy::setUiScale().
void applyResize(UiContext& ctx, int w, int h) {
    const aa::sim::ScreenLayout layout = aa::sim::ScreenLayout::compute(w, h);
    ctx.screen.nativeWidth = static_cast<float>(layout.width);
    ctx.screen.nativeHeight = static_cast<float>(layout.height);
    ctx.screen.anchorCorrectionX = layout.anchorAspectCorrectionX;
    ctx.screen.anchorCorrectionY = layout.anchorAspectCorrectionY;
    ctx.screen.pixelScale = layout.pixelScale;
    ctx.screen.uiScale = layout.pixelScale / 2.0f;
    ctx.screen.letterBox = layout.letterBox;
    ctx.screen.letterBoxFrameWidth = layout.playFieldNativeX();
    ctx.screen.widescreenScaling = layout.pixelScale < 1.0f;
    if (ctx.resources) ctx.resources->setUiScale(ctx.screen.uiScale);
}

std::string rectStr(const Rect& r) {
    return "{" + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h) + "}";
}

// Walks two view trees (assumed structurally identical — same scene, same construction code) in lockstep
// and reports every frame, pivot or scale that differs, by name path, up to a cap so one badly-off scene
// doesn't flood the output. Used to compare a live-resized scene's tree against a scene built fresh at the
// same size. The pivot matters as much as the frame: a zoom / pop animation and the HPIVOT / VPIVOT
// anchors both go through it, and a resize that reloads a sprite can put its atlas pivot back over a
// centre pivot the constructor set.
void compareFrames(const View* resized, const View* fresh, const std::string& path, std::vector<std::string>& mismatches) {
    if (!resized || !fresh) {
        if (resized != fresh) mismatches.push_back(path + ": one side is null");
        return;
    }
    // A subtree invisible on both sides (an unpopulated LevelSelectorButton slot's internals, still hidden
    // behind Setup()'s early return for a type this sweep's bare Fixture never populates) has no on-screen
    // effect either way — its exact stale frame_ is an implementation detail, not something worth pinning
    // byte-exact. Still recurse: a child could be visible even when its parent isn't.
    const bool bothHidden = !resized->isVisible() && !fresh->isVisible();
    const Rect a = resized->frame();
    const Rect b = fresh->frame();
    // A tiny (< 0.02 px) epsilon absorbs float-rounding noise between two independently-computed paths to
    // the same value (e.g. screenW*0.01f*100 vs the exact root frame), not a relaxation of the geometry
    // itself.
    constexpr float kEps = 0.02f;
    const auto close = [](float x, float y) { return std::fabs(x - y) < kEps; };
    if (!bothHidden && (!close(a.x, b.x) || !close(a.y, b.y) || !close(a.w, b.w) || !close(a.h, b.h))) {
        if (mismatches.size() < 25) mismatches.push_back(path + ": resized=" + rectStr(a) + " fresh=" + rectStr(b));
    }
    const Point pa = resized->pivot();
    const Point pb = fresh->pivot();
    if (!bothHidden && (!close(pa.x, pb.x) || !close(pa.y, pb.y))) {
        if (mismatches.size() < 25) {
            mismatches.push_back(path + ": pivot resized={" + std::to_string(pa.x) + "," + std::to_string(pa.y) + "} fresh={" +
                                 std::to_string(pb.x) + "," + std::to_string(pb.y) + "}");
        }
    }
    if (!bothHidden && !close(resized->scale(), fresh->scale())) {
        if (mismatches.size() < 25) {
            mismatches.push_back(path + ": scale resized=" + std::to_string(resized->scale()) + " fresh=" + std::to_string(fresh->scale()));
        }
    }
    const std::vector<View*>& subA = resized->subviews();
    const std::vector<View*>& subB = fresh->subviews();
    if (subA.size() != subB.size()) {
        mismatches.push_back(path + ": subview count " + std::to_string(subA.size()) + " vs " + std::to_string(subB.size()));
        return;
    }
    for (std::size_t i = 0; i < subA.size(); ++i) {
        const std::string name = subA[i] ? subA[i]->viewName() : "?";
        compareFrames(subA[i], subB[i], path + "/" + name, mismatches);
    }
}

// Each Fixture gets its own save directory: several tests build more than one (a "resized" one reused
// across a resize sweep alongside a fresh one per comparison point), and progress/sandbox-index state
// (ChapterSelectionView::refresh's checkForNewLocationUnlocks, MyContraptionsView::refresh's
// loadSandboxLocation) is read from and written back to disk — sharing one directory across Fixtures let
// an earlier one's writes leak into a later one's "fresh" comparison.
int nextSaveDir() {
    static int n = 0;
    return n++;
}

struct Fixture {
    aa::data::AssetRoot root;
    ResourceProxy resources;
    aa::game::Localization localization;
    Animator animator;
    UiContext ctx;
    AppState app;
    aa::game::SaveStore saves;

    explicit Fixture(int w, int h, bool unlockAll = false, const std::string& saveDir = "")
        : root(assetsDir()),
          saves(!saveDir.empty() ? saveDir
                                  : std::filesystem::temp_directory_path().string() + "/aa_ui_layout_saves_" + std::to_string(nextSaveDir())) {
        const aa::sim::ScreenLayout layout = aa::sim::ScreenLayout::compute(w, h);
        ctx.screen.nativeWidth = static_cast<float>(w);
        ctx.screen.nativeHeight = static_cast<float>(h);
        ctx.screen.anchorCorrectionX = layout.anchorAspectCorrectionX;
        ctx.screen.anchorCorrectionY = layout.anchorAspectCorrectionY;
        ctx.screen.pixelScale = layout.pixelScale;
        ctx.screen.uiScale = layout.pixelScale / 2.0f;
        ctx.screen.letterBox = layout.letterBox;
        ctx.screen.letterBoxFrameWidth = layout.playFieldNativeX();
        ctx.screen.widescreenScaling = layout.pixelScale < 1.0f;
        resources.load(root, ctx.screen.uiScale);
        localization.setLocale("en_EN", aa::data::loadTextTableFile(root.textsPath("en_EN")));
        ctx.resources = &resources;
        ctx.localization = &localization;
        ctx.animator = &animator;
        app.assets = &root;
        app.saves = &saves;
        app.localization = &localization;
        app.unlockAllLevels = unlockAll;
        app.loadCatalogue();
    }
};

}  // namespace

TEST_CASE("ui layout: MainMenu at 1024x768") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    MainMenuScene scene(f.ctx, f.app);
    scene.init();
    MainMenuView* v = scene.view();
    // ButtonPlay: HCENTER / TOP at Y 65 %, the 400×400 background at uiScale 0.5 → 200×200.
    const Rect play = v->playButton()->frame();
    CHECK(play.w == 200.0f);
    CHECK(play.h == 200.0f);
    CHECK(play.x == 412.0f);
    CHECK(play.y == 500.0f);   // ceil(768 · 0.65)
    // ImageLogo (1413×823 → 707×412): centred on ButtonPlay, its bottom 1 % above the button's top.
    const Rect logo = v->logo()->frame();
    CHECK(logo.w == 707.0f);
    CHECK(logo.h == 412.0f);
    CHECK(logo.x == 159.0f);   // ceil(512 − 353.5)
    CHECK(logo.y == 81.0f);    // ceil(500 − 412 − 7.68)
    // SettingsSlider at 3 % / 3 % of its parent PanelTop (full screen).
    const Rect slider = v->settingsSlider()->frame();
    CHECK(slider.x == 31.0f);
    CHECK(slider.y == 24.0f);
    CHECK(v->panelTop()->frame().w == 1024.0f);
}

TEST_CASE("ui layout: MainMenu at 1280x720 (anchor aspect correction)") {
    AA_REQUIRE_ASSETS();
    Fixture f(1280, 720);
    CHECK(f.ctx.screen.anchorCorrectionX == doctest::Approx(0.7777778f));
    CHECK(f.ctx.screen.anchorCorrectionY == doctest::Approx(1.09375f));
    MainMenuScene scene(f.ctx, f.app);
    scene.init();
    MainMenuView* v = scene.view();
    const Rect play = v->playButton()->frame();
    CHECK(play.x == 540.0f);
    CHECK(play.y == 468.0f);
    const Rect logo = v->logo()->frame();
    CHECK(logo.x == 287.0f);
    CHECK(logo.y == 49.0f);    // ceil(468 − 412 − 7.2 · 1.09375)
    // The slider's percentages are multiplied by the correction factors (a target view exists).
    const Rect slider = v->settingsSlider()->frame();
    CHECK(slider.x == 30.0f);  // ceil(1280 · 0.03 · 0.7778)
    CHECK(slider.y == 24.0f);  // ceil(720 · 0.03 · 1.09375)
}

TEST_CASE("ui layout: GameView sidebar buttons") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(f.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    GameScene scene(f.ctx, f.app, templates);
    scene.init();
    GameView* gv = scene.gameView();
    // SidebarRight (257×272 → 129×136): its left edge at the screen's right edge (off screen) and at
    // y = 0.8 % · 1.0; ButtonPlay 2 % right of it, vertically centred on it.
    const Rect right = gv->sidebarRight()->frame();
    CHECK(right.x == 1024.0f);
    CHECK(right.y == 7.0f);
    CHECK(right.w == 129.0f);
    const Rect play = gv->playButton()->frame();
    CHECK(play.w == 100.0f);   // BUTTON_LARGE_BASE 200 → 100
    CHECK(play.x == 1045.0f);  // ceil(1024 + 1024 · 0.02 · 1.0)
    CHECK(play.y == 25.0f);    // ceil(7 + 136/2 − 50)
    // SidebarButtonArea (664×242 → 332×121) hangs off the left edge; the pause button 7 % from its right.
    const Rect area = gv->sidebarButtonArea()->frame();
    CHECK(area.x == -332.0f);
    CHECK(area.w == 332.0f);
    const Rect pause = gv->pauseButton()->frame();
    CHECK(pause.w == 71.0f);   // BUTTON_SMALL_BASE 141 → 70.5 → 71
    CHECK(pause.x == 238.0f);  // ceil(332 − 71 − 332 · 0.07): the target is the parent, so its width is the base
}

TEST_CASE("ui layout: GameView sidebar caches follow a live resize") {
    // The live-resize path re-derives leftOpenX_/leftShownX_/leftHiddenX_ and rightShown_/rightHidden_
    // (GameView::relayout) and snaps the sidebars to whichever endpoint matches the current menuState_ /
    // controlsShown_ — checked against a scene built fresh at the new size with the same menu state, not
    // just that the numbers changed.
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(resized.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);

    GameScene scene(resized.ctx, resized.app, templates);
    scene.init();
    scene.gameView()->openPauseMenu(false);
    resized.animator.update(0.0f);   // resolves openPauseMenu(false)'s zero-duration hideGameControls
    REQUIRE(scene.gameView()->menuState() == GameView::MenuState::Open);

    applyResize(resized.ctx, 1280, 720);
    scene.relayout(1280, 720);

    Fixture fresh(1280, 720);
    GameScene freshScene(fresh.ctx, fresh.app, templates);
    freshScene.init();
    freshScene.gameView()->openPauseMenu(false);
    fresh.animator.update(0.0f);

    GameView* gv = scene.gameView();
    GameView* fgv = freshScene.gameView();
    CHECK(gv->sidebarButtonArea()->frame().x == fgv->sidebarButtonArea()->frame().x);
    CHECK(gv->sidebarButtonArea()->frame().y == fgv->sidebarButtonArea()->frame().y);
    CHECK(gv->sidebarBackground()->frame().x == fgv->sidebarBackground()->frame().x);
    CHECK(gv->sidebarRight()->frame().x == fgv->sidebarRight()->frame().x);
    CHECK(gv->playButton()->frame().x == fgv->playButton()->frame().x);
    CHECK(gv->pauseButton()->frame().x == fgv->pauseButton()->frame().x);
    // Not the stale, 1024-wide endpoint.
    CHECK(gv->sidebarRight()->frame().x != 1024.0f);
}

TEST_CASE("ui layout: GameView resize during an in-flight controls slide snaps to the fresh endpoint") {
    // Regression test for Animator::completeAnimation: FinishAnimation alone leaves the item in the
    // list, so the very next Animator::update() re-applies its stale, pre-resize target frame on top of
    // the fresh geometry GameView::relayout() just computed. rightShown_/rightHidden_ are anchored off
    // the right screen edge, so — unlike the left sidebar's asset-width-only endpoints — their endpoint
    // actually moves with the window width, which is what makes this discriminating.
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(resized.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);

    GameScene scene(resized.ctx, resized.app, templates);
    scene.init();
    scene.gameView()->showGameControls(true);
    resized.animator.update(1.0f / 60.0f);   // mid-flight: far short of kMenuAnim's 0.2s duration

    applyResize(resized.ctx, 1280, 720);
    scene.relayout(1280, 720);
    resized.animator.update(1.0f / 60.0f);   // a stale item left in the list would re-apply here

    Fixture fresh(1280, 720);
    GameScene freshScene(fresh.ctx, fresh.app, templates);
    freshScene.init();
    freshScene.gameView()->showGameControls(false);
    fresh.animator.update(0.0f);   // resolves showGameControls(false)'s zero-duration slide

    GameView* gv = scene.gameView();
    GameView* fgv = freshScene.gameView();
    CHECK(gv->sidebarRight()->frame().x == fgv->sidebarRight()->frame().x);
    CHECK(gv->playButton()->frame().x == fgv->playButton()->frame().x);
}

TEST_CASE("ui layout: LevelSelectionView's button grid follows a live resize") {
    // layoutSlots() re-derives every slot's grid rect from the SelectorArea percentages after a resize —
    // checked against the same scene built fresh at the new size (the buttons carry no Relative/Anchor
    // data of their own, so the generic View::relayout() recursion alone would leave them untouched).
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    LevelSelectionScene scene(resized.ctx, resized.app);
    scene.init();
    const Rect before = scene.view()->button(0)->frame();

    applyResize(resized.ctx, 1280, 720);
    scene.relayout(1280, 720);

    Fixture fresh(1280, 720);
    LevelSelectionScene freshScene(fresh.ctx, fresh.app);
    freshScene.init();

    for (int i : {0, 4, 7, 20, LevelSelectionView::kSlots - 1}) {
        const Rect got = scene.view()->button(i)->frame();
        const Rect want = freshScene.view()->button(i)->frame();
        CHECK(got.x == want.x);
        CHECK(got.y == want.y);
        CHECK(got.w == want.w);
        CHECK(got.h == want.h);
    }
    // Not the stale 1024×768 rect.
    CHECK(scene.view()->button(0)->frame().w != before.w);
}

TEST_CASE("ui engine: an animation writes only the components it changes (remake deviation)") {
    // A resize's relayout() re-frames a view mid-fade: the original's Interpolate would put the frame it
    // captured at Animate time back on the next Update; the remake leaves the components an animation
    // never meant to drive alone — and still drives the ones it does.
    Fixture f(1024, 768);
    View v(f.ctx);
    v.init();
    v.setFrame(Rect{10.0f, 20.0f, 100.0f, 50.0f});
    AnimationParameters fade = AnimationParameters::fromView(v);
    fade.alpha = 0.0f;
    fade.duration = 1.0f;
    const int fadeId = f.animator.animate(&v, fade, nullptr);
    f.animator.update(0.25f);
    v.setFrame(Rect{0.0f, 0.0f, 640.0f, 480.0f});   // the relayout
    f.animator.update(0.25f);
    CHECK(v.frame().w == 640.0f);
    CHECK(v.frame().h == 480.0f);
    CHECK(v.alpha() == doctest::Approx(0.5f));
    f.animator.update(1.0f);   // reaches the duration; the next update applies the end values and retires it
    f.animator.update(0.0f);   // the end: apply(to) — the frame still untouched
    CHECK_FALSE(f.animator.isRunning(fadeId));
    CHECK(v.frame().w == 640.0f);
    CHECK(v.alpha() == 0.0f);
    AnimationParameters slide = AnimationParameters::fromView(v);
    slide.frame.x = 100.0f;
    slide.duration = 1.0f;
    f.animator.animate(&v, slide, nullptr);
    f.animator.update(0.5f);
    CHECK(v.frame().x == 50.0f);
    v.setAlpha(1.0f);   // not the slide's business
    f.animator.update(0.5f);
    CHECK(v.frame().x == 100.0f);
    CHECK(v.alpha() == 1.0f);
}

TEST_CASE("ui engine: relayout() completes the frame-moving animations under the view, not the fades") {
    Fixture f(1024, 768);
    View root(f.ctx);
    root.init();
    View mover(f.ctx);
    mover.init();
    View fader(f.ctx);
    fader.init();
    View outside(f.ctx);
    outside.init();
    root.addSubview(&mover);
    root.addSubview(&fader);
    AnimationParameters p = AnimationParameters::fromView(mover);
    p.frame.x = 200.0f;
    p.duration = 1.0f;
    const int moveId = f.animator.animate(&mover, p, nullptr);
    AnimationParameters q = AnimationParameters::fromView(fader);
    q.alpha = 0.0f;
    q.duration = 1.0f;
    const int fadeId = f.animator.animate(&fader, q, nullptr);
    AnimationParameters r = AnimationParameters::fromView(outside);
    r.frame.y = 50.0f;
    r.duration = 1.0f;
    const int outsideId = f.animator.animate(&outside, r, nullptr);
    f.animator.update(0.25f);
    root.relayout();
    CHECK_FALSE(f.animator.isRunning(moveId));   // snapped to its end
    CHECK(mover.frame().x == 200.0f);
    CHECK(f.animator.isRunning(fadeId));         // an alpha fade never touches the frame: left running
    CHECK(f.animator.isRunning(outsideId));      // not under root
}

TEST_CASE("ui layout: LevelSelectionView resized mid-fade keeps the fresh frame on the next animator update") {
    // The reviewer's repro: show(true) animates the view's own alpha over 0.3 s; the original Interpolate
    // would re-apply the 1024×768 frame captured at show() time on the update after the relayout — so the
    // view no longer covers the root (hit tests miss the grown area, the layout sits off-centre).
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    f.app.loadLocation(0);
    LevelSelectionScene scene(f.ctx, f.app);
    scene.init();
    scene.view()->show(true);
    f.animator.update(1.0f / 60.0f);
    applyResize(f.ctx, 1280, 720);
    scene.relayout(1280, 720);
    CHECK(scene.view()->frame().w == 1280.0f);
    f.animator.update(1.0f / 60.0f);
    CHECK(scene.view()->frame().w == 1280.0f);
    CHECK(scene.view()->frame().h == 720.0f);
    f.animator.update(1.0f);
    CHECK(scene.view()->frame().w == 1280.0f);
    CHECK(scene.view()->alpha() == 1.0f);
}

TEST_CASE("ui layout: a resize keeps the level list's page and its returning-from-game flag") {
    // relayout() is not a Refresh: a Refresh would snap the list back to the first unplayed level's page on
    // every resize frame of a click-drag and consume the returning_ flag set for the way back from a level.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    f.app.loadLocation(0);
    REQUIRE(f.app.location() != nullptr);
    REQUIRE(f.app.location()->levelCount() > LevelSelectionView::kPerPage);
    LevelSelectionScene scene(f.ctx, f.app);
    scene.init();
    scene.view()->show(false);
    scene.view()->update(0.0f);   // Refresh: page 0 (nothing played)
    CHECK(scene.view()->panel()->activePage() == 0);
    scene.view()->panel()->setActivePage(1, false);
    CHECK(scene.view()->panel()->activePage() == 1);
    scene.setReturningFromGame(true);
    f.app.currentLevel = LevelSelectionView::kPerPage + 1;   // a level on page 1
    applyResize(f.ctx, 1280, 720);
    scene.relayout(1280, 720);
    scene.view()->update(0.0f);
    CHECK(scene.view()->panel()->activePage() == 1);   // the page the user was on, not the first unplayed one
    CHECK(scene.view()->panel()->pageSize().w == 1280.0f);
    // The flag survived: the next Refresh (the way back from the level) still lands on currentLevel's page.
    scene.view()->panel()->setActivePage(0, false);
    scene.view()->show(false);
    scene.view()->update(0.0f);
    CHECK(scene.view()->panel()->activePage() == 1);
}

TEST_CASE("ui layout: a resize of My Contraptions neither refreshes the list nor turns level deleting off") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    MyContraptionsScene scene(f.ctx, f.app);
    scene.init();
    scene.view()->requestRefresh();
    scene.view()->update(0.0f);
    scene.view()->enableLevelDeleting(true);
    CHECK(scene.view()->trashButton()->isChecked());
    applyResize(f.ctx, 1280, 720);
    scene.relayout(1280, 720);
    scene.view()->update(0.0f);   // a pending Refresh would run here and reset the toggle
    CHECK(scene.view()->trashButton()->isChecked());
    CHECK(scene.view()->panel()->pageSize().w == 1280.0f);
}

TEST_CASE("ui layout: WrapText on a tip and a level title") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    LabelView label(f.ctx);
    label.init();
    label.setFont("FONT_4");
    label.setFrame(Rect{0.0f, 0.0f, 300.0f, 200.0f});
    label.setNonLocalizedText("You can use the Undo button to go back to an earlier configuration.");
    CHECK(label.lines().size() >= 2);
    const BitmapFont* font = f.resources.font("FONT_4");
    REQUIRE(font != nullptr);
    for (const std::string& l : label.lines()) CHECK(font->stringWidth(l) < 300.0f);
    // A literal "\n" breaks the line.
    label.setNonLocalizedText("First\\nSecond");
    REQUIRE(label.lines().size() == 2);
    CHECK(label.lines()[0] == "First");
    CHECK(label.lines()[1] == "Second");
    // Auto-resize takes the text extents: max line width, descent + lines × leading.
    LabelView autoLabel(f.ctx);
    autoLabel.init();
    autoLabel.setFont("FONT_3");
    autoLabel.setAutoResize(true, true);
    autoLabel.setNonLocalizedText("Playtime");
    CHECK(autoLabel.frame().w == std::floor(font->stringWidth("") + 0.5f) + std::floor(f.resources.font("FONT_3")->stringWidth("Playtime") + 0.5f));
    CHECK(autoLabel.frame().h == std::floor(f.resources.font("FONT_3")->maxDescending() + f.resources.font("FONT_3")->leading() + 0.5f));
    // MaxRows with the ellipsis.
    label.setMaxRows(1);
    label.setNonLocalizedText("You can use the Undo button to go back to an earlier configuration.");
    REQUIRE(label.lines().size() == 1);
    CHECK(label.lines()[0].size() >= 3);
    CHECK(label.lines()[0].substr(label.lines()[0].size() - 3) == "...");
}

TEST_CASE("ui layout: hit test of a scaled button and the scene draw") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    MainMenuScene scene(f.ctx, f.app);
    scene.init();
    scene.aboutToActivate();
    scene.activate();
    scene.update(0.016f);
    MainMenuView* v = scene.view();
    Button* play = v->playButton();
    // The play button is at (412, 500) 200×200: its centre hits, a point just outside misses.
    View* hit = scene.hitTest(Point{512.0f, 600.0f});
    CHECK(hit == play);
    hit = scene.hitTest(Point{412.0f + 200.0f + 5.0f, 600.0f});
    CHECK(hit != play);
    // Highlighted: the hit area doubles around the centre.
    TouchEvent e;
    e.id = 1;
    e.position = Point{512.0f, 600.0f};
    play->touchesStarted(e);
    CHECK(play->state() == button_state::kHighlighted);
    hit = scene.hitTest(Point{412.0f + 200.0f + 50.0f, 600.0f});
    CHECK(hit == play);
    // Drawing emits sprites (the background, the logo, the buttons).
    RecordingRenderer r;
    scene.draw(r);
    CHECK(r.sprites >= 4);
    play->touchesCancel(e);   // releases the shared processed-touch id for the next test case
}

TEST_CASE("ui layout: the settings slider opens downwards, the last-added button nearest the gear") {
    // SlidingButton::ShowMenu / LayoutMenuButtons [verified]: "Direction" DOWN; the inner button is bare
    // at (0, 0) with the background's size (BUTTON_LARGE_BASE 200 → 100), the slider's frame the same size.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    MainMenuScene scene(f.ctx, f.app);
    scene.init();
    SlidingButton* s = scene.view()->settingsSlider();
    CHECK(s->frame().w == 100.0f);
    CHECK(s->frame().h == 100.0f);
    CHECK(s->button().frame().x == 0.0f);
    CHECK(s->button().frame().y == 0.0f);
    CHECK(s->button().frame().w == 100.0f);
    REQUIRE(s->menu().subviews().size() == 3);   // ButtonCredits, ButtonMusic (remake), then ButtonAudio
    s->showMenu(false);
    const Rect m = s->menu().frame();
    CHECK(m.x == 0.0f);
    CHECK(m.y == 50.0f);                       // h / 2
    CHECK(m.w == 100.0f);
    CHECK(m.h == 50.0f + 3.0f * 100.0f);        // h / 2 + n · itemH
    CHECK(s->menu().isVisible());
    const View* credits = s->menu().subviews()[0];
    const View* music = s->menu().subviews()[1];
    const View* audio = s->menu().subviews()[2];
    auto centerOf = [](const View* v) { return Point{v->position().x + v->center().x, v->position().y + v->center().y}; };
    // DOWN: button i centred at menuH − (i + 0.5) · itemH; in the slider's space audio at h + 0.5 h, music at
    // h + 1.5 h, credits at h + 2.5 h, all centred under the gear (SetCenter: ceil(50 − 71 / 2) = 15).
    CHECK(audio->position().x == 15.0f);
    CHECK(centerOf(audio).y + m.y == 150.0f);
    CHECK(music->position().x == 15.0f);
    CHECK(centerOf(music).y + m.y == 250.0f);
    CHECK(credits->position().x == 15.0f);
    CHECK(centerOf(credits).y + m.y == 350.0f);
    s->hideMenu(false);
    CHECK(s->menu().frame().h == 0.0f);
    CHECK(s->menu().frame().y == 50.0f);
    CHECK_FALSE(s->menu().isVisible());
    // UP (the constructor's default) mirrors: the menu hangs above, button 0 at the top.
    s->setMenuDirection(false);
    s->showMenu(false);
    const Rect up = s->menu().frame();
    CHECK(up.y == -300.0f);
    CHECK(up.h == 350.0f);
    CHECK(centerOf(credits).y == 50.0f);
    CHECK(centerOf(music).y == 150.0f);
    CHECK(centerOf(audio).y == 250.0f);
    s->hideMenu(false);
}

namespace {

struct NamedScene : Scene {
    std::string sceneName;
    int inactivations = 0;
    int pauses = 0;
    NamedScene(UiContext& ctx, std::string name) : Scene(ctx), sceneName(std::move(name)) {}
    const char* name() const override { return sceneName.c_str(); }
    void inactivationComplete() override { ++inactivations; }
    void setPaused(bool paused) override { if (paused) ++pauses; }
};

}  // namespace

TEST_CASE("ui engine: SceneManager RemoveScene is middle-only, PushScene stacks duplicates") {
    // The "back to the books" path of LevelCompletedView::ButtonPressed on a chapter's last level.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    SceneManager manager(f.ctx);
    for (const char* n : {"Main", "Chapters", "Levels", "Loading", "Game"}) manager.registerScene(std::make_unique<NamedScene>(f.ctx, n));
    auto settle = [&] {
        for (int i = 0; i < 10 && manager.inTransition(); ++i) manager.update(1.0f / 60.0f);
        REQUIRE_FALSE(manager.inTransition());
    };
    manager.setRootScene("Main");
    settle();
    for (const char* n : {"Chapters", "Levels", "Loading", "Game"}) {
        manager.pushScene(n);
        settle();
    }
    REQUIRE(manager.stack().size() == 5);
    // The top scene is never removed, nor the root.
    CHECK_FALSE(manager.removeScene("Game"));
    CHECK_FALSE(manager.removeScene("Main"));
    CHECK(manager.stack().size() == 5);
    CHECK(manager.activeScene()->state() == scene_state::kActive);
    // A middle scene goes without any transition.
    CHECK(manager.removeScene("Levels"));
    CHECK(manager.stack().size() == 4);
    CHECK_FALSE(manager.inTransition());
    CHECK(static_cast<NamedScene*>(manager.scene("Levels"))->inactivations == 1);   // from the earlier push
    // Pushing the loading scene again stacks it twice; the game inactivates, the loading activates.
    CHECK(manager.pushScene("Loading"));
    CHECK(manager.stack().size() == 5);
    CHECK(manager.stack()[2] == manager.stack()[4]);
    settle();
    CHECK(manager.activeScene() == manager.scene("Loading"));
    CHECK(manager.activeScene()->state() == scene_state::kActive);
    CHECK(manager.scene("Game")->state() == scene_state::kInactive);
    // PopScenesUntil clears both copies and the game.
    CHECK(manager.popScenesUntil("Chapters"));
    settle();
    REQUIRE(manager.stack().size() == 2);
    CHECK(manager.activeScene() == manager.scene("Chapters"));
    CHECK(manager.activeScene()->state() == scene_state::kActive);
}

namespace {

// Records the tint every sprite is drawn with.
struct TintRecorder : Renderer {
    Color tint = Renderer::kNoTint;
    std::vector<Color> sprites;
    void setState(const DrawState&) override {}
    void drawSprite(const SpriteRef&, float, float, float, float) override { sprites.push_back(tint); }
    void drawColorRect(const Rect&, Color) override {}
    void setTint(Color c) override { tint = c; }
};

bool sameColor(Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

int glyphCount(const BitmapFont& font, const std::string& s) {
    int n = 0;
    for (int code : decodeUtf8(s)) n += font.isCharacterSupported(code) ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("ui layout: HighlightLabelView wraps without its markers and tints the highlighted fill (remake-only)") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    CHECK(HighlightLabelView::stripMarkers("a *b c* d") == "a b c d");
    const Color yellow{255, 214, 0, 255};
    HighlightLabelView label(f.ctx);
    label.init();
    label.setFont("FONT_4");   // outlined: laid out with FONT_4_OUTLINES, as OutlineLabelView
    CHECK(label.fontName() == "FONT_4_OUTLINES");
    label.setHighlightColor(yellow);
    label.setAnchor(FontAnchorH::Center, FontAnchorV::Top);
    label.setFrame(Rect{0.0f, 0.0f, 300.0f, 400.0f});
    const std::string text = "Hey, it's me, Alex! *Tap play* to get *the ball into the basket*.";
    label.setNonLocalizedText(text);
    REQUIRE(label.lines().size() >= 2);
    const BitmapFont* outline = f.resources.font("FONT_4_OUTLINES");
    const BitmapFont* fill = f.resources.font("FONT_4");
    REQUIRE(outline != nullptr);
    REQUIRE(fill != nullptr);
    for (const std::string& l : label.lines()) CHECK(outline->stringWidth(HighlightLabelView::stripMarkers(l)) < 300.0f);
    TintRecorder r;
    label.draw(r, label.frame());
    // Every glyph of the wrapped lines twice (the outline, then the fill over it); only the highlighted
    // fill is tinted.
    std::string visible;
    std::string highlighted;
    bool state = false;
    for (const std::string& l : label.lines()) {
        for (char c : l) {
            if (c == '*') state = !state;
            else (state ? highlighted : visible).push_back(c);
        }
    }
    std::string letters = highlighted;   // a line break's space goes with the wrap
    letters.erase(std::remove(letters.begin(), letters.end(), ' '), letters.end());
    CHECK(letters == "Tapplaytheballintothebasket");
    const int all = glyphCount(*outline, visible) + glyphCount(*outline, highlighted);
    CHECK(static_cast<int>(r.sprites.size()) == all + glyphCount(*fill, visible) + glyphCount(*fill, highlighted));
    const int tinted = static_cast<int>(std::count_if(r.sprites.begin(), r.sprites.end(), [&](Color c) { return sameColor(c, yellow); }));
    CHECK(tinted == glyphCount(*fill, highlighted));
    CHECK(sameColor(r.tint, Renderer::kNoTint));   // restored after the draw
}

TEST_CASE("ui engine: the level tip shows in the game beside the toolbox strip for its reading time; the info button toggles it (remake-only)") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(f.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    f.app.loadLocation(0);
    GameScene scene(f.ctx, f.app, templates);
    scene.init();
    GameView* gv = scene.gameView();
    constexpr float kDt = 1.0f / 60.0f;
    const auto step = [&] {
        scene.update(kDt);
        f.animator.update(kDt);
    };
    REQUIRE(scene.selectLevel(1));   // Catch That Ball: one shelf in the strip, a tutorial
    scene.activate();
    int frame = 0;
    for (; frame < 60 * 2 && !gv->isTipShown(); ++frame) step();
    REQUIRE(gv->isTipShown());   // after the view's show fade, with the level name
    CHECK(gv->tipButton()->isVisible());
    const std::string tip = f.localization.text(f.app.meta(0, 1).tipId);
    CHECK(gv->tipLabel()->text() == tip);
    // Up for the reading time plus the two fades.
    const float up = GameView::tipReadingTime(HighlightLabelView::stripMarkers(tip)) + 2.0f * GameView::kTipFade;
    int shownFrames = 0;
    bool placed = false;
    for (; shownFrames < 60 * 10 && gv->isTipShown(); ++shownFrames) {
        step();
        if (shownFrames == 60) {
            // The strip is out by now: the panel sits right of the info button and left of the strip, its
            // bottom on the button's, all on screen.
            const aa::sim::ScreenRect strip = scene.session().toolbox().getToolboxRectangle();
            const Rect panel = gv->tipPanel()->frame();
            const Rect button = gv->tipButton()->frame();
            CHECK(strip.left < 1024.0f);
            CHECK(panel.x >= button.x + button.w);
            CHECK(panel.x + panel.w <= strip.left);
            CHECK(panel.y + panel.h == doctest::Approx(button.y + button.h));
            CHECK(button.y + button.h <= 768.0f);
            CHECK(gv->tipLabel()->lines().size() >= 1);
            placed = true;
        }
    }
    CHECK(placed);
    CHECK(static_cast<float>(shownFrames) * kDt == doctest::Approx(up).epsilon(0.02));
    CHECK_FALSE(gv->isTipShown());
    // The info button shows it again and hides it, the tutorial left running.
    const bool tutorial = scene.session().tutorial().running;
    gv->buttonPressed(gv->tipButton()->id());
    CHECK(gv->isTipShown());
    gv->buttonPressed(gv->tipButton()->id());
    CHECK_FALSE(gv->isTipShown());
    CHECK(scene.session().tutorial().running == tutorial);
    // Opening the pause menu hides it.
    gv->buttonPressed(gv->tipButton()->id());
    REQUIRE(gv->isTipShown());
    gv->buttonPressed(gv->pauseButton()->id());
    CHECK_FALSE(gv->isTipShown());
    // The info button during the view's show (the menu held off until its slide ends) keeps it off.
    scene.activate();
    step();
    REQUIRE(gv->tipButton()->isVisible());
    REQUIRE_FALSE(gv->pauseButton()->isInteractable());
    gv->buttonAboutToBePressed(gv->tipButton()->id());
    gv->buttonPressed(gv->tipButton()->id());
    CHECK(gv->isTipShown());
    CHECK_FALSE(gv->pauseButton()->isInteractable());
    // The reading time: clamped at both ends.
    CHECK(GameView::tipReadingTime("") == GameView::kTipMinTime);
    CHECK(GameView::tipReadingTime(std::string(1000, 'a')) == GameView::kTipMaxTime);
}

TEST_CASE("ui engine: the info button slides off the left edge while the simulation runs and back in set-up (remake-only)") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(f.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    f.app.loadLocation(0);
    GameScene scene(f.ctx, f.app, templates);
    scene.init();
    GameView* gv = scene.gameView();
    constexpr float kDt = 1.0f / 60.0f;
    const auto step = [&] {
        scene.update(kDt);
        f.animator.update(kDt);
    };
    REQUIRE(scene.selectLevel(1));
    scene.activate();
    for (int i = 0; i < 60 * 2 && !gv->isTipShown(); ++i) step();
    REQUIRE(gv->isTipShown());
    Button* button = gv->tipButton();
    const float shownX = button->position().x;
    CHECK(shownX >= 0.0f);
    CHECK(button->isInteractable());
    scene.session().stopTutorial();
    scene.session().play();
    REQUIRE(scene.session().controllerState() == 4);
    // No presses from the first frame of the run; the tip goes with it.
    step();
    CHECK_FALSE(button->isInteractable());
    CHECK_FALSE(gv->isTipShown());
    for (int i = 0; i < 30; ++i) step();
    CHECK(button->position().x + button->size().w <= 0.0f);
    scene.session().stop();
    for (int i = 0; i < 60; ++i) step();
    REQUIRE(scene.session().controllerState() == 2);
    CHECK(button->position().x == doctest::Approx(shownX));
    CHECK(button->isInteractable());
}

TEST_CASE("ui engine: a button reset to Normal while pressed releases the shared touch id") {
    // Remake fix (docs/06 §1.1): ChapterSelectionView::Refresh resets the books' state on a page change;
    // a release on such a button must not keep the static processed-touch id.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    Button a(f.ctx);
    a.init();
    a.setBackground("BUTTON_SMALL_BASE");
    Button b(f.ctx);
    b.init();
    b.setBackground("BUTTON_SMALL_BASE");
    TouchEvent e;
    e.id = 0;
    e.position = Point{10.0f, 10.0f};
    a.touchesStarted(e);
    CHECK(a.state() == button_state::kHighlighted);
    a.setState(button_state::kNormal);
    a.touchesFinishedInside(e);
    CHECK(a.state() == button_state::kNormal);
    b.touchesStarted(e);
    CHECK(b.state() == button_state::kHighlighted);
    b.touchesCancel(e);
    CHECK(b.state() == button_state::kNormal);
}

TEST_CASE("ui engine: a tutorial hand stopped mid-fade stays hidden on the location's later levels") {
    // A touch stops the Classroom script while the hand is opaque; ImageHand keeps that pose. The next
    // levels have no script, so the state-2 re-show (ShowOverlay(1)) must not reveal the stale hand.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(f.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    f.app.loadLocation(0);
    GameScene scene(f.ctx, f.app, templates);
    scene.init();
    REQUIRE(scene.selectLevel(1));   // fetch_all_items_tutorial: the hand fades in below the toolbox
    scene.activate();
    const auto step = [&] {
        scene.update(1.0f / 60.0f);
        f.animator.update(1.0f / 60.0f);
    };
    for (int i = 0; i < 60 * 8 && !(scene.session().tutorial().running && scene.session().tutorial().hand.alpha >= 1.0f); ++i) step();
    REQUIRE(scene.session().tutorial().running);
    REQUIRE(scene.session().tutorial().hand.alpha >= 1.0f);
    REQUIRE(scene.tutorialView()->isVisible());
    scene.session().stopTutorial();   // GameView::ButtonPressed / a touch: the alpha stays 1
    step();
    CHECK_FALSE(scene.session().tutorial().running);
    CHECK_FALSE(scene.tutorialView()->isVisible());
    REQUIRE(scene.selectLevel(7));   // no script past level 6
    scene.activate();
    for (int i = 0; i < 60 * 3; ++i) step();
    REQUIRE(scene.session().controllerState() == 2);
    CHECK_FALSE(scene.session().tutorial().running);
    CHECK_FALSE(scene.tutorialView()->isVisible());
    // A run stopped on that level takes the 4 → 2 way back: still no hand.
    scene.session().play();
    REQUIRE(scene.session().controllerState() == 4);
    for (int i = 0; i < 30; ++i) step();
    scene.session().stop();
    for (int i = 0; i < 30; ++i) step();
    REQUIRE(scene.session().controllerState() == 2);
    CHECK_FALSE(scene.tutorialView()->isVisible());
}

TEST_CASE("ui engine: the pause chain (nativePause → GameApp::activate → SceneManager::Pause → GameScene::SetPaused)") {
    // GameScene::SetPaused(true) [verified 0x115e04]: set-up (state 2) → the level menu opens over the
    // paused session; simulation (state 4, not completing) → toggleSimulation back to set-up and the menu
    // opens; SetPaused(false) does nothing — the player resumes from the pause menu.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(f.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    f.app.loadLocation(1);   // a level without the classroom script (play() needs the input enabled)
    GameScene scene(f.ctx, f.app, templates);
    scene.init();
    REQUIRE(scene.selectLevel(0));
    scene.activate();
    for (int i = 0; i < 60; ++i) {   // the view's show animation completes (enablePauseMenu → Shown)
        scene.update(1.0f / 60.0f);
        f.animator.update(1.0f / 60.0f);
    }
    REQUIRE(scene.session().controllerState() == 2);
    REQUIRE_FALSE(scene.levelMenuOpen());
    REQUIRE(scene.gameView()->menuState() == GameView::MenuState::Shown);
    SUBCASE("set-up state") {
        scene.setPaused(true);
        CHECK(scene.levelMenuOpen());
        CHECK(scene.gameView()->menuState() == GameView::MenuState::Open);
        CHECK(scene.session().controllerState() == 2);
        scene.setPaused(false);   // no change: the menu stays until the player closes it
        CHECK(scene.levelMenuOpen());
        CHECK(scene.gameView()->menuState() == GameView::MenuState::Open);
    }
    SUBCASE("simulation state") {
        scene.session().play();
        REQUIRE(scene.session().controllerState() == 4);
        for (int i = 0; i < 30; ++i) {   // the menu's hide animation completes, the stop button shows
            scene.update(1.0f / 60.0f);
            f.animator.update(1.0f / 60.0f);
        }
        REQUIRE(scene.session().controllerState() == 4);
        REQUIRE(scene.gameView()->menuState() == GameView::MenuState::Hidden);
        REQUIRE(static_cast<ToggleButton*>(scene.gameView()->playButton())->isChecked());
        scene.setPaused(true);
        CHECK(scene.session().controllerState() == 2);
        CHECK(scene.levelMenuOpen());
        CHECK(scene.gameView()->menuState() == GameView::MenuState::Open);
        // The set-up overlay (the HUD back) waits for the menu to close: doFrame does not run meanwhile.
        for (int i = 0; i < 30; ++i) {
            scene.update(1.0f / 60.0f);
            f.animator.update(1.0f / 60.0f);
        }
        CHECK(scene.levelMenuOpen());
        CHECK(scene.gameView()->menuState() == GameView::MenuState::Open);
        scene.gameView()->buttonPressed(scene.gameView()->pauseButton()->id());   // continue
        REQUIRE_FALSE(scene.levelMenuOpen());
        for (int i = 0; i < 30; ++i) {
            scene.update(1.0f / 60.0f);
            f.animator.update(1.0f / 60.0f);
        }
        CHECK_FALSE(static_cast<ToggleButton*>(scene.gameView()->playButton())->isChecked());
        CHECK(scene.gameView()->menuState() == GameView::MenuState::Shown);
    }
    SUBCASE("SceneManager::Pause: the activating, the inactivating and the top scene, no duplicate guard") {
        // SceneManager::Pause [verified 0x10e8c4] calls SetPaused on +0x78 / +0x74 / the stack's top without
        // checking whether they coincide — a scene being pushed hears it twice (it is on the stack already).
        SceneManager manager(f.ctx);
        for (const char* n : {"Main", "Levels"}) manager.registerScene(std::make_unique<NamedScene>(f.ctx, n));
        manager.setRootScene("Main");
        for (int i = 0; i < 10 && manager.inTransition(); ++i) manager.update(1.0f / 60.0f);
        manager.pushScene("Levels");
        manager.pause(true);
        CHECK(static_cast<NamedScene*>(manager.scene("Main"))->pauses == 1);     // inactivating
        CHECK(static_cast<NamedScene*>(manager.scene("Levels"))->pauses == 2);   // activating + top
        for (int i = 0; i < 10 && manager.inTransition(); ++i) manager.update(1.0f / 60.0f);
        REQUIRE_FALSE(manager.inTransition());
        manager.pause(true);   // settled: the top scene only
        CHECK(static_cast<NamedScene*>(manager.scene("Main"))->pauses == 1);
        CHECK(static_cast<NamedScene*>(manager.scene("Levels"))->pauses == 3);
    }
}

TEST_CASE("ui layout: the dialogs of Dialogs.json (InfoDialog / LegalDialog / MessageDialog)") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    const aa::data::SceneTree dialogs = aa::data::SceneTree::loadFile(f.root.path("ui/scenes/Dialogs.json"));
    const aa::data::SceneTree scene = aa::data::SceneTree::loadFile(f.root.path("ui/scenes/MyContraptionsScene.json"));
    const aa::data::JsonNode view = scene.view("MyContraptionsView");
    const aa::data::JsonNode dlg = dialogs.view("Dialogs");

    // ErrorParsingLevel: a single-button MessageDialog over the whole screen; the MESSAGE_* pieces
    // (1020×79 / 80 / 79 sprite px → 510×40 / 40 / 40) make a 510-wide box 46.875 % of the height tall,
    // centred; the middle piece fills the rest.
    MessageDialog error(f.ctx, true);
    error.setViewName("ErrorParsingLevel");
    error.init(view.child("ErrorParsingLevel"), dlg);
    error.setFrame(Rect{0.0f, 0.0f, 1024.0f, 768.0f});
    error.updateViewAnchors(true, true);
    CHECK(error.frame().w == 1024.0f);
    const Rect bg = error.background()->frame();
    CHECK(bg.w == 510.0f);
    CHECK(bg.h == 360.0f);
    CHECK(bg.x == 257.0f);
    CHECK(bg.y == 204.0f);
    CHECK(error.background()->middle()->size().h == 280.0f);
    CHECK(error.background()->middle()->position().y == 40.0f);
    CHECK(error.background()->bottom()->position().y == 320.0f);
    CHECK(error.title()->text() == f.localization.text("TEXT_MSGBOX_NEGATIVE_TITLE"));
    CHECK(error.message()->text() == f.localization.text("TEXT_MSGBOX_ERROR_VERSION_FAIL"));
    // The message is wrapped after SetMessage's resize — the outline and the fill label alike (the fill
    // once kept the empty line of the zero-width wrap: an outline-only text).
    CHECK(error.message()->lines().size() >= 1);
    CHECK_FALSE(error.message()->lines()[0].empty());
    CHECK(error.message()->inner().lines() == error.message()->lines());
    // The message label spans the box minus the 3 % paddings.
    CHECK(error.message()->size().w == std::floor(510.0f - 2.0f * (1024.0f * 0.01f * 3.0f) + 0.5f));
    // SingleConfirmButton: centred on the box's bottom edge (the 200×200 BUTTON_LARGE_BASE at 0.5 → 100).
    REQUIRE(error.isSingle());
    CHECK(error.cancelButton() == nullptr);
    const Rect ok = error.confirmButton()->frame();
    CHECK(ok.w == 100.0f);
    CHECK(ok.x + ok.w * 0.5f == doctest::Approx(bg.x + bg.w * 0.5f).epsilon(0.01));
    CHECK(ok.y + ok.h * 0.5f == doctest::Approx(bg.y + bg.h).epsilon(0.01));
    // The back key presses the confirm button; without a delegate the dialog hides itself.
    error.show();
    CHECK(error.isVisible());
    CHECK(error.keyDown(SceneManager::kKeyBack));
    CHECK_FALSE(error.isVisible());

    // LegalTextDialog: an InfoDialog of type LegalDialog → the wide POPUP_* pieces (1620 → 810 px).
    InfoDialog legal(f.ctx);
    legal.setViewName("LegalTextDialog");
    legal.init(view.child("LegalTextDialog"), dlg);
    legal.setFrame(Rect{0.0f, 0.0f, 1024.0f, 768.0f});
    legal.updateViewAnchors(true, true);
    const Rect wide = legal.background()->frame();
    CHECK(wide.w == 810.0f);
    CHECK(wide.h == 360.0f);
    CHECK(wide.x == 107.0f);
    CHECK(legal.message()->text() == f.localization.text("TEXT_MYLEVELS_LEGAL_PROMPT"));
    CHECK(legal.message()->lines().size() > 1);
    CHECK(legal.message()->size().w == std::floor(810.0f - 2.0f * (1024.0f * 0.01f * 3.0f) + 0.5f));
    struct Confirmations : InfoDialogDelegate {
        int confirmed = 0;
        void messageConfirmed(int) override { ++confirmed; }
    } confirmations;
    legal.setDelegate(&confirmations);
    legal.show();
    legal.buttonPressed(legal.confirmButton()->id());
    CHECK(confirmations.confirmed == 1);
    CHECK(legal.isVisible());   // the delegate decides; the view hides nothing by itself
}

TEST_CASE("ui layout: a shown dialog takes the back key from the view under it (View::KeyDown forwards)") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    // The main menu: Esc shows the exit dialog — a two-button MessageDialog in the remake (SK_EXIT /
    // ITEM_ARE_YOU_SURE, the title above the message); Esc again reaches MessageDialog::keyDown through
    // MainMenuView → View::keyDown and cancels it (the original's InfoDialog confirmed: no way to stay).
    MainMenuScene menu(f.ctx, f.app);
    menu.init();
    MainMenuView* mv = menu.view();
    CHECK_FALSE(mv->exitDialog()->isVisible());
    CHECK(mv->keyDown(SceneManager::kKeyBack));
    CHECK(mv->exitDialog()->isVisible());
    MessageDialog* exit = mv->exitDialog();
    REQUIRE_FALSE(exit->isSingle());
    REQUIRE(exit->cancelButton() != nullptr);
    CHECK(exit->title()->text() == f.localization.text("SK_EXIT"));
    CHECK(exit->message()->text() == f.localization.text("ITEM_ARE_YOU_SURE"));
    CHECK(exit->title()->position().y + exit->title()->size().h <= exit->message()->position().y);
    CHECK(exit->message()->size().w == std::floor(510.0f - 2.0f * (1024.0f * 0.01f * 3.0f) + 0.5f));
    // The links slider of the original's top panel is gone.
    CHECK(mv->findViewByName("LinkSlider") == nullptr);
    CHECK_FALSE(f.app.quitRequested);
    CHECK(mv->keyDown(SceneManager::kKeyBack));
    CHECK_FALSE(exit->isVisible());
    CHECK_FALSE(f.app.quitRequested);
    // Shown again: the cross keeps playing, the check asks to quit.
    CHECK(mv->keyDown(SceneManager::kKeyBack));
    exit->buttonPressed(exit->cancelButton()->id());
    CHECK_FALSE(exit->isVisible());
    CHECK_FALSE(f.app.quitRequested);
    CHECK(mv->keyDown(SceneManager::kKeyBack));
    exit->buttonPressed(exit->confirmButton()->id());
    CHECK_FALSE(exit->isVisible());
    CHECK(f.app.quitRequested);
    // My Contraptions has no keyDown override: the base forwarding alone hands the key to the visible legal
    // prompt, whose confirmation accepts the terms and hides it; the hidden dialogs take nothing.
    f.app.settings.sandboxLegalAccepted = false;
    MyContraptionsScene myc(f.ctx, f.app);
    myc.init();
    MyContraptionsView* v = myc.view();
    v->show();
    REQUIRE(v->legalDialog()->isVisible());
    CHECK(v->keyDown(SceneManager::kKeyBack));
    CHECK(f.app.settings.sandboxLegalAccepted);
    CHECK_FALSE(v->legalDialog()->isVisible());
    CHECK_FALSE(v->keyDown(SceneManager::kKeyBack));   // nothing visible takes it: the scene pops
    // The parsing error: raised by the loading scene while the list is hidden under it, it waits for the
    // list's next show() (which hides every dialog first) instead of being hidden by it; raised while the
    // list is shown, it opens at once; hide() drops it with the rest and does not re-raise it later.
    v->hide();
    v->showParsingError();
    CHECK_FALSE(v->parsingErrorDialog()->isVisible());
    v->show();
    CHECK(v->parsingErrorDialog()->isVisible());
    v->hide();
    v->show();
    CHECK_FALSE(v->parsingErrorDialog()->isVisible());
    v->showParsingError();
    CHECK(v->parsingErrorDialog()->isVisible());
}

TEST_CASE("ui layout: the credits list, its version label and the back button's two places") {
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    CreditsScene scene(f.ctx, f.app);
    scene.init();
    CreditsView* v = scene.view();
    const View* content = v->panel()->contentView();
    REQUIRE(content->subviews().size() > 60);
    // ImageHeader: TOP at the panel's TOP + 95 % of the screen height — the list starts one screen down.
    const View* header = content->subviews()[0];
    CHECK(header->viewName() == "ImageHeader");
    CHECK(header->position().y == 730.0f);   // ceil(768 · 0.95)
    // LabelTitle hangs 5 % below the header, LabelVersion below it with the remake's version line.
    const View* title = content->subviews()[1];
    CHECK(title->viewName() == "LabelTitle");
    CHECK(title->position().y == std::ceil(header->position().y + header->size().h + 768.0f * 0.05f));
    CHECK(v->versionLabel()->text() == CreditsView::kVersionText);
    CHECK(v->versionLabel()->position().y > title->position().y);
    // The content ends 1.2 screen heights under the footer; the offset starts at 5.
    const View* footer = content->subviews().back();
    CHECK(footer->viewName() == "ImageFooter");
    const float* pad = footer->padding();
    CHECK(v->panel()->contentSize().h == doctest::Approx(footer->position().y + footer->size().h + pad[2] + pad[3] + 768.0f * 1.2f));
    CHECK(v->panel()->contentOffset().y == 5.0f);
    // The PostProductionLead group exists (a remake deviation: the original's table lacks it), so the
    // Operations title hangs 5 % of the screen height under its name label instead of overlapping the
    // earlier groups.
    auto find = [&](const char* name) -> const View* {
        for (const View* c : content->subviews()) {
            if (c->viewName() == name) return c;
        }
        return nullptr;
    };
    const View* lead = find("LabelPostProductionLead");
    const View* leadTitle = find("LabelTitlePostProductionLead");
    const View* operations = find("LabelTitleOperations");
    REQUIRE(lead != nullptr);
    REQUIRE(leadTitle != nullptr);
    REQUIRE(operations != nullptr);
    CHECK(leadTitle->position().y > find("LabelOperationsManager")->position().y);
    CHECK(operations->position().y == std::ceil(lead->position().y + lead->size().h + 768.0f * 0.05f));
    CHECK(operations->position().y > leadTitle->position().y);
    // The back button is anchored above the screen (its hidden place); Show(false) puts it 3 % below the top.
    CHECK(v->backButton()->position().y == -123.0f);
    v->show(false);
    CHECK(v->backButton()->position().y == 23.0f);
    CHECK(v->backButton()->isInteractable());
    v->hide(false);
    CHECK(v->backButton()->position().y == -123.0f);
    CHECK_FALSE(v->isVisible());
}

TEST_CASE("ui layout: the widescreen tweak at 1136x640 (books 0.85, comic frames 0.85, level buttons 0.89)") {
    // DeviceParams::AssetScalingForWidescreen, set for the iPhone 4 / 5 sizes (docs/11 §1); the remake's
    // rule is PixelScale < 1.
    AA_REQUIRE_ASSETS();
    SUBCASE("1136x640: on") {
        Fixture f(1136, 640);
        REQUIRE(f.ctx.screen.widescreenScaling);
        ChapterSelectionScene chapters(f.ctx, f.app);
        chapters.init();
        CHECK(chapters.view()->book(0)->scale() == 0.85f);
        ComicScene comic(f.ctx, f.app);
        comic.init();
        comic.setComicView(0, 0);   // ComicViewBegin1
        View* frame0 = comic.findView("Frame0");
        REQUIRE(frame0 != nullptr);
        CHECK(frame0->scale() == 0.85f);
        LevelSelectionScene levels(f.ctx, f.app);
        levels.init();
        View* panel = levels.findView("PanelTop");
        REQUIRE(panel != nullptr);
        CHECK(panel->scale() == 0.89f);
        View* number = levels.findView("LabelNumber");
        REQUIRE(number != nullptr);
        CHECK(number->scale() == 0.89f);
        View* star = levels.findView("StarOne");
        REQUIRE(star != nullptr);
        CHECK(star->scale() == 1.0f);
    }
    SUBCASE("1280x720: off") {
        Fixture f(1280, 720);
        REQUIRE_FALSE(f.ctx.screen.widescreenScaling);
        ChapterSelectionScene chapters(f.ctx, f.app);
        chapters.init();
        CHECK(chapters.view()->book(0)->scale() == 1.0f);
        // Five books (the chapters and My Contraptions; LotW / WoC dropped) in a panel of 5.5 book widths.
        CHECK(ChapterSelectionView::kBookCount == 5);
        CHECK(chapters.view()->book(4)->viewName() == "Button_4");
        CHECK(chapters.findView("Button_5") == nullptr);
        CHECK(chapters.view()->panel()->contentSize().w == doctest::Approx(1280.0f * 0.5f * 5.5f));
        LevelSelectionScene levels(f.ctx, f.app);
        levels.init();
        View* panel = levels.findView("PanelTop");
        REQUIRE(panel != nullptr);
        CHECK(panel->scale() == 1.0f);
    }
}

TEST_CASE("ui layout: the level button's thumbnail, frame hole and number share the panel centre after the refreshes") {
    // LevelSelectionView::Update runs RefreshThumbs before Refresh [verified]; Setup leaves FrameNormal's
    // pivot at the sprite's (the hole's centre) and never sets the button's state — so no ZoomIn animation
    // captures the views mid-setup and puts the old frames / pivots back (the M5 remake did, and the
    // thumbnail scaled about its top-left on the widescreen tweak — 10 §11 item 15 (d)).
    AA_REQUIRE_ASSETS();
    for (const int w : {1024, 1136}) {
        Fixture f(w, w == 1024 ? 768 : 640);
        f.app.loadLocation(0);
        for (int i = 0; i < 8; ++i) f.app.locationState.levels[static_cast<std::size_t>(i)].status = i < 4 ? 3 : 2;
        LevelSelectionScene levels(f.ctx, f.app);
        levels.init();
        levels.activate();
        auto run = [&](int frames) {
            for (int i = 0; i < frames; ++i) {
                levels.update(1.0f / 60.0f);
                f.animator.update(1.0f / 60.0f);
            }
        };
        run(30);
        levels.activationComplete();
        run(10);
        View* panel = levels.findView("PanelTop");
        View* thumb = levels.findView("ThumbNormal");
        View* frame = levels.findView("FrameNormal");
        View* number = levels.findView("LabelNumber");
        REQUIRE(panel != nullptr);
        REQUIRE(thumb != nullptr);
        REQUIRE(frame != nullptr);
        REQUIRE(number != nullptr);
        CHECK(static_cast<ImageView*>(thumb)->imageName() == "thumb:Playtime");
        CHECK(thumb->pivot().x == thumb->center().x);
        CHECK(thumb->pivot().y == thumb->center().y);
        // The pivots sit at the panel's centre: the frame's hole over the thumbnail's middle, the number on both.
        const Point c = panel->center();
        CHECK(frame->frame().x + frame->pivot().x == doctest::Approx(c.x).epsilon(0.02));
        CHECK(frame->frame().y + frame->pivot().y == doctest::Approx(c.y).epsilon(0.02));
        CHECK(thumb->frame().x + thumb->pivot().x == doctest::Approx(c.x).epsilon(0.02));
        CHECK(thumb->frame().y + thumb->pivot().y == doctest::Approx(c.y).epsilon(0.02));
        CHECK(number->frame().x + number->pivot().x == doctest::Approx(c.x).epsilon(0.02));
        CHECK(number->frame().y + number->pivot().y == doctest::Approx(c.y).epsilon(0.02));
        if (w == 1024) {
            CHECK(frame->pivot().x == 94.5f);   // LEVEL_FRAME1's pivot (189, 196) at the UI scale
            CHECK(frame->pivot().y == 98.0f);
            CHECK(frame->position().x == 6.0f);
            CHECK(frame->position().y == 17.0f);
        } else {
            CHECK(thumb->scale() == 0.89f);
            CHECK(frame->scale() == 0.89f);
        }
    }
}

namespace {

struct BackdropRenderer : Renderer {
    std::vector<Rect> rects;
    std::vector<Rect> sprites;
    void setState(const DrawState&) override {}
    void drawSprite(const SpriteRef&, float x, float y, float w, float h) override { sprites.push_back(Rect{x, y, w, h}); }
    void drawColorRect(const Rect& r, Color) override { rects.push_back(r); }
};

}  // namespace

TEST_CASE("ui layout: the letterbox backdrop — the fills and the border sprites under the world") {
    AA_REQUIRE_ASSETS();
    SUBCASE("1024x768: nothing to draw") {
        Fixture f(1024, 768);
        BackdropRenderer r;
        drawLetterBoxBackdrop(r, f.ctx, aa::sim::ScreenLayout::compute(1024, 768));
        CHECK(r.rects.empty());
        CHECK(r.sprites.empty());
    }
    SUBCASE("2400x1080: two side fills, the sprites against the play field") {
        Fixture f(2400, 1080);
        const aa::sim::ScreenLayout layout = aa::sim::ScreenLayout::compute(2400, 1080);
        BackdropRenderer r;
        drawLetterBoxBackdrop(r, f.ctx, layout);
        REQUIRE(r.rects.size() == 2);
        CHECK(r.rects[0].x == 0.0f);
        CHECK(r.rects[0].w == 481.0f);
        CHECK(r.rects[1].x == 1919.0f);
        if (f.resources.imageSize("BORDERIMAGE_LEFT").w <= 0.0f) {
            MESSAGE("skipped: the asset tree has no BORDER_BORDER sheet (import with --border-profile)");
            return;
        }
        // Sprites at PixelScale 1.40625: 278 / 281 × 1125 px, scaled to 1080 / 1125 = 0.96.
        REQUIRE(r.sprites.size() == 2);
        const Size left = f.resources.imageSize("BORDERIMAGE_LEFT");
        const float scale = 1080.0f / left.h;
        CHECK(r.sprites[0].h == 1080.0f);
        CHECK(r.sprites[0].w == std::ceil(left.w * scale));
        CHECK(r.sprites[0].x == std::ceil(layout.playFieldNativeX() - left.w * scale));
        CHECK(r.sprites[1].x == std::ceil(2400.0f - layout.playFieldNativeX() - 1.0f));
    }
    SUBCASE("1280x1153: the two bands, fill only — no sprites") {
        Fixture f(1280, 1153);
        BackdropRenderer r;
        drawLetterBoxBackdrop(r, f.ctx, aa::sim::ScreenLayout::compute(1280, 1153));
        REQUIRE(r.rects.size() == 2);
        CHECK(r.rects[0].h == 97.0f);
        CHECK(r.rects[1].y == 1153.0f - 97.0f);
        CHECK(r.sprites.empty());
    }
}

TEST_CASE("ui layout: the pause dim is the first child of GameView (over the world and the backdrop)") {
    AA_REQUIRE_ASSETS();
    Fixture f(2400, 1080);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(f.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    GameScene scene(f.ctx, f.app, templates);
    scene.init();
    CHECK(scene.findView("BorderLeft") == nullptr);
    View* dim = scene.findView("Background");
    REQUIRE(dim != nullptr);
    REQUIRE_FALSE(dim->parent()->subviews().empty());
    CHECK(dim->parent()->subviews()[0] == dim);
}

TEST_CASE("ui engine: a release off the window ends a button's press (remake-only)") {
    // The desktop pointer dragged off the window keeps reporting positions outside every view: the
    // EventHandler must still hand the started button its exit / finished-outside, or it stays Highlighted
    // with the static touch id claimed and the next press anywhere is swallowed.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    SceneManager manager(f.ctx);
    manager.registerScene(std::make_unique<MainMenuScene>(f.ctx, f.app));
    REQUIRE(manager.setRootScene(scene_names::kMainMenu));
    for (int i = 0; i < 60; ++i) {
        manager.update(1.0f / 60.0f);
        f.animator.update(1.0f / 60.0f);
    }
    auto* scene = dynamic_cast<MainMenuScene*>(manager.activeScene());
    REQUIRE(scene != nullptr);
    Button* play = scene->view()->playButton();
    const Rect r = play->frame();
    const Point inside{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    // Past the window's far corner: the raw-position hit test of a highlighted button (twice its size around
    // its local centre, no conversion [verified]) would still count a point near the origin as inside.
    const Point offWindow{1500.0f, 1200.0f};
    TouchEvent e;
    e.id = 0;
    e.position = inside;
    e.previous = inside;
    manager.touchesStarted(e);
    CHECK(play->state() == button_state::kHighlighted);
    e.previous = e.position;
    e.position = offWindow;
    manager.touchesMoved(e);
    CHECK(play->state() == button_state::kNormal);   // touchesMovedExit
    manager.touchesFinished(e);
    CHECK(play->state() == button_state::kNormal);
    // The second press is not swallowed by a still-claimed touch id.
    e.position = inside;
    e.previous = inside;
    manager.touchesStarted(e);
    CHECK(play->state() == button_state::kHighlighted);
    manager.touchesCancel(e);
    CHECK(play->state() == button_state::kNormal);
}

TEST_CASE("ui engine: the desktop wheel turns one level page per swipe and clamps at the ends (remake-only)") {
    // SceneManager::wheelScrolled → ScrollView::wheelScrolled with the sample streams of a macOS trackpad
    // (the shapes of the 2026-09-15 log): a swipe ramps up, its momentum tail decays for a second with
    // dropped-frame dips, a new swipe begins after a ~50 ms pause with a small sample or with a reversal.
    // One page per swipe; a mouse notch (1.0 after quiet) is a page; the first and the last page clamp; a
    // wheel over the back button (no ScrollView among its ancestors) is not consumed.
    AA_REQUIRE_ASSETS();
    Fixture f(1024, 768);
    f.app.loadLocation(0);
    for (auto& level : f.app.locationState.levels) level.status = 3;
    SceneManager manager(f.ctx);
    manager.registerScene(std::make_unique<LevelSelectionScene>(f.ctx, f.app));
    REQUIRE(manager.setRootScene(scene_names::kLevelSelection));
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            manager.update(1.0f / 60.0f);
            f.animator.update(1.0f / 60.0f);
        }
    };
    run(60);
    Scene* scene = manager.activeScene();
    REQUIRE(scene != nullptr);
    auto* panel = dynamic_cast<ScrollView*>(scene->findView("PanelLevelContent"));
    auto* pages = dynamic_cast<PageControl*>(scene->findView("LevelPages"));
    REQUIRE(panel != nullptr);
    REQUIRE(pages != nullptr);
    REQUIRE(panel->numberOfPages() == 2);
    CHECK(panel->activePage() == 0);
    const Point over{512.0f, 384.0f};
    // A mouse drag of more than half a page first (the touch path), there and back: the wheel must not
    // depend on the touch filter's last drag (EndDragScrolling zeroes the fling after such a drag).
    auto drag = [&](float fromX, float toX) {
        TouchEvent e;
        e.id = 7;
        e.position = Point{fromX, over.y};
        e.previous = e.position;
        manager.touchesStarted(e);
        for (int i = 1; i <= 10; ++i) {
            run(1);
            e.previous = e.position;
            e.position = Point{fromX + (toX - fromX) * static_cast<float>(i) / 10.0f, over.y};
            e.time += 1.0 / 60.0;
            manager.touchesMoved(e);
        }
        manager.touchesFinished(e);
        run(40);
    };
    drag(900.0f, 100.0f);
    CHECK(panel->activePage() == 1);
    drag(100.0f, 900.0f);
    CHECK(panel->activePage() == 0);
    // A sample every frame, horizontal (dx) unless told otherwise; `sign` flips the direction.
    auto stream = [&](std::initializer_list<float> samples, float sign, bool vertical = false) {
        for (const float v : samples) {
            const Point d = vertical ? Point{0.0f, v * sign} : Point{v * sign, 0.0f};
            CHECK(manager.wheelScrolled(over, d));
            run(1);
        }
    };
    // A swipe left: the ramp, the momentum's decay with two dips, the sparse tail — one page.
    const std::initializer_list<float> ramp{0.1f, 0.8f, 0.5f, 2.0f, 1.2f, 3.3f, 1.8f, 5.1f, 2.7f, 4.3f, 5.8f, 13.2f};
    const std::initializer_list<float> tail{12.9f, 12.1f, 11.3f, 10.8f, 10.5f, 9.9f, 4.6f, 9.1f, 8.6f, 8.1f, 7.6f, 7.2f, 6.6f, 6.2f,
                                            5.7f, 5.4f, 5.1f, 4.7f, 4.2f, 4.1f, 3.8f, 3.4f, 3.2f, 3.0f, 2.9f, 1.3f, 2.5f, 2.2f,
                                            2.1f, 1.9f, 1.6f, 1.6f, 1.5f, 1.4f, 1.3f, 1.2f, 1.0f, 1.0f, 0.8f, 0.8f, 0.8f, 0.6f,
                                            0.6f, 0.6f, 0.4f, 0.4f, 0.4f, 0.3f, 0.2f, 0.2f, 0.2f, 0.1f, 0.1f};
    stream(ramp, -1.0f);
    stream(tail, -1.0f);
    CHECK(panel->activePage() == 1);
    CHECK(pages->activePage() == 1);
    // The sparse end of a tail (a pause, then 0.1-samples): no page.
    run(3);
    stream({0.1f, 0.1f}, -1.0f);
    run(3);
    stream({0.1f, 0.1f, 0.1f}, -1.0f);
    CHECK(panel->activePage() == 1);
    // A swipe right inside the tail — a pause of three frames, then the ramp: the page back.
    run(3);
    stream({0.4f, 1.1f, 1.8f, 2.1f, 3.3f, 5.1f, 11.2f}, 1.0f);
    stream(tail, 1.0f);
    CHECK(panel->activePage() == 0);
    // A reversal without a pause (the finger lands mid-tail): the next page at once.
    stream({-1.7f, 0.2f, 4.4f, 5.4f, 7.0f, 11.0f, 11.1f, 10.6f}, -1.0f);
    stream(tail, -1.0f);
    CHECK(panel->activePage() == 1);
    // A dropped-frame pause mid-momentum resumes at the old magnitude: still the same swipe, no page back.
    run(30);
    stream({0.5f, 2.0f, 5.0f, 12.7f, 8.6f, 8.2f, 7.8f, 7.2f}, 1.0f);
    run(2);
    stream({6.8f, 6.4f, 5.9f, 5.6f, 5.3f, 4.8f, 4.5f, 4.2f, 3.8f, 3.4f, 3.2f, 3.0f, 2.7f, 2.5f, 2.2f, 2.1f, 1.9f, 1.6f}, 1.0f);
    CHECK(panel->activePage() == 0);
    // Clamped at the first page: a further swipe right changes nothing; a swipe left after the quiet goes on.
    run(30);
    stream(ramp, 1.0f);
    stream(tail, 1.0f);
    CHECK(panel->activePage() == 0);
    run(30);
    stream(ramp, -1.0f);
    stream(tail, -1.0f);
    CHECK(panel->activePage() == 1);
    // Clamped at the last page.
    run(30);
    stream(ramp, -1.0f);
    stream(tail, -1.0f);
    CHECK(panel->activePage() == 1);
    // A mouse notch (a lone vertical 1.0 after quiet) over the horizontal list turns a page, up and down.
    run(30);
    stream({1.0f}, 1.0f, true);
    run(30);
    CHECK(panel->activePage() == 0);
    stream({1.0f}, -1.0f, true);
    run(30);
    CHECK(panel->activePage() == 1);
    // Not over the list: nothing consumes it.
    View* back = scene->findView("ButtonBack");
    REQUIRE(back != nullptr);
    Point p = back->center();
    for (View* v = back; v->parent(); v = v->parent()) p = Point{p.x + v->frame().x, p.y + v->frame().y};
    CHECK(scene->hitTest(p) == back);
    CHECK_FALSE(manager.wheelScrolled(p, Point{0.0f, -1.0f}));
}


namespace {
// A sweep of sizes and aspect ratios a live resize should survive, applied to the SAME scene in sequence
// (not one fresh jump each) so a per-step error that only compounds across many consecutive resizes — the
// way an actual click-drag fires IsWindowResized() on many intermediate frames — would show up too. Ends
// by returning to the 1024x768 construction baseline: the "shrink then grow back" case.
const std::vector<std::pair<int, int>>& resizeSweepSizes() {
    static const std::vector<std::pair<int, int>> sizes = {
        {480, 360},    // app.cpp's desktop SetWindowMinSize floor
        {500, 400},
        {600, 900},    // narrow / tall
        {1400, 500},   // wide / short
        {800, 600},    // 4:3
        {700, 700},    // square
        {1920, 1080},  // large widescreen
        {1024, 768},   // back to the construction baseline
    };
    return sizes;
}
}  // namespace

TEST_CASE("ui layout: MainMenuScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    MainMenuScene scene(resized.ctx, resized.app);
    scene.init();
    // A never-opened SlidingMenu's buttons sit centred against menu_'s frame, which starts at the
    // degenerate {0,0,0,0} Init left it at until the first ShowMenu/HideMenu — showing it once puts both
    // trees in the same real (non-degenerate) state relayout() itself always produces.
    scene.view()->settingsSlider()->showMenu(false);
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.view()->update(0.0f);   // resolves Button/PageControl's dirty_-deferred child refresh
        Fixture fresh(w, h);
        MainMenuScene freshScene(fresh.ctx, fresh.app);
        freshScene.init();
        freshScene.view()->settingsSlider()->showMenu(false);
        freshScene.view()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.view(), freshScene.view(), "MainMenuView", mismatches);
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("ui layout: GameScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(resized.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    GameScene scene(resized.ctx, resized.app, templates);
    scene.init();
    scene.gameView()->hidePauseMenu(false);   // a definite state: default MenuState::Shown never matches
    resized.animator.update(0.0f);            // a never-shown GameView's actual (Hidden) rest position
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.gameView()->update(0.0f);
        Fixture fresh(w, h);
        GameScene freshScene(fresh.ctx, fresh.app, templates);
        freshScene.init();
        freshScene.gameView()->hidePauseMenu(false);
        fresh.animator.update(0.0f);
        freshScene.gameView()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.gameView(), freshScene.gameView(), "GameView", mismatches);
        compareFrames(scene.completedView(), freshScene.completedView(), "LevelCompletedView", mismatches);
        // Not tutorialView(): its one child, ImageHand, is placed by GameScene::updateTutorialView on every
        // frame the script runs (setHand), so its rest frame is meaningless — and differs, since the
        // constructor never resolves its HCENTER anchor while relayout() does.
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("ui layout: SandboxScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    const aa::sim::FrameTable frames = aa::data::loadFrameTableFile(resized.root.atlasJsonPath("GameItems"));
    const aa::sim::TemplateTable templates = aa::sim::initTemplates(frames);
    SandboxScene scene(resized.ctx, resized.app, templates);
    scene.init();
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.view()->update(0.0f);
        Fixture fresh(w, h);
        SandboxScene freshScene(fresh.ctx, fresh.app, templates);
        freshScene.init();
        freshScene.view()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.view(), freshScene.view(), "SandboxView", mismatches);
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("ui layout: LevelSelectionScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    resized.app.loadLocation(0);   // Refresh() bails out (no setup() at all) while app_->location() is null
    LevelSelectionScene scene(resized.ctx, resized.app);
    scene.init();
    scene.view()->show(false);
    scene.view()->update(0.0f);   // Refresh (the slots' Setup) once, at the baseline — a resize redoes only the geometry
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.view()->update(0.0f);
        Fixture fresh(w, h);
        fresh.app.loadLocation(0);
        LevelSelectionScene freshScene(fresh.ctx, fresh.app);
        freshScene.init();
        freshScene.view()->show(false);
        freshScene.view()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.view(), freshScene.view(), "LevelSelectionView", mismatches);
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("ui layout: ChapterSelectionScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    ChapterSelectionScene scene(resized.ctx, resized.app);
    scene.init();
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.view()->update(0.0f);
        Fixture fresh(w, h);
        ChapterSelectionScene freshScene(fresh.ctx, fresh.app);
        freshScene.init();
        freshScene.view()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.view(), freshScene.view(), "ChapterSelectionView", mismatches);
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("ui layout: CreditsScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    CreditsScene scene(resized.ctx, resized.app);
    scene.init();
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.view()->update(0.0f);
        Fixture fresh(w, h);
        CreditsScene freshScene(fresh.ctx, fresh.app);
        freshScene.init();
        freshScene.view()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.view(), freshScene.view(), "CreditsView", mismatches);
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("ui layout: MyContraptionsScene tracks a sweep of live resizes across sizes and aspect ratios") {
    AA_REQUIRE_ASSETS();
    Fixture resized(1024, 768);
    MyContraptionsScene scene(resized.ctx, resized.app);
    scene.init();
    // Refresh (the slots' Setup — the "add" tile here) once, at the baseline: a resize must not Refresh
    // again (a disk read per user level, a page snap), only redo the geometry.
    scene.view()->requestRefresh();
    scene.view()->update(0.0f);
    for (const auto& [w, h] : resizeSweepSizes()) {
        applyResize(resized.ctx, w, h);
        scene.relayout(w, h);
        scene.view()->update(0.0f);
        Fixture fresh(w, h);
        MyContraptionsScene freshScene(fresh.ctx, fresh.app);
        freshScene.init();
        freshScene.view()->requestRefresh();
        freshScene.view()->update(0.0f);
        std::vector<std::string> mismatches;
        compareFrames(scene.view(), freshScene.view(), "MyContraptionsView", mismatches);
        for (const std::string& m : mismatches) MESSAGE(w, "x", h, " ", m);
        CHECK(mismatches.empty());
    }
}

TEST_CASE("app state: --unlock-all opens every chapter and level without writing the save") {
    AA_REQUIRE_ASSETS();
    const std::string dir = std::filesystem::temp_directory_path().string() + "/aa_ui_layout_saves_unlock_all";
    std::filesystem::remove_all(dir);

    // A fresh save with the flag: every chapter reports open and every level of one at least unlocked, but
    // the stored flags (what SaveStore (de)serialises) are untouched — locked, as a fresh save really is.
    Fixture on(1024, 768, /*unlockAll=*/true, dir);
    for (int i = 0; i < aa::game::kLocationCount; ++i) {
        CHECK(on.app.progress.locationUnlocked(i) == true);
        CHECK(on.app.progress.locations[static_cast<std::size_t>(i)].unlocked == false);
    }
    on.app.loadLocation(3);   // Treehouse, the chapter with the four unlisted extra levels appended
    REQUIRE(on.app.location()->levelCount() == 36);
    for (int i = 0; i < on.app.location()->levelCount(); ++i) CHECK(on.app.locationState.status(i) >= 2);   // reported: unlocked
    // Stored: still locked past the first page (levels 0-3 of a fresh location are genuinely unlocked
    // already, LocationState::fresh — not because of the flag).
    for (int i = 4; i < on.app.location()->levelCount(); ++i) {
        CHECK(on.app.locationState.levels[static_cast<std::size_t>(i)].status < 2);
    }
    // Opening a level (LevelLoadingScene's tail: setLevelPlayed + saveLocation) writes nothing with the
    // flag on: no location file appears.
    {
        SceneManager manager(on.ctx);
        manager.registerScene(std::make_unique<LevelLoadingScene>(on.ctx, on.app));
        auto* loading = dynamic_cast<LevelLoadingScene*>(manager.scene(scene_names::kLevelLoading));
        REQUIRE(loading != nullptr);
        loading->init();
        loading->setLoadingLocation(LevelLoadingScene::kLocationCampaign, 9);
        loading->activationComplete();   // no GameScene registered: only the played / save tail runs
        REQUIRE(on.app.locationState.levels[9].played == true);
        CHECK(!std::filesystem::exists(dir + "/location_3.json"));
    }
    // A completion under the flag advances the session's state only: the save files stay untouched (a
    // cheat session is read-only for progress — a 3+n slot on the still-locked page 8–11 would otherwise
    // unlock the rest of the page through repairPages on the next load).
    on.app.locationState.markLevelAsDone(3, 9, *on.app.location());
    on.app.progress.addEarnedStars(3, 3);
    CHECK(on.app.locationState.status(9) == 6);
    on.app.saveLocation();
    on.app.saveProgress();
    CHECK(!std::filesystem::exists(dir + "/location_3.json"));

    // Reload the very same save directory without the flag: nothing the session did persisted.
    Fixture off(1024, 768, /*unlockAll=*/false, dir);
    CHECK(off.app.progress.locations[3].unlocked == false);
    CHECK(off.app.progress.locations[3].stars == 0);
    off.app.loadLocation(3);
    CHECK(off.app.locationState.status(9) == 1);
    CHECK(off.app.locationState.status(10) == 1);
    CHECK(off.app.locationState.status(4) == 1);
    CHECK(off.app.locationState.status(35) == 1);
}

