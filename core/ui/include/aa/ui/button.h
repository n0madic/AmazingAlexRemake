// UI::Button / ToggleButton / SlidingButton [verified: Button::Button / Init / SetState / Update /
// Touches* / ZoomIn / ZoomOut / AnimationFinished / IsPointInView / PlayPress / PlayRelease,
// ToggleButton::Init / SetChecked / Update / DelegateCalled, SlidingButton::Init / ShowMenu / HideMenu /
// LayoutMenuButtons / ButtonPressed — decompile + disassembly, 2026-09-14].
#pragma once

#include "aa/ui/animator.h"
#include "aa/ui/view.h"

#include <array>
#include <string>
#include <vector>

namespace aa::ui {

class Button;

class ButtonDelegate {
public:
    virtual ~ButtonDelegate() = default;
    // ButtonPressed(id): after the release animation; ButtonAboutToBePressed(id): at the release.
    virtual void buttonPressed(int) {}
    virtual void buttonAboutToBePressed(int) {}
};

// Button states (+0xF0): 0 disabled, 1 normal, 2 highlighted (pressed), 3 selected (released, the
// zoom-in animation runs).
namespace button_state {
constexpr int kDisabled = 0;
constexpr int kNormal = 1;
constexpr int kHighlighted = 2;
constexpr int kSelected = 3;
}  // namespace button_state

class Button : public View, public AnimatorDelegate {
public:
    using View::init;
    explicit Button(UiContext& ctx);
    // Button::Init: ImageBackground, ImageState{Normal,Highlighted,Selected,Disabled} (+ Localized…),
    // TextState* / TextFont, ResizeParent.
    void init(const aa::data::JsonNode& dict) override;
    void setDelegate(ButtonDelegate* d) { delegate_ = d; }
    // SetState: 0 disables (interaction off, alpha 0.5 when ChangeAlpha), else enables.
    virtual void setState(int state);
    int state() const { return state_; }
    void setImageForState(const std::string& image, int state, bool localized);
    void setBackground(const std::string& image, bool localized = false);
    void setSilent(bool silent) { silent_ = silent; }
    void setChangeAlpha(bool on) { changeAlpha_ = on; }
    void setAnimateOnlyBackground(bool on) { animateOnlyBackground_ = on; }
    void setRotateBackground(bool on) { rotateBackground_ = on; }
    void setScale(float s) override;
    void setTextForState(const std::string& textId, int state);
    void setFont(const std::string& font);
    ImageView& backgroundView() { return *background_; }
    ImageView& imageView() { return *stateImage_; }
    // ResizeFrameToBackground: the frame takes the background image size.
    void resizeFrameToBackground();
    bool isBackgroundSet() const { return !backgroundName_.empty(); }
    // Hit-test in the (1.15×) zoomed area while highlighted / selected.
    bool isPointInView(Point p) const override;
    void touchesStarted(const TouchEvent& e) override;
    void touchesFinishedInside(const TouchEvent& e) override;
    void touchesFinishedOutside(const TouchEvent& e) override;
    void touchesMovedEnter(const TouchEvent& e) override;
    void touchesMovedExit(const TouchEvent& e) override;
    void touchesCancel(const TouchEvent& e) override;
    void update(float dt) override;
    void draw(Renderer& renderer, const Rect& rect) override;
    void animationFinished(int id) override;
    // ZoomOut: the image views grow to 1.15 in 0.1 s (the press); ZoomIn: back to 1.0 in 0.05 s
    // (LevelSelectorButton animates its own views instead).
    virtual void zoomOut();
    virtual void zoomIn();
    // SetOverlayForState(image, state, offset %): an image centred on the button (+ offset % of the
    // frame) while in `state` — the chapter books' lock [verified].
    void setOverlayForState(const std::string& image, int state, Point offsetPercent);
    void playPress();
    void playRelease();
    // DelegateCalled: the hook a subclass overrides when the press completes (ToggleButton flips).
    virtual void delegateCalled() {}
    // Re-derives the frame Init/SetBackground sized from a sprite's native pixel dimensions at the
    // ctx_->resources scale then current — a live resize changes that scale (ResourceProxy::setUiScale),
    // so the frame this button and stateImage_/overlay_ were sized to goes stale without this.
    void recomputeAutoSize() override;

protected:
    void initSubviews(const aa::data::JsonNode& dict);
    std::vector<View*> zoomViews();
    std::unique_ptr<ImageView> background_;
    std::unique_ptr<ImageView> stateImage_;
    std::unique_ptr<ImageView> overlay_;
    std::unique_ptr<OutlineLabelView> label_;
    std::array<std::string, 4> stateImages_;
    std::array<bool, 4> stateLocalized_{};
    std::array<std::string, 4> stateTexts_;
    std::array<std::string, 4> stateOverlays_;
    // The raw percent SetOverlayForState was given — converted to px against this view's own frame_ at
    // Update()'s dirty_ processing time (not cached, unlike most percent-driven values here), since a
    // book button's frame_ isn't current until ChapterSelectionView::layoutBooks() has run, well after
    // recomputeAutoSize()'s pass.
    std::array<Point, 4> overlayOffsetPercent_{};
    std::string backgroundName_;
    bool backgroundLocalized_ = false;
    // Init's ResizeParent (default true): whether the normal-state image also resizes the button's own
    // frame_, or only stateImage_ (kept so recomputeAutoSize() can redo the same choice).
    bool resizeParent_ = true;
    std::string textFont_;
    ButtonDelegate* delegate_ = nullptr;
    int state_ = button_state::kNormal;
    bool dirty_ = true;
    int trackedTouch_ = -1;
    int zoomInAnimation_ = 0;
    int zoomOutAnimation_ = 0;
    bool animateOnlyBackground_ = false;
    bool rotateBackground_ = false;
    bool changeAlpha_ = true;
    bool silent_ = false;
    float selectedTimer_ = 0.0f;
    static int processedTouchId_;
};

// UI::ToggleButton: the On / Off image sets, swapped by `checked`; a press flips it.
class ToggleButton : public Button {
public:
    using View::init;
    explicit ToggleButton(UiContext& ctx) : Button(ctx) {}
    void init(const aa::data::JsonNode& dict) override;
    void setChecked(bool on) {
        checked_ = on;
        toggleDirty_ = true;
    }
    bool isChecked() const { return checked_; }
    void setToggleImage(const std::string& image, int state, bool on);
    void update(float dt) override;
    void delegateCalled() override {
        toggleDirty_ = true;
        checked_ = !checked_;
    }
    void recomputeAutoSize() override;

private:
    bool checked_ = false;
    bool toggleDirty_ = false;
    std::array<std::string, 8> images_;   // [state] off, [4 + state] on
};

// UI::SlidingButton: a button that slides a column of menu buttons out (Direction DOWN / UP).
class SlidingButton : public View, public ButtonDelegate, public AnimatorDelegate {
public:
    using View::init;
    explicit SlidingButton(UiContext& ctx);
    void init(const aa::data::JsonNode& dict) override;
    void setDelegate(ButtonDelegate* d) { delegate_ = d; }
    void addMenuButton(Button* b);
    void setMenuDirection(bool down) { down_ = down; }
    void setMenuItemHeight(float h) { itemHeight_ = h; }
    void setBackground(const std::string& image);
    View& menu() { return *menu_; }
    Rect openMenuFrame() const;
    Rect closedMenuFrame() const;
    void showMenu(bool animated);
    void hideMenu(bool animated);
    bool isMenuOpen() const { return menuOpen_; }
    Button& button() { return *button_; }
    void update(float dt) override;
    bool isPointInView(Point p) const override;
    void buttonPressed(int id) override;
    void animationFinished(int id) override;
    void animationCanceled(int id) override;
    // This view's own frame (SetBackground copies button_'s AutoResize'd background size) and menu_'s
    // (open/closed frames both derived from it) go stale after a live resize without these.
    void recomputeAutoSize() override;
    void relayout() override;

private:
    void layoutMenuButtons();
    std::unique_ptr<Button> button_;
    std::unique_ptr<View> menu_;
    ButtonDelegate* delegate_ = nullptr;
    bool down_ = false;   // SlideButtonDirection: 0 = UP (the constructor's default), 1 = DOWN
    int animation_ = 0;
    bool menuOpen_ = false;
    float itemHeight_ = 0.0f;
    bool laidOut_ = false;
};

}  // namespace aa::ui
