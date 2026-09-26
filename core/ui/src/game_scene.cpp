#include "aa/ui/game_scene.h"
#include "aa/ui/remake_views.h"

#include "aa/data/level_loader.h"

#include <algorithm>
#include <cmath>

namespace aa::ui {

namespace {

constexpr float kMenuAnim = 0.2f;           // 0x3e4ccccd: the sidebar / dim / control animations
constexpr float kFade = 0.3f;               // 0x3e99999a: the view fade, the level name
constexpr float kNameHideDelay = 2.2f;      // 0x400ccccd
constexpr float kDimAlpha = 0.5f;
constexpr float kDisabledAlpha = 0.6f;
constexpr int kCurveEaseIn = 1;
constexpr int kCurveEaseOut = 2;
constexpr int kCurveSmooth = 4;
constexpr int kMusicGame = 2;
constexpr float kAlexScale = 0.46f;
constexpr float kWorldWidth = 3.41f;
constexpr float kWorldHeight = 2.12459f;
constexpr float kStarPopScale = 1.2f;
constexpr float kStarPopDuration = 0.1f;    // 0x3dcccccd
constexpr float kStarDelays[3] = {0.4f, 0.8f, 1.2f};
constexpr int kResultStarSounds[3] = {0x44, 0x45, 0x46};
constexpr float kResultStarVolume = 0.35f;   // remake: the original 0.5 cut 30 % (the chimes read harsh)
constexpr int kUiButtonPush = 3;
constexpr float kUiVolume = 0.2f;

// GameScreenTransitionsUtils::ValueAnimator segments {duration, from, to}: a sine-squared ease per segment.
struct ValueSegment {
    float duration;
    float from;
    float to;
};
// DAT_002467b4: the Alex popup of GameView (0.3 → 1.2 in 0.2 s, 1.2 → 1.0 in 0.05 s).
constexpr ValueSegment kAlexPopup[2] = {{0.2f, 0.3f, 1.2f}, {0.05f, 1.2f, 1.0f}};
// DAT_002469b8: the result panel's Alex (the original holds 0.46 for 1 s, then 0.46 → 1.0 in 0.3 s).
// A deliberate deviation: the hold is 0.7 s and the travel 0.2 s — the original's pause on top of the
// simulation's 2.05 s completion delay read as the panel stalling.
constexpr ValueSegment kAlexResult[2] = {{0.7f, 0.46f, 0.46f}, {0.2f, 0.46f, 1.0f}};

float valueAnimator(float dt, float& time, int& segment, const ValueSegment* segments, int count) {
    time += dt;
    // The overshoot carries into the next segment (the original restarts it at 0: a one-frame dwell at
    // the segment's start).
    while (segment < count && time > segments[segment].duration) {
        time -= segments[segment].duration;
        ++segment;
    }
    if (segment >= count) {
        segment = count;
        return segments[count - 1].to;
    }
    const ValueSegment& s = segments[segment];
    const float x = std::sin((time / s.duration) * 3.1415927f * 0.5f);
    return s.from + x * x * (s.to - s.from);
}

template <class T, class... Args>
T* make(std::vector<std::unique_ptr<View>>& owned, Args&&... args) {
    auto v = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = v.get();
    owned.push_back(std::move(v));
    return raw;
}

aa::data::JsonNode sub(const aa::data::JsonNode& dict, const char* path) {
    std::string p = path;
    aa::data::JsonNode n = dict;
    std::size_t start = 0;
    while (start <= p.size()) {
        const std::size_t slash = p.find('/', start);
        const std::string key = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        n = n.optional(key.c_str());
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return n;
}

AnimationParameters slide(float dx, float duration) {
    AnimationParameters d;
    d.alpha = 0.0f;
    d.scale = 0.0f;
    d.frame.x = dx;
    d.duration = duration;
    d.repeat = 1;
    return d;
}

AnimationParameters fade(float dAlpha, float duration) {
    AnimationParameters d;
    d.alpha = dAlpha;
    d.scale = 0.0f;
    d.duration = duration;
    d.repeat = 1;
    return d;
}

}  // namespace

// --- GameTutorialView ------------------------------------------------------------------------------

GameTutorialView::GameTutorialView(UiContext& ctx, const aa::data::JsonNode& dict) : View(ctx) {
    View::init(dict);
    const aa::data::JsonNode hand = sub(dict, "ImageHand");
    pointImage_ = hand.getString("ImageHandPoint", "BUTTON_HAND_POINT");
    tapImage_ = hand.getString("ImageHandTap", "BUTTON_HAND_TAP");
    hand_ = make<ImageView>(owned_, ctx);
    hand_->setViewName("ImageHand");
    hand_->init(hand);
    hand_->setInteraction(false);
    addSubview(hand_);
    setInteraction(false);
    setVisible(false);
}

void GameTutorialView::show() { setVisible(true); }
void GameTutorialView::hide() { setVisible(false); }

void GameTutorialView::setHand(Point screen, int image, float alpha, float angle) {
    hand_->setAlpha(alpha);
    hand_->setAngle(angle);
    hand_->setVisible(true);
    if (image != image_) {
        hand_->setImage(image == 1 ? tapImage_ : pointImage_);
        hand_->resizeFrameToImage(true, true);
        image_ = image;
    }
    // SetCenter(pos + (centre − pivot)): the sprite's pivot (the fingertip) lands on the point.
    const Point p = hand_->pivot();
    const Point c = hand_->center();
    hand_->setCenter(Point{(screen.x + c.x) - p.x, (screen.y + c.y) - p.y});
}

// --- GameView --------------------------------------------------------------------------------------

GameView::GameView(UiContext& ctx, AppState& app, GameScene& scene, const aa::data::JsonNode& dict) : View(ctx), app_(&app), scene_(&scene) {
    View::init(dict);
    levelName_ = make<OutlineLabelView>(owned_, ctx);
    levelName_->setViewName("LabelLevelName");
    levelName_->init(sub(dict, "LabelLevelName"));
    levelName_->setAlpha(0.0f);
    levelName_->setInteraction(false);
    levelName_->setVisible(false);
    const aa::data::JsonNode left = sub(dict, "SidebarLeft");
    sidebarButtonArea_ = make<ImageView>(owned_, ctx);
    sidebarButtonArea_->setViewName("SidebarButtonArea");
    sidebarButtonArea_->init(sub(left, "SidebarButtonArea"));
    sidebarButtonArea_->setInteraction(true);
    sidebarBackground_ = make<ImageView>(owned_, ctx);
    sidebarBackground_->setViewName("SidebarBackground");
    sidebarBackground_->init(sub(left, "SidebarBackground"));
    sidebarBackground_->setInteraction(true);
    sidebarBackground_->setSize(Size{sidebarBackground_->size().w, ctx.screen.nativeHeight - sidebarButtonArea_->size().h});
    pause_ = make<Button>(owned_, ctx);
    pause_->setViewName("ButtonPause");
    pause_->init(sub(left, "ButtonPause"));
    pause_->setDelegate(this);
    levelNumber_ = make<OutlineLabelView>(owned_, ctx);
    levelNumber_->setViewName("LabelLevelNumber");
    levelNumber_->init(sub(left, "LabelLevelNumber"));
    menu_ = make<Button>(owned_, ctx);
    menu_->setViewName("ButtonMenu");
    menu_->init(sub(left, "ButtonMenu"));
    menu_->setDelegate(this);
    restart_ = make<Button>(owned_, ctx);
    restart_->setViewName("ButtonRestart");
    restart_->init(sub(left, "ButtonRestart"));
    restart_->setDelegate(this);
    solutions_ = make<Button>(owned_, ctx);
    solutions_->setViewName("ButtonSolutions");
    solutions_->init(sub(left, "ButtonSolutions"));
    solutions_->setDelegate(this);
    solutions_->setVisible(false);
    audio_ = make<ToggleButton>(owned_, ctx);
    audio_->setViewName("ButtonAudio");
    audio_->init(sub(left, "ButtonAudio"));
    audio_->setRelativePosition(Point{audio_->relativePosition().x, remake::kGameAudioButtonY});
    audio_->setDelegate(this);
    music_ = make<ToggleButton>(owned_, ctx);
    music_->setViewName("ButtonMusic");
    music_->init(remake::gameMusicButton().root());
    music_->setDelegate(this);
    resultAlex_ = make<ImageView>(owned_, ctx);
    resultAlex_->setViewName("GameViewResultAlex");
    resultAlex_->init(sub(dict, "ResultAlex"));
    resultAlex_->setPivot(Point{resultAlex_->size().w * 0.5f, resultAlex_->size().h * 0.5f});
    resultAlex_->setScale(kAlexScale);
    resultAlex_->setAlpha(0.0f);
    resultAlex_->setInteraction(false);
    circle_ = make<View>(owned_, ctx);
    circle_->setViewName("CircleView");
    circle_->init();
    circle_->setInteraction(false);
    sidebarRight_ = make<ImageView>(owned_, ctx);
    sidebarRight_->setViewName("SidebarRight");
    sidebarRight_->init(sub(dict, "SidebarRight"));
    play_ = make<ToggleButton>(owned_, ctx);
    play_->setViewName("ButtonPlay");
    play_->init(sub(dict, "ButtonPlay"));
    play_->setDelegate(this);
    tipPanel_ = make<View>(owned_, ctx);
    tipPanel_->setViewName("TipPanel");
    tipPanel_->init(remake::gameTipPanel().root());
    tipPanel_->setInteraction(false);   // the taps go through to the world
    tipPanel_->setVisible(false);
    tip_ = make<HighlightLabelView>(owned_, ctx);
    tip_->setViewName("LabelTip");
    tip_->init(remake::gameTipLabel().root());
    tip_->setInteraction(false);
    tipPanel_->addSubview(tip_);
    tipButton_ = make<Button>(owned_, ctx);
    tipButton_->setViewName("ButtonTip");
    tipButton_->init(remake::gameTipButton().root());
    tipButton_->setDelegate(this);
    tipButton_->setVisible(false);
    addSubview(resultAlex_);
    addSubview(circle_);
    // The tip views under the sidebars: the open pause menu covers them.
    addSubview(tipPanel_);
    addSubview(tipButton_);
    addSubview(sidebarBackground_);
    addSubview(sidebarButtonArea_);
    sidebarButtonArea_->addSubview(pause_);
    sidebarButtonArea_->addSubview(levelNumber_);
    sidebarBackground_->addSubview(menu_);
    sidebarBackground_->addSubview(restart_);
    sidebarBackground_->addSubview(solutions_);
    sidebarBackground_->addSubview(audio_);
    sidebarBackground_->addSubview(music_);
    addSubview(sidebarRight_);
    addSubview(play_);
    addSubview(levelName_);
    // "Background" (+0x120) [verified: GameView::Init]: the dim overlay of the open pause menu — a
    // full-screen black view at alpha 0.5, hidden, no interaction.
    dim_ = make<View>(owned_, ctx);
    dim_->setViewName("Background");
    dim_->init(dict);
    dim_->setFrame(Rect{0.0f, 0.0f, ctx.screen.nativeWidth, ctx.screen.nativeHeight});
    dim_->setBackgroundColor(Color{0, 0, 0, 255});
    dim_->setAlpha(0.5f);
    dim_->setVisible(false);
    dim_->setInteraction(false);
    // GameView::Init [verified]: the dim is added right after the letterbox borders (the remake draws
    // them under the world, drawLetterBoxBackdrop) — under the sidebars, over the world.
    insertSubview(dim_, 0);
    updateViewAnchors(true, true);
    rightShown_ = Point{sidebarRight_->position().x - sidebarRight_->size().w, sidebarRight_->position().y};
    rightHidden_ = sidebarRight_->position();
    leftOpenX_ = 0.0f;
    leftShownX_ = -sidebarBackground_->size().w;
    leftHiddenX_ = -sidebarButtonArea_->size().w;
}

std::vector<View*> GameView::leftViews() { return {sidebarButtonArea_, sidebarBackground_}; }
std::vector<View*> GameView::rightViews() { return {sidebarRight_, play_}; }

void GameView::setMenuInteraction(bool on) {
    pause_->setInteraction(on);
    menu_->setInteraction(on);
    restart_->setInteraction(on);
    solutions_->setInteraction(on);
    audio_->setInteraction(on);
    music_->setInteraction(on);
}

Point GameView::playButtonCenter() const {
    const Point g = play_->globalPosition();
    return Point{g.x + play_->size().w * 0.5f, g.y + play_->size().h * 0.5f};
}

void GameView::recomputeAutoSize() {
    // sidebarBackground_'s children (ButtonMenu / ButtonRestart / …) anchor to its own height — this must
    // land before View::relayout()'s anchor pass, not after (sidebarButtonArea_'s own AutoResize size, if
    // any, is already current: recomputeAutoSizeRecursive() is post-order).
    sidebarBackground_->setSize(Size{sidebarBackground_->size().w, ctx_->screen.nativeHeight - sidebarButtonArea_->size().h});
    // The constructor's centre pivot (the popup zooms about it), against the fresh AutoResize size.
    resultAlex_->setPivot(Point{resultAlex_->size().w * 0.5f, resultAlex_->size().h * 0.5f});
}

void GameView::relayout() {
    // sidebarBackground_/sidebarButtonArea_ (leftViews()) and sidebarRight_/play_ (rightViews()) slide as
    // pairs by a shared delta (Animator::animate(vector<View*>, delta, …)); View::relayout() completes
    // whatever slide is in flight (Animator::completeAnimations, closeAnim_'s freshly spawned
    // showControlsAnim_ included) before it puts every view back at its fresh anchor rest.
    View::relayout();
    // The dim overlay: full-screen, constructor-baked (not Relative/Anchor-driven).
    dim_->setFrame(Rect{0.0f, 0.0f, ctx_->screen.nativeWidth, ctx_->screen.nativeHeight});
    // sidebarButtonArea_ / sidebarRight_ are anchored off screen (their Anchor targets the screen edge, not
    // a named sibling), so their post-relayout anchor-rest position *is* the fresh leftHiddenX_ / rightHidden_
    // endpoint; sidebarBackground_ / play_ are anchored to *them* by name, so they are already correctly
    // offset. Re-derive the three left endpoints and the two right ones exactly as the constructor did, then
    // apply the one delta each pair still needs to reach the state that was current before this call.
    leftOpenX_ = 0.0f;
    leftShownX_ = -sidebarBackground_->size().w;
    leftHiddenX_ = -sidebarButtonArea_->size().w;
    float leftTarget = leftHiddenX_;
    if (menuState_ == MenuState::Open) leftTarget = leftOpenX_;
    else if (menuState_ == MenuState::Shown || menuState_ == MenuState::Closing) leftTarget = leftShownX_;
    const float leftDx = leftTarget - sidebarButtonArea_->position().x;
    if (leftDx != 0.0f) {
        for (View* v : leftViews()) v->setPosition(Point{v->position().x + leftDx, v->position().y});
    }
    rightHidden_ = sidebarRight_->position();
    rightShown_ = Point{rightHidden_.x - sidebarRight_->size().w, rightHidden_.y};
    layoutTip();
    const float rightTarget = controlsShown_ ? rightShown_.x : rightHidden_.x;
    const float rightDx = rightTarget - sidebarRight_->position().x;
    if (rightDx != 0.0f) {
        for (View* v : rightViews()) v->setPosition(Point{v->position().x + rightDx, v->position().y});
    }
}

void GameView::showGameControls(bool animated) {
    play_->setInteraction(false);
    play_->setChecked(false);
    resultAlex_->setAlpha(0.0f);
    controlsShown_ = true;
    if (showControlsAnim_ != 0) return;
    ctx_->animator->cancelAnimation(hideControlsAnim_);
    hideControlsAnim_ = 0;
    showControlsAnim_ = ctx_->animator->animate(rightViews(), slide(rightShown_.x - sidebarRight_->position().x, animated ? kMenuAnim : 0.0f), this);
}

void GameView::hideGameControls(bool animated) {
    play_->setInteraction(false);
    play_->setChecked(false);
    controlsShown_ = false;
    if (hideControlsAnim_ != 0) return;
    ctx_->animator->cancelAnimation(showControlsAnim_);
    showControlsAnim_ = 0;
    hideControlsAnim_ = ctx_->animator->animate(rightViews(), slide(rightHidden_.x - sidebarRight_->position().x, animated ? kMenuAnim : 0.0f), this);
}

void GameView::enableGameControls(bool animated) {
    play_->setInteraction(false);
    play_->setChecked(false);
    if (enableControlsAnim_ != 0) return;
    ctx_->animator->cancelAnimation(disableControlsAnim_);
    disableControlsAnim_ = 0;
    enableControlsAnim_ = ctx_->animator->animate(rightViews(), fade(1.0f - sidebarRight_->alpha(), animated ? kMenuAnim : 0.0f), this);
}

void GameView::disableGameControls(bool animated) {
    play_->setInteraction(false);
    play_->setChecked(false);
    if (disableControlsAnim_ != 0) return;
    ctx_->animator->cancelAnimation(enableControlsAnim_);
    enableControlsAnim_ = 0;
    disableControlsAnim_ = ctx_->animator->animate(rightViews(), fade((1.0f - sidebarRight_->alpha()) - kDisabledAlpha, animated ? kMenuAnim : 0.0f), this);
}

void GameView::showSimulationControls() {
    play_->setChecked(true);
    wasRunning_ = true;
}

void GameView::hideSimulationControls() {
    play_->setChecked(false);
    wasRunning_ = false;
}

void GameView::showPauseMenu(bool animated) {
    dim_->setVisible(true);
    if (!animated) {
        for (View* v : leftViews()) v->setPosition(Point{leftShownX_, v->position().y});
        menuState_ = MenuState::Shown;
        return;
    }
    setMenuInteraction(false);
    if (showMenuAnim_ != 0) return;
    ctx_->animator->cancelAnimation(hideMenuAnim_);
    hideMenuAnim_ = 0;
    showMenuAnim_ = ctx_->animator->animate(leftViews(), slide(leftShownX_ - sidebarButtonArea_->position().x, kMenuAnim), this);
}

void GameView::hidePauseMenu(bool animated) {
    dim_->setVisible(true);
    dim_->setAlpha(0.0f);
    if (!animated) {
        for (View* v : leftViews()) v->setPosition(Point{leftHiddenX_, v->position().y});
        menuState_ = MenuState::Hidden;
        return;
    }
    setMenuInteraction(false);
    if (hideMenuAnim_ != 0) return;
    ctx_->animator->cancelAnimation(showMenuAnim_);
    showMenuAnim_ = 0;
    hideMenuAnim_ = ctx_->animator->animate(leftViews(), slide(leftHiddenX_ - sidebarButtonArea_->position().x, kMenuAnim), this);
}

void GameView::openPauseMenu(bool animated) {
    audio_->setChecked(!app_->settings.soundEffectsOn);
    music_->setChecked(!app_->settings.musicOn);
    hideGameControls(animated);
    dim_->setVisible(true);
    dim_->setAlpha(0.0f);
    dim_->setInteraction(true);
    if (sidebarButtonArea_->alpha() < 1.0f) {
        sidebarButtonArea_->setAlpha(1.0f);
        pause_->setState(button_state::kNormal);
    }
    if (!animated) {
        for (View* v : leftViews()) v->setPosition(Point{leftOpenX_, v->position().y});
        menuState_ = MenuState::Open;
        dim_->setAlpha(kDimAlpha);
        return;
    }
    setMenuInteraction(false);
    AnimationParameters d = AnimationParameters::fromView(*dim_);
    d.alpha = kDimAlpha;
    d.duration = kMenuAnim;
    d.repeat = 1;
    ctx_->animator->animate(dim_, d, nullptr);
    if (openAnim_ != 0) return;
    ctx_->animator->cancelAnimation(closeAnim_);
    closeAnim_ = 0;
    openAnim_ = ctx_->animator->animate(leftViews(), slide(leftOpenX_ - sidebarButtonArea_->position().x, kMenuAnim), this);
}

void GameView::closePauseMenu(bool animated) {
    dim_->setVisible(true);
    dim_->setAlpha(kDimAlpha);
    dim_->setInteraction(false);
    if (!animated) {
        for (View* v : leftViews()) v->setPosition(Point{leftShownX_, v->position().y});
        menuState_ = MenuState::Closing;
        return;
    }
    setMenuInteraction(false);
    AnimationParameters d = AnimationParameters::fromView(*dim_);
    d.alpha = 0.0f;
    d.duration = kMenuAnim;
    d.repeat = 1;
    ctx_->animator->animate(dim_, d, nullptr);
    if (closeAnim_ != 0) return;
    ctx_->animator->cancelAnimation(openAnim_);
    openAnim_ = 0;
    closeAnim_ = ctx_->animator->animate(leftViews(), slide(leftShownX_ - sidebarButtonArea_->position().x, kMenuAnim), this);
}

void GameView::enablePauseMenu(bool animated) {
    dim_->setVisible(true);
    dim_->setAlpha(0.0f);
    if (!animated) {
        sidebarButtonArea_->setAlpha(1.0f);
        pause_->setState(button_state::kNormal);
        menuState_ = MenuState::Shown;
        return;
    }
    setMenuInteraction(false);
    if (enableMenuAnim_ != 0) return;
    ctx_->animator->cancelAnimation(disableMenuAnim_);
    disableMenuAnim_ = 0;
    enableMenuAnim_ = ctx_->animator->animate(leftViews(), fade(1.0f - sidebarButtonArea_->alpha(), kMenuAnim), this);
}

void GameView::disablePauseMenu(bool animated) {
    dim_->setVisible(true);
    dim_->setAlpha(0.0f);
    if (!animated) {
        sidebarButtonArea_->setAlpha(kDisabledAlpha);
        pause_->setState(button_state::kDisabled);
        menuState_ = MenuState::Hidden;
        return;
    }
    setMenuInteraction(false);
    if (disableMenuAnim_ != 0) return;
    ctx_->animator->cancelAnimation(enableMenuAnim_);
    enableMenuAnim_ = 0;
    disableMenuAnim_ = ctx_->animator->animate(leftViews(), fade((1.0f - sidebarButtonArea_->alpha()) - kDisabledAlpha, kMenuAnim), this);
}

void GameView::showLevelName(bool animated) {
    levelName_->setVisible(true);
    if (!animated) {
        levelName_->setAlpha(1.0f);
        return;
    }
    AnimationParameters p = AnimationParameters::fromView(*levelName_);
    p.alpha = 1.0f;
    p.curve = kCurveEaseOut;
    p.duration = kFade;
    p.repeat = 1;
    nameShowAnim_ = ctx_->animator->animate(levelName_, p, this);
}

void GameView::hideLevelName(bool animated) {
    if (!animated) {
        levelName_->setVisible(false);
        levelName_->setAlpha(0.0f);
        return;
    }
    AnimationParameters p = AnimationParameters::fromView(*levelName_);
    p.alpha = 0.0f;
    p.curve = kCurveEaseIn;
    p.delay = kNameHideDelay;
    p.duration = kFade;
    p.repeat = 1;
    nameHideAnim_ = ctx_->animator->animate(levelName_, p, this);
}

void GameView::updateLevelInfo() {
    const int loc = app_->locationIndex;
    const int level = app_->currentLevel;
    if (loc >= 0) {
        levelName_->setText(app_->meta(loc, level).titleId);
        levelNumber_->setNonLocalizedText(std::to_string(loc + 1) + "-" + std::to_string(level + 1));
        levelNumber_->setVisible(true);
        levelNumber_->updateViewAnchors(true, false);
    } else {
        levelNumber_->setVisible(false);
    }
    solutions_->setVisible(false);
    solutions_->setInteraction(false);
    // A campaign level's tip (a sandbox level's description is its author's, not a localised tip).
    std::string tipId;
    if (loc >= 0 && !app_->isSandboxLocation(loc)) tipId = app_->meta(loc, level).tipId;
    // localizedText hands an id without a text back as the id itself: no tip then either.
    const std::string text = tipId.empty() ? std::string() : localizedText(*ctx_, tipId);
    hideTip();
    const bool hasTip = !text.empty() && text != tipId;
    if (hasTip) tip_->setText(tipId);
    else tip_->setNonLocalizedText("");
    tipButton_->setVisible(hasTip);
    tipButton_->setInteraction(hasTip);
    layoutTip();
}

float GameView::tipReadingTime(const std::string& visibleText) {
    const float letters = static_cast<float>(decodeUtf8(visibleText).size());
    return std::clamp(kTipBaseTime + kTipTimePerLetter * letters, kTipMinTime, kTipMaxTime);
}

void GameView::showTip() {
    if (!tipButton_->isVisible()) return;
    tipTime_ = tipReadingTime(HighlightLabelView::stripMarkers(tip_->text())) + 2.0f * kTipFade;
    tipPanel_->setVisible(true);
    tipPanel_->setAlpha(0.0f);
    layoutTip();
}

void GameView::hideTip() {
    tipTime_ = 0.0f;
    tipPanel_->setVisible(false);
}

void GameView::layoutTip() {
    // The toolbox strip: native px, y up from the window's bottom (Toolbox, docs/05 §5).
    const float h = ctx_->screen.nativeHeight;
    const float playLeft = ctx_->screen.letterBoxFrameWidth;
    const float playRight = ctx_->screen.nativeWidth - ctx_->screen.letterBoxFrameWidth;
    const aa::sim::Toolbox& tb = scene_->session().toolbox();
    const aa::sim::ScreenRect strip = tb.getToolboxRectangle();
    const float margin = std::round(16.0f * ctx_->screen.uiScale);
    const float padding = std::round(14.0f * ctx_->screen.uiScale);
    // The info button: at the play field's left edge, on the strip's centre line.
    const Size b = tipButton_->size();
    const float centreY = h - tb.y;
    const float shownX = playLeft + margin;
    const float s = tipButtonSlide_;
    const float eased = s * s * (3.0f - 2.0f * s);   // smoothstep, as the sidebars' kCurveSmooth
    tipButton_->setPosition(Point{shownX + (-b.w - shownX) * eased, std::min(centreY - b.h * 0.5f, h - margin - b.h)});
    if (!tipPanel_->isVisible()) return;
    // The panel: right of the button up to the strip, its bottom on the button's; above the strip when the
    // strip leaves too little room beside it (a long toolbox).
    const float stripLeft = std::min(strip.left, playRight);
    float left = tipButton_->position().x + b.w + margin;
    float right = stripLeft - margin;
    float bottom = tipButton_->position().y + b.h;
    if (right - left < (playRight - playLeft) * 0.35f) {
        left = playLeft + margin;
        right = playRight - margin;
        bottom = h - (strip.top) - margin;
    }
    const float labelW = std::max(1.0f, right - left - 2.0f * padding);
    if (tip_->size().w != labelW) {
        tip_->setFrame(Rect{padding, padding, labelW, tip_->size().h});
        tip_->reWrap();
    }
    const float panelH = tip_->size().h + 2.0f * padding;
    tipPanel_->setFrame(Rect{left, bottom - panelH, right - left, panelH});
    tip_->setPosition(Point{padding, padding});
}

void GameView::startLevelCompleted(Point goalScreen) {
    circle_->setCenter(goalScreen);
    circle_->setAlpha(1.0f);
    alexTime_ = 0.0f;
    alexSegment_ = 0;
    circle_->setScale(0.3f);
    resultAlex_->setCenter(goalScreen);
    resultAlex_->setAlpha(0.0f);
    resultAlex_->setScale(kAlexScale);
    alexAnimating_ = true;
}

void GameView::hideLevelCompleteStartAnim() {
    resultAlex_->setAlpha(0.0f);
    circle_->setAlpha(0.0f);
}

void GameView::show() {
    hidePauseMenu(false);
    showPauseMenu(true);
    hideGameControls(false);
    showGameControls(true);
    updateLevelInfo();
    resultAlex_->setAlpha(0.0f);
    circle_->setAlpha(0.0f);
    alexAnimating_ = false;
    dim_->setVisible(true);
    dim_->setAlpha(kDimAlpha);
    dim_->setInteraction(false);
    setAlpha(0.0f);
    AnimationParameters p = AnimationParameters::fromView(*this);
    p.alpha = 1.0f;
    p.curve = kCurveSmooth;
    p.duration = kFade;
    p.repeat = 1;
    showAnim_ = ctx_->animator->animate(this, p, this);
    // ShowPauseMenu(false) is repeated at the end of Show: the menu closes to the shown state.
    for (View* v : leftViews()) v->setPosition(Point{leftShownX_, v->position().y});
    menuState_ = MenuState::Closing;
    setMenuInteraction(false);
    AnimationParameters d = AnimationParameters::fromView(*dim_);
    d.alpha = 0.0f;
    d.duration = kMenuAnim;
    d.repeat = 1;
    ctx_->animator->animate(dim_, d, nullptr);
    if (closeAnim_ == 0) {
        ctx_->animator->cancelAnimation(openAnim_);
        openAnim_ = 0;
        closeAnim_ = ctx_->animator->animate(leftViews(), slide(0.0f, kMenuAnim), this);
    }
}

void GameView::hide() {
    hidePauseMenu(true);
    hideGameControls(true);
    resultAlex_->setAlpha(0.0f);
    circle_->setAlpha(0.0f);
    alexAnimating_ = false;
    AnimationParameters p = AnimationParameters::fromView(*this);
    p.alpha = 0.0f;
    p.curve = kCurveSmooth;
    p.duration = kFade;
    p.repeat = 1;
    hideAnim_ = ctx_->animator->animate(this, p, this);
}

void GameView::update(float dt) {
    View::update(dt);
    // The info button leaves with the HUD once the simulation starts (controller state 4+) and while the
    // level completes; it takes no presses from the first frame of its slide out.
    const aa::sim::Session& session = scene_->session();
    const bool buttonAway = session.controllerState() >= 4 || session.completing();
    const float slideStep = dt / kMenuAnim;
    tipButtonSlide_ = std::clamp(tipButtonSlide_ + (buttonAway ? slideStep : -slideStep), 0.0f, 1.0f);
    tipButton_->setInteraction(tipButton_->isVisible() && !buttonAway && tipButtonSlide_ == 0.0f);
    if (tipPanel_->isVisible()) {
        tipTime_ -= dt;
        const float total = tipReadingTime(HighlightLabelView::stripMarkers(tip_->text())) + 2.0f * kTipFade;
        if (tipTime_ <= 0.0f || buttonAway) {
            hideTip();
        } else {
            const float elapsed = total - tipTime_;
            tipPanel_->setAlpha(std::clamp(std::min(elapsed, tipTime_) / kTipFade, 0.0f, 1.0f));
            layoutTip();
        }
    } else {
        layoutTip();   // the info button follows the strip's slide
    }
    if (alexAnimating_) {
        if (alexSegment_ >= 2) {
            alexAnimating_ = false;
            resultAlex_->setAlpha(1.0f);
            circle_->setAlpha(0.0f);
        } else {
            const float s = valueAnimator(dt, alexTime_, alexSegment_, kAlexPopup, 2);
            circle_->setScale(s);
            resultAlex_->setScale(s * kAlexScale);
            resultAlex_->setAlpha(1.0f);
        }
    }
}

void GameView::animationFinished(int id) {
    if (id == showAnim_) {
        showAnim_ = 0;
        enablePauseMenu(false);
        enableGameControls(false);
        showLevelName(true);
        showTip();
    }
    if (id == hideAnim_) hideAnim_ = 0;
    if (id == nameShowAnim_) {
        nameShowAnim_ = 0;
        hideLevelName(true);
    }
    if (id == nameHideAnim_) {
        nameHideAnim_ = 0;
        hideLevelName(false);
    }
    if (id == showMenuAnim_) {
        showMenuAnim_ = 0;
        menuState_ = MenuState::Shown;
        setMenuInteraction(true);
        dim_->setInteraction(false);
        dim_->setVisible(false);
    } else if (id == hideMenuAnim_) {
        hideMenuAnim_ = 0;
        menuState_ = MenuState::Hidden;
        setMenuInteraction(true);
        dim_->setInteraction(false);
        dim_->setVisible(false);
    } else if (id == openAnim_) {
        openAnim_ = 0;
        menuState_ = MenuState::Open;
        setMenuInteraction(true);
    } else if (id == closeAnim_) {
        closeAnim_ = 0;
        menuState_ = MenuState::Closing;
        setMenuInteraction(true);
        dim_->setInteraction(false);
        dim_->setVisible(false);
        showGameControls(true);
        menuState_ = MenuState::Shown;
    } else if (id == enableMenuAnim_) {
        enableMenuAnim_ = 0;
        menuState_ = MenuState::Shown;
        pause_->setState(button_state::kNormal);
        setMenuInteraction(true);
        dim_->setInteraction(false);
        dim_->setVisible(false);
    } else if (id == disableMenuAnim_) {
        disableMenuAnim_ = 0;
        menuState_ = MenuState::Hidden;
        setMenuInteraction(true);
        dim_->setInteraction(false);
        dim_->setVisible(false);
    }
    if (id == showControlsAnim_) {
        showControlsAnim_ = 0;
        play_->setInteraction(true);
        play_->setAlpha(1.0f);
    }
    if (id == hideControlsAnim_) {
        hideControlsAnim_ = 0;
        play_->setInteraction(false);
    }
    if (id == enableControlsAnim_) {
        enableControlsAnim_ = 0;
        play_->setState(button_state::kNormal);
        play_->setAlpha(1.0f);
    }
    if (id == disableControlsAnim_) disableControlsAnim_ = 0;
}

void GameView::buttonAboutToBePressed(int id) {
    // Remake: the info button leaves the menu's interaction as it is (a show / slide may hold it off).
    if (id == tipButton_->id()) return;
    setMenuInteraction(false);
}

void GameView::buttonPressed(int id) {
    // Remake: the info button toggles the tip and leaves the tutorial, the held item and the menu alone.
    if (id == tipButton_->id()) {
        if (isTipShown()) hideTip();
        else showTip();
        return;
    }
    scene_->session().stopTutorial();   // GameView::ButtonPressed [verified]: the gizmo off, the tutorial stopped
    scene_->session().pause();          // the held item released (set-up state only)
    hideLevelName(false);
    setMenuInteraction(true);
    SceneManager* manager = scene_->manager();
    if (id == pause_->id()) {
        hideLevelName(false);
        if (menuState_ == MenuState::Closing || menuState_ == MenuState::Shown) {
            hideTip();   // remake: the tip would sit over the dim and the sidebar
            scene_->setLevelMenu(true);
            openPauseMenu(true);
        } else if (menuState_ == MenuState::Open) {
            scene_->setLevelMenu(false);
            closePauseMenu(true);
            enableGameControls(true);
        }
        return;
    }
    if (id == menu_->id()) {
        hidePauseMenu(true);
        if (!showChapterComplete(*manager, *app_)) manager->popScene();
        return;
    }
    if (id == restart_->id()) {
        // Remake fix: the original's FinishAnimation leaves the hide item in the list, and its deferred
        // callback (hideLevelName(false)) would then hide the name showLevelName(true) is about to fade in
        // (a restart within the ~2.5 s the name stays on screen). CompleteAnimation runs each chain to its
        // end synchronously: show → hide → hidden, nothing left in flight.
        ctx_->animator->completeAnimation(nameShowAnim_);
        ctx_->animator->completeAnimation(nameHideAnim_);
        closePauseMenu(true);
        scene_->restartLevel();
        showLevelName(true);
        return;
    }
    if (id == audio_->id()) {
        app_->settings.setAudioState(!app_->settings.soundEffectsOn);
        app_->saveSettings();
        if (app_->audio) app_->audio->setMuted(!app_->settings.soundEffectsOn);
        return;
    }
    if (id == music_->id()) {
        app_->settings.setMusicState(!app_->settings.musicOn);
        app_->saveSettings();
        if (app_->audio) app_->audio->setMusicEnabled(app_->settings.musicOn);
        return;
    }
    if (id == play_->id() && play_->isInteractable()) {
        // handleButtonRelease(5): toggleSimulation.
        aa::sim::Session& s = scene_->session();
        if (s.physicsMode() == aa::sim::PhysicsMode::SetUp) s.play();
        else s.stop();
    }
}

bool GameView::keyDown(int key) {
    if (View::keyDown(key)) return true;
    if (key != SceneManager::kKeyBack) return false;
    if (scene_->session().completing() || alexAnimating_ || resultAlex_->alpha() > 0.0f) return false;
    // KeyDown [verified]: the back key presses the play button while the menu is hidden (the simulation
    // runs), else the pause button.
    if (menuState_ == MenuState::Hidden) buttonPressed(play_->id());
    else if (pause_->isInteractable()) buttonPressed(pause_->id());
    return true;
}

void GameView::touchesStarted(const TouchEvent& e) {
    if (scene_->levelMenuOpen()) return;
    scene_->session().pointerDown(e.id, aa::sim::Vec2(e.position.x, e.position.y));
}
void GameView::touchesMovedInside(const TouchEvent& e) {
    if (scene_->levelMenuOpen()) return;
    scene_->session().pointerMove(e.id, aa::sim::Vec2(e.position.x, e.position.y));
}
void GameView::touchesMovedOutside(const TouchEvent& e) { touchesMovedInside(e); }
void GameView::touchesFinishedInside(const TouchEvent& e) {
    if (scene_->levelMenuOpen()) return;
    scene_->session().pointerUp(e.id, aa::sim::Vec2(e.position.x, e.position.y));
}
void GameView::touchesFinishedOutside(const TouchEvent& e) { touchesFinishedInside(e); }
void GameView::touchesCancel(const TouchEvent& e) { scene_->session().pointerCancel(e.id); }

// --- LevelCompletedView ----------------------------------------------------------------------------

LevelCompletedView::LevelCompletedView(UiContext& ctx, AppState& app, GameScene& scene, const aa::data::JsonNode& dict)
    : View(ctx), app_(&app), scene_(&scene) {
    View::init(dict);
    const aa::data::JsonNode panel = sub(dict, "PanelResult");
    panelResult_ = make<View>(owned_, ctx);
    panelResult_->setViewName("PanelResult");
    panelResult_->init(panel);
    panelBackground_ = make<View>(owned_, ctx);
    panelBackground_->setViewName("PanelBackground");
    panelBackground_->init(sub(panel, "PanelBackground"));
    resultAlex2_ = make<ImageView>(owned_, ctx);
    resultAlex2_->setViewName("ImageResultAlex2");
    resultAlex2_->init(sub(panel, "ResultAlex"));
    backgroundBottom_ = make<View>(owned_, ctx);
    backgroundBottom_->setViewName("ImageBackgroundBottom");
    backgroundBottom_->init(sub(panel, "ImageBackgroundBottom"));
    resultAlex_ = make<ImageView>(owned_, ctx);
    resultAlex_->setViewName("ImageResultAlex");
    resultAlex_->init(sub(panel, "ResultAlex"));
    recomputeAutoSize();
    panelBackground_->addSubview(backgroundBottom_);
    panelBackground_->addSubview(resultAlex2_);
    resultAlex_->setScale(kAlexScale);
    resultAlex_->setAlpha(0.0f);
    resultAlex_->setViewAnchor(Anchor{}, Anchor{}, "", "");
    resultAlex_->setInteraction(false);
    addSubview(resultAlex_);
    auto button = [&](const char* key, Button*& out) {
        out = make<Button>(owned_, ctx);
        out->setViewName(key);
        out->init(sub(panel, key));
        out->setDelegate(this);
    };
    button("ButtonMenu", menu_);
    button("ButtonMenuWoC", menuWoC_);
    button("ButtonRetry", retry_);
    button("ButtonRetryWoC", retryWoC_);
    button("ButtonForward", forward_);
    button("ButtonShare", share_);
    share_->setVisible(false);
    const char* emptyKeys[3] = {"Stars/OneEmpty", "Stars/TwoEmpty", "Stars/ThreeEmpty"};
    const char* starKeys[3] = {"Stars/One", "Stars/Two", "Stars/Three"};
    const char* emptyNames[3] = {"OneEmpty", "TwoEmpty", "ThreeEmpty"};
    const char* starNames[3] = {"One", "Two", "Three"};
    for (int i = 0; i < 3; ++i) {
        starEmpty_[i] = make<ImageView>(owned_, ctx);
        starEmpty_[i]->setViewName(emptyNames[i]);
        starEmpty_[i]->init(sub(panel, emptyKeys[i]));
        star_[i] = make<ImageView>(owned_, ctx);
        star_[i]->setViewName(starNames[i]);
        star_[i]->init(sub(panel, starKeys[i]));
    }
    bestResult_ = make<ImageView>(owned_, ctx);
    bestResult_->setViewName("ImageBestResult");
    bestResult_->init(sub(panel, "ImageBestResult"));
    panelResult_->addSubview(panelBackground_);
    for (Button* b : {menu_, menuWoC_, retry_, retryWoC_, forward_, share_}) panelResult_->addSubview(b);
    for (int i = 0; i < 3; ++i) panelResult_->addSubview(starEmpty_[i]);
    for (int i = 0; i < 3; ++i) panelResult_->addSubview(star_[i]);
    panelResult_->addSubview(bestResult_);
    addSubview(panelResult_);
    updateViewAnchors(true, true);
    panelHome_ = panelResult_->position();
    menuWoC_->setVisible(false);
    retryWoC_->setVisible(false);
}

void LevelCompletedView::recomputeAutoSize() {
    // Init's sizing tail [verified], over the Relative sizes View::init gave the panels (relayout() runs
    // this after its relative-frame pass, post-order — resultAlex2_'s AutoResize is already current): the
    // panel background is 0.9 × the Alex picture's height; the panel keeps its width ratio; the bottom
    // strip takes the background's width; the panel's pivot is its centre (the original only sets it
    // while still zero — always so at Init; a resize re-centres it against the fresh size).
    const float ratio = panelResult_->size().w / panelBackground_->size().w;
    const float alexH = resultAlex2_->imageSize().h * 0.9f;
    panelBackground_->setSize(Size{panelBackground_->size().w, alexH});
    panelResult_->setSize(Size{alexH * ratio, panelResult_->size().h});
    panelResult_->setPivot(panelResult_->center());
    backgroundBottom_->setSize(Size{panelBackground_->size().w, backgroundBottom_->size().h});
    // The travelling Alex zooms about its centre (a plain AutoResize reload would put the atlas pivot back).
    resultAlex_->setPivot(Point{resultAlex_->size().w * 0.5f, resultAlex_->size().h * 0.5f});
}

void LevelCompletedView::relayout() {
    View::relayout();
    // panelHome_ is the anchor-rest position captured once by the constructor (hideButtons() resets
    // panelResult_ to it) — re-capture it from the fresh anchor position View::relayout() just derived, or
    // a later hideButtons() would snap back to a stale, pre-resize coordinate.
    panelHome_ = panelResult_->position();
}

void LevelCompletedView::hideButtons() {
    for (Button* b : {menu_, menuWoC_, retry_, retryWoC_, forward_, share_}) b->setVisible(false);
    panelResult_->setPosition(panelHome_);
}

void LevelCompletedView::show(Point goalScreen, int stars, bool improved) {
    stars_ = stars;
    improved_ = improved;
    setVisible(true);
    setInteraction(true);
    bestResult_->setVisible(false);
    hideButtons();
    panelResult_->setAlpha(0.0f);
    panelBackground_->setAlpha(0.0f);
    backgroundBottom_->setAlpha(0.0f);
    // The travelling Alex: from the goal position (scale 0.46) to the panel's picture (scale 1).
    resultAlex_->setScale(kAlexScale);
    resultAlex_->setAlpha(1.0f);
    resultAlex_->setCenter(goalScreen);
    alexStart_ = resultAlex_->position();
    const Point target = resultAlex2_->globalPosition();
    const Point self = globalPosition();
    const Size ts = resultAlex2_->size();
    const Size ss = resultAlex_->size();
    const float extra = std::max(ss.h * 0.25f, ts.h * 0.5f);
    alexDelta_ = Point{(target.x - self.x + ts.w * 0.5f) - goalScreen.x, (extra + (target.y - self.y)) - goalScreen.y};
    alexTime_ = 0.0f;
    alexSegment_ = 0;
    alexAnimating_ = true;
    for (int i = 0; i < 3; ++i) {
        star_[i]->setVisible(false);
        starEmpty_[i]->setVisible(false);
    }
}

void LevelCompletedView::hide() {
    setVisible(false);
    setInteraction(false);
    alexAnimating_ = false;
}

void LevelCompletedView::update(float dt) {
    View::update(dt);
    if (!isVisible() || !alexAnimating_) return;
    if (alexSegment_ < 2) {
        const float s = valueAnimator(dt, alexTime_, alexSegment_, kAlexResult, 2);
        resultAlex_->setScale(s);
        const float t = (s - kAlexScale) / (1.0f - kAlexScale);
        resultAlex_->setPosition(Point{alexStart_.x + t * alexDelta_.x, alexStart_.y + t * alexDelta_.y});
        return;
    }
    alexAnimating_ = false;
    resultAlex_->setAlpha(0.0f);
    showPanels();
}

void LevelCompletedView::showPanels() {
    panelResult_->setAlpha(1.0f);
    panelBackground_->setAlpha(1.0f);
    backgroundBottom_->setAlpha(1.0f);
    showStars(stars_);
    // ShowPanels' unlock branch (levelImproved && every level completed) ran in GameScene::completeLevel.
}

void LevelCompletedView::showStars(int stars) {
    bestResult_->setVisible(false);
    for (int i = 0; i < 3; ++i) {
        starEmpty_[i]->setVisible(true);
        star_[i]->setVisible(false);
    }
    if (stars <= 0) {
        showButtons();
        return;
    }
    for (int i = 0; i < stars && i < 3; ++i) {
        ImageView* s = star_[i];
        s->setScale(kStarPopScale);
        AnimationParameters p = AnimationParameters::fromView(*s);
        p.scale = 1.0f;
        p.delay = kStarDelays[i];
        p.duration = kStarPopDuration;
        p.repeat = 1;
        // The star is placed at its empty twin's centre before it pops.
        const Point c = starEmpty_[i]->center();
        const Point ep = starEmpty_[i]->position();
        s->setCenter(Point{ep.x + c.x, ep.y + c.y});
        s->setPivot(s->center());
        starAnim_[i] = ctx_->animator->animate(s, p, this);
    }
}

void LevelCompletedView::popButton(Button* b) {
    b->setVisible(true);
    b->setScale(kStarPopScale);
    AnimationParameters p = AnimationParameters::fromView(*b);
    p.scale = 1.0f;
    p.duration = kStarPopDuration;
    p.repeat = 1;
    ctx_->animator->animate(b, p, nullptr);
}

void LevelCompletedView::showButtons() {
    popButton(menu_);
    popButton(retry_);
    forward_->setVisible(true);
    const aa::game::LocationInfo* info = app_->location();
    bool canNext = info && app_->locationState.canPlayNextLevel(*info);
    if (!canNext && info) {
        // The last level of a chapter: the forward button leads to the chapter panel / the books.
        const bool chapterDone = app_->locationState.completedLevelsCount(*info) == info->levelCount();
        const bool nextChapterOpen = app_->locationIndex + 1 < aa::game::kLocationCount &&
                                     app_->progress.locationUnlocked(app_->locationIndex + 1);
        canNext = chapterDone || (nextChapterOpen && app_->currentLevel == info->levelCount() - 1);
    }
    forward_->setState(canNext ? button_state::kNormal : button_state::kDisabled);
    popButton(forward_);
}

void LevelCompletedView::animationStarted(int id) {
    for (int i = 0; i < 3; ++i) {
        if (id != starAnim_[i]) continue;
        starEmpty_[i]->setVisible(false);
        star_[i]->setVisible(true);
        if (app_->soundSink) app_->soundSink->play(kResultStarSounds[i], kResultStarVolume, aa::sim::Vec2(0.0f, 0.0f));
    }
}

void LevelCompletedView::animationFinished(int id) {
    for (int i = 0; i < 3; ++i) {
        if (id != starAnim_[i]) continue;
        starAnim_[i] = 0;
        if (i == 2 && improved_) bestResult_->setVisible(true);
        if (stars_ == i + 1) {
            if (improved_ && i < 2) bestResult_->setVisible(true);
            showButtons();
        }
        return;
    }
    if (ctx_->sounds) ctx_->sounds->playUiSound(kUiButtonPush, kUiVolume);
}

void LevelCompletedView::buttonPressed(int id) {
    SceneManager* manager = scene_->manager();
    if (id == menu_->id() || id == menuWoC_->id()) {
        if (showChapterComplete(*manager, *app_)) return;
        manager->popScene();
        return;
    }
    if (id == retry_->id() || id == retryWoC_->id()) {
        scene_->replayLevel();
        hide();
        return;
    }
    if (id == forward_->id()) {
        if (showChapterComplete(*manager, *app_)) return;
        const aa::game::LocationInfo* info = app_->location();
        if (info && app_->currentLevel == info->levelCount() - 1) {
            // The chapter's last level: back to the books through the loading screen (location 8).
            manager->removeScene(scene_names::kGame);
            if (auto* levels = dynamic_cast<LevelSelectionScene*>(manager->scene(scene_names::kLevelSelection))) levels->setReturningFromGame(false);
            auto* loading = dynamic_cast<LevelLoadingScene*>(manager->scene(scene_names::kLevelLoading));
            if (!loading || loading->loadingLocation() == 0) {
                manager->pushScene(scene_names::kLevelLoading);
                if (loading) loading->setLoadingLocation(LevelLoadingScene::kLocationBackToChapters, -1);
            }
            if (auto* chapters = dynamic_cast<ChapterSelectionScene*>(manager->scene(scene_names::kChapterSelection))) chapters->setReturningFromGame(false);
            return;
        }
        if (info && app_->locationState.canPlayNextLevel(*info)) {
            scene_->playNextLevel();
            hide();
        }
    }
}

bool LevelCompletedView::keyDown(int key) {
    if (View::keyDown(key) || alexAnimating_) return true;
    if (!menu_->isVisible()) return false;
    if (key == SceneManager::kKeyBack) {
        buttonPressed(menu_->id());
        return true;
    }
    return false;
}

// --- GameScene -------------------------------------------------------------------------------------

GameScene::GameScene(UiContext& ctx, AppState& app, const aa::sim::TemplateTable& templates)
    : GameSceneBase(ctx, app), session_(std::make_unique<aa::sim::Session>(templates)), templates_(&templates) {
    session_->setToolboxFrameSizes(app.toolboxSizes);
    session_->setSoundSink(app.soundSink);
}

void GameScene::init() {
    Scene::init();
    // GameScene::Init passes the root view's frame to each view's Init(UIRect) [verified: disassembly
    // 0x1159d8..0x115a64]; the scene dictionaries only carry their subviews.
    const Rect screen = root_->frame();
    gameView_ = make<GameView>(*app_, *this, tree().view("GameView"));
    gameView_->setViewName("GameView");
    gameView_->setFrame(screen);
    tutorialView_ = make<GameTutorialView>(tree().view("GameTutorialView"));
    tutorialView_->setViewName("GameTutorialView");
    tutorialView_->setFrame(screen);
    completedView_ = make<LevelCompletedView>(*app_, *this, tree().view("LevelCompletedView"));
    completedView_->setViewName("LevelCompletedView");
    completedView_->setFrame(screen);
    completedView_->setVisible(false);
    root_->addSubview(gameView_);
    root_->addSubview(tutorialView_);
    root_->addSubview(completedView_);
    setViewport(width_, height_);
}

void GameScene::setViewport(int width, int height) {
    width_ = width;
    height_ = height;
    session_->setViewport(aa::sim::ScreenLayout::compute(width, height, app_->profilePixelScale));
}

void GameScene::relayout(int width, int height) {
    resizeRootAndMainView(gameView_);
    if (gameView_) gameView_->relayout();
    resizeRootAndMainView(tutorialView_);
    if (tutorialView_) tutorialView_->relayout();
    resizeRootAndMainView(completedView_);
    if (completedView_) completedView_->relayout();
    setViewport(width, height);
}

Point GameScene::worldToScreen(aa::sim::Vec2 world) const {
    const aa::sim::Vec2 p = session_->worldToScreen(world);
    return Point{p.x, static_cast<float>(height_) - p.y};
}

bool GameScene::selectLevel(int level) {
    const aa::game::LocationInfo* info = app_->location();
    if (!info || level < 0 || level >= info->levelCount()) return false;
    app_->currentLevel = level;
    app_->locationState.currentLevel = level;
    const aa::sim::Level data = app_->loadLevel(app_->locationIndex, level);
    session_->setTutorialContext(app_->locationIndex, level);
    session_->load(data, aa::sim::GameMode::Campaign);
    session_->visual().revealGoalMarkers();
    startLevelWithGoals();
    return true;
}

void GameScene::startLevelWithGoals() {
    lastControllerState_ = session_->controllerState();
    completing_ = false;
    completed_ = false;
    menuOpen_ = false;
    overlay_ = -1;
    levelImproved_ = false;
    completedStars_ = 0;
}

void GameScene::activate() {
    Scene::activate();
    gameView_->show();
    if (completedView_) completedView_->hide();
    if (app_->audio) {
        app_->audio->stopMusic();
        app_->audio->playMusic(kMusicGame);
    }
}

void GameScene::inactivate() {
    Scene::inactivate();
    gameView_->hide();
    if (app_->audio) app_->audio->stopMusic();
}

void GameScene::setPaused(bool paused) {
    if (!paused) return;
    const int state = session_->controllerState();
    if (state == 2) {
        setLevelMenu(true);   // session_->pause() = doFrame(0), the gizmo / tutorial off, releaseHeldItems, doFrame(0)
        gameView_->openPauseMenu(false);
    } else if (state == 4 && !completing_) {
        session_->stop();     // toggleSimulation
        menuOpen_ = true;     // setLevelMenuState
        gameView_->openPauseMenu(false);
    }
}

void GameScene::setLevelMenu(bool open) {
    if (open) {
        session_->pause();
        menuOpen_ = true;
    } else {
        menuOpen_ = false;
    }
}

void GameScene::playNextLevel() {
    gameView_->showPauseMenu(true);
    gameView_->showGameControls(true);
    selectLevel(app_->currentLevel + 1);
    app_->locationState.setLevelPlayed(app_->currentLevel);
    app_->saveLocation();
    gameView_->updateLevelInfo();
    gameView_->showLevelName(true);
    gameView_->showTip();
}

void GameScene::replayLevel() {
    gameView_->showPauseMenu(true);
    gameView_->showGameControls(true);
    selectLevel(app_->currentLevel);
}

void GameScene::restartLevel() {
    gameView_->showPauseMenu(true);
    gameView_->showGameControls(true);
    gameView_->setInteraction(true);
    menuOpen_ = false;
    session_->pause();
    session_->restart();
    onControllerState(session_->controllerState());
    gameView_->showPauseMenu(true);
    gameView_->showGameControls(true);
    gameView_->hideSimulationControls();
}

void GameScene::completeLevel() {
    // setCompletedState's campaign branch [verified; docs/05 §4]: WasLevelImproved before
    // MarkLevelAsDone, the star difference to AddEarnedStars, the progress saved when improved, the
    // location state always; then LevelCompletedView::ShowPanels' unlock branch.
    const aa::game::LocationInfo* info = app_->location();
    if (!info) return;
    aa::game::LocationState& state = app_->locationState;
    const int level = app_->currentLevel;
    const int newStars = session_->goalState().collectedStars;
    const bool improved = state.wasLevelImproved(newStars, level);
    const int oldStars = state.levelStarCount(level);
    state.markLevelAsDone(newStars, level, *info);
    if (newStars - oldStars > 0) app_->progress.addEarnedStars(newStars - oldStars, app_->locationIndex);
    if (improved) app_->saveProgress();
    app_->saveLocation();
    levelImproved_ = improved;
    completedStars_ = newStars;
    if (improved && state.completedLevelsCount(*info) == info->levelCount()) {
        app_->progress.checkForNewLocationUnlocks();
        app_->progress.unlockItems(app_->locationIndex, state.starCount(*info) == info->maxStarCount());
        app_->saveProgress();
    }
}

void GameScene::onControllerState(int state) {
    // GameScene::ShowOverlay by the controller's state changes [verified: doFrame's delegate calls].
    if (state == 4 && lastControllerState_ != 4) {
        // ShowOverlay(8) — emitted by doFrame's state-3 case (the set-up → simulation transition, which the
        // Session folds into state 4 in the same frame) [verified: 0xbb42c]: the pause menu retracts, the stop
        // button shows (the tutorial view stays).
        gameView_->hidePauseMenu(true);
        gameView_->showSimulationControls();
    } else if (state == 2 && lastControllerState_ != 2) {
        // The run's way back emits ShowOverlay(10 / 7) through state 5: the HUD returns. ShowOverlay(1) (the
        // tutorial view shown in the campaign, getMode() == 0, hidden in every other mode) belongs to the
        // goals-display state 1 of a campaign level's start [verified: doFrame's dispatch, docs/10 §11 item
        // 14 (n)]; updateTutorialView shows the view when the script runs, and this re-show is a no-op.
        // Only while the script runs: ImageHand keeps the pose of its last setHand, and a script stopped by a
        // touch mid-fade leaves it opaque — an unconditional show would reveal that stale hand on every later
        // level (no script there, so nothing ever hides it again).
        if (session_->gameMode() == aa::sim::GameMode::Campaign && session_->tutorial().running) tutorialView_->show();
        else tutorialView_->hide();
        gameView_->showPauseMenu(true);
        gameView_->showGameControls(true);
        gameView_->hideSimulationControls();
    }
    if (state == 6 && !completed_) {
        completed_ = true;
        completeLevel();
        completedView_->show(worldToScreen(session_->levelCompletePos()), completedStars_, levelImproved_);
        gameView_->hideLevelCompleteStartAnim();
    }
    lastControllerState_ = state;
}

void GameScene::handleEvents() {
    for (const aa::sim::SessionEvent& e : session_->drainEvents()) {
        switch (e.kind) {
        case aa::sim::SessionEvent::Kind::Sound:
            if (app_->soundSink) app_->soundSink->play(e.soundId, e.volume, e.position);
            break;
        case aa::sim::SessionEvent::Kind::GoalComplete:
            if (!completing_) {
                completing_ = true;
                // LevelCompletionStarted + ShowOverlay(2): the popup at the goal, the HUD retracts. The
                // original maps the goal with its 4:3 formula (W / 3.41, fieldH / 2.12459); the camera
                // mapping is the same point at 4:3 and, unlike the formula, the point the result panel's
                // Alex starts from (state 6) on a widescreen layout — no jump at the hand-over.
                gameView_->startLevelCompleted(worldToScreen(session_->levelCompletePos()));
                gameView_->hidePauseMenu(false);
                gameView_->hideGameControls(false);
            }
            break;
        default: break;
        }
    }
}

void GameScene::update(float dt) {
    Scene::update(dt);
    if (state_ != scene_state::kActive && state_ != scene_state::kActivating) return;
    // tutorial_chap0_level0 reads the ButtonPlay frame when the script starts.
    session_->setTutorialPlayButton(aa::sim::Vec2(gameView_->playButtonCenter().x, gameView_->playButtonCenter().y));
    if (!menuOpen_) session_->advance(dt);
    session_->drainActions();
    handleEvents();
    // The controller's state changes reach the view through doFrame's delegate calls [verified], and
    // doFrame does not run while the level menu is open: SetPaused's stop from a running simulation shows
    // its overlay (the HUD back, ShowOverlay 10 / 7) only when the menu closes — not over the open menu.
    const int cs = session_->controllerState();
    if (!menuOpen_ && cs != lastControllerState_) onControllerState(cs);
    updateTutorialView();
}

void GameScene::updateTutorialView() {
    // GameTutorialView::Update [verified]: Show / Hide when the run flag changes; the hand at the
    // original's 4:3 mapping of the world position (LetterBoxFrameWidth + (W − 2·LBFW) / 3.41 · x,
    // (H − floor) − (H − floor) / 2.12459 · y), with the state's alpha, angle and image.
    const aa::sim::TutorialState& t = session_->tutorial();
    if (t.running != tutorialRunning_) {
        if (t.running) tutorialView_->show();
        else tutorialView_->hide();
        tutorialRunning_ = t.running;
    }
    if (!t.running) return;
    const aa::sim::ScreenLayout& layout = session_->viewport();
    const float w = ctx_->screen.nativeWidth;
    const float h = ctx_->screen.nativeHeight;
    const float lb = ctx_->screen.letterBoxFrameWidth;
    const float floorPx = layout.floor * (h / layout.virtualHeight);   // GameParams::FloorHeightInPixels
    const Point screen{lb + ((w - (lb + lb)) / kWorldWidth) * t.hand.pos.x, (h - floorPx) - ((h - floorPx) / kWorldHeight) * t.hand.pos.y};
    tutorialView_->setHand(screen, t.hand.image, t.hand.alpha, t.hand.angle);
}

void GameScene::draw(Renderer& renderer) {
    drawLetterBoxBackdrop(renderer, *ctx_, session_->viewport());
    drawWorld();
    Scene::draw(renderer);
}

bool GameScene::keyDown(int key) {
    if (completedView_ && completedView_->isVisible() && completedView_->keyDown(key)) return true;
    return gameView_ && gameView_->keyDown(key);
}

}  // namespace aa::ui
