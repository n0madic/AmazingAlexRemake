// UI::SandboxScene with SandboxView — the My Contraptions editor (docs/05 §1, docs/06 §1.3) [verified:
// SandboxView::Init / Show / Hide / ShowLeftPanel / HideLeftPanel / ShowGameControls / HideGameControls /
// ShowSimulationControls / HideSimulationControls / HideInstructions / Update / ButtonPressed / KeyDown /
// AnimationFinished / Touches*, SandboxScene::Init / Activate / Inactivate / ShowOverlay / SetGameMode /
// EnableGameUI / DisableGameUI, GameScreenController::playNewLevel (mode 1) / saveSandboxLevel /
// saveSandboxLevelAndThumb, LevelLoadingScene::ActivationComplete cases 2 / 3 — decompile + disassembly,
// 2026-09-14]. The scene owns its own aa::sim::Session (the original shares one controller with the
// GameScene; nothing of it survives a level change); the world is drawn by the platform subclass.
#pragma once

#include "aa/sim/session.h"
#include "aa/ui/menu_scenes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

namespace scene_names {
constexpr const char* kSandbox = "SandboxScene";
}  // namespace scene_names

class SandboxScene;

class SandboxView : public View, public ButtonDelegate, public AnimatorDelegate {
public:
    static constexpr float kFade = 0.3f;    // Show / Hide: the view's alpha
    static constexpr float kSlide = 0.2f;   // the sidebars
    using View::init;
    SandboxView(UiContext& ctx, SandboxScene& scene, const aa::data::JsonNode& dict);
    void show();
    void hide(bool animated);
    void showLeftPanel(bool animated);
    void hideLeftPanel(bool animated);
    void showGameControls(bool animated);
    void hideGameControls(bool animated);
    void showSimulationControls();
    void hideSimulationControls();
    void hideInstructions();
    void relayout() override;
    void update(float dt) override;
    void buttonPressed(int id) override;
    void animationFinished(int id) override;
    bool keyDown(int key) override;
    void touchesStarted(const TouchEvent& e) override;
    void touchesMovedInside(const TouchEvent& e) override;
    void touchesMovedOutside(const TouchEvent& e) override;
    void touchesFinishedInside(const TouchEvent& e) override;
    void touchesFinishedOutside(const TouchEvent& e) override;
    void touchesCancel(const TouchEvent& e) override;
    Button* backButton() { return back_; }
    Button* backgroundButton() { return background_; }
    ToggleButton* playButton() { return play_; }
    ToggleButton* readyButton() { return ready_; }
    OutlineLabelView* instructions() { return instructions_; }
    ImageView* sidebarLeft() { return sidebarLeft_; }
    ImageView* sidebarRight() { return sidebarRight_; }
    bool leftPanelHidden() const { return leftHidden_; }

private:
    void setInstructions(const std::string& textId);
    SandboxScene* scene_;
    ImageView* sidebarLeft_ = nullptr;
    Button* back_ = nullptr;
    Button* background_ = nullptr;
    ImageView* sidebarRight_ = nullptr;
    ToggleButton* play_ = nullptr;
    ToggleButton* ready_ = nullptr;
    OutlineLabelView* instructions_ = nullptr;
    ImageView* stripeLeft_ = nullptr;
    ImageView* stripeRight_ = nullptr;
    std::string textWorking_, textComplete_, textThreeStars_, textPiecesToToolbox_, textAtLeastOneItem_;
    Point rightShown_, rightHidden_;        // +0x11d8 / +0x11e0
    bool controlsShown_ = true;             // sidebarRight_ at rightShown_ (true) vs. rightHidden_ (false)
    Point backgroundHome_;                  // the background button's anchored position (see hideGameControls)
    float leftWidth_ = 0.0f;                // +0x2b70
    Point leftHidden_pos_;                  // +0x2b74 / +0x2b78
    bool leftHidden_ = true;                // +0xec
    bool simulationControls_ = false;       // +0xe8 (1 = the game controls are up)
    int showAnim_ = 0, hideAnim_ = 0;                                       // +0xe0 / +0xe4
    int leftShowAnim_ = 0, leftHideAnim_ = 0;                               // +0xf0 / +0xf4
    int bgShowAnim_ = 0, bgHideAnim_ = 0, rightShowAnim_ = 0, rightHideAnim_ = 0;   // +0xf8 / +0xfc / +0x100 / +0x104
    int toolboxItems_ = 0;                  // +0x276c
    bool deferredBack_ = false;             // +0x2b7d
    std::vector<std::unique_ptr<View>> owned_;
};

class SandboxScene : public GameSceneBase {
public:
    SandboxScene(UiContext& ctx, AppState& app, const aa::sim::TemplateTable& templates);
    const char* name() const override { return scene_names::kSandbox; }
    void init() override;
    void activate() override;
    void inactivate() override;
    void update(float dt) override;
    void draw(Renderer& renderer) override;
    void relayout(int width, int height) override;

    // LevelLoadingScene case 2: the sandbox level `level` of the My Contraptions location loads into the
    // session (false when its file does not parse). Case 3: a new level under a unique name, added to the
    // index (saved), titled LEVEL_SHARE_CONTRAPTION_DEFAULT, the author from the settings; returns its index.
    bool selectLevel(int level);
    int createNewLevel();
    // GameScreenController::saveSandboxLevel(AndThumb): the live layout to <name>.json (the author
    // refreshed), then the thumbnail through the platform's hook.
    void saveLevel(bool withThumbnail);
    // Reloads the level file (SandboxView's back button in the toolbox step).
    void reloadLevel();
    aa::sim::Session& session() { return *session_; }
    const aa::sim::Session& session() const { return *session_; }
    SandboxView* view() { return view_; }
    // The overlay of the controller's state changes (SandboxScene::ShowOverlay).
    void showOverlay(int overlay);
    Point worldToScreen(aa::sim::Vec2 world) const;
    void setViewport(int width, int height);
    virtual void drawWorld() {}
    // The platform's thumbnail writer: renders the session's world to `path` (a no-op headless).
    void setThumbnailer(std::function<void(const aa::sim::Session&, const std::string&)> fn) { thumbnailer_ = std::move(fn); }
    bool rendering() const { return rendering_; }
    void setRendering(bool on) { rendering_ = on; }
    const std::string& levelName() const { return levelName_; }

protected:
    void handleEvents();
    void onControllerState(int state);
    std::unique_ptr<aa::sim::Session> session_;
    const aa::sim::TemplateTable* templates_;
    SandboxView* view_ = nullptr;
    std::function<void(const aa::sim::Session&, const std::string&)> thumbnailer_;
    std::string levelName_;
    int lastControllerState_ = 2;
    int overlay_ = -1;                 // +0x24
    bool rendering_ = false;           // GameScreenController+0x5a0
    int width_ = 1024;
    int height_ = 768;
};

}  // namespace aa::ui
