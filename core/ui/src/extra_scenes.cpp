#include "aa/ui/extra_scenes.h"

#include "aa/data/level_loader.h"

#include <cmath>
#include <exception>

namespace aa::ui {

namespace {

constexpr int kMusicTheme = 1;

template <class T, class... Args>
T* make(std::vector<std::unique_ptr<View>>& owned, Args&&... args) {
    auto v = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = v.get();
    owned.push_back(std::move(v));
    return raw;
}

aa::data::JsonNode sub(const aa::data::JsonNode& dict, const char* key) { return dict.optional(key); }

bool isBackKey(int key) { return key == SceneManager::kKeyBack || key == SceneManager::kKeyAndroidBack; }

}  // namespace

// --- Comics ----------------------------------------------------------------------------------------

ComicView::ComicView(UiContext& ctx, const aa::data::JsonNode& dict) : View(ctx) {
    // ComicView::Init(rect, dict) [verified]: View::Init(rect) — the dictionary's own attributes are not
    // read — the Background image from the dictionary, a bare silent tap button over the whole view, the
    // hidden ButtonNext, one ImageView per Frames entry (×0.85 on 16:9 phones); added background, frames,
    // tap area, next.
    background_ = make<ImageView>(owned_, ctx);
    background_->setViewName("Background");
    background_->init(dict);
    tapArea_ = make<Button>(owned_, ctx);
    tapArea_->setViewName("TapAreaButton");
    tapArea_->init(aa::data::JsonNode(nullptr, ""));
    tapArea_->setDelegate(this);
    tapArea_->setSilent(true);
    tapArea_->setAnimateOnlyBackground(true);
    next_ = make<Button>(owned_, ctx);
    next_->setViewName("ButtonNext");
    next_->init(sub(dict, "ButtonNext"));
    next_->setDelegate(this);
    next_->setVisible(false);
    const aa::data::JsonNode frames = sub(dict, "Frames");
    if (frames.isArray()) {
        for (const aa::data::JsonNode& f : frames.array()) {
            ImageView* v = make<ImageView>(owned_, ctx);
            v->setViewName("Frame" + std::to_string(frames_.size()));
            v->init(f);
            if (ctx.screen.widescreenScaling) v->setScale(0.85f);   // [verified: ComicView::Init]
            frames_.push_back(v);
        }
    }
    addSubview(background_);
    for (ImageView* f : frames_) addSubview(f);
    addSubview(tapArea_);
    addSubview(next_);
}

void ComicView::show() {
    // Show [verified]: the background on, every frame hidden, the counters reset.
    background_->setVisible(true);
    for (ImageView* f : frames_) f->setVisible(false);
    shown_ = 0;
    timer_ = 0.0f;
    setVisible(true);
}

void ComicView::hide() {
    background_->setVisible(false);
    for (ImageView* f : frames_) f->setVisible(false);
    next_->setVisible(false);
    setVisible(false);
}

void ComicView::showNextFrame() {
    timer_ = 0.0f;
    shown_ = shown_ + 1;
}

void ComicView::update(float dt) {
    View::update(dt);
    // The tap button covers the view's frame (Button::Init(rect) at Init time; the frame is set by the scene).
    if (tapArea_->size().w != frame_.w || tapArea_->size().h != frame_.h) tapArea_->setFrame(Rect{0.0f, 0.0f, frame_.w, frame_.h});
    const int count = frameCount();
    const int visible = shown_ < count ? shown_ : count;
    for (int i = 0; i < visible; ++i) frames_[static_cast<std::size_t>(i)]->setVisible(true);
    if (timer_ < kFrameInterval || shown_ >= count) timer_ = timer_ + dt;
    else showNextFrame();
    if (shown_ >= count && timer_ >= kNextDelay) next_->setVisible(true);
}

void ComicView::buttonPressed(int id) {
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (id == tapArea_->id()) {
        if (shown_ < frameCount()) showNextFrame();
        else next_->setVisible(true);
        return;
    }
    if (id == next_->id() && manager) manager->popScene();
}

void ComicView::relayout() {
    View::relayout();
    // AssetScalingForWidescreen [verified: ComicView::Init]: the constructor applies it only once, but
    // ctx_->screen.widescreenScaling can flip on a resize (ScreenLayout::compute's pixelScale < 1 rule).
    const float s = ctx_->screen.widescreenScaling ? 0.85f : 1.0f;
    for (ImageView* f : frames_) f->setScale(s);
}

bool ComicView::keyDown(int key) {
    if (View::keyDown(key)) return true;
    // KeyDown [verified]: the begin comic pops on the back key; the end comic ignores it.
    if (type_ == 0 && isBackKey(key)) {
        if (SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr) manager->popScene();
        return true;
    }
    return false;
}

void ComicScene::init() { Scene::init(); }

void ComicScene::setComicView(int type, int location) {
    const std::string name = std::string(type == 0 ? "ComicViewBegin" : "ComicViewEnd") + std::to_string(location + 1);
    if (current_) {
        root_->removeSubview(current_.get());
        current_.reset();
        view_ = nullptr;
    }
    current_ = std::make_unique<ComicView>(*ctx_, tree().view(name));
    view_ = current_.get();
    view_->setViewName(name);
    view_->setFrame(root_->frame());
    view_->setComicType(type);
    view_->updateViewAnchors(true, true);
    view_->show();
    root_->addSubview(view_);
}

void ComicScene::activate() {
    Scene::activate();
    if (view_) view_->show();
}

void ComicScene::relayout(int, int) {
    // view_ is a dynamically swapped ComicView (setComicView), null until the first comic is shown.
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

bool showChapterComic(SceneManager& manager, AppState& app, int type) {
    // UI::showChapterComic [verified: 0x1251a4]. Every call site guards on location 0 — the shipped
    // bundle carries the Classroom's comic only (resource sets 5..12 all map to COMIC_CH1).
    if (app.locationIndex < 0) return false;
    bool& flag = type == 0 ? app.locationState.visited : app.locationState.finished;
    if (flag) return false;
    if (auto* comic = dynamic_cast<ComicScene*>(manager.scene(scene_names::kComic))) {
        comic->setComicView(type, app.locationIndex);
        manager.pushScene(scene_names::kComic);
    } else if (type == 1 && app.audio) {
        app.audio->playMusic(kMusicTheme);
    }
    flag = true;
    app.saveLocation();
    return true;
}

// --- Credits ---------------------------------------------------------------------------------------

CreditsView::CreditsView(UiContext& ctx, const aa::data::JsonNode& dict) : View(ctx) {
    // CreditsView::Init [verified: 0x12ff4c]: Background(dict), ButtonBack, PanelScroll with ImageHeader,
    // LabelTitle (TitleText), LabelVersion (VersionText with Version::Get — the remake's kVersionText here),
    // LabelCopyright,
    // ButtonPrivacyPolicy, ButtonEula, ImageSubHeader, LabelTitle<Name> / Label<Name> per credit group,
    // ImageFooter; vertical scrolling only; the content height = the footer's bottom plus its paddings plus
    // 1.2 screen heights; the offset starts at 5. The original's table has 28 groups and lacks
    // PostProductionLead, although CreditsScene.json chains LabelTitleOperations below LabelPostProductionLead
    // — its groups from Operations on overlap the earlier ones; the remake adds the group (a deviation,
    // docs/10 §11 item 14 (d)), so the chain is continuous.
    static constexpr const char* kGroups[] = {
        "Credits", "ExecutiveProducers", "ProjectManager", "Producers", "LeadProgrammers", "Programmers", "LeadArtists", "Artists",
        "LevelDesigners", "GameDesigners", "QAManagers", "QACoordinators", "QALead", "QATeam", "FunctionalityQA", "HeadOfQA", "OPManager",
        "QAProjectLead", "FunctionalityQATech", "MarketingAndPR", "MusicAndSound", "Sound", "AdditionalSound", "OperationsManager",
        "PostProductionLead", "Operations", "Caseys", "PhysicsPoweredBy", "PlatformPort"};
    background_ = make<ImageView>(owned_, ctx);
    background_->setViewName("Background");
    background_->init(dict);
    back_ = make<Button>(owned_, ctx);
    back_->setViewName("ButtonBack");
    back_->init(sub(dict, "ButtonBack"));
    back_->setDelegate(this);
    const aa::data::JsonNode scroll = sub(dict, "PanelScroll");
    panel_ = make<ScrollView>(owned_, ctx);
    panel_->setViewName("PanelScroll");
    panel_->init(scroll);
    auto image = [&](const char* key) {
        ImageView* v = make<ImageView>(owned_, ctx);
        v->setViewName(key);
        v->init(sub(scroll, key));
        panel_->addSubview(v);
        return v;
    };
    auto label = [&](const std::string& key, const char* textKey) {
        const aa::data::JsonNode d = sub(scroll, key.c_str());
        OutlineLabelView* v = make<OutlineLabelView>(owned_, ctx);
        v->setViewName(key);
        v->init(d);
        if (textKey && d.has(textKey)) v->setText(d.getString(textKey));
        panel_->addSubview(v);
        labels_.push_back(v);
        return v;
    };
    auto button = [&](const char* key) {
        Button* b = make<Button>(owned_, ctx);
        b->setViewName(key);
        b->init(sub(scroll, key));
        b->setDelegate(this);
        panel_->addSubview(b);
        return b;
    };
    image("ImageHeader");
    label("LabelTitle", "TitleText");
    version_ = label("LabelVersion", nullptr);
    version_->setNonLocalizedText(kVersionText);
    label("LabelCopyright", nullptr);
    button("ButtonPrivacyPolicy")->setState(button_state::kDisabled);   // the Rovio links: inert offline
    button("ButtonEula")->setState(button_state::kDisabled);
    image("ImageSubHeader");
    for (const char* group : kGroups) {
        const std::string title = std::string("LabelTitle") + group;
        if (scroll.has(title.c_str())) label(title, nullptr);
        const std::string body = std::string("Label") + group;
        if (scroll.has(body.c_str())) label(body, nullptr);
    }
    footer_ = image("ImageFooter");
    panel_->setInteraction(true);
    panel_->setHorizontalScrolling(false);
    panel_->setVerticalScrolling(true);
    panel_->setDelegate(this);
    addSubview(background_);
    addSubview(panel_);
    addSubview(back_);
    updateViewAnchors(true, true);
    // The back button is anchored above the screen (BOTTOM at the view's TOP, 3 % up): that is its hidden
    // place (+0x9d8); shown (+0x9d0) mirrors it below the top edge: y = −(hidden.y + h) [verified: 0x120c58].
    backHidden_ = back_->position();
    backShown_ = Point{backHidden_.x, -(backHidden_.y + back_->size().h)};
    const float* pad = footer_->padding();
    const float bottom = footer_->position().y + footer_->size().h + pad[2] + pad[3] + ctx.screen.nativeHeight * 1.2f;
    panel_->setContentSize(Size{panel_->size().w, bottom});
    panel_->setContentOffset(Point{0.0f, kInitialOffset}, false);
}

void CreditsView::show(bool animated) {
    setVisible(true);
    shown_ = true;
    if (!animated) {
        setInteraction(true);
        back_->setPosition(backShown_);
        return;
    }
    AnimationParameters p = AnimationParameters::fromView(*back_);
    p.frame.x = backShown_.x;
    p.frame.y = backShown_.y;
    p.duration = kSlide;
    p.repeat = 1;
    showAnim_ = ctx_->animator->animate(back_, p, this);
}

void CreditsView::hide(bool animated) {
    setInteraction(false);
    shown_ = false;
    if (!animated) {
        back_->setPosition(backHidden_);
        setVisible(false);
        return;
    }
    AnimationParameters p = AnimationParameters::fromView(*back_);
    p.frame.x = backHidden_.x;
    p.frame.y = backHidden_.y;
    p.duration = kSlide;
    p.repeat = 1;
    hideAnim_ = ctx_->animator->animate(back_, p, this);
}

void CreditsView::update(float dt) {
    View::update(dt);
    if (!autoScroll_) return;
    // Update [verified: 0x12e5a4]: below the initial offset the list wraps to its end; otherwise it
    // creeps down by 6.5 % of the screen height per second until 5 px before the end.
    Point off = panel_->contentOffset();
    const float limit = panel_->contentSize().h - panel_->size().h - kInitialOffset;
    if (off.y < kInitialOffset) off.y = limit;
    else if (off.y <= limit) off.y = off.y + ctx_->screen.nativeHeight * kScrollRate * dt;
    panel_->setContentOffset(off, false);
}

bool CreditsView::keyDown(int key) {
    if (View::keyDown(key)) return true;
    // A plain pop would keep autoScroll_ and the offset for the next visit; the hide animation resets them.
    if (!isBackKey(key)) return false;
    if (hideAnim_ == 0) hide(true);
    return true;
}

void CreditsView::buttonPressed(int id) {
    if (id == back_->id()) hide(true);
    // ButtonPrivacyPolicy / ButtonEula open Rovio's site in the original: dropped (offline).
}

void CreditsView::animationFinished(int id) {
    if (id == hideAnim_) {
        hideAnim_ = 0;
        setVisible(false);
        autoScroll_ = false;
        panel_->setContentOffset(Point{0.0f, kInitialOffset}, false);
        if (SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr) manager->popScene();
    } else if (id == showAnim_) {
        showAnim_ = 0;
        setInteraction(true);
        autoScroll_ = true;
    }
}

void CreditsView::scrollViewStartedDecelerating(int) { autoScroll_ = false; }
void CreditsView::scrollViewFinishedDecelerating(int) { autoScroll_ = true; }

void CreditsView::relayout() {
    // View::relayout() completes an in-flight back-button slide first; hideAnim_'s callback pops this
    // scene (see CreditsView::animationFinished) — completing it mid-resize is the intended "snap to
    // endpoint" behavior for an in-flight exit slide.
    View::relayout();
    backHidden_ = back_->position();
    backShown_ = Point{backHidden_.x, -(backHidden_.y + back_->size().h)};
    back_->setPosition(shown_ ? backShown_ : backHidden_);
    // The content height, re-derived from the footer's fresh anchored position exactly as the constructor
    // did; the scroll offset resets to the top rather than risk sitting past a shrunk content size.
    const float* pad = footer_->padding();
    const float bottom = footer_->position().y + footer_->size().h + pad[2] + pad[3] + ctx_->screen.nativeHeight * 1.2f;
    panel_->setContentSize(Size{panel_->size().w, bottom});
    panel_->setContentOffset(Point{0.0f, kInitialOffset}, false);
}

void CreditsScene::init() {
    Scene::init();
    view_ = make<CreditsView>(tree().view("CreditsView"));
    view_->setViewName("CreditsView");
    view_->setFrame(root_->frame());
    view_->updateViewAnchors(true, true);
    view_->setVisible(true);
    root_->addSubview(view_);
}

void CreditsScene::activate() {
    Scene::activate();
    view_->show(true);
}

void CreditsScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

// --- My Contraptions -------------------------------------------------------------------------------

MyContraptionsView::MyContraptionsView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict, const aa::data::JsonNode& selectorDict)
    : View(ctx), app_(&app) {
    // MyContraptionsView::Init [verified: 0x1462c4]: like LevelSelectionView (Background with the three
    // strips, LabelTitle, ButtonBack, ButtonAdd, the ButtonTrash toggle, PanelLevelContent, LevelPages,
    // the 96 level buttons on the SelectorArea grid) plus the two single-button message dialogs and the
    // legal InfoDialog, all hidden.
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
    add_ = make<Button>(owned_, ctx);
    add_->setViewName("ButtonAdd");
    add_->init(sub(dict, "ButtonAdd"));
    add_->setDelegate(this);
    trash_ = make<ToggleButton>(owned_, ctx);
    trash_->setViewName("ButtonTrash");
    trash_->init(sub(dict, "ButtonTrash"));
    trash_->setDelegate(this);
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
    const aa::data::JsonNode dialogs = app.dialogs();
    parsingError_ = make<MessageDialog>(owned_, ctx, true);
    parsingError_->setViewName("ErrorParsingLevel");
    parsingError_->init(sub(dict, "ErrorParsingLevel"), dialogs);
    parsingError_->setVisible(false);
    parsingError_->setDelegate(this);
    storageFull_ = make<MessageDialog>(owned_, ctx, true);
    storageFull_->setViewName("ErrorLevelStorageFull");
    storageFull_->init(sub(dict, "ErrorLevelStorageFull"), dialogs);
    storageFull_->setVisible(false);
    storageFull_->setDelegate(this);
    legal_ = make<InfoDialog>(owned_, ctx);
    legal_->setViewName("LegalTextDialog");
    legal_->init(sub(dict, "LegalTextDialog"), dialogs);
    legal_->setDelegate(this);
    legal_->setVisible(false);
    const aa::data::JsonNode area = sub(sub(dict, "SelectorArea"), "Relative");
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
    addSubview(title_);
    addSubview(panel_);
    addSubview(pages_);
    addSubview(back_);
    addSubview(add_);
    addSubview(trash_);
    addSubview(parsingError_);
    addSubview(storageFull_);
    addSubview(legal_);
    updateViewAnchors(true, true);
}

Rect MyContraptionsView::slotRect(int i) const {
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

void MyContraptionsView::layoutSlots() {
    for (int i = 0; i < kSlots; ++i) {
        LevelSelectorButton* b = buttons_[static_cast<std::size_t>(i)];
        b->setSlotRect(slotRect(i));
    }
}

void MyContraptionsView::relayout() {
    // Not a Refresh (see LevelSelectionView::relayout): that re-reads every user level's file from disk —
    // once per resize frame of a click-drag — snaps the page and turns level deleting off. Keep the page
    // (read before the page size changes) and redo only Refresh's geometry.
    const int page = panel_->activePage();
    View::relayout();
    layoutSlots();
    layoutPages();
    panel_->setActivePage(page, false);
    pages_->setActivePage(page);
    // parsingError_ / storageFull_ (MessageDialogs) and legal_ (an InfoDialog) all have their own
    // relayout() override; none is virtual-dispatched by the generic recursion above, only reached by an
    // explicit call from whoever holds the pointer (see MainMenuView::relayout).
    parsingError_->relayout();
    storageFull_->relayout();
    legal_->relayout();
}

void MyContraptionsView::refresh() {
    // Refresh [verified: 0x147630]: LoadFromDocs, then from the last slot down: a user level per listed
    // name (a level whose title does not load is removed from the index, saved, and the refresh restarts),
    // the "add" tile after the last one while there is room; the pages of eight (12 when full). The
    // original restarts by recursion, re-reading the index from disk; the remake restarts at most once per
    // slot and keeps the in-memory list when the index cannot be written back (a read-only save
    // directory), so the unloadable levels disappear from the list instead of overflowing the stack.
    if (refresh_) enableLevelDeleting(false);
    refresh_ = false;
    int count = 0;
    bool reload = true;
    aa::game::LocationInfo* info = nullptr;
    for (int attempt = 0; attempt <= kSlots; ++attempt) {
        if (reload) app_->loadSandboxLocation();
        app_->loadLocation(AppState::kSandboxLocation);
        info = &app_->locations[static_cast<std::size_t>(AppState::kSandboxLocation)];
        title_->setText(info->nameId);
        count = info->levelCount();
        bool restart = false;
        for (int i = kSlots - 1; i >= 0; --i) {
            LevelSelectorButton* b = buttons_[static_cast<std::size_t>(i)];
            if (i < count) {
                if (!b->setup(LevelSelectorButton::kTypeUserLevel, i, &app_->locationState, info)) {
                    app_->removeSandboxLevel(i);
                    reload = app_->saveSandboxLocation();
                    restart = attempt < kSlots;
                    break;
                }
                b->setInteraction(true);
                b->setVisible(true);
            } else {
                b->setup(LevelSelectorButton::kTypeEmpty, -1, &app_->locationState, info);
                b->setInteraction(false);
                b->setVisible(false);
            }
        }
        if (!restart) break;
    }
    if (count < kSlots) {
        LevelSelectorButton* add = buttons_[static_cast<std::size_t>(count)];
        add->setup(LevelSelectorButton::kTypeAddLevel, -1, &app_->locationState, info);
        add->setInteraction(true);
        add->setVisible(true);
        pageCount_ = static_cast<int>(std::ceil(static_cast<float>(count + 1) * 0.125f));
    } else {
        pageCount_ = kSlots / kPerPage;
    }
    layoutPages();
}

void MyContraptionsView::layoutPages() {
    const float w = ctx_->screen.nativeWidth;
    const float h = ctx_->screen.nativeHeight;
    panel_->setContentSize(Size{w * static_cast<float>(pageCount_), h});
    panel_->setPageSize(Size{w, h});
    pages_->setPageCount(pageCount_);
}

void MyContraptionsView::enableLevelDeleting(bool on) {
    trash_->setChecked(on);
    showLevelButtonTrashCans(on);
}

void MyContraptionsView::showLevelButtonTrashCans(bool on) {
    for (LevelSelectorButton* b : buttons_) b->setTrashCanVisible(on);
}

void MyContraptionsView::showParsingError() {
    if (shown_) parsingError_->show();
    else parsingErrorPending_ = true;
}

void MyContraptionsView::hideAllDialogs() {
    parsingError_->hide();
    storageFull_->hide();
    legal_->hide();
}

void MyContraptionsView::show() {
    // Show [verified]: the dialogs hidden, the trash cans off, a refresh when the view was hidden or
    // marked, then visible; the legal prompt while the setting is clear.
    hideAllDialogs();
    panel_->setVisible(true);
    enableLevelDeleting(false);
    if (!isVisible() || refresh_) refresh();
    setVisible(true);
    setInteraction(true);
    legal_->setVisible(!app_->settings.sandboxLegalAccepted);
    shown_ = true;
    if (parsingErrorPending_) {
        parsingErrorPending_ = false;
        parsingError_->show();
    }
}

void MyContraptionsView::hide() {
    shown_ = false;
    panel_->setVisible(false);
    enableLevelDeleting(false);
    setInteraction(false);
    hideAllDialogs();
}

void MyContraptionsView::update(float dt) {
    View::update(dt);
    if (refresh_ && isVisible()) refresh();
}

void MyContraptionsView::scrollViewMoved(int id) {
    if (id != panel_->id()) return;
    pages_->setActivePage(panel_->activePage());
}

void MyContraptionsView::messageConfirmed(int dialogId) {
    // MessageConfirmed [verified]: the legal prompt confirmed → the setting saved; every dialog hides.
    if (dialogId == legal_->id()) {
        app_->settings.sandboxLegalAccepted = true;
        app_->saveSettings();
    }
    hideAllDialogs();
}

void MyContraptionsView::messageCanceled(int) { hideAllDialogs(); }

void MyContraptionsView::addLevel() {
    // ButtonAdd [verified]: below the cap the loading scene creates the level (location 3); at the cap
    // the storage-full dialog.
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (!manager) return;
    const int count = app_->locations[static_cast<std::size_t>(AppState::kSandboxLocation)].levelCount();
    if (count >= aa::game::SaveStore::kMaxSandboxLevels) {
        storageFull_->show();
        return;
    }
    auto* loading = dynamic_cast<LevelLoadingScene*>(manager->scene(scene_names::kLevelLoading));
    if (loading && loading->loadingLocation() == 0) loading->setLoadingLocation(LevelLoadingScene::kLocationNewSandbox, -1);
    manager->pushScene(scene_names::kLevelLoading);
    for (LevelSelectorButton* b : buttons_) b->setThumbImage("");
}

void MyContraptionsView::openLevel(int level) {
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (!manager) return;
    if (trash_->isChecked()) {
        // The trash toggle on: the level's files and its index entry go; the list refreshes at once.
        app_->removeSandboxLevel(level);
        app_->saveSandboxLocation();
        refresh();
        return;
    }
    auto* loading = dynamic_cast<LevelLoadingScene*>(manager->scene(scene_names::kLevelLoading));
    if (loading && loading->loadingLocation() == 0) loading->setLoadingLocation(LevelLoadingScene::kLocationSandbox, level);
    manager->pushScene(scene_names::kLevelLoading);
    for (LevelSelectorButton* b : buttons_) b->setThumbImage("");
}

void MyContraptionsView::buttonPressed(int id) {
    SceneManager* manager = parentScene() ? parentScene()->manager() : nullptr;
    if (!manager) return;
    if (id == back_->id()) {
        manager->popScene();
        return;
    }
    if (id == add_->id()) {
        addLevel();
        return;
    }
    if (id == trash_->id()) {
        showLevelButtonTrashCans(trash_->isChecked());
        return;
    }
    const int count = app_->locations[static_cast<std::size_t>(AppState::kSandboxLocation)].levelCount();
    if (count < kSlots && id == buttons_[static_cast<std::size_t>(count)]->id() &&
        buttons_[static_cast<std::size_t>(count)]->type() == LevelSelectorButton::kTypeAddLevel) {
        addLevel();
        return;
    }
    for (int i = 0; i < count && i < kSlots; ++i) {
        if (buttons_[static_cast<std::size_t>(i)]->id() == id) {
            openLevel(i);
            return;
        }
    }
}

void MyContraptionsScene::init() {
    Scene::init();
    const aa::data::SceneTree selector = aa::data::SceneTree(app_->assets->json("ui/scenes/LevelSelectionScene.json"));
    view_ = make<MyContraptionsView>(*app_, tree().view("MyContraptionsView"), selector.view("LevelSelectorButton"));
    view_->setViewName("MyContraptionsView");
    view_->setFrame(root_->frame());
    view_->updateViewAnchors(true, true);
    view_->setVisible(true);
    root_->addSubview(view_);
}

void MyContraptionsScene::activate() {
    // Activate [verified: 0x127370]: the view's refresh flag, then Show (which refreshes), the theme.
    Scene::activate();
    view_->requestRefresh();
    view_->show();
    if (app_->audio) app_->audio->playMusic(kMusicTheme);
}

void MyContraptionsScene::inactivationComplete() {
    Scene::inactivationComplete();
    view_->hide();
}

void MyContraptionsScene::relayout(int, int) {
    resizeRootAndMainView(view_);
    if (view_) view_->relayout();
}

}  // namespace aa::ui
