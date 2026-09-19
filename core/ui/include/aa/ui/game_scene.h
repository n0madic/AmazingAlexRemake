// UI::GameScene with GameView, LevelCompletedView and GameTutorialView [verified: GameView::Init /
// ButtonPressed / AnimationFinished / Show*/Hide*/Open*/Close* / ShowLevelName / HideLevelName /
// UpdateLevelInfo / startLevelCompleted / KeyDown / Draw, GameScene::Activate / Inactivate / ShowOverlay /
// SetPaused / PlayNextLevel / ReplayLevel / RestartLevel / LevelCompletionStarted / AnimationFinished,
// LevelCompletedView::Init / Show / Update / ShowPanels / ShowStars / ShowButtons / AnimationStarted /
// AnimationFinished / ButtonPressed / KeyDown, GameScreenController::setCompletedState /
// handleButtonRelease / setLevelMenuState / continuePlaying / PlayNextLevel — decompile + disassembly,
// 2026-09-14]. The scene owns the aa::sim::Session; the world is drawn by the platform subclass
// (drawWorld) between the scene background and the view tree, as GameView::Draw's doFrame did.
#pragma once

#include "aa/sim/session.h"
#include "aa/ui/menu_scenes.h"

#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

class GameScene;

// The tutorial hand (GameTutorialView): one image view moved / swapped / faded by the tutorial state.
class GameTutorialView : public View {
public:
    using View::init;
    GameTutorialView(UiContext& ctx, const aa::data::JsonNode& dict);
    void show();
    void hide();
    // The hand's screen position (px, y down), image (0 = pointer, 1 = pressed), alpha and angle.
    void setHand(Point screen, int image, float alpha, float angle);

private:
    ImageView* hand_ = nullptr;
    std::string pointImage_;
    std::string tapImage_;
    int image_ = -1;
    std::vector<std::unique_ptr<View>> owned_;
};

// The in-game HUD.
class GameView : public View, public ButtonDelegate, public AnimatorDelegate {
public:
    // Pause-menu states (+0xF4): 0 open, 1 closing, 2 shown (the pause button only), 3 hidden.
    enum class MenuState : int { Open = 0, Closing = 1, Shown = 2, Hidden = 3 };
    using View::init;
    GameView(UiContext& ctx, AppState& app, GameScene& scene, const aa::data::JsonNode& dict);
    void show();
    void hide();
    void update(float dt) override;
    void buttonPressed(int id) override;
    void buttonAboutToBePressed(int id) override;
    void animationFinished(int id) override;
    bool keyDown(int key) override;
    void touchesStarted(const TouchEvent& e) override;
    void touchesMovedInside(const TouchEvent& e) override;
    void touchesMovedOutside(const TouchEvent& e) override;
    void touchesFinishedInside(const TouchEvent& e) override;
    void touchesFinishedOutside(const TouchEvent& e) override;
    void touchesCancel(const TouchEvent& e) override;

    void relayout() override;
    // sidebarBackground_'s height compensates for sidebarButtonArea_ against the screen height — the
    // constructor derives it before its own final updateViewAnchors(true,true) call (SidebarBackground's
    // Anchor-driven children read its height); this must run before View::relayout()'s anchor pass too.
    void recomputeAutoSize() override;
    void showGameControls(bool animated);
    void hideGameControls(bool animated);
    void enableGameControls(bool animated);
    void disableGameControls(bool animated);
    void showSimulationControls();
    void hideSimulationControls();
    void showPauseMenu(bool animated);
    void hidePauseMenu(bool animated);
    void openPauseMenu(bool animated);
    void closePauseMenu(bool animated);
    void enablePauseMenu(bool animated);
    void disablePauseMenu(bool animated);
    void showLevelName(bool animated);
    void hideLevelName(bool animated);
    void updateLevelInfo();
    // startLevelCompleted: the Alex popup at the goal (grows 0.3 → 1.2 → 1.0).
    void startLevelCompleted(Point goalScreen);
    void hideLevelCompleteStartAnim();
    MenuState menuState() const { return menuState_; }
    Button* playButton() { return play_; }
    Button* pauseButton() { return pause_; }
    ImageView* sidebarRight() { return sidebarRight_; }
    ImageView* sidebarButtonArea() { return sidebarButtonArea_; }
    ImageView* sidebarBackground() { return sidebarBackground_; }
    OutlineLabelView* levelName() { return levelName_; }
    // The play button's centre in screen px (the level-0 tutorial target).
    Point playButtonCenter() const;

private:
    void setMenuInteraction(bool on);
    std::vector<View*> leftViews();
    std::vector<View*> rightViews();
    AppState* app_;
    GameScene* scene_;
    OutlineLabelView* levelName_ = nullptr;
    ImageView* sidebarButtonArea_ = nullptr;
    ImageView* sidebarBackground_ = nullptr;
    Button* pause_ = nullptr;
    OutlineLabelView* levelNumber_ = nullptr;
    Button* menu_ = nullptr;
    Button* restart_ = nullptr;
    Button* solutions_ = nullptr;
    ToggleButton* audio_ = nullptr;
    ToggleButton* music_ = nullptr;   // remake: the music-only switch
    ImageView* resultAlex_ = nullptr;
    View* circle_ = nullptr;
    ImageView* sidebarRight_ = nullptr;
    ToggleButton* play_ = nullptr;
    View* dim_ = nullptr;
    float leftOpenX_ = 0.0f;      // +0x490: the sidebar fully out
    float leftShownX_ = 0.0f;     // +0x498: the pause button peeking
    float leftHiddenX_ = 0.0f;    // +0x4a0: everything off screen
    Point rightShown_, rightHidden_;
    bool controlsShown_ = false;   // rightViews() at rightShown_ (true) vs. rightHidden_ (false)
    MenuState menuState_ = MenuState::Shown;
    int openAnim_ = 0, closeAnim_ = 0, showMenuAnim_ = 0, hideMenuAnim_ = 0, enableMenuAnim_ = 0, disableMenuAnim_ = 0;
    int showControlsAnim_ = 0, hideControlsAnim_ = 0, enableControlsAnim_ = 0, disableControlsAnim_ = 0;
    int nameShowAnim_ = 0, nameHideAnim_ = 0;
    int showAnim_ = 0, hideAnim_ = 0;
    bool wasRunning_ = false;
    bool alexAnimating_ = false;
    float alexTime_ = 0.0f;
    int alexSegment_ = 0;
    std::vector<std::unique_ptr<View>> owned_;
};

// The result panel.
class LevelCompletedView : public View, public ButtonDelegate, public AnimatorDelegate {
public:
    using View::init;
    LevelCompletedView(UiContext& ctx, AppState& app, GameScene& scene, const aa::data::JsonNode& dict);
    void show(Point goalScreen, int stars, bool improved);
    void hide();
    void update(float dt) override;
    void buttonPressed(int id) override;
    void animationStarted(int id) override;
    void animationFinished(int id) override;
    // Init's sizing tail (the panels from the Alex picture, the centre pivots), also run by relayout().
    void recomputeAutoSize() override;
    void relayout() override;
    bool keyDown(int key) override;
    Button* forwardButton() { return forward_; }
    Button* retryButton() { return retry_; }
    Button* menuButton() { return menu_; }
    View* panelResult() { return panelResult_; }
    ImageView* resultAlex() { return resultAlex_; }

private:
    void showPanels();
    void showStars(int stars);
    void showButtons();
    void hideButtons();
    void popButton(Button* b);
    AppState* app_;
    GameScene* scene_;
    View* panelResult_ = nullptr;
    View* panelBackground_ = nullptr;
    ImageView* resultAlex2_ = nullptr;
    View* backgroundBottom_ = nullptr;
    ImageView* resultAlex_ = nullptr;
    Button* menu_ = nullptr;
    Button* menuWoC_ = nullptr;
    Button* retry_ = nullptr;
    Button* retryWoC_ = nullptr;
    Button* forward_ = nullptr;
    Button* share_ = nullptr;
    ImageView* starEmpty_[3] = {nullptr, nullptr, nullptr};
    ImageView* star_[3] = {nullptr, nullptr, nullptr};
    ImageView* bestResult_ = nullptr;
    Point panelHome_;
    int starAnim_[3] = {0, 0, 0};
    int stars_ = 0;
    bool improved_ = false;
    bool alexAnimating_ = false;
    float alexTime_ = 0.0f;
    int alexSegment_ = 0;
    Point alexStart_, alexDelta_;
    std::vector<std::unique_ptr<View>> owned_;
};

// GameScene: the level session plus its HUD. The platform subclass draws the world.
class GameScene : public GameSceneBase {
public:
    GameScene(UiContext& ctx, AppState& app, const aa::sim::TemplateTable& templates);
    const char* name() const override { return scene_names::kGame; }
    void init() override;
    void activate() override;
    void inactivate() override;
    void update(float dt) override;
    void draw(Renderer& renderer) override;
    void relayout(int width, int height) override;
    bool keyDown(int key) override;
    // GameScene::SetPaused(true) [verified: 0x115e04]: in the set-up state the held item is released and
    // the pause menu opens without animation; while the simulation runs (and the level is not completing)
    // it is stopped first. Coming back (false) does nothing.
    void setPaused(bool paused) override;

    // GameApp::selectLevel: loads the level file of the current location's level and prepares the
    // session (GameStateUtils::CreateNew + prepareForNewLevel).
    bool selectLevel(int level);
    // GameScene::PlayNextLevel / ReplayLevel / RestartLevel.
    void playNextLevel();
    void replayLevel();
    void restartLevel();
    // GameView's pause button: setLevelMenuState / continuePlaying(false).
    void setLevelMenu(bool open);
    bool levelMenuOpen() const { return menuOpen_; }
    aa::sim::Session& session() { return *session_; }
    const aa::sim::Session& session() const { return *session_; }
    GameView* gameView() { return gameView_; }
    LevelCompletedView* completedView() { return completedView_; }
    GameTutorialView* tutorialView() { return tutorialView_; }
    // World ↔ screen through the session (y down on the screen side).
    Point worldToScreen(aa::sim::Vec2 world) const;
    void setViewport(int width, int height);
    // The hook the platform implements: draw the world of `session()` (the renderer's transform is
    // the platform's own; the UI renderer state is re-established afterwards).
    virtual void drawWorld() {}
    // The level-completed bookkeeping (setCompletedState's campaign branch) runs once per completion.
    bool levelImproved() const { return levelImproved_; }
    int completedStars() const { return completedStars_; }

protected:
    void handleEvents();
    void updateTutorialView();
    void onControllerState(int state);
    void completeLevel();
    void startLevelWithGoals();
    std::unique_ptr<aa::sim::Session> session_;
    const aa::sim::TemplateTable* templates_;
    GameView* gameView_ = nullptr;
    LevelCompletedView* completedView_ = nullptr;
    GameTutorialView* tutorialView_ = nullptr;
    int lastControllerState_ = 2;
    bool tutorialRunning_ = false;    // GameTutorialView+0x210: the last seen run flag
    bool menuOpen_ = false;
    bool completing_ = false;
    bool completed_ = false;
    bool levelImproved_ = false;
    int completedStars_ = 0;
    int overlay_ = -1;
    int width_ = 1024;
    int height_ = 768;
};

}  // namespace aa::ui
