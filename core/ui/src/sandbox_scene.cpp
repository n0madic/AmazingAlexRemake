#include "aa/ui/sandbox_scene.h"

#include "aa/data/level_loader.h"
#include "aa/data/level_writer.h"
#include "aa/ui/extra_scenes.h"

#include <cmath>
#include <cstdio>
#include <exception>

namespace aa::ui {

namespace {

constexpr int kMusicGame = 2;

template <class T, class... Args>
T* make(std::vector<std::unique_ptr<View>>& owned, Args&&... args) {
    auto v = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = v.get();
    owned.push_back(std::move(v));
    return raw;
}

aa::data::JsonNode sub(const aa::data::JsonNode& dict, const char* key) { return dict.optional(key); }

AnimationParameters slide(float dx, float dy, float duration) {
    AnimationParameters d;
    d.alpha = 0.0f;
    d.scale = 0.0f;
    d.frame.x = dx;
    d.frame.y = dy;
    d.duration = duration;
    d.repeat = 1;
    return d;
}

aa::sim::UnlockedItems unlockedItems(const AppState& app) {
    aa::sim::UnlockedItems u{};
    for (std::size_t i = 0; i < u.size() && i < app.progress.itemUnlocked.size(); ++i) u[i] = app.progress.itemUnlocked[i];
    return u;
}

}  // namespace

// --- SandboxView -----------------------------------------------------------------------------------

SandboxView::SandboxView(UiContext& ctx, SandboxScene& scene, const aa::data::JsonNode& dict) : View(ctx), scene_(&scene) {
    // SandboxView::Init [verified: 0x139024]: SidebarLeft (with ButtonBack), ButtonBackground, SidebarRight
    // (with ButtonPlay state 1 and ButtonReady state 0), LabelInstructions (hidden; its five text ids),
    // the construction stripes, the letterbox borders; then the sidebars' shown / hidden positions.
    sidebarLeft_ = make<ImageView>(owned_, ctx);
    sidebarLeft_->setViewName("SidebarLeft");
    sidebarLeft_->init(sub(dict, "SidebarLeft"));
    back_ = make<Button>(owned_, ctx);
    back_->setViewName("ButtonBack");
    back_->init(sub(sub(dict, "SidebarLeft"), "ButtonBack"));
    back_->setDelegate(this);
    background_ = make<Button>(owned_, ctx);
    background_->setViewName("ButtonBackground");
    background_->init(sub(dict, "ButtonBackground"));
    background_->setDelegate(this);
    sidebarRight_ = make<ImageView>(owned_, ctx);
    sidebarRight_->setViewName("SidebarRight");
    sidebarRight_->init(sub(dict, "SidebarRight"));
    play_ = make<ToggleButton>(owned_, ctx);
    play_->setViewName("ButtonPlay");
    play_->init(sub(sub(dict, "SidebarRight"), "ButtonPlay"));
    play_->setState(button_state::kNormal);
    play_->setDelegate(this);
    ready_ = make<ToggleButton>(owned_, ctx);
    ready_->setViewName("ButtonReady");
    ready_->init(sub(sub(dict, "SidebarRight"), "ButtonReady"));
    ready_->setState(button_state::kDisabled);
    ready_->setDelegate(this);
    const aa::data::JsonNode label = sub(dict, "LabelInstructions");
    instructions_ = make<OutlineLabelView>(owned_, ctx);
    instructions_->setViewName("LabelInstructions");
    instructions_->init(label);
    instructions_->setVisible(false);
    textWorking_ = label.getString("TextWorkingContraption", "");
    textComplete_ = label.getString("TextCompleteDesign", "");
    textThreeStars_ = label.getString("TextThreeStars", "");
    textPiecesToToolbox_ = label.getString("TextPiecesToToolbox", "");
    textAtLeastOneItem_ = label.getString("TextAtleastOneItem", "");
    stripeLeft_ = make<ImageView>(owned_, ctx);
    stripeLeft_->setViewName("ConstructionBorderLeft");
    stripeLeft_->init(sub(dict, "ConstructionBorderLeft"));
    stripeRight_ = make<ImageView>(owned_, ctx);
    stripeRight_->setViewName("ConstructionBorderRight");
    stripeRight_->init(sub(dict, "ConstructionBorderRight"));
    addSubview(sidebarLeft_);
    sidebarLeft_->addSubview(back_);
    addSubview(background_);
    addSubview(sidebarRight_);
    sidebarRight_->addSubview(play_);
    sidebarRight_->addSubview(ready_);
    addSubview(instructions_);
    addSubview(stripeLeft_);
    addSubview(stripeRight_);
    updateViewAnchors(true, true);
    rightShown_ = sidebarRight_->position();
    rightHidden_ = Point{ctx.screen.nativeWidth, sidebarRight_->position().y};
    leftWidth_ = sidebarLeft_->size().w;
    leftHidden_pos_ = sidebarLeft_->position();   // anchored right-at-left: off screen
    backgroundHome_ = background_->position();
}

void SandboxView::relayout() {
    // Unlike GameView's sidebars, sidebarLeft_ (back_) and sidebarRight_ (play_, ready_) carry their own
    // controls as subviews, not independent siblings — repositioning the parent is enough. View::relayout()
    // completes any in-flight slide first (the documented "snap, don't rebase" trade-off).
    View::relayout();
    // sidebarLeft_ is anchored off screen (its rest position *is* leftHidden_pos_); sidebarRight_ is
    // anchored on screen (its rest position *is* rightShown_) — both already correct post-relayout when
    // that is the current state, and only need an explicit nudge for the other one.
    leftWidth_ = sidebarLeft_->size().w;
    leftHidden_pos_ = sidebarLeft_->position();
    if (!leftHidden_) sidebarLeft_->setPosition(Point{leftHidden_pos_.x + leftWidth_, leftHidden_pos_.y});
    rightShown_ = sidebarRight_->position();
    rightHidden_ = Point{ctx_->screen.nativeWidth, rightShown_.y};
    if (!controlsShown_) sidebarRight_->setPosition(rightHidden_);
    // background_'s only off-anchor position (hideGameControls' animated y = −h/2) is invisible when it
    // applies, and showGameControls always computes its delta from the live position — refreshing the
    // cache is enough; nothing to snap.
    backgroundHome_ = background_->position();
}

void SandboxView::setInstructions(const std::string& textId) {
    instructions_->setVisible(true);
    instructions_->setText(textId);
}

void SandboxView::show() {
    // SandboxView::Show [verified: 0x13ad2c]: mode 1, playNewLevel (the level saved at once, mode 1's
    // branch), the render flag, nothing fixed, the left panel and the controls slide in, the view fades
    // in over 0.3 s, the play button enabled, the instructions (three stars once tested, else "working
    // contraption"), the loading scene removed from beneath.
    aa::sim::Session& session = scene_->session();
    session.setMode(aa::sim::GameMode::Sandbox);
    scene_->saveLevel(true);
    scene_->setRendering(true);
    session.markAllObjectsNotFixed();
    hideLeftPanel(false);
    showLeftPanel(true);
    hideGameControls(false);
    showGameControls(true);
    setAlpha(0.0f);
    AnimationParameters p = AnimationParameters::fromView(*this);
    p.alpha = 1.0f;
    p.curve = 4;
    p.duration = kFade;
    p.repeat = 1;
    showAnim_ = ctx_->animator->animate(this, p, this);
    setVisible(true);
    play_->setState(button_state::kNormal);
    setInstructions(session.tested() ? textThreeStars_ : textWorking_);
    if (SceneManager* manager = scene_->manager()) manager->removeScene(scene_names::kLevelLoading);
}

void SandboxView::hide(bool animated) {
    hideLeftPanel(animated);
    hideGameControls(animated);
    AnimationParameters p = AnimationParameters::fromView(*this);
    p.alpha = 0.0f;   // the fade-out [verified: 0x13ace4]
    p.curve = 4;
    p.duration = kFade;
    p.repeat = 1;
    hideAnim_ = ctx_->animator->animate(this, p, this);
    instructions_->setVisible(false);
}

void SandboxView::showLeftPanel(bool animated) {
    if (!leftHidden_) return;
    if (!animated) {
        sidebarLeft_->setPosition(Point{leftHidden_pos_.x + leftWidth_, leftHidden_pos_.y});
        leftHidden_ = false;
        return;
    }
    leftShowAnim_ = ctx_->animator->animate(std::vector<View*>{sidebarLeft_}, slide(leftWidth_, 0.0f, kSlide), this);
}

void SandboxView::hideLeftPanel(bool animated) {
    if (leftHidden_) return;
    if (!animated) {
        sidebarLeft_->setPosition(leftHidden_pos_);
        leftHidden_ = true;
        return;
    }
    leftHideAnim_ = ctx_->animator->animate(std::vector<View*>{sidebarLeft_}, slide(-leftWidth_, 0.0f, kSlide), this);
}

void SandboxView::showGameControls(bool animated) {
    // ShowGameControls [verified: 0x13a6fc]: the play button enabled outside the toolbox step; in the
    // step the ready button follows the strip's item count, elsewhere it is enabled once tested; the
    // right sidebar slides to its shown x, the background button to its place (the original animates it
    // by a zero delta and never restores a hide — the remake puts it back, a documented deviation);
    // the background button is visible outside the step; the play button unchecked; the instructions.
    aa::sim::Session& session = scene_->session();
    const aa::sim::GameMode mode = session.gameMode();
    play_->setState(mode != aa::sim::GameMode::SandboxToolbox ? button_state::kNormal : button_state::kDisabled);
    if (mode == aa::sim::GameMode::SandboxToolbox) {
        if (toolboxItems_ < 1) {
            setInstructions(textPiecesToToolbox_);
            ready_->setState(button_state::kDisabled);
            ready_->setInteraction(true);
        } else {
            ready_->setState(button_state::kNormal);
        }
    } else {
        ready_->setState(session.tested() ? button_state::kNormal : button_state::kDisabled);
    }
    controlsShown_ = true;
    const float duration = animated ? kSlide : 0.0f;
    if (rightShowAnim_ == 0) {
        ctx_->animator->cancelAnimation(rightHideAnim_);
        rightHideAnim_ = 0;
        rightShowAnim_ = ctx_->animator->animate(std::vector<View*>{sidebarRight_}, slide(rightShown_.x - sidebarRight_->position().x, 0.0f, duration), this);
    }
    if (bgShowAnim_ == 0) {
        ctx_->animator->cancelAnimation(bgHideAnim_);
        bgHideAnim_ = 0;
        bgShowAnim_ = ctx_->animator->animate(std::vector<View*>{background_},
                                              slide(backgroundHome_.x - background_->position().x, backgroundHome_.y - background_->position().y, duration), this);
    }
    if (mode != aa::sim::GameMode::SandboxToolbox) {
        background_->setVisible(true);
        background_->setInteraction(true);
    }
    play_->setInteraction(true);
    ready_->setInteraction(true);
    play_->setChecked(false);
    if (!instructions_->isVisible()) setInstructions(session.tested() ? textThreeStars_ : textComplete_);
}

void SandboxView::hideGameControls(bool animated) {
    // HideGameControls [verified: 0x13aaa8]: the right sidebar slides off to the right; the background
    // button hides at once or slides up to y = −h/2; every button's interaction off, play unchecked.
    controlsShown_ = false;
    const float duration = animated ? kSlide : 0.0f;
    if (rightHideAnim_ == 0) {
        ctx_->animator->cancelAnimation(rightShowAnim_);
        rightShowAnim_ = 0;
        rightHideAnim_ = ctx_->animator->animate(std::vector<View*>{sidebarRight_}, slide(rightHidden_.x - sidebarRight_->position().x, 0.0f, duration), this);
    }
    if (!animated) {
        background_->setVisible(false);
    } else if (bgHideAnim_ == 0) {
        ctx_->animator->cancelAnimation(bgShowAnim_);
        bgShowAnim_ = 0;
        const float targetY = -background_->size().h * 0.5f;
        bgHideAnim_ = ctx_->animator->animate(std::vector<View*>{background_}, slide(0.0f, targetY - background_->position().y, duration), this);
    }
    background_->setInteraction(false);
    play_->setInteraction(false);
    ready_->setInteraction(false);
    play_->setChecked(false);
}

void SandboxView::showSimulationControls() {
    hideLeftPanel(false);
    play_->setChecked(true);
    background_->setVisible(false);
    background_->setInteraction(false);
    simulationControls_ = true;
}

void SandboxView::hideSimulationControls() {
    showLeftPanel(false);
    play_->setChecked(false);
    if (scene_->session().gameMode() != aa::sim::GameMode::SandboxToolbox) {
        background_->setVisible(true);
        background_->setInteraction(true);
    }
    simulationControls_ = false;
}

void SandboxView::hideInstructions() { instructions_->setVisible(false); }

void SandboxView::update(float dt) {
    View::update(dt);
    aa::sim::Session& session = scene_->session();
    // Update [verified: 0x13b754]: in the toolbox step the ready button follows the strip's item count;
    // a back press deferred while an item was being added / removed fires now.
    if (session.gameMode() == aa::sim::GameMode::SandboxToolbox) {
        const int items = session.toolbox().slotCount;
        if (items != toolboxItems_) {
            toolboxItems_ = items;
            if (items < 1) {
                ready_->setState(button_state::kDisabled);
                ready_->setInteraction(true);
            } else {
                ready_->setState(button_state::kNormal);
            }
        }
    }
    if (deferredBack_) {
        deferredBack_ = false;
        buttonPressed(back_->id());
    }
}

void SandboxView::buttonPressed(int id) {
    // SandboxView::ButtonPressed [verified: 0x13afe8]. The gizmo goes off first (GameState+0x5738c = −1).
    aa::sim::Session& session = scene_->session();
    session.stopTutorial();
    SceneManager* manager = scene_->manager();
    instructions_->setVisible(false);
    if (id == back_->id()) {
        if (session.gameMode() == aa::sim::GameMode::SandboxToolbox) {
            // The toolbox step's back: the level file reloaded, mode 1, the play button back, the ready
            // button when tested, the "complete design" text, the background button back.
            scene_->reloadLevel();
            session.setMode(aa::sim::GameMode::Sandbox);
            play_->setState(button_state::kNormal);
            play_->setChecked(false);
            if (session.tested()) ready_->setState(button_state::kNormal);
            setInstructions(textComplete_);
            background_->setVisible(true);
            background_->setInteraction(true);
            return;
        }
        if (session.isManipulationActive()) {
            deferredBack_ = true;   // isAddingOrRemovingItems: the press repeats next frame
            return;
        }
        session.markAllObjectsFixed();
        scene_->saveLevel(true);
        // (the untested level's "_solution" file the original writes here has no offline reader — dropped)
        if (manager) manager->popScene();
        return;
    }
    if (id == background_->id()) {
        const int next = (session.level().backgroundIndex + 1) % 4;
        session.setBackground(next);
        ready_->setState(button_state::kDisabled);
        return;
    }
    if (id == play_->id()) {
        ready_->setState(button_state::kDisabled);
        ready_->setChecked(false);
        if (play_->isChecked()) {
            session.setMode(aa::sim::GameMode::TestPlay);
            session.play();
        } else {
            session.stop();
            session.setMode(aa::sim::GameMode::Sandbox);
            setInstructions(textComplete_);
        }
        return;
    }
    if (id == ready_->id()) {
        if (session.gameMode() == aa::sim::GameMode::Sandbox) {
            if (!session.tested()) {
                ready_->setState(button_state::kDisabled);   // "move forward" needs a tested contraption
                return;
            }
            play_->setState(button_state::kDisabled);
            session.setMode(aa::sim::GameMode::SandboxToolbox);
            background_->setVisible(false);
            background_->setInteraction(false);
            toolboxItems_ = 0;
            setInstructions(textPiecesToToolbox_);
            ready_->setState(button_state::kDisabled);
            ready_->setInteraction(true);
            // [verified: 0x13b588 → 0x13b3c0] the level (and its thumbnail) is saved as the step begins —
            // what the step's back button reloads.
            scene_->saveLevel(true);
        } else if (session.gameMode() == aa::sim::GameMode::SandboxToolbox) {
            // The step's forward path is the sharing view (online, dropped); the original only reaches
            // it with the ready button fully opaque and otherwise asks for at least one item — and saves.
            setInstructions(textAtLeastOneItem_);
            scene_->saveLevel(true);
        }
    }
}

void SandboxView::animationFinished(int id) {
    if (id == showAnim_) {
        showAnim_ = 0;
    } else if (id == hideAnim_) {
        hideAnim_ = 0;
        scene_->setRendering(false);
    } else if (id == leftShowAnim_) {
        leftShowAnim_ = 0;
        leftHidden_ = false;
    } else if (id == leftHideAnim_) {
        leftHideAnim_ = 0;
        leftHidden_ = true;
    } else if (id == bgShowAnim_) {
        bgShowAnim_ = 0;
    } else if (id == bgHideAnim_) {
        bgHideAnim_ = 0;
    } else if (id == rightShowAnim_) {
        rightShowAnim_ = 0;
    } else if (id == rightHideAnim_) {
        rightHideAnim_ = 0;
    }
}

bool SandboxView::keyDown(int key) {
    if (View::keyDown(key)) return true;
    if (key != SceneManager::kKeyBack && key != SceneManager::kKeyAndroidBack) return false;
    // KeyDown [verified: 0x138adc]: with the left panel shown the back button (deferred to the next
    // frame); during a run the play button (stop); otherwise consumed.
    if (!leftHidden_) {
        deferredBack_ = true;
        return true;
    }
    if (play_->state() == button_state::kNormal && play_->isChecked()) {
        play_->setChecked(false);
        buttonPressed(play_->id());
    }
    return true;
}

void SandboxView::touchesStarted(const TouchEvent& e) {
    instructions_->setVisible(false);
    scene_->session().pointerDown(e.id, aa::sim::Vec2(e.position.x, e.position.y));
}
void SandboxView::touchesMovedInside(const TouchEvent& e) { scene_->session().pointerMove(e.id, aa::sim::Vec2(e.position.x, e.position.y)); }
void SandboxView::touchesMovedOutside(const TouchEvent& e) { touchesMovedInside(e); }
void SandboxView::touchesFinishedInside(const TouchEvent& e) { scene_->session().pointerUp(e.id, aa::sim::Vec2(e.position.x, e.position.y)); }
void SandboxView::touchesFinishedOutside(const TouchEvent& e) { touchesFinishedInside(e); }
void SandboxView::touchesCancel(const TouchEvent& e) { scene_->session().pointerCancel(e.id); }

// --- SandboxScene ----------------------------------------------------------------------------------

SandboxScene::SandboxScene(UiContext& ctx, AppState& app, const aa::sim::TemplateTable& templates)
    : GameSceneBase(ctx, app), session_(std::make_unique<aa::sim::Session>(templates)), templates_(&templates) {
    session_->setToolboxFrameSizes(app.toolboxSizes);
    session_->setSoundSink(app.soundSink);
}

void SandboxScene::init() {
    Scene::init();
    view_ = make<SandboxView>(*this, tree().view("SandboxView"));
    view_->setViewName("SandboxView");
    view_->setFrame(root_->frame());
    view_->setVisible(true);
    root_->addSubview(view_);
    setViewport(width_, height_);
}

void SandboxScene::setViewport(int width, int height) {
    width_ = width;
    height_ = height;
    session_->setViewport(aa::sim::ScreenLayout::compute(width, height, app_->profilePixelScale));
}

void SandboxScene::relayout(int width, int height) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
    setViewport(width, height);
}

Point SandboxScene::worldToScreen(aa::sim::Vec2 world) const {
    const aa::sim::Vec2 p = session_->worldToScreen(world);
    return Point{p.x, static_cast<float>(height_) - p.y};
}

bool SandboxScene::selectLevel(int level) {
    const aa::game::LocationInfo* info = app_->location();
    if (!info || !app_->isSandboxLocation(app_->locationIndex) || level < 0 || level >= info->levelCount()) return false;
    try {
        const aa::sim::Level data = app_->loadLevel(app_->locationIndex, level);
        session_->setUnlockedItems(unlockedItems(*app_));
        session_->setTutorialContext(-1, level);
        session_->load(data, aa::sim::GameMode::Sandbox);   // rejects a layout the loader let through (a duplicate handle)
    } catch (const std::exception&) {
        return false;   // the list shows its parsing error
    }
    app_->currentLevel = level;
    levelName_ = info->levels[static_cast<std::size_t>(level)];
    lastControllerState_ = session_->controllerState();
    overlay_ = -1;
    return true;
}

int SandboxScene::createNewLevel() {
    // LevelLoadingScene::ActivationComplete case 3 [verified]: CreateNewSandbox, background 0, a unique
    // name added to the index and saved, the level index = the last, the author from the settings.
    aa::game::LocationInfo& info = app_->locations[static_cast<std::size_t>(AppState::kSandboxLocation)];
    app_->loadLocation(AppState::kSandboxLocation);
    const std::string name = app_->saves->generateSandboxName(info);
    info.levels.push_back(name);
    app_->saveSandboxLocation();
    const int level = info.levelCount() - 1;
    app_->currentLevel = level;
    levelName_ = name;
    aa::sim::Level data;
    data.title = localizedText(*ctx_, aa::sim::kDefaultSandboxTitleId);
    data.authorName = app_->settings.playerName;
    data.backgroundIndex = 0;
    session_->setUnlockedItems(unlockedItems(*app_));
    session_->setTutorialContext(-1, level);
    session_->load(data, aa::sim::GameMode::Sandbox);
    lastControllerState_ = session_->controllerState();
    overlay_ = -1;
    return level;
}

void SandboxScene::saveLevel(bool withThumbnail) {
    if (levelName_.empty()) return;
    aa::sim::Level data = session_->sandboxLevel();
    data.authorName = app_->settings.playerName;   // LevelInfoUtils::SetAuthorName
    const std::string path = app_->saves->sandboxLevelPath(levelName_);
    try {
        aa::data::writeLevelFile(path, data);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "amazing_alex: cannot save the sandbox level %s: %s\n", path.c_str(), e.what());
    }
    if (withThumbnail && thumbnailer_) thumbnailer_(*session_, app_->saves->sandboxThumbPath(levelName_));
    app_->loadSandboxLocation();   // the list's titles follow the file
}

void SandboxScene::reloadLevel() {
    // The toolbox step's back button: LoadLevel in the current mode (5 — prepareForNewLevel's campaign
    // branch fixes everything and activates the file's strip), then the view's setMode(1) runs the 5 → 1
    // transition that un-fixes every object (the file was saved with the stars fixed) [verified: 0x13b0c8,
    // setMode 0xb9f34]; loading in mode 1 would skip that transition and leave the stars locked.
    if (levelName_.empty()) return;
    try {
        const aa::sim::Level data = aa::data::loadLevelFile(app_->saves->sandboxLevelPath(levelName_));
        session_->setUnlockedItems(unlockedItems(*app_));
        session_->load(data, session_->gameMode());
        lastControllerState_ = session_->controllerState();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "amazing_alex: cannot reload the sandbox level %s: %s\n", levelName_.c_str(), e.what());
    }
}

void SandboxScene::activate() {
    Scene::activate();
    view_->show();
    if (app_->audio) {
        app_->audio->stopMusic();
        app_->audio->playMusic(kMusicGame);
    }
}

void SandboxScene::inactivate() {
    Scene::inactivate();
    view_->hide(true);
    if (app_->audio) app_->audio->stopMusic();
}

void SandboxScene::showOverlay(int overlay) {
    // SandboxScene::ShowOverlay [verified: 0x1276f4].
    if (overlay_ == overlay) return;
    overlay_ = overlay;
    switch (overlay) {
    case 1:
        view_->hideLeftPanel(true);
        view_->hideGameControls(false);
        break;
    case 2:
        view_->hideLeftPanel(false);
        view_->hideGameControls(false);
        break;
    case 8:
        view_->showSimulationControls();
        break;
    case 10:
        session_->markAllObjectsNotFixed();
        view_->showGameControls(true);
        view_->hideSimulationControls();
        break;
    default: break;
    }
}

void SandboxScene::onControllerState(int state) {
    // doFrame's delegate calls [verified]: state 3 (set-up → simulation) → ShowOverlay(8); state 5
    // (simulation → set-up, toggleSimulation's stop half and the test-play completion) → ShowOverlay(10).
    // State 1 (the level menu) is never entered here: the editor has no pause button.
    if (state == 4 && lastControllerState_ != 4) showOverlay(8);
    else if (state == 2 && lastControllerState_ != 2) showOverlay(10);
    lastControllerState_ = state;
}

void SandboxScene::handleEvents() {
    for (const aa::sim::SessionEvent& e : session_->drainEvents()) {
        switch (e.kind) {
        case aa::sim::SessionEvent::Kind::Sound:
            if (app_->soundSink) app_->soundSink->play(e.soundId, e.volume, e.position);
            break;
        case aa::sim::SessionEvent::Kind::GoalComplete:
            showOverlay(2);   // LevelCompletionStarted: the HUD retracts while the countdown runs
            break;
        default: break;
        }
    }
}

void SandboxScene::update(float dt) {
    Scene::update(dt);
    if (state_ != scene_state::kActive && state_ != scene_state::kActivating) return;
    session_->advance(dt);
    session_->drainActions();
    handleEvents();
    const int cs = session_->controllerState();
    if (cs != lastControllerState_) onControllerState(cs);
}

void SandboxScene::draw(Renderer& renderer) {
    if (rendering_) {
        drawLetterBoxBackdrop(renderer, *ctx_, session_->viewport());
        drawWorld();
    }
    Scene::draw(renderer);
}

}  // namespace aa::ui
