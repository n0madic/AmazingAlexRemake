// UI::Scene / SceneManager / EventHandler [verified: SceneManager::PushScene / PopScene / SetRootScene /
// InsertScene / Update / Draw / SimultaneousTransition / NonSimultaneousTransition / Touches* /
// SetUserInteractionEnabled / KeyPressed, Scene::Init / Activate / Inactivate / Update / HitTest,
// EventHandler::TouchesStarted / Moved / Finished / Cancel / PurgeTouches — decompile, 2026-09-14].
#pragma once

#include "aa/sim/screen_layout.h"
#include "aa/ui/animator.h"
#include "aa/ui/view.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

// Scene states (+0xC): 0 inactive, 1 active, 2 activating, 3 inactivating.
namespace scene_state {
constexpr int kInactive = 0;
constexpr int kActive = 1;
constexpr int kActivating = 2;
constexpr int kInactivating = 3;
}  // namespace scene_state

// The touch dispatcher of a view tree: hit-tests the root, remembers the view a touch started in and the
// one it is over, and delivers Started / MovedInside / MovedOutside / Enter / Exit / FinishedInside /
// FinishedOutside / Cancel.
class EventHandler {
public:
    void setRootView(View* root) { root_ = root; }
    View* rootView() const { return root_; }
    bool touchesStarted(const TouchEvent& e);
    bool touchesMoved(const TouchEvent& e);
    bool touchesFinished(const TouchEvent& e);
    void touchesCancel(const TouchEvent& e);
    // PurgeTouches(view): cancels every tracked touch inside `view`'s subtree.
    void purgeTouches(View* view);

private:
    View* root_ = nullptr;
    std::map<int, View*> started_;
    std::map<int, View*> current_;
};

class SceneManager;

// The letterbox backdrop of the game and sandbox scenes (docs/11 §1, ScreenLayout::letterBoxFills /
// borderSpriteRect), drawn in screen px before the world, which covers its middle: the fills in the colour
// of BORDERIMAGE_LEFT / RIGHT's outer column, then — over side strips only — the two sprites (the 1024X768
// profile's BORDER_BORDER sheet imported ×2, docs/12 §2; nothing when the profile has none).
void drawLetterBoxBackdrop(Renderer& renderer, const UiContext& ctx, const aa::sim::ScreenLayout& layout);

class Scene {
public:
    explicit Scene(UiContext& ctx) : ctx_(&ctx) {}
    virtual ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    virtual const char* name() const = 0;
    // Scene::Init: the root view sized to the screen; subclasses build their views (once).
    virtual void init();
    bool initialized() const { return initialized_; }
    View* view() const { return root_.get(); }
    View* findView(const std::string& name) const { return root_ ? root_->findViewByName(name) : nullptr; }
    int state() const { return state_; }
    void setState(int s) { state_ = s; }
    UiContext& ctx() const { return *ctx_; }

    // The transition hooks (the SceneManager calls them in this order for a push: the old scene's
    // AboutToInactivate + Inactivate, the new scene's AboutToActivate + Activate; when both settle,
    // InactivationComplete / ActivationComplete).
    virtual void aboutToActivate() { if (!initialized_) init(); }
    virtual void activate() { state_ = scene_state::kActivating; }
    virtual void activationComplete() {}
    virtual void aboutToInactivate() {}
    virtual void inactivate() { state_ = scene_state::kInactivating; }
    virtual void inactivationComplete() {}
    // Scene::Update: an activating scene becomes active, an inactivating one inactive (no transition
    // animation of its own); then the view tree updates.
    virtual void update(float dt);
    virtual void draw(Renderer& renderer);
    virtual void updateLocale() { if (root_) root_->updateLocale(); }
    virtual bool keyDown(int key);
    // Scene::SetPaused: the app going to the background / coming back (SceneManager::Pause); the base
    // does nothing, GameScene opens its pause menu [verified].
    virtual void setPaused(bool) {}
    // Hit-testing only while active.
    View* hitTest(Point p) const;
    void setManager(SceneManager* m) { manager_ = m; }
    SceneManager* manager() const { return manager_; }

    // Owns a view created for this scene.
    template <class T, class... Args>
    T* make(Args&&... args) {
        auto v = std::make_unique<T>(*ctx_, std::forward<Args>(args)...);
        T* raw = v.get();
        owned_.push_back(std::move(v));
        return raw;
    }

    // relayout(width, height): re-derives the scene's view tree after ctx_->screen changed underneath a
    // live scene (a window / viewport resize) — the counterpart to init() for a scene that is never
    // re-Init'd. The base resizes the root and re-lays out its subtree; a scene with its own top-level
    // view(s) beneath root_ (every GameSceneBase subclass) overrides this to resize those too before
    // relaying them out. `width` / `height` are unused by the base, present for GameScene / SandboxScene to
    // also resize their Session's viewport.
    virtual void relayout(int width, int height);

protected:
    // Sets root_ (and, when given, the scene's main view) to the current screen size — what Scene::init()
    // and every concrete scene's constructor already do once, replayed after a resize.
    void resizeRootAndMainView(View* mainView);

    UiContext* ctx_;
    std::unique_ptr<View> root_;
    std::vector<std::unique_ptr<View>> owned_;
    SceneManager* manager_ = nullptr;
    int state_ = scene_state::kInactive;
    bool initialized_ = false;
};

// The scene stack with the original's transition state machine. Scenes are registered by name once and
// re-used (the original keeps them alive and re-Inits nothing).
class SceneManager {
public:
    explicit SceneManager(UiContext& ctx) : ctx_(&ctx) {}
    void registerScene(std::unique_ptr<Scene> scene);
    Scene* scene(const std::string& name) const;
    Scene* activeScene() const { return stack_.empty() ? nullptr : stack_.back(); }
    const std::vector<Scene*>& stack() const { return stack_; }

    // relayoutAll(width, height): every registered scene's relayout(), state-agnostic (idempotent
    // regardless of state_ or a scene mid-transition) — the desktop / Web resize hook.
    void relayoutAll(int width, int height);

    bool pushScene(const std::string& name);
    bool popScene();
    bool setRootScene(const std::string& name);
    // InsertScene(index, name): a scene put beneath the top (no transition).
    bool insertScene(int index, const std::string& name);
    bool popScenesUntil(const std::string& name);
    // RemoveScene(name): drops the scene from the middle of the stack (never the root or the top; no
    // transition) [verified].
    bool removeScene(const std::string& name);
    bool inTransition() const { return activating_ != nullptr || inactivating_ != nullptr; }
    void setSimultaneousTransition(bool on) { simultaneous_ = on; }

    void update(float dt);
    void draw(Renderer& renderer);
    void updateLocale();
    // SetUserInteractionEnabled(false): a touch that begins is remembered (its id swallowed) until it ends.
    void setUserInteractionEnabled(bool on) { interactionEnabled_ = on; }
    bool userInteractionEnabled() const { return interactionEnabled_; }
    void touchesStarted(const TouchEvent& e);
    void touchesMoved(const TouchEvent& e);
    void touchesFinished(const TouchEvent& e);
    void touchesCancel(const TouchEvent& e);
    // WheelScrolled(position, delta): remake-only (the desktop mouse wheel / trackpad); the same gates as
    // a touch, delivered to the view under `position` and up its ancestors until one consumes it.
    bool wheelScrolled(Point position, Point delta);
    // KeyPressed: the active scene first; the back key pops (unless at the root).
    bool keyPressed(int key);
    // The back key codes the original's views accept alike (SceneManager::KeyPressed, MainMenuView::KeyDown
    // test `0x28 || 0x56`) [verified]: 0x28 is the framework's, 0x56 what the Android JNI layer sends for
    // KEYCODE_BACK (nativeKeyInput; KEYCODE_MENU becomes 0x57, which no view handles).
    static constexpr int kKeyBack = 0x28;
    static constexpr int kKeyAndroidBack = 0x56;
    // SceneManager::Pause(paused): SetPaused on the activating scene, the inactivating one and the top of
    // the stack (GameApp::activate(!paused) on nativePause / nativeResume) [verified: 0xfe8c4].
    void pause(bool paused);

private:
    void nonSimultaneousTransition(float dt);
    void simultaneousTransition(float dt);
    UiContext* ctx_;
    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<Scene*> stack_;
    Scene* activating_ = nullptr;
    Scene* inactivating_ = nullptr;
    bool popping_ = false;          // +0x7C: the inactivating scene draws on top
    bool simultaneous_ = true;
    bool interactionEnabled_ = true;
    int swallowedTouch_ = -1;
    EventHandler events_;
    float lastDt_ = 0.0f;
};

}  // namespace aa::ui
