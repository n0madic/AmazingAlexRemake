// The menu scenes of docs/05 §1 [verified: SplashView / SplashScene, MainMenuView / MainMenuScene,
// ChapterSelectionView / ChapterSelectionScene, LevelSelectionView / LevelSelectorButton /
// LevelSelectionScene, LevelLoadingView / LevelLoadingScene, ChapterCompleteView /
// ChapterComplete3StarsView — decompile + disassembly, 2026-09-14]. Every view builds its subviews from
// the scene JSON exactly as the original's Init does (the named sub-dictionaries, in the same order).
#pragma once

#include "aa/ui/app_state.h"
#include "aa/ui/button.h"
#include "aa/ui/dialogs.h"
#include "aa/ui/scene.h"
#include "aa/ui/scroll_view.h"
#include "aa/ui/view.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

// The scene names (SceneManager::GetScene keys).
namespace scene_names {
constexpr const char* kSplash = "SplashScene";
constexpr const char* kMainMenu = "MainMenuScene";
constexpr const char* kChapterSelection = "ChapterSelectionScene";
constexpr const char* kLevelSelection = "LevelSelectionScene";
constexpr const char* kLevelLoading = "LevelLoadingScene";
constexpr const char* kGame = "GameScene";
constexpr const char* kChapterComplete = "ChapterCompleteScene";
constexpr const char* kChapterComplete3Stars = "ChapterComplete3StarsScene";
}  // namespace scene_names

// A scene whose root view is built from `ui/scenes/<Name>.json`.
class GameSceneBase : public Scene {
public:
    GameSceneBase(UiContext& ctx, AppState& app) : Scene(ctx), app_(&app) {}
    AppState& app() const { return *app_; }
    // The scene JSON (loaded once).
    const aa::data::SceneTree& tree();

protected:
    AppState* app_;
    std::unique_ptr<aa::data::SceneTree> tree_;
};

// --- Splash ----------------------------------------------------------------------------------------

// SplashView: Page0 (the Rovio logo button, a tap skips to the next page) for kSplashPageTime, then
// Page1 (the title background + "Loading…") for another kSplashPageTime. The original held each page
// for 2 s; the remake loads everything before the first frame, so each page is 1 s.
class SplashView : public View, public ButtonDelegate {
public:
    using View::init;
    SplashView(UiContext& ctx, const aa::data::JsonNode& dict);
    void show();
    void update(float dt) override;
    static constexpr float kSplashPageTime = 1.0f;   // the original: 2 s per page
    // IsPageCompleted(n): n · kSplashPageTime < timer.
    bool isPageCompleted(int page) const { return static_cast<float>(page) * kSplashPageTime < timer_; }
    void buttonPressed(int id) override;
    float timer() const { return timer_; }

private:
    Button* page0_ = nullptr;
    ImageView* legal_ = nullptr;
    ImageView* page1_ = nullptr;
    OutlineLabelView* loading_ = nullptr;
    float timer_ = 0.0f;
    bool done_ = false;
    std::vector<std::unique_ptr<View>> owned_;
};

class SplashScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kSplash; }
    void init() override;
    void activate() override;
    void relayout(int width, int height) override;
    bool isPageCompleted(int page) const { return view_ && view_->isPageCompleted(page); }

private:
    SplashView* view_ = nullptr;
};

// --- Main menu -------------------------------------------------------------------------------------

class MainMenuView : public View, public ButtonDelegate, public AnimatorDelegate {
public:
    using View::init;
    MainMenuView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict);
    void show();
    // Hide(true) [verified]: the top panel slides up; its end pushes the Credits scene (ButtonCredits).
    void hide();
    void update(float dt) override;
    void buttonPressed(int id) override;
    void animationFinished(int id) override;
    void relayout() override;
    // The back key at the main menu shows the ExitDialog; its confirmation asks the app to quit, the
    // back key again (or the cross) dismisses it.
    bool keyDown(int key) override;
    Button* playButton() { return play_; }
    Button* creditsButton() { return credits_; }
    MessageDialog* exitDialog() { return exit_; }
    ToggleButton* audioButton() { return audio_; }
    ToggleButton* musicButton() { return music_; }
    SlidingButton* settingsSlider() { return settings_; }
    ImageView* logo() { return logo_; }
    View* panelTop() { return panelTop_; }

private:
    AppState* app_;
    ImageView* background_ = nullptr;
    ImageView* logo_ = nullptr;
    View* panelTop_ = nullptr;
    SlidingButton* settings_ = nullptr;
    ToggleButton* autoShare_ = nullptr;
    ToggleButton* audio_ = nullptr;
    ToggleButton* music_ = nullptr;   // remake: the music-only switch
    Button* credits_ = nullptr;
    Button* play_ = nullptr;
    MessageDialog* exit_ = nullptr;
    std::unique_ptr<MessageDialogDelegate> exitDelegate_;
    int hideAnim_ = 0;   // +0xe8
    bool refresh_ = true;
    std::vector<std::unique_ptr<View>> owned_;
};

class MainMenuScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kMainMenu; }
    void init() override;
    void activate() override;
    void relayout(int width, int height) override;
    MainMenuView* view() const { return view_; }

private:
    MainMenuView* view_ = nullptr;
};

// --- Chapter selection -----------------------------------------------------------------------------

class ChapterSelectionView : public View, public ButtonDelegate, public ScrollViewDelegate, public AnimatorDelegate {
public:
    static constexpr int kBookCount = 5;   // the four chapters and My Contraptions (LotW / WoC dropped)
    using View::init;
    ChapterSelectionView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict);
    void show();
    void hide();
    void refresh();
    void update(float dt) override;
    void buttonPressed(int id) override;
    void scrollViewMoved(int id) override;
    void scrollViewFinishedDecelerating(int id) override;
    void animationFinished(int id) override;
    void relayout() override;
    void scrollToPage(int page);
    // UpdateChapterPosition: the first location that is not fully 3-starred.
    void updateChapterPosition();
    void setReturningFromGame(bool returning) { updatePosition_ = !returning; }
    ScrollView* panel() { return panel_; }
    Button* book(int i) { return books_[static_cast<std::size_t>(i)]; }

private:
    // layoutBooks(): the per-book Button position / scale and the panel's page geometry, derived from the
    // BookWidth/Height/X/Y percentages of PanelRight — called at construction and again from relayout()
    // (the buttons carry no relative/anchor data of their own, so the generic View::relayout() recursion
    // leaves them untouched).
    void layoutBooks();
    AppState* app_;
    ImageView* background_ = nullptr;
    Button* back_ = nullptr;
    ScrollView* panel_ = nullptr;
    std::array<Button*, kBookCount> books_{};
    PageControl* pages_ = nullptr;
    ImageView* totalStars_ = nullptr;
    OutlineLabelView* totalStarsLabel_ = nullptr;
    OutlineLabelView* chapterStarsLabel_ = nullptr;
    ImageView* chapterStars_ = nullptr;
    OutlineLabelView* infoText_ = nullptr;
    std::string customInfo_;   // the My Contraptions page's info text id
    float bookWidthPct_ = 0.0f, bookHeightPct_ = 0.0f, bookXPct_ = 0.0f, bookYPct_ = 0.0f;   // PanelRight/Book*
    Point panelShown_, panelHidden_;
    Point starsShown_, starsHidden_;
    int showAnimation_ = 0;
    int hideAnimation_ = 0;
    int starsShowAnimation_ = 0;
    int starsHideAnimation_ = 0;
    bool refresh_ = false;
    bool updatePosition_ = true;
    bool shown_ = false;   // which of panelShown_/panelHidden_ (starsShown_/starsHidden_) is current
    std::vector<std::unique_ptr<View>> owned_;
};

class ChapterSelectionScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kChapterSelection; }
    void init() override;
    void activate() override;
    void inactivate() override;
    void relayout(int width, int height) override;
    void setReturningFromGame(bool r) { if (view_) view_->setReturningFromGame(r); }
    ChapterSelectionView* view() const { return view_; }

private:
    ChapterSelectionView* view_ = nullptr;
};

// --- Level selection -------------------------------------------------------------------------------

// LevelSelectorButton: frame + thumbnail + number / name + stars of one level slot.
class LevelSelectorButton : public Button {
public:
    // Setup types [verified]: 0 empty slot, 1 campaign level, 3 user level (My Contraptions), 6 the "new
    // level" tile (LEVEL_EMPTY_SLOT); 2 / 4 / 5 are the online lists.
    static constexpr int kTypeEmpty = 0;
    static constexpr int kTypeCampaign = 1;
    static constexpr int kTypeUserLevel = 3;
    static constexpr int kTypeAddLevel = 6;
    using View::init;
    LevelSelectorButton(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict, const Rect& rect);
    // Setup(type, level, state, info): false for a user level whose title could not be read (the file
    // does not load) — the list drops that entry.
    bool setup(int type, int level, const aa::game::LocationState* state, const aa::game::LocationInfo* info);
    // setSlotRect: re-derives this slot's frame (the grid rect from LevelSelectionView's SelectorArea) —
    // both this view's own frame and the LayoutView's, which LevelSelectorButton::LevelSelectorButton
    // sizes to it once at construction — then the rest scale (applyRestScale) and Setup's geometry for
    // the current type (relayoutContents) against it.
    void setSlotRect(const Rect& rect);
    // applyRestScale: reapplies the AssetScalingForWidescreen tweak to the zoom views at the rest scale
    // (restScale()) — the constructor sets it only once, but ctx_->screen.widescreenScaling can flip on a
    // resize (ScreenLayout::compute's pixelScale < 1 rule).
    void applyRestScale();
    int levelIndex() const { return level_; }
    int type() const { return type_; }
    void setThumbImage(const std::string& name);
    // SetTrashCanVisible: only the user-level types (2 / 3) show the trash can.
    void setTrashCanVisible(bool visible);
    ImageView* trashCan() { return trash_; }
    void update(float dt) override;
    void animationFinished(int id) override;
    void zoomIn() override;
    void zoomOut() override;

private:
    static constexpr float kWidescreenScale = 0.89f;
    std::vector<View*> zoomViews();
    float restScale() const;
    // Setup's geometry only (the PanelTop's frame, the centres and pivots), for a live resize.
    void relayoutContents();
    void animateButton();
    int frameId(int level) const { return (level % 4) + 1; }   // GetFrameId: LEVEL_FRAME1..4
    AppState* app_;
    View* layout_ = nullptr;
    View* panelTop_ = nullptr;
    ImageView* thumbEmpty_ = nullptr;
    ImageView* thumbNormal_ = nullptr;
    ImageView* frameEmpty_ = nullptr;
    ImageView* frameNormal_ = nullptr;
    ImageView* trash_ = nullptr;
    ImageView* starOne_ = nullptr;
    ImageView* starTwo_ = nullptr;
    ImageView* starThree_ = nullptr;
    OutlineLabelView* labelName_ = nullptr;
    LabelView* labelNumber_ = nullptr;
    std::string frameImage_;     // "LEVEL_FRAME{0}"
    std::string thumbImage_;     // DEFAULT_THUMBNAIL
    std::string starImage_;
    std::string starEmptyImage_;
    int type_ = 0;
    int level_ = -1;
    int pulseAnimation_ = 0;
    bool pulseUp_ = true;
    std::vector<std::unique_ptr<View>> owned_;
};

class LevelSelectionView : public View, public ButtonDelegate, public ScrollViewDelegate, public AnimatorDelegate {
public:
    static constexpr int kSlots = 0x60;
    static constexpr int kPerPage = 8;
    using View::init;
    LevelSelectionView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict, const aa::data::JsonNode& selectorDict);
    void show(bool animated);
    void hide(bool animated);
    void refresh();
    void refreshThumbs() { refreshThumbs_ = true; }
    void purgeThumbs();
    void update(float dt) override;
    void buttonPressed(int id) override;
    void buttonAboutToBePressed(int id) override;
    void scrollViewMoved(int id) override;
    void scrollViewFinishedDecelerating(int id) override;
    void animationFinished(int id) override;
    void relayout() override;
    void setReturningFromGame(bool r) { returning_ = r; }
    ScrollView* panel() { return panel_; }
    LevelSelectorButton* button(int i) { return buttons_[static_cast<std::size_t>(i)]; }

private:
    // slotRect(i): the grid rect of slot `i` from the SelectorArea percentages — the single source the
    // constructor (building each LevelSelectorButton) and layoutSlots() (relayout()'s re-grid) share.
    Rect slotRect(int i) const;
    void layoutSlots();
    // The panel's content / page size for pageCount_ pages of the current screen size (refresh and relayout).
    void layoutPages();
    AppState* app_;
    View* background_ = nullptr;
    OutlineLabelView* title_ = nullptr;
    Button* back_ = nullptr;
    ScrollView* panel_ = nullptr;
    PageControl* pages_ = nullptr;
    std::array<LevelSelectorButton*, kSlots> buttons_{};
    float areaXPct_ = 0.0f, areaYPct_ = 0.0f, areaWPct_ = 0.0f, areaHPct_ = 0.0f;   // SelectorArea/Relative
    int showAnimation_ = 0;
    int hideAnimation_ = 0;
    int pageCount_ = 1;   // refresh()'s page count, for relayout()'s re-sized pages
    bool refresh_ = true;
    bool refreshThumbs_ = false;
    bool returning_ = false;
    std::vector<std::unique_ptr<View>> owned_;
};

class LevelSelectionScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kLevelSelection; }
    void init() override;
    void activate() override;
    void activationComplete() override;
    void inactivate() override;
    void relayout(int width, int height) override;
    void purgeThumbs() { if (view_) view_->purgeThumbs(); }
    void setReturningFromGame(bool r) { if (view_) view_->setReturningFromGame(r); }
    LevelSelectionView* view() const { return view_; }

private:
    LevelSelectionView* view_ = nullptr;
};

// --- Level loading ---------------------------------------------------------------------------------

// The 0.7 s "Loading…" screen: the level is loaded when the scene activates; the show animation's end
// removes any GameScene from the stack and pushes a fresh one (loading location 1), or pops back to the
// chapter selection (location 8).
class LevelLoadingScene : public GameSceneBase, public AnimatorDelegate {
public:
    static constexpr int kLocationCampaign = 1;
    static constexpr int kLocationSandbox = 2;        // a My Contraptions level (the level index)
    static constexpr int kLocationNewSandbox = 3;     // a new sandbox level (CreateNewSandbox)
    static constexpr int kLocationBackToChapters = 8;
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kLevelLoading; }
    void init() override;
    void activate() override;
    void activationComplete() override;
    void inactivate() override;
    void inactivationComplete() override;
    void update(float dt) override;
    void relayout(int width, int height) override;
    void setLoadingLocation(int location, int level) {
        loadingLocation_ = location;
        level_ = level;
    }
    int loadingLocation() const { return loadingLocation_; }
    void animationFinished(int id) override;
    // LevelLoadingView::KeyDown [verified: 0x13e384] consumes every key: the back key cannot pop the loading
    // screen while the level loads.
    bool keyDown(int) override { return true; }

private:
    View* view_ = nullptr;
    ImageView* background_ = nullptr;
    OutlineLabelView* label_ = nullptr;
    int loadingLocation_ = 0;
    int level_ = -1;
    int showAnimation_ = 0;
    bool pendingPush_ = false;
};

// --- Chapter complete ------------------------------------------------------------------------------

class ChapterCompleteScene : public GameSceneBase, public ButtonDelegate {
public:
    ChapterCompleteScene(UiContext& ctx, AppState& app, bool threeStars) : GameSceneBase(ctx, app), threeStars_(threeStars) {}
    const char* name() const override { return threeStars_ ? scene_names::kChapterComplete3Stars : scene_names::kChapterComplete; }
    void init() override;
    void activate() override;
    void relayout(int width, int height) override;
    void buttonPressed(int id) override;
    bool keyDown(int key) override;

private:
    bool threeStars_;
    View* view_ = nullptr;
    ImageView* background_ = nullptr;
    ImageView* items_ = nullptr;
    OutlineLabelView* congratulations_ = nullptr;
    OutlineLabelView* unlocked_ = nullptr;
    Button* next_ = nullptr;
    std::string itemsImage_;
};

// UI::showChapterComplete [verified]: after a level of a chapter is completed, pushes the chapter-complete
// scene once (all levels done) or the 3-stars scene once (all levels 3-starred); returns true when a
// scene was pushed.
bool showChapterComplete(SceneManager& manager, AppState& app);

// The location (chapter) book / panel names by index.
const char* chapterBookName(int index);
const char* chapterCompletionImage(int index);

}  // namespace aa::ui
