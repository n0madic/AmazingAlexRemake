#include "aa/ui/menu_scenes.h"
#include "aa/ui/remake_views.h"

#include "aa/ui/dialogs.h"
#include "aa/ui/extra_scenes.h"
#include "aa/ui/game_scene.h"
#include "aa/ui/sandbox_scene.h"

#include <algorithm>
#include <cmath>

namespace aa::ui {

namespace {

constexpr float kMenuSlide = 0.3f;         // 0x3e99999a
constexpr float kLoadingShow = 0.7f;       // 0x3f333333
constexpr float kSplashLogoTime = SplashView::kSplashPageTime;
constexpr float kSplashDone = 2.0f * SplashView::kSplashPageTime;
constexpr int kMusicTheme = 1;
// The original's seven books minus Level of the Week and World of Contraptions (Rovio's servers; the remake
// drops the books and their containers, docs/12 §2).
constexpr const char* kBookNames[ChapterSelectionView::kBookCount] = {"CLASSROOM", "BACKYARD", "BEDROOM", "TREEHOUSE", "MYCONTRAPTIONS"};

template <class T, class... Args>
T* make(std::vector<std::unique_ptr<View>>& owned, Args&&... args) {
    auto v = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = v.get();
    owned.push_back(std::move(v));
    return raw;
}

aa::data::JsonNode sub(const aa::data::JsonNode& dict, const char* path) {
    // DataDictionary::GetValueDictionaryAtPath: "A/B" descends.
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

}  // namespace

const char* chapterBookName(int index) { return index >= 0 && index < ChapterSelectionView::kBookCount ? kBookNames[index] : ""; }
const char* chapterCompletionImage(int index) { return chapterBookName(index); }

const aa::data::SceneTree& GameSceneBase::tree() {
    if (!tree_) tree_ = std::make_unique<aa::data::SceneTree>(app_->assets->json(std::string("ui/scenes/") + name() + ".json"));
    return *tree_;
}

// --- Splash ----------------------------------------------------------------------------------------

SplashView::SplashView(UiContext& ctx, const aa::data::JsonNode& dict) : View(ctx) {
    View::init(dict);
    page0_ = make<Button>(owned_, ctx);
    page0_->setViewName("Page0");
    page0_->init(sub(dict, "Page0"));
    legal_ = make<ImageView>(owned_, ctx);
    legal_->setViewName("ImageLegalText");
    legal_->init(sub(dict, "Page0/ImageLegalText"));
    legal_->setInteraction(false);
    page0_->addSubview(legal_);
    page0_->setDelegate(this);
    page0_->setSilent(true);
    page0_->setAnimateOnlyBackground(true);
    page1_ = make<ImageView>(owned_, ctx);
    page1_->setViewName("Page1");
    page1_->init(sub(dict, "Page1"));
    loading_ = make<OutlineLabelView>(owned_, ctx);
    loading_->setViewName("LoadingText");
    loading_->init(sub(dict, "Page1/LabelLoading"));
    addSubview(page0_);
    addSubview(page1_);
    addSubview(loading_);
    updateViewAnchors(true, true);
}

void SplashView::show() {
    page0_->setVisible(true);
    page1_->setVisible(false);
    loading_->setVisible(false);
    timer_ = 0.0f;
    done_ = false;
}

void SplashView::update(float dt) {
    View::update(dt);
    timer_ += dt;
    if (timer_ >= kSplashLogoTime) {
        page0_->setVisible(false);
        page1_->setVisible(true);
        loading_->setVisible(true);
    }
    if (timer_ >= kSplashDone) done_ = true;
}

void SplashView::buttonPressed(int id) {
    if (id == page0_->id()) timer_ = kSplashLogoTime;
}

void SplashScene::init() {
    Scene::init();
    view_ = make<SplashView>(tree().view("SplashView"));
    view_->setViewName("SplashView");
    view_->setVisible(true);
    view_->setFrame(root_->frame());   // <Scene>::Init passes the root frame to <View>::Init(UIRect) [verified]
    root_->addSubview(view_);
}

void SplashScene::activate() {
    Scene::activate();
    view_->show();
}

void SplashScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

// --- Main menu -------------------------------------------------------------------------------------

MainMenuView::MainMenuView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict) : View(ctx), app_(&app) {
    View::init(dict);
    background_ = make<ImageView>(owned_, ctx);
    background_->setViewName("Background");
    background_->init(dict);
    logo_ = make<ImageView>(owned_, ctx);
    logo_->setViewName("ImageLogo");
    logo_->init(sub(dict, "ImageLogo"));
    panelTop_ = make<View>(owned_, ctx);
    panelTop_->setViewName("PanelTop");
    panelTop_->init(sub(dict, "PanelTop"));
    const aa::data::JsonNode panel = sub(dict, "PanelTop");
    autoShare_ = make<ToggleButton>(owned_, ctx);
    autoShare_->setViewName("ButtonAutoShare");
    autoShare_->init(sub(panel, "SettingsSlider/ButtonAutoShare"));
    autoShare_->setDelegate(this);
    audio_ = make<ToggleButton>(owned_, ctx);
    audio_->setViewName("ButtonAudio");
    audio_->init(sub(panel, "SettingsSlider/ButtonAudio"));
    audio_->setDelegate(this);
    music_ = make<ToggleButton>(owned_, ctx);
    music_->setViewName("ButtonMusic");
    music_->init(remake::mainMenuMusicButton().root());
    music_->setDelegate(this);
    credits_ = make<Button>(owned_, ctx);
    credits_->setViewName("ButtonCredits");
    credits_->init(sub(panel, "SettingsSlider/ButtonCredits"));
    credits_->setDelegate(this);
    settings_ = make<SlidingButton>(owned_, ctx);
    settings_->setViewName("SettingsSlider");
    settings_->init(sub(panel, "SettingsSlider"));
    settings_->setDelegate(this);
    // DOWN: the last-added sits nearest the gear — gear, speaker, note, info.
    settings_->addMenuButton(credits_);
    settings_->addMenuButton(music_);
    settings_->addMenuButton(audio_);
    // The original's LinkSlider (Twitter / Facebook / video) at the top right is dropped: Rovio's links.
    panelTop_->addSubview(settings_);
    play_ = make<Button>(owned_, ctx);
    play_->setViewName("ButtonPlay");
    play_->init(sub(dict, "ButtonPlay"));
    play_->setDelegate(this);
    // ExitDialog (SK_EXIT / ITEM_ARE_YOU_SURE): the back key's "leave the game?" prompt. The original made
    // it an InfoDialog — one check button, the back key confirming, no way out; the remake uses the
    // two-button MessageDialog of the same layout so a cancel (the cross, or the back key) keeps playing.
    exit_ = make<MessageDialog>(owned_, ctx, false);
    exit_->setViewName("ExitDialog");
    exit_->init(sub(dict, "ExitDialog"), app.dialogs());
    exit_->setVisible(false);
    struct ExitConfirm : MessageDialogDelegate {
        AppState* app;
        MessageDialog* dialog;
        void messageConfirmed(int) override {
            app->quitRequested = true;
            dialog->hide();
        }
        void messageCanceled(int) override { dialog->hide(); }
    };
    auto confirm = std::make_unique<ExitConfirm>();
    confirm->app = &app;
    confirm->dialog = exit_;
    exit_->setDelegate(confirm.get());
    exitDelegate_ = std::move(confirm);
    addSubview(background_);
    addSubview(logo_);
    addSubview(panelTop_);
    addSubview(play_);
    addSubview(exit_);
    updateViewAnchors(true, true);
}

void MainMenuView::show() {
    setVisible(true);
    setInteraction(true);
    AnimationParameters p = AnimationParameters::fromView(*panelTop_);
    p.frame.y = 0.0f;
    p.duration = kMenuSlide;
    p.repeat = 1;
    ctx_->animator->animate(panelTop_, p, nullptr);
    settings_->hideMenu(false);
    refresh_ = true;
}

void MainMenuView::hide() {
    setInteraction(false);
    AnimationParameters p = AnimationParameters::fromView(*panelTop_);
    p.frame.y = p.frame.y - p.frame.h;
    p.duration = kMenuSlide;
    p.repeat = 1;
    hideAnim_ = ctx_->animator->animate(panelTop_, p, this);
}

void MainMenuView::animationFinished(int id) {
    // MainMenuView::AnimationFinished [verified: 0x143ef0]: the hide animation's end pushes the credits.
    if (id != hideAnim_) return;
    hideAnim_ = 0;
    if (SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr) manager->pushScene(scene_names::kCredits);
}

void MainMenuView::relayout() {
    View::relayout();
    // exit_ / settings_ are reached only through this explicit call: the generic View::relayout() recursion
    // walks the whole subtree for the granular (relative frame / padding / auto-size / reWrap) fixups, but
    // it does not virtual-dispatch each subview's own relayout() override — only whoever holds the pointer
    // can.
    if (exit_) exit_->relayout();
    if (settings_) settings_->relayout();
}

bool MainMenuView::keyDown(int key) {
    if (View::keyDown(key)) return true;
    if (key == SceneManager::kKeyBack || key == SceneManager::kKeyAndroidBack) {
        exit_->show();
        return true;
    }
    return false;
}

void MainMenuView::update(float dt) {
    View::update(dt);
    if (!refresh_ || !isVisible()) return;
    autoShare_->setChecked(true);
    audio_->setChecked(!app_->settings.soundEffectsOn);
    music_->setChecked(!app_->settings.musicOn);
    refresh_ = false;
}

void MainMenuView::buttonPressed(int id) {
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (!manager) return;
    if (id == play_->id()) {
        // MainMenuView::ButtonPressed [verified]: a fresh install (the Classroom still locked) opens the
        // first level directly with the chapter / level scenes beneath — under the Classroom's begin comic
        // when that has not been seen (showChapterComic(0): the loading scene is inserted beneath the
        // comic instead of pushed); otherwise the chapter books.
        if (!app_->progress.locationUnlocked(0)) {
            app_->loadLocation(0);
            auto* loading = dynamic_cast<LevelLoadingScene*>(manager->scene(scene_names::kLevelLoading));
            if (showChapterComic(*manager, *app_, 0)) manager->insertScene(1, scene_names::kLevelLoading);
            else manager->pushScene(scene_names::kLevelLoading);
            if (loading) loading->setLoadingLocation(LevelLoadingScene::kLocationCampaign, 0);
            manager->insertScene(1, scene_names::kChapterSelection);
            manager->insertScene(2, scene_names::kLevelSelection);
            return;
        }
        manager->pushScene(scene_names::kChapterSelection);
        return;
    }
    if (id == credits_->id()) {
        hide();
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
    // ButtonAutoShare: inert.
}

void MainMenuScene::init() {
    Scene::init();
    view_ = make<MainMenuView>(*app_, tree().view("MainMenuView"));
    view_->setViewName("MainMenuView");
    view_->setVisible(true);
    view_->setFrame(root_->frame());   // <Scene>::Init passes the root frame to <View>::Init(UIRect) [verified]
    root_->addSubview(view_);
}

void MainMenuScene::activate() {
    Scene::activate();
    if (app_->audio) app_->audio->playMusic(kMusicTheme);
    view_->show();
}

// Not in the original relayout audit's per-scene list, but MainMenuScene::Init builds view_ exactly like
// every other GameSceneBase scene (view_->setFrame(root_->frame())) — without this override, view_'s own
// frame stays at the old screen size after a resize (a hitTest miss on the new area when the window grows).
void MainMenuScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

// --- Chapter selection -----------------------------------------------------------------------------

ChapterSelectionView::ChapterSelectionView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict) : View(ctx), app_(&app) {
    View::init(dict);
    background_ = make<ImageView>(owned_, ctx);
    background_->setViewName("Background");
    background_->init(dict);
    // CustomChapterInfo0 / 1 (LotW / WoC) belong to the dropped books; 2 is My Contraptions'.
    customInfo_ = dict.getString("CustomChapterInfo2", "");
    back_ = make<Button>(owned_, ctx);
    back_->setViewName("ButtonBack");
    back_->init(sub(dict, "ButtonBack"));
    back_->setDelegate(this);
    const aa::data::JsonNode panelDict = sub(dict, "PanelRight");
    panel_ = make<ScrollView>(owned_, ctx);
    panel_->setViewName("PanelRight");
    panel_->init(panelDict);
    panel_->setDelegate(this);
    bookWidthPct_ = panelDict.getFloat("BookWidth");
    bookHeightPct_ = panelDict.getFloat("BookHeight");
    bookXPct_ = panelDict.getFloat("BookX");
    bookYPct_ = panelDict.getFloat("BookY");
    const std::string imageName = panelDict.getString("ChapterImageName", "BOOK_{0}");
    const std::string lockImage = panelDict.getString("ChapterImageLock", "CHAPTER_LOCK");
    for (int i = 0; i < kBookCount; ++i) {
        std::string image = imageName;
        const std::size_t at = image.find("{0}");
        if (at != std::string::npos) image.replace(at, 3, kBookNames[i]);
        Button* b = make<Button>(owned_, ctx);
        b->setViewName("Button_" + std::to_string(i));
        b->init();
        b->setImageForState(image, button_state::kNormal, true);
        b->setImageForState(image, button_state::kDisabled, true);
        // ResizeFrameToImage(normal, true): the frame takes the (localised) composite's size.
        const Size s = ctx.resources->imageSize(ctx.localization ? ctx.localization->imageName(image) : image);
        b->setFrame(Rect{0.0f, 0.0f, s.w, s.h});
        b->setDelegate(this);
        b->setChangeAlpha(false);
        b->setState(button_state::kDisabled);
        // The lock overlay of the disabled state: SetOverlayForState(lock, 0, (4 %, 0)) [verified].
        b->setOverlayForState(lockImage, button_state::kDisabled, Point{4.0f, 0.0f});
        books_[static_cast<std::size_t>(i)] = b;
        panel_->addSubview(b);
    }
    panel_->setScrollingByTap(true);
    layoutBooks();
    pages_ = make<PageControl>(owned_, ctx);
    pages_->setViewName("ChapterPages");
    pages_->init(sub(dict, "ChapterPages"));
    pages_->setPageCount(kBookCount);
    pages_->setInteraction(false);
    totalStars_ = make<ImageView>(owned_, ctx);
    totalStars_->setViewName("ImageTotalStars");
    totalStars_->init(sub(dict, "ImageTotalStars"));
    totalStarsLabel_ = make<OutlineLabelView>(owned_, ctx);
    totalStarsLabel_->setViewName("LabelTotalStars");
    totalStarsLabel_->init(sub(dict, "ImageTotalStars/LabelTotalStars"));
    totalStars_->addSubview(totalStarsLabel_);
    totalStars_->setInteraction(false);
    chapterStarsLabel_ = make<OutlineLabelView>(owned_, ctx);
    chapterStarsLabel_->setViewName("LabelChapterStars");
    chapterStarsLabel_->init(sub(dict, "LabelChapterStars"));
    chapterStars_ = make<ImageView>(owned_, ctx);
    chapterStars_->setViewName("ImageChapterStars");
    chapterStars_->init(sub(dict, "ImageChapterStars"));
    chapterStars_->setInteraction(false);
    infoText_ = make<OutlineLabelView>(owned_, ctx);
    infoText_->setViewName("LabelInfoText");
    infoText_->init(sub(dict, "LabelInfoText"));
    addSubview(background_);
    addSubview(pages_);
    addSubview(panel_);
    addSubview(chapterStarsLabel_);
    addSubview(chapterStars_);
    addSubview(infoText_);
    addSubview(totalStars_);
    addSubview(back_);
    updateViewAnchors(true, true);
    // The anchored positions are the hidden ones: the panel sits off screen right, the star counter
    // below the screen [verified: ChapterSelectionView::Init tail].
    starsHidden_ = totalStars_->position();
    const float dy = starsHidden_.y - ctx.screen.nativeHeight;
    starsShown_ = Point{starsHidden_.x, starsHidden_.y - (dy + dy + totalStars_->size().h)};
    panelHidden_ = panel_->position();
    panelShown_ = Point{panelHidden_.x - ctx.screen.nativeWidth, panelHidden_.y};
}

// layoutBooks(): the per-book Button position / scale and the panel's page geometry, from the
// BookWidth/Height/X/Y percentages. The buttons carry no Relative/Anchor data of their own (they are
// built with Button::init(null) and positioned by this formula alone), so this is the only thing that
// moves them — called once by the constructor and again by relayout().
void ChapterSelectionView::layoutBooks() {
    const float w = ctx_->screen.nativeWidth;
    const float h = ctx_->screen.nativeHeight;
    const float bookW = w * 0.01f * bookWidthPct_;
    const float bookH = h * 0.01f * bookHeightPct_;
    const float bookX = w * 0.01f * bookXPct_;
    const float bookY = h * 0.01f * bookYPct_;
    for (int i = 0; i < kBookCount; ++i) {
        Button* b = books_[static_cast<std::size_t>(i)];
        const Size s = b->size();
        const float fi = static_cast<float>(i);
        if (ctx_->screen.widescreenScaling) {
            // AssetScalingForWidescreen [verified]: the book at 0.85, lifted by a third of the shrink.
            b->setScale(0.85f);
            b->setPosition(Point{bookX + (fi + 0.5f) * bookW + (bookW - s.w) * 0.5f, (bookY - (bookH - bookH * 0.85f) / 3.0f) + (bookH - s.h) * 0.5f});
        } else {
            b->setScale(1.0f);
            b->setPosition(Point{bookX + (fi + 0.5f) * bookW + (bookW - s.w) * 0.5f, bookY + (bookH - s.h) * 0.5f});
        }
    }
    panel_->setContentSize(Size{bookW * (static_cast<float>(kBookCount) + 0.5f), panel_->size().h});
    panel_->setPageSize(Size{bookW, bookH});
    panel_->setPageControlAreaLength(bookW * 0.5f);
}

void ChapterSelectionView::show() {
    panel_->setVisible(true);
    setInteraction(false);
    shown_ = true;
    refresh_ = true;
    if (updatePosition_) updateChapterPosition();
    if (hideAnimation_ != 0) ctx_->animator->finishAnimation(hideAnimation_);
    if (showAnimation_ != 0) ctx_->animator->finishAnimation(showAnimation_);
    panel_->setActivePage(panel_->activePage(), false);
    AnimationParameters p = AnimationParameters::fromView(*panel_);
    p.frame.x = panelShown_.x;
    p.frame.y = panelShown_.y;
    p.duration = kMenuSlide;
    p.repeat = 1;
    showAnimation_ = ctx_->animator->animate(panel_, p, this);
    if (starsHideAnimation_ != 0) ctx_->animator->finishAnimation(starsHideAnimation_);
    if (starsShowAnimation_ != 0) ctx_->animator->finishAnimation(starsShowAnimation_);
    AnimationParameters s = AnimationParameters::fromView(*totalStars_);
    s.frame.x = starsShown_.x;
    s.frame.y = starsShown_.y;
    s.duration = kMenuSlide;
    s.repeat = 1;
    starsShowAnimation_ = ctx_->animator->animate(totalStars_, s, nullptr);
}

void ChapterSelectionView::hide() {
    panel_->setVisible(false);
    setInteraction(false);
    shown_ = false;
    if (hideAnimation_ != 0) ctx_->animator->finishAnimation(hideAnimation_);
    if (showAnimation_ != 0) ctx_->animator->finishAnimation(showAnimation_);
    panel_->setActivePage(panel_->activePage(), false);
    AnimationParameters p = AnimationParameters::fromView(*panel_);
    p.frame.x = panelHidden_.x;
    p.frame.y = panelHidden_.y;
    p.duration = kMenuSlide;
    p.repeat = 1;
    hideAnimation_ = ctx_->animator->animate(panel_, p, this);
    if (starsHideAnimation_ != 0) ctx_->animator->finishAnimation(starsHideAnimation_);
    if (starsShowAnimation_ != 0) ctx_->animator->finishAnimation(starsShowAnimation_);
    AnimationParameters s = AnimationParameters::fromView(*totalStars_);
    s.frame.x = starsHidden_.x;
    s.frame.y = starsHidden_.y;
    s.duration = kMenuSlide;
    s.repeat = 1;
    starsHideAnimation_ = ctx_->animator->animate(totalStars_, s, nullptr);
    panel_->setVisible(true);
}

void ChapterSelectionView::updateChapterPosition() {
    int page = 0;
    for (int i = 0; i < aa::game::kLocationCount; ++i) {
        if (app_->progress.locations[static_cast<std::size_t>(i)].stars < app_->locations[static_cast<std::size_t>(i)].maxStarCount()) {
            page = i;
            break;
        }
    }
    panel_->setActivePage(page, false);
}

void ChapterSelectionView::refresh() {
    refresh_ = false;
    app_->progress.checkForNewLocationUnlocks();
    for (int i = 0; i < aa::game::kLocationCount; ++i) {
        if (app_->progress.locationUnlocked(i)) books_[static_cast<std::size_t>(i)]->setState(button_state::kNormal);
    }
    // The last book (My Contraptions) opens with the progress flag [verified: Refresh tests GameProgress+0].
    if (app_->progress.myContraptions) books_[kBookCount - 1]->setState(button_state::kNormal);
    const int page = panel_->activePage();
    if (page < aa::game::kLocationCount) {
        chapterStars_->setVisible(true);
        chapterStarsLabel_->setVisible(true);
        infoText_->setVisible(false);
        const int stars = app_->progress.locations[static_cast<std::size_t>(page)].stars;
        const int max = app_->locations[static_cast<std::size_t>(page)].maxStarCount();
        chapterStarsLabel_->setNonLocalizedText(std::to_string(stars) + "/" + std::to_string(max));
        chapterStarsLabel_->updateViewAnchors(false, false);
        chapterStars_->updateViewAnchors(false, false);
    } else {
        chapterStars_->setVisible(false);
        chapterStarsLabel_->setVisible(false);
        infoText_->setVisible(true);
        infoText_->setText(customInfo_);
        infoText_->updateViewAnchors(false, false);
    }
    totalStarsLabel_->setNonLocalizedText(std::to_string(app_->progress.collectedStarCount()));
}

void ChapterSelectionView::update(float dt) {
    View::update(dt);
    if (refresh_) refresh();
}

void ChapterSelectionView::scrollViewMoved(int id) {
    if (id != panel_->id()) return;
    const int page = panel_->activePage();
    if (pages_->activePage() != page) {
        pages_->setActivePage(page);
        refresh_ = true;
    }
}

void ChapterSelectionView::scrollViewFinishedDecelerating(int) { setInteraction(true); }

void ChapterSelectionView::animationFinished(int id) {
    if (id == showAnimation_) {
        showAnimation_ = 0;
        setInteraction(true);
    } else if (id == hideAnimation_) {
        hideAnimation_ = 0;
    }
}

void ChapterSelectionView::relayout() {
    // The panel / star-counter positions the animator drives (panelShown_/Hidden_, starsShown_/Hidden_)
    // are constructor-baked geometry, not anchor-driven — View::relayout() snaps any in-flight slide to
    // its endpoint (the documented resize trade-off) so it does not keep interpolating towards a stale,
    // pre-resize target.
    View::relayout();
    // activePage() divides the current scroll offset by the (about to change) page width, so it must be
    // read before layoutBooks() resizes the pages — otherwise a wide-enough resize picks the wrong page.
    const int page = panel_->activePage();
    layoutBooks();
    panel_->setActivePage(page, false);
    // The anchor-rest position updateViewAnchors just derived for panel_/totalStars_ *is* the hidden
    // endpoint (their Anchor formula puts them off screen — see the constructor's tail comment); re-derive
    // panelShown_/starsShown_ from it exactly as the constructor did, then snap to whichever is current.
    const float w = ctx_->screen.nativeWidth;
    const float h = ctx_->screen.nativeHeight;
    starsHidden_ = totalStars_->position();
    const float dy = starsHidden_.y - h;
    starsShown_ = Point{starsHidden_.x, starsHidden_.y - (dy + dy + totalStars_->size().h)};
    panelHidden_ = panel_->position();
    panelShown_ = Point{panelHidden_.x - w, panelHidden_.y};
    if (shown_) {
        panel_->setPosition(panelShown_);
        totalStars_->setPosition(starsShown_);
    } else {
        panel_->setPosition(panelHidden_);
        totalStars_->setPosition(starsHidden_);
    }
}

void ChapterSelectionView::scrollToPage(int page) {
    panel_->setActivePage(page, false);
    pages_->setActivePage(page);
    updatePosition_ = false;
}

void ChapterSelectionView::buttonPressed(int id) {
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (!manager) return;
    if (id == back_->id()) {
        if (auto* levels = dynamic_cast<LevelSelectionScene*>(manager->scene(scene_names::kLevelSelection))) levels->purgeThumbs();
        manager->popScene();
        return;
    }
    for (int i = 0; i < aa::game::kLocationCount; ++i) {
        if (id != books_[static_cast<std::size_t>(i)]->id()) continue;
        if (!app_->progress.locationUnlocked(i)) return;
        app_->loadLocation(i);
        manager->pushScene(scene_names::kLevelSelection);
        // The Classroom's begin comic on top of its level list, once [verified: `if (location == 0)`].
        if (i == 0) showChapterComic(*manager, *app_, 0);
        if (auto* levels = dynamic_cast<LevelSelectionScene*>(manager->scene(scene_names::kLevelSelection))) levels->setReturningFromGame(false);
        updatePosition_ = false;
        return;
    }
    // The last book: My Contraptions once the Classroom's chapter panel was shown (GameProgress+0).
    if (id == books_[kBookCount - 1]->id()) {
        if (!app_->progress.myContraptions) return;
        if (auto* levels = dynamic_cast<LevelSelectionScene*>(manager->scene(scene_names::kLevelSelection))) levels->purgeThumbs();
        app_->loadLocation(AppState::kSandboxLocation);
        manager->pushScene(scene_names::kMyContraptions);
        updatePosition_ = false;
    }
}

void ChapterSelectionScene::init() {
    Scene::init();
    view_ = make<ChapterSelectionView>(*app_, tree().view("ChapterSelectionView"));
    view_->setViewName("ChapterSelectionView");
    view_->setVisible(true);
    view_->setFrame(root_->frame());   // <Scene>::Init passes the root frame to <View>::Init(UIRect) [verified]
    root_->addSubview(view_);
}

void ChapterSelectionScene::activate() {
    Scene::activate();
    view_->show();
    if (app_->audio) app_->audio->playMusic(kMusicTheme);
}

void ChapterSelectionScene::inactivate() {
    Scene::inactivate();
    view_->hide();
}

void ChapterSelectionScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

// --- Level selector button -------------------------------------------------------------------------

LevelSelectorButton::LevelSelectorButton(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict, const Rect& rect) : Button(ctx), app_(&app) {
    // LevelSelectorButton::Init(rect) [verified]: Button::Init(rect); the layout view is Init(UIRect)'d
    // with (0, 0, w, h) — the "LayoutView" dictionary is not read.
    Button::init(aa::data::JsonNode(nullptr, ""));
    setFrame(rect);
    layout_ = make<View>(owned_, ctx);
    layout_->setViewName("LayoutView");
    layout_->init();
    layout_->setFrame(Rect{0.0f, 0.0f, rect.w, rect.h});
    panelTop_ = make<View>(owned_, ctx);
    panelTop_->setViewName("PanelTop");
    panelTop_->init(sub(dict, "PanelTop"));
    const aa::data::JsonNode thumb = sub(dict, "Thumb");
    thumbImage_ = thumb.getString("ImageThumb", "DEFAULT_THUMBNAIL");
    thumbEmpty_ = make<ImageView>(owned_, ctx);
    thumbEmpty_->setViewName("ThumbEmpty");
    thumbEmpty_->init(thumb);
    thumbEmpty_->setImage(thumbImage_);
    thumbEmpty_->resizeFrameToImage(true, true);
    thumbEmpty_->setBackgroundColor(Color{28, 99, 158, 255});
    thumbEmpty_->setImage("");
    thumbEmpty_->setPivot(thumbEmpty_->center());
    thumbEmpty_->setVisible(false);
    thumbNormal_ = make<ImageView>(owned_, ctx);
    thumbNormal_->setViewName("ThumbNormal");
    thumbNormal_->init(thumb);
    thumbNormal_->setImage(thumbImage_);
    thumbNormal_->resizeFrameToImage(true, true);
    thumbNormal_->setVisible(false);
    const aa::data::JsonNode frame = sub(dict, "Frame");
    frameImage_ = frame.getString("ImageLevel", "LEVEL_FRAME{0}");
    // FrameEmpty: the Frame dictionary's own Image (LEVEL_EMPTY_SLOT), sized to it, pivot at its centre.
    frameEmpty_ = make<ImageView>(owned_, ctx);
    frameEmpty_->setViewName("FrameEmpty");
    frameEmpty_->init(frame);
    frameEmpty_->resizeFrameToImage(true, true);
    frameEmpty_->setPivot(frameEmpty_->center());
    frameEmpty_->setVisible(false);
    frameNormal_ = make<ImageView>(owned_, ctx);
    frameNormal_->setViewName("FrameNormal");
    frameNormal_->init(frame);
    frameNormal_->setVisible(false);
    const aa::data::JsonNode stars = sub(dict, "Stars");
    starImage_ = stars.getString("Image", "STAR_SMALL");
    starEmptyImage_ = stars.getString("ImageEmpty", "STAR_SMALL_EMPTY");
    starOne_ = make<ImageView>(owned_, ctx);
    starOne_->setViewName("StarOne");
    starOne_->init(sub(dict, "Stars/One"));
    starOne_->setVisible(false);
    starTwo_ = make<ImageView>(owned_, ctx);
    starTwo_->setViewName("StarTwo");
    starTwo_->init(sub(dict, "Stars/Two"));
    starTwo_->setVisible(false);
    starThree_ = make<ImageView>(owned_, ctx);
    starThree_->setViewName("StarThree");
    starThree_->init(sub(dict, "Stars/Three"));
    starThree_->setVisible(false);
    labelName_ = make<OutlineLabelView>(owned_, ctx);
    labelName_->setViewName("LabelName");
    labelName_->init(sub(dict, "LabelName"));
    labelName_->setSize(Size{frame_.w * 0.92f, -1.0f});
    labelName_->setVisible(false);
    labelNumber_ = make<LabelView>(owned_, ctx);
    labelNumber_->setViewName("LabelNumber");
    labelNumber_->init(sub(dict, "LabelNumber"));
    labelNumber_->setVisible(false);
    trash_ = make<ImageView>(owned_, ctx);
    trash_->setViewName("Trash");
    trash_->init(sub(dict, "Trash"));
    trash_->setVisible(false);
    panelTop_->addSubview(thumbEmpty_);
    panelTop_->addSubview(thumbNormal_);
    panelTop_->addSubview(frameEmpty_);
    panelTop_->addSubview(frameNormal_);
    panelTop_->addSubview(labelNumber_);
    panelTop_->addSubview(starOne_);
    panelTop_->addSubview(starTwo_);
    panelTop_->addSubview(starThree_);
    panelTop_->addSubview(trash_);
    layout_->addSubview(panelTop_);
    layout_->addSubview(labelName_);
    addSubview(layout_);
    for (View* v : {layout_, panelTop_, static_cast<View*>(thumbEmpty_), static_cast<View*>(thumbNormal_), static_cast<View*>(frameEmpty_),
                    static_cast<View*>(frameNormal_), static_cast<View*>(starOne_), static_cast<View*>(starTwo_), static_cast<View*>(starThree_),
                    static_cast<View*>(labelName_), static_cast<View*>(labelNumber_)}) {
        v->setInteraction(false);
    }
    setAnimateOnlyBackground(true);
    // AssetScalingForWidescreen [verified: LevelSelectorButton::Init]: the panel, thumbs, frames and the
    // number at 0.89 on a widescreen layout (the stars, the title and the trash can keep their size).
    applyRestScale();
}

// The views LevelSelectorButton::ZoomIn / ZoomOut animate [verified]: PanelTop, the frames, the thumbs and
// the level number (the original's list also has the online lists' ThumbSmall / FrameSmall).
std::vector<View*> LevelSelectorButton::zoomViews() {
    return {panelTop_, frameEmpty_, frameNormal_, thumbEmpty_, thumbNormal_, labelNumber_};
}

float LevelSelectorButton::restScale() const { return ctx_->screen.widescreenScaling ? kWidescreenScale : 1.0f; }

void LevelSelectorButton::setSlotRect(const Rect& rect) {
    setFrame(rect);
    // labelName_'s width (no Relative.W/AutoResizeW of its own — see the constructor) is 92 % of this
    // view's own width, set once there; redo it against the new frame_.w before layout_->relayout() below
    // re-wraps it (LabelView::AutoResizeH derives the height from the width current at that point).
    labelName_->setSize(Size{frame_.w * 0.92f, -1.0f});
    layout_->setFrame(Rect{0.0f, 0.0f, rect.w, rect.h});
    // panelTop_ / thumbEmpty_ / frameEmpty_ / … (Anchor-driven, off layout_'s frame or panelTop_'s in turn)
    // were all resolved once against the old layout_ frame — a live resize's setSlotRect() moves it well
    // after construction's own final relayout pass, so without this everything inside stays positioned for
    // the old slot rect until (and for a type without one, such as an empty slot, regardless of) the next
    // Setup() call. layout_ is a plain View, so its own relayout() is exactly the generic four-pass
    // sequence, correctly ordered.
    layout_->relayout();
    // That pass's recomputeAutoSizeRecursive() reaches thumbEmpty_ too, through the plain (and here wrong:
    // no image, so a zero size) ImageView::recomputeAutoSize() — redo the constructor's sized-then-blanked
    // sequence, centre pivot included (SetImage("") zeroes it; the "?" tile is a zoom view), now that it
    // has run last.
    thumbEmpty_->setImage(thumbImage_);
    thumbEmpty_->resizeFrameToImage(true, true);
    thumbEmpty_->setImage("");
    thumbEmpty_->setPivot(thumbEmpty_->center());
    // The rest scale before relayoutContents(): Setup's realFrame() reads go through it.
    applyRestScale();
    relayoutContents();
}

void LevelSelectorButton::relayoutContents() {
    // Setup's geometry for the current type — the PanelTop from the frame sprite's real size (see setup's
    // `extra`), the thumb / frame centred in it, the centre pivots of the zoom views — without Setup's
    // content, visibility, interaction or pulse side effects, so a resize needs neither a Refresh (a disk
    // read per user level on My Contraptions) nor Refresh's page snap. The sprite sizes themselves
    // (resizeFrameToImage) are already current: View::relayout()'s AutoResize pass tracks them.
    if (type_ == kTypeEmpty) return;
    const float extra = ctx_->screen.nativeHeight * 0.0272f * restScale();
    if (type_ == kTypeAddLevel) {
        const Rect fs = frameEmpty_->realFrame();
        panelTop_->setFrame(Rect{(frame_.w - fs.w) * 0.5f, 0.0f, fs.w, fs.h + extra});
        frameEmpty_->setPivot(frameEmpty_->center());
        frameEmpty_->setCenter(panelTop_->center());
        updateViewAnchors(true, false);
        return;
    }
    const Rect fs = frameNormal_->realFrame();
    panelTop_->setFrame(Rect{(frame_.w - fs.w) * 0.5f, 0.0f, fs.w, fs.h + extra});
    if (type_ == kTypeUserLevel) {
        thumbNormal_->setPivot(thumbNormal_->center());
        thumbNormal_->setCenter(panelTop_->center());
        updateViewAnchors(true, false);
        return;
    }
    labelNumber_->setPivot(labelNumber_->center());
    thumbEmpty_->setCenter(panelTop_->center());
    thumbNormal_->setCenter(panelTop_->center());
    updateViewAnchors(true, false);
}

void LevelSelectorButton::applyRestScale() {
    const float s = restScale();
    for (View* v : zoomViews()) v->setScale(s);
}

// LevelSelectorButton::ZoomOut [verified: 0x110d78]: the views grow to 1.15 (1.0235 = 1.15 · 0.89 on the
// widescreen tweak) in 0.1 s, relative to the panel's current scale and the button's own.
void LevelSelectorButton::zoomOut() {
    if (zoomOutAnimation_ != 0) return;
    AnimationParameters d;
    d.frame = Rect{};
    d.angle = 0.0f;
    d.alpha = 0.0f;
    d.pivot = Point{};
    d.scale = ((ctx_->screen.widescreenScaling ? 1.0235f : 1.15f) - panelTop_->scale()) - (1.0f - scale_);
    d.curve = 4;
    d.duration = 0.1f;
    d.repeat = 1;
    zoomOutAnimation_ = ctx_->animator->animate(zoomViews(), d, this);
}

// LevelSelectorButton::ZoomIn [verified: 0x110f50]: back to the rest scale in 0.05 s.
void LevelSelectorButton::zoomIn() {
    if (zoomInAnimation_ != 0) return;
    AnimationParameters d;
    d.frame = Rect{};
    d.angle = 0.0f;
    d.alpha = 0.0f;
    d.pivot = Point{};
    d.scale = (restScale() - panelTop_->scale()) - (1.0f - scale_);
    d.curve = 4;
    d.duration = 0.05f;
    d.repeat = 1;
    zoomInAnimation_ = ctx_->animator->animate(zoomViews(), d, this);
}

void LevelSelectorButton::setThumbImage(const std::string& name) {
    if (type_ == kTypeEmpty || type_ == kTypeAddLevel) return;   // SetThumbImage [verified]: types 0 / 6 keep no thumb
    if (name.empty()) {
        thumbNormal_->setImage(thumbImage_);
    } else {
        thumbNormal_->setImage(name);
    }
    thumbNormal_->resizeFrameToImage(true, true);
    thumbNormal_->setPivot(thumbNormal_->center());
    thumbNormal_->setCenter(panelTop_->center());
}

void LevelSelectorButton::setTrashCanVisible(bool visible) {
    if (type_ != 2 && type_ != kTypeUserLevel) visible = false;
    trash_->setVisible(visible);
}

bool LevelSelectorButton::setup(int type, int level, const aa::game::LocationState* state, const aa::game::LocationInfo* info) {
    type_ = type;
    level_ = level;
    if (pulseAnimation_ != 0) {
        ctx_->animator->cancelAnimation(pulseAnimation_);
        pulseAnimation_ = 0;
    }
    if (type == kTypeEmpty) {
        for (View* v : {static_cast<View*>(thumbEmpty_), static_cast<View*>(thumbNormal_), static_cast<View*>(frameEmpty_), static_cast<View*>(frameNormal_),
                        static_cast<View*>(starOne_), static_cast<View*>(starTwo_), static_cast<View*>(starThree_), static_cast<View*>(labelName_),
                        static_cast<View*>(labelNumber_), static_cast<View*>(trash_)}) {
            v->setVisible(false);
        }
        return true;
    }
    // PanelTop: the frame image's real (scaled) size plus 2.72 % of the screen height (×0.89 on the
    // widescreen tweak) below it for the title.
    const float extra = ctx_->screen.nativeHeight * 0.0272f * restScale();
    if (type == kTypeAddLevel) {
        // Case 6 [verified]: the PanelTop sized like a level's, only the empty-slot frame showing.
        const Rect fs = frameEmpty_->realFrame();
        panelTop_->setFrame(Rect{(frame_.w - fs.w) * 0.5f, 0.0f, fs.w, fs.h + extra});
        labelName_->setMaxLetters(-1);
        labelName_->setMaxRows(-1);
        for (View* v : {static_cast<View*>(thumbEmpty_), static_cast<View*>(thumbNormal_), static_cast<View*>(frameNormal_), static_cast<View*>(starOne_),
                        static_cast<View*>(starTwo_), static_cast<View*>(starThree_), static_cast<View*>(labelNumber_), static_cast<View*>(labelName_),
                        static_cast<View*>(trash_)}) {
            v->setVisible(false);
        }
        frameEmpty_->setVisible(true);
        frameEmpty_->setPivot(frameEmpty_->center());
        frameEmpty_->setCenter(panelTop_->center());
        updateViewAnchors(true, false);
        return true;
    }
    if (type == kTypeUserLevel) {
        // Case 3 [verified]: LoadLevelTitle — an empty title means the file did not load: type 0, false.
        const std::string title = app_->meta(AppState::kSandboxLocation, level).titleId;
        if (title.empty()) {
            type_ = kTypeEmpty;
            level_ = -1;
            return false;
        }
        std::string frame = frameImage_;
        const std::size_t at = frame.find("{0}");
        if (at != std::string::npos) frame.replace(at, 3, std::to_string(frameId(level)));
        frameNormal_->setImage(frame);
        frameNormal_->resizeFrameToImage(true, true);
        const Rect fs = frameNormal_->realFrame();
        panelTop_->setFrame(Rect{(frame_.w - fs.w) * 0.5f, 0.0f, fs.w, fs.h + extra});
        labelName_->setMaxLetters(20);
        labelName_->setMaxRows(2);
        labelName_->setNonLocalizedText(title);
        // The thumbnail next to the level file, when it exists (ResourceProxy::ReloadLoadSpriteFromDocs).
        const std::string thumb = ResourceProxy::thumbnailNameForFile(app_->saves->sandboxThumbPath(info->levels[static_cast<std::size_t>(level)]));
        setThumbImage(ctx_->resources->sprite(thumb).valid() ? thumb : "");
        for (View* v : {static_cast<View*>(thumbEmpty_), static_cast<View*>(frameEmpty_), static_cast<View*>(starOne_), static_cast<View*>(starTwo_),
                        static_cast<View*>(starThree_), static_cast<View*>(labelNumber_)}) {
            v->setVisible(false);
        }
        thumbNormal_->setVisible(true);
        frameNormal_->setVisible(true);
        labelName_->setVisible(true);
        thumbNormal_->setCenter(panelTop_->center());
        updateViewAnchors(true, false);
        return true;
    }
    // Type 1 (a campaign level) [verified: LevelSelectorButton::Setup case 1].
    std::string frame = frameImage_;
    const std::size_t at = frame.find("{0}");
    if (at != std::string::npos) frame.replace(at, 3, std::to_string(frameId(level)));
    frameNormal_->setImage(frame);
    frameNormal_->resizeFrameToImage(true, true);
    // FrameNormal keeps the sprite's pivot (the hole's centre — the LEVEL_FRAME sheets carry a shadow to
    // the bottom right), which the HPIVOT / VPIVOT anchors put at the panel's centre, over the thumbnail.
    const Rect fs = frameNormal_->realFrame();
    panelTop_->setFrame(Rect{(frame_.w - fs.w) * 0.5f, 0.0f, fs.w, fs.h + extra});
    frameEmpty_->setVisible(false);
    trash_->setVisible(false);
    const int status = state->status(level);
    const int stars = state->levelStarCount(level);
    labelName_->setMaxLetters(-1);
    labelName_->setMaxRows(-1);
    labelNumber_->setScale(restScale());   // the pulse animation may have left it grown
    if (status == 1) {
        labelNumber_->setNonLocalizedText("?");
    } else {
        labelNumber_->setNonLocalizedText(std::to_string(level + 1));
        labelName_->setText(app_->meta(info->index, level).titleId);
    }
    labelNumber_->setPivot(labelNumber_->center());
    pulseUp_ = true;
    labelNumber_->setVisible(true);
    labelName_->setVisible(status != 1);
    thumbEmpty_->setVisible(status == 1);
    thumbNormal_->setVisible(status != 1);
    frameNormal_->setVisible(true);
    if (status < 3) {
        starOne_->setVisible(false);
        starTwo_->setVisible(false);
        starThree_->setVisible(false);
    } else {
        starOne_->setImage(stars < 1 ? starEmptyImage_ : starImage_);
        starTwo_->setImage(stars < 2 ? starEmptyImage_ : starImage_);
        starThree_->setImage(stars < 3 ? starEmptyImage_ : starImage_);
        for (ImageView* s : {starOne_, starTwo_, starThree_}) {
            s->resizeFrameToImage(true, true);
            s->setVisible(true);
        }
    }
    thumbEmpty_->setCenter(panelTop_->center());
    thumbNormal_->setCenter(panelTop_->center());
    // Setup never touches the button's own state or visibility [verified]: the list's Refresh calls
    // SetVisible / SetInteraction after it (a SetState here would start ZoomIn's animation before the
    // anchors are updated, and its frames would put the old layout back).
    updateViewAnchors(true, false);
    if (status == 2 && !state->isLevelPlayed(level)) {
        if (pulseAnimation_ == 0) animateButton();
    }
    return true;
}

void LevelSelectorButton::animateButton() {
    // AnimateButton [verified]: the level number pulses 1.0 ↔ 1.1 (0.25 s; the growth waits 0.75 s);
    // 0.89 ↔ 0.979 on the widescreen tweak.
    AnimationParameters p = AnimationParameters::fromView(*labelNumber_);
    p.scale = (pulseUp_ ? 1.1f : 1.0f) * restScale();
    p.curve = pulseUp_ ? 1 : 2;
    p.delay = pulseUp_ ? 0.75f : 0.0f;
    p.duration = 0.25f;
    p.repeat = 1;
    pulseAnimation_ = ctx_->animator->animate(labelNumber_, p, this);
    pulseUp_ = !pulseUp_;
}

void LevelSelectorButton::animationFinished(int id) {
    if (id == pulseAnimation_) {
        pulseAnimation_ = 0;
        animateButton();
        return;
    }
    Button::animationFinished(id);
}

void LevelSelectorButton::update(float dt) { Button::update(dt); }

// --- Level selection view --------------------------------------------------------------------------

LevelSelectionView::LevelSelectionView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict, const aa::data::JsonNode& selectorDict)
    : View(ctx), app_(&app) {
    View::init(dict);
    background_ = make<View>(owned_, ctx);
    background_->setViewName("Background");
    background_->init(sub(dict, "Background"));
    for (const char* key : {"BackgroundLined", "BackgroundSide", "BackgroundBinding"}) {
        ImageView* v = make<ImageView>(owned_, ctx);
        v->setViewName(key);
        v->init(sub(sub(dict, "Background"), key));
        background_->addSubview(v);
    }
    title_ = make<OutlineLabelView>(owned_, ctx);
    title_->setViewName("LabelTitle");
    title_->init(sub(dict, "LabelTitle"));
    back_ = make<Button>(owned_, ctx);
    back_->setViewName("ButtonBack");
    back_->init(sub(dict, "ButtonBack"));
    back_->setDelegate(this);
    panel_ = make<ScrollView>(owned_, ctx);
    panel_->setViewName("PanelLevelContent");
    panel_->init(sub(dict, "PanelLevelContent"));
    panel_->setContentSize(frame_.w > 0.0f ? Size{frame_.w, frame_.h} : Size{ctx.screen.nativeWidth, ctx.screen.nativeHeight});
    panel_->setDelegate(this);
    pages_ = make<PageControl>(owned_, ctx);
    pages_->setViewName("LevelPages");
    pages_->init(sub(dict, "LevelPages"));
    pages_->setPageCount(1);
    pages_->setActivePage(0);
    pages_->setInteraction(false);
    const aa::data::JsonNode area = sub(dict, "SelectorArea/Relative");
    areaXPct_ = area.getFloat("X");
    areaYPct_ = area.getFloat("Y");
    areaWPct_ = area.getFloat("W");
    areaHPct_ = area.getFloat("H");
    for (int i = 0; i < kSlots; ++i) {
        LevelSelectorButton* b = make<LevelSelectorButton>(owned_, ctx, app, selectorDict, slotRect(i));
        b->setViewName("Button_" + std::to_string(i));
        b->setDelegate(this);
        panel_->addSubview(b);
        buttons_[static_cast<std::size_t>(i)] = b;
    }
    addSubview(background_);
    addSubview(pages_);
    addSubview(title_);
    addSubview(panel_);
    addSubview(back_);
    updateViewAnchors(true, true);
}

Rect LevelSelectionView::slotRect(int i) const {
    const float w = ctx_->screen.nativeWidth;
    const float h = ctx_->screen.nativeHeight;
    const float sw = w * 0.01f * areaWPct_;
    const float sh = h * 0.01f * areaHPct_;
    const float sx = w * 0.01f * areaXPct_;
    const float sy = h * 0.01f * areaYPct_;
    const int col = i & 3;
    const int row = (i & 7) >> 2;
    const int page = i >> 3;
    return Rect{static_cast<float>(static_cast<int>(sx + static_cast<float>(col) * sw + w * static_cast<float>(page))),
               static_cast<float>(static_cast<int>(sy + static_cast<float>(row) * sh)), sw, sh};
}

// layoutSlots(): re-derives every slot's grid rect from the SelectorArea percentages and reapplies it —
// LevelSelectorButton's frame is constructor-set from an explicit rect, not Relative/Anchor data, so the
// generic View::relayout() recursion leaves it untouched.
void LevelSelectionView::layoutSlots() {
    for (int i = 0; i < kSlots; ++i) {
        LevelSelectorButton* b = buttons_[static_cast<std::size_t>(i)];
        b->setSlotRect(slotRect(i));
    }
}

void LevelSelectionView::relayout() {
    // Not a Refresh: it would snap the list to the first unplayed level's page on every resize frame and
    // consume returning_. Keep the page the user is on — read before the page size changes (activePage()
    // divides the scroll offset by it) — and redo only Refresh's geometry: the slots (each button's own
    // relayoutContents), the pages' size against the fresh screen, the same page re-snapped.
    const int page = panel_->activePage();
    View::relayout();
    layoutSlots();
    layoutPages();
    panel_->setActivePage(page, false);
    pages_->setActivePage(page);
}

void LevelSelectionView::layoutPages() {
    const float w = ctx_->screen.nativeWidth;
    const float h = ctx_->screen.nativeHeight;
    panel_->setContentSize(Size{w * static_cast<float>(pageCount_), h});
    panel_->setPageSize(Size{w, h});
    pages_->setPageCount(pageCount_);
}

void LevelSelectionView::show(bool animated) {
    setInteraction(false);
    panel_->setVisible(true);
    AnimationParameters p = AnimationParameters::fromView(*this);
    p.alpha = 1.0f;
    p.duration = animated ? kMenuSlide : 0.0f;
    p.repeat = 1;
    showAnimation_ = ctx_->animator->animate(this, p, this);
    refresh_ = true;
}

void LevelSelectionView::hide(bool animated) {
    setInteraction(false);
    panel_->setVisible(false);
    AnimationParameters p = AnimationParameters::fromView(*this);
    p.alpha = 0.0f;
    p.duration = animated ? kMenuSlide : 0.0f;
    p.repeat = 1;
    hideAnimation_ = ctx_->animator->animate(this, p, this);
}

void LevelSelectionView::purgeThumbs() {
    for (LevelSelectorButton* b : buttons_) b->setThumbImage("");
}

void LevelSelectionView::refresh() {
    // LevelSelectionView::Refresh [verified]: one button per level slot, pages of eight, the initial
    // page from the first unplayed level unless returning from a level.
    refresh_ = false;
    const aa::game::LocationInfo* info = app_->location();
    if (!info) return;
    const aa::game::LocationState& state = app_->locationState;
    title_->setText(info->nameId);
    const int count = info->levelCount();
    for (int i = 0; i < kSlots; ++i) {
        LevelSelectorButton* b = buttons_[static_cast<std::size_t>(i)];
        if (i < count && state.status(i) > 0) {
            b->setup(1, i, &state, info);
            b->setVisible(true);
            b->setInteraction(state.status(i) > 1);
        } else {
            b->setup(0, -1, &state, info);
            b->setInteraction(false);
            b->setVisible(false);
        }
    }
    pageCount_ = count / kPerPage + (count % kPerPage > 0 ? 1 : 0);
    if (pageCount_ == 0) pageCount_ = 1;
    layoutPages();
    int page;
    if (!returning_) {
        page = state.firstUnplayedLevel(*info) / kPerPage;
    } else {
        page = app_->currentLevel / kPerPage;
    }
    panel_->setActivePage(page, false);
    pages_->setActivePage(page);
    returning_ = false;
}

void LevelSelectionView::update(float dt) {
    // LevelSelectionView::Update [verified]: RefreshThumbs (raised by the scene's ActivationComplete) before
    // Refresh (raised by Activate), each when flagged. The order matters on the widescreen tweak: Setup's
    // SetState → ZoomIn captures the thumb's pivot for its 0.05 s animation, so the thumb's centre pivot from
    // SetThumbImage must already be there — or the animation's last frame puts the top-left pivot back.
    View::update(dt);
    if (refreshThumbs_) {
        refreshThumbs_ = false;
        const aa::game::LocationInfo* info = app_->location();
        if (info) {
            for (int i = 0; i < info->levelCount(); ++i) {
                if (app_->locationState.status(i) > 1) buttons_[static_cast<std::size_t>(i)]->setThumbImage(ResourceProxy::thumbnailName(info->levels[static_cast<std::size_t>(i)]));
            }
        }
    }
    if (refresh_) refresh();
}

void LevelSelectionView::buttonPressed(int id) {
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (!manager) return;
    setInteraction(true);
    if (id == back_->id()) {
        purgeThumbs();
        manager->popScene();
        return;
    }
    for (int i = 0; i < kSlots; ++i) {
        if (buttons_[static_cast<std::size_t>(i)]->id() != id) continue;
        if (app_->locationState.status(i) <= 1) return;
        manager->pushScene(scene_names::kLevelLoading);
        if (auto* loading = dynamic_cast<LevelLoadingScene*>(manager->scene(scene_names::kLevelLoading))) {
            loading->setLoadingLocation(LevelLoadingScene::kLocationCampaign, i);
        }
        returning_ = true;
        purgeThumbs();
        return;
    }
}

void LevelSelectionView::buttonAboutToBePressed(int) { setInteraction(false); }

void LevelSelectionView::scrollViewMoved(int id) {
    if (id != panel_->id()) return;
    pages_->setActivePage(panel_->activePage());
}

void LevelSelectionView::scrollViewFinishedDecelerating(int) { setInteraction(true); }

void LevelSelectionView::animationFinished(int id) {
    if (id == showAnimation_) {
        showAnimation_ = 0;
        setInteraction(true);
    } else if (id == hideAnimation_) {
        hideAnimation_ = 0;
    }
}

void LevelSelectionScene::init() {
    Scene::init();
    view_ = make<LevelSelectionView>(*app_, tree().view("LevelSelectionView"), tree().view("LevelSelectorButton"));
    view_->setViewName("LevelSelectionView");
    view_->setVisible(true);
    view_->setFrame(root_->frame());   // <Scene>::Init passes the root frame to <View>::Init(UIRect) [verified]
    root_->addSubview(view_);
}

void LevelSelectionScene::activate() {
    Scene::activate();
    if (app_->audio) app_->audio->playMusic(kMusicTheme);
    view_->show(true);
}

void LevelSelectionScene::activationComplete() {
    Scene::activationComplete();
    view_->refreshThumbs();
}

void LevelSelectionScene::inactivate() {
    Scene::inactivate();
    view_->hide(false);
}

void LevelSelectionScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

// --- Level loading ---------------------------------------------------------------------------------

void LevelLoadingScene::init() {
    Scene::init();
    const aa::data::JsonNode dict = tree().view("LevelLoadingView");
    view_ = make<View>();
    view_->setViewName("LevelLoadingView");
    view_->init(dict);
    view_->setFrame(root_->frame());
    background_ = make<ImageView>();
    background_->setViewName("Background");
    background_->init(dict);
    label_ = make<OutlineLabelView>();
    label_->setViewName("LabelLoading");
    label_->init(sub(dict, "LabelLoading"));
    view_->addSubview(background_);
    view_->addSubview(label_);
    view_->updateViewAnchors(true, true);
    view_->setVisible(true);
    root_->addSubview(view_);
}

void LevelLoadingScene::activate() {
    Scene::activate();
    pendingPush_ = false;   // a push left over from an interrupted show animation must not fire for this run
    view_->setVisible(true);
    AnimationParameters p = AnimationParameters::fromView(*view_);
    p.duration = kLoadingShow;
    p.repeat = 1;
    showAnimation_ = ctx_->animator->animate(view_, p, this);
    if (app_->audio) app_->audio->stopMusic();
}

void LevelLoadingScene::activationComplete() {
    Scene::activationComplete();
    // LevelLoadingScene::ActivationComplete [verified: 0x1264bc]. Location 2: the sandbox level loads
    // (a file that does not parse → location 0, the loading scene pops itself, MyContraptionsScene shows
    // the parsing error); location 3: a new level (CreateNewSandbox, a unique name added to the index and
    // saved, the author from the settings).
    if (loadingLocation_ == kLocationSandbox || loadingLocation_ == kLocationNewSandbox) {
        auto* sandbox = dynamic_cast<SandboxScene*>(manager_->scene(scene_names::kSandbox));
        if (!sandbox) return;
        if (loadingLocation_ == kLocationSandbox) {
            if (!sandbox->selectLevel(level_)) {
                loadingLocation_ = 0;
                level_ = -1;
                if (auto* list = dynamic_cast<MyContraptionsScene*>(manager_->scene(scene_names::kMyContraptions))) list->showParsingError();
            }
            return;
        }
        level_ = sandbox->createNewLevel();
        return;
    }
    // Location 1: the level is selected, the Classroom is marked unlocked on its first level, the level
    // is marked played and the state saved.
    if (loadingLocation_ != kLocationCampaign) return;
    if (auto* game = dynamic_cast<GameScene*>(manager_->scene(scene_names::kGame))) game->selectLevel(level_);
    if (app_->locationIndex == 0 && !app_->progress.locations[0].unlocked) {
        app_->progress.locations[0].unlocked = true;
        app_->saveProgress();
    }
    app_->locationState.setLevelPlayed(level_);
    app_->saveLocation();
}

void LevelLoadingScene::inactivate() { Scene::inactivate(); }

void LevelLoadingScene::inactivationComplete() {
    // LevelLoadingScene::InactivationComplete [verified]: the loading location resets to 0 and the level
    // to -1, so the scene pops itself when the game returns onto it (AnimationFinished, location 0).
    Scene::inactivationComplete();
    loadingLocation_ = 0;
    level_ = -1;
}

void LevelLoadingScene::update(float dt) {
    Scene::update(dt);
    if (pendingPush_ && state_ == scene_state::kActive) {
        pendingPush_ = false;
        if (loadingLocation_ == kLocationBackToChapters) {
            if (!manager_->popScenesUntil(scene_names::kChapterSelection)) manager_->pushScene(scene_names::kChapterSelection);
        } else if (loadingLocation_ == kLocationCampaign) {
            manager_->removeScene(scene_names::kGame);
            manager_->pushScene(scene_names::kGame);
        } else if (loadingLocation_ == kLocationSandbox || loadingLocation_ == kLocationNewSandbox) {
            manager_->removeScene(scene_names::kSandbox);
            manager_->pushScene(scene_names::kSandbox);
        } else {
            manager_->popScene();
        }
    }
}

void LevelLoadingScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

void LevelLoadingScene::animationFinished(int id) {
    if (id != showAnimation_) return;
    showAnimation_ = 0;
    pendingPush_ = true;
}

// --- Chapter complete ------------------------------------------------------------------------------

void ChapterCompleteScene::init() {
    Scene::init();
    const aa::data::JsonNode dict = tree().view(threeStars_ ? "ChapterComplete3StarsView" : "ChapterCompleteView");
    view_ = make<View>();
    view_->setViewName(threeStars_ ? "ChapterComplete3StarsView" : "ChapterCompleteView");
    view_->init(dict);
    view_->setFrame(root_->frame());
    background_ = make<ImageView>();
    background_->setViewName("Background");
    background_->init(dict);
    items_ = make<ImageView>();
    items_->setViewName("ImageUnlockedItems");
    items_->init(sub(dict, "ImageUnlockedItems"));
    itemsImage_ = sub(dict, "ImageUnlockedItems").getString("ItemsImage", "{0}_COMPLETE");
    congratulations_ = make<OutlineLabelView>();
    congratulations_->setViewName("LabelCongratulations");
    congratulations_->init(sub(dict, "LabelCongratulations"));
    unlocked_ = make<OutlineLabelView>();
    unlocked_->setViewName("LabelUnlockedItems");
    unlocked_->init(sub(dict, "LabelUnlockedItems"));
    next_ = make<Button>();
    next_->setViewName("ButtonNext");
    next_->init(sub(dict, "ButtonNext"));
    next_->setDelegate(this);
    view_->addSubview(background_);
    view_->addSubview(items_);
    view_->addSubview(congratulations_);
    view_->addSubview(unlocked_);
    view_->addSubview(next_);
    view_->setVisible(true);
    root_->addSubview(view_);
}

void ChapterCompleteScene::activate() {
    Scene::activate();
    if (app_->audio) app_->audio->playMusic(kMusicTheme);
    // The chapter's picture: <BOOK>_COMPLETE / <BOOK>_PERFECT.
    std::string image = itemsImage_;
    const std::size_t at = image.find("{0}");
    if (at != std::string::npos) image.replace(at, 3, chapterCompletionImage(app_->locationIndex));
    items_->setImage(image);
    items_->resizeFrameToImage(true, true);
    view_->updateViewAnchors(true, true);
}

void ChapterCompleteScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

void ChapterCompleteScene::buttonPressed(int id) {
    if (id != next_->id()) return;
    // ChapterCompleteView::ButtonPressed [verified]: with every star collected and the 3-stars panel not
    // shown yet, its byte is set, the progress saved and the 3-stars panel pushed; otherwise back to
    // the chapter books (PopScenesUntil) with the Classroom's end comic on top, once.
    // ChapterComplete3StarsView::ButtonPressed [verified]: always back to the books (+ the comic).
    const aa::game::LocationInfo* info = app_->location();
    aa::game::LocationProgress& lp = app_->progress.locations[static_cast<std::size_t>(std::max(0, app_->locationIndex))];
    if (!threeStars_ && info && !lp.threeStarsShown && app_->locationState.starCount(*info) == info->maxStarCount()) {
        lp.threeStarsShown = true;
        app_->saveProgress();
        manager_->pushScene(scene_names::kChapterComplete3Stars);
        return;
    }
    if (!manager_->popScenesUntil(scene_names::kChapterSelection)) manager_->pushScene(scene_names::kChapterSelection);
    if (app_->locationIndex == 0) showChapterComic(*manager_, *app_, 1);
}

bool ChapterCompleteScene::keyDown(int key) {
    if (key == SceneManager::kKeyBack) {
        buttonPressed(next_->id());
        return true;
    }
    return Scene::keyDown(key);
}

bool showChapterComplete(SceneManager& manager, AppState& app) {
    const aa::game::LocationInfo* info = app.location();
    if (!info || app.locationIndex < 0 || app.locationIndex >= aa::game::kLocationCount) return false;
    aa::game::LocationProgress& lp = app.progress.locations[static_cast<std::size_t>(app.locationIndex)];
    const aa::game::LocationState& state = app.locationState;
    // UI::showChapterComplete [verified]: the shown byte is set and the progress saved as the panel is
    // pushed (the chapter-complete panel first; the 3-stars one when the chapter panel was shown before).
    if (!lp.chapterCompleteShown && state.completedLevelsCount(*info) == info->levelCount()) {
        lp.chapterCompleteShown = true;
        app.saveProgress();
        manager.pushScene(scene_names::kChapterComplete);
        return true;
    }
    if (!lp.threeStarsShown && state.starCount(*info) == info->maxStarCount()) {
        lp.threeStarsShown = true;
        app.saveProgress();
        manager.pushScene(scene_names::kChapterComplete3Stars);
        return true;
    }
    return false;
}

}  // namespace aa::ui
