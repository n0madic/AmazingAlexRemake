// The scenes of M6 besides the editor (docs/06 §1.3) [verified: ComicScene::setComicView / Update /
// InactivationComplete, ComicView::Init / Update / ButtonPressed / KeyDown / Show / Hide, UI::showChapterComic
// and its four callers, CreditsScene::Init / Activate / Update, CreditsView::Init / Show / Hide / Update /
// ButtonPressed / AnimationFinished / ScrollView*Decelerating, MyContraptionsScene::Activate /
// ActivationComplete / InactivationComplete / ShowParsingError, MyContraptionsView::Init / Refresh /
// ButtonPressed / Show / Hide / Update / EnableLevelDeleting / ShowLevelButtonTrashCans / MessageConfirmed /
// MessageCanceled / HideAllDialogs, MainMenuView::AnimationFinished — decompile + disassembly, 2026-09-14].
#pragma once

#include "aa/ui/dialogs.h"
#include "aa/ui/menu_scenes.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

namespace scene_names {
constexpr const char* kComic = "ComicScene";
constexpr const char* kCredits = "CreditsScene";
constexpr const char* kMyContraptions = "MyContraptionsScene";
}  // namespace scene_names

// --- Comics ----------------------------------------------------------------------------------------

// One comic page: the frames appear one by one (2 s apart, or on a tap); the next button shows a second
// after the last frame. Chapter comic type 0 = begin (the back key pops), 1 = end (the back key is inert).
class ComicView : public View, public ButtonDelegate {
public:
    static constexpr float kFrameInterval = 2.0f;   // Update: the next frame after 2 s
    static constexpr float kNextDelay = 1.0f;       // the next button 1 s after the last frame
    using View::init;
    ComicView(UiContext& ctx, const aa::data::JsonNode& dict);
    void setComicType(int type) { type_ = type; }
    int comicType() const { return type_; }
    void show();
    void hide();
    void update(float dt) override;
    void buttonPressed(int id) override;
    bool keyDown(int key) override;
    void relayout() override;
    int shownFrames() const { return shown_; }
    int frameCount() const { return static_cast<int>(frames_.size()); }
    Button* nextButton() { return next_; }
    Button* tapArea() { return tapArea_; }

private:
    void showNextFrame();
    ImageView* background_ = nullptr;
    Button* tapArea_ = nullptr;
    Button* next_ = nullptr;
    std::vector<ImageView*> frames_;
    int shown_ = 0;        // +0xe0
    float timer_ = 0.0f;   // +0xe4
    int type_ = 0;         // +0x1200
    std::vector<std::unique_ptr<View>> owned_;
};

class ComicScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kComic; }
    void init() override;
    // setComicView(type, location): a fresh ComicView from "ComicViewBegin{loc+1}" / "ComicViewEnd{loc+1}".
    void setComicView(int type, int location);
    void activate() override;
    void relayout(int width, int height) override;
    ComicView* view() const { return view_; }

private:
    ComicView* view_ = nullptr;
    std::unique_ptr<ComicView> current_;
};

// UI::showChapterComic(type): type 0 pushes the begin comic once per location (LocationState.visited),
// type 1 the end comic once (finished); the flag is set and the location saved either way. Returns true
// when the flag was clear (a comic was pushed, or would have been without the scene).
bool showChapterComic(SceneManager& manager, AppState& app, int type);

// --- Credits ---------------------------------------------------------------------------------------

class CreditsView : public View, public ButtonDelegate, public ScrollViewDelegate, public AnimatorDelegate {
public:
    static constexpr float kInitialOffset = 5.0f;     // +0xe4
    static constexpr float kScrollRate = 0.065f;      // Update: 6.5 % of the screen height per second
    static constexpr float kSlide = 0.3f;             // the back button's slide
    // LabelVersion's text: the original showed "v{0}" (TEXT_VERSION) with Version::Get = "1.0.5"; the remake
    // is written from scratch, so the line names its author and the version it is based on instead.
    static constexpr const char* kVersionText = "Remake by n0madic\nbased on v1.0.5";
    using View::init;
    CreditsView(UiContext& ctx, const aa::data::JsonNode& dict);
    void show(bool animated);
    void hide(bool animated);
    bool keyDown(int key) override;   // the back key leaves like the back button: through hide(true)
    void update(float dt) override;
    void buttonPressed(int id) override;
    void animationFinished(int id) override;
    void scrollViewStartedDecelerating(int id) override;
    void scrollViewFinishedDecelerating(int id) override;
    void relayout() override;
    ScrollView* panel() { return panel_; }
    Button* backButton() { return back_; }
    bool autoScrolling() const { return autoScroll_; }
    OutlineLabelView* versionLabel() { return version_; }

private:
    ImageView* background_ = nullptr;
    Button* back_ = nullptr;
    ScrollView* panel_ = nullptr;
    OutlineLabelView* version_ = nullptr;
    ImageView* footer_ = nullptr;
    std::vector<OutlineLabelView*> labels_;
    Point backShown_, backHidden_;   // +0x9d0 / +0x9d8
    bool shown_ = false;             // back_ at backShown_ (true) vs. backHidden_ (false)
    int showAnim_ = 0;               // +0xec
    int hideAnim_ = 0;               // +0xe8
    bool autoScroll_ = false;        // +0x2718
    std::vector<std::unique_ptr<View>> owned_;
};

class CreditsScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kCredits; }
    void init() override;
    void activate() override;
    void relayout(int width, int height) override;
    CreditsView* view() const { return view_; }

private:
    CreditsView* view_ = nullptr;
};

// --- My Contraptions -------------------------------------------------------------------------------

class MyContraptionsView : public View, public ButtonDelegate, public ScrollViewDelegate, public MessageDialogDelegate, public InfoDialogDelegate {
public:
    static constexpr int kSlots = LevelSelectionView::kSlots;
    static constexpr int kPerPage = LevelSelectionView::kPerPage;
    using View::init;
    MyContraptionsView(UiContext& ctx, AppState& app, const aa::data::JsonNode& dict, const aa::data::JsonNode& selectorDict);
    void show();
    void hide();
    void refresh();
    void update(float dt) override;
    void buttonPressed(int id) override;
    void scrollViewMoved(int id) override;
    void messageConfirmed(int dialogId) override;
    void messageCanceled(int dialogId) override;
    void relayout() override;
    void enableLevelDeleting(bool on);
    void showLevelButtonTrashCans(bool on);
    void showParsingError();
    void hideAllDialogs();
    // MyContraptionsScene::Activate sets the view's refresh flag before Show.
    void requestRefresh() { refresh_ = true; }
    LevelSelectorButton* button(int i) { return buttons_[static_cast<std::size_t>(i)]; }
    ToggleButton* trashButton() { return trash_; }
    InfoDialog* legalDialog() { return legal_; }
    MessageDialog* parsingErrorDialog() { return parsingError_; }
    MessageDialog* storageFullDialog() { return storageFull_; }
    ScrollView* panel() { return panel_; }

private:
    void openLevel(int level);
    void addLevel();
    // slotRect(i) / layoutSlots(): the same SelectorArea grid factoring as LevelSelectionView — see there.
    Rect slotRect(int i) const;
    void layoutSlots();
    void layoutPages();
    float areaXPct_ = 0.0f, areaYPct_ = 0.0f, areaWPct_ = 0.0f, areaHPct_ = 0.0f;
    int pageCount_ = 1;   // refresh()'s page count, for relayout()'s re-sized pages
    AppState* app_;
    View* background_ = nullptr;
    OutlineLabelView* title_ = nullptr;
    Button* back_ = nullptr;
    Button* add_ = nullptr;
    ToggleButton* trash_ = nullptr;
    ScrollView* panel_ = nullptr;
    PageControl* pages_ = nullptr;
    std::array<LevelSelectorButton*, kSlots> buttons_{};
    MessageDialog* parsingError_ = nullptr;
    MessageDialog* storageFull_ = nullptr;
    InfoDialog* legal_ = nullptr;
    bool refresh_ = false;   // +0xec
    // Remake: the parsing error is raised by the loading scene while this view is hidden under it, and
    // show() (the way back) hides every dialog first — so the request waits for the next show().
    bool shown_ = false;
    bool parsingErrorPending_ = false;
    std::vector<std::unique_ptr<View>> owned_;
};

class MyContraptionsScene : public GameSceneBase {
public:
    using GameSceneBase::GameSceneBase;
    const char* name() const override { return scene_names::kMyContraptions; }
    void init() override;
    void activate() override;
    void inactivationComplete() override;
    void relayout(int width, int height) override;
    void showParsingError() { if (view_) view_->showParsingError(); }
    MyContraptionsView* view() const { return view_; }

private:
    MyContraptionsView* view_ = nullptr;
};

}  // namespace aa::ui
