// UI::ScrollView + TouchFilter and UI::PageControl [verified: ScrollView::Init / Update / Touches* /
// EndDragScrolling / SetActivePage / GetActivePage / ScrollToNextPageInDirection / SetContentOffset /
// SetContentSize / GetNumberOfPages / HandleTapScrolling, TouchFilter::*, PageControl::RefreshPages /
// UpdatePageSprite / SetActivePage — decompile, 2026-09-14].
#pragma once

#include "aa/ui/animator.h"
#include "aa/ui/scene.h"
#include "aa/ui/view.h"

#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

class ScrollView;

// ScrollViewDelegate [verified: the vtable order and the call sites]: StartedDecelerating at the end of
// EndDragScrolling (the finger let go), FinishedDecelerating when the scroll animation ends, Moved from
// Update whenever the offset changed.
class ScrollViewDelegate {
public:
    virtual ~ScrollViewDelegate() = default;
    virtual void scrollViewStartedDecelerating(int) {}
    virtual void scrollViewFinishedDecelerating(int) {}
    virtual void scrollViewMoved(int) {}
};

// ScrollView::TouchFilter: the touch that started the drag, its latest position and the timing.
class TouchFilter {
public:
    void reset();
    void notifyTouch(const TouchEvent& e);
    void notifyMove(const TouchEvent& e);
    void notifyUp(const TouchEvent& e);
    bool isHandling() const { return startId_ != -1; }
    bool isHandling(const TouchEvent& e) const { return startId_ == e.id; }
    bool isDragging() const { return startId_ != -1 && startId_ != lastId_; }
    Point drag() const { return Point{last_.x - start_.x, last_.y - start_.y}; }
    Point dragDiff() const { return diff_; }
    Point tap() const { return Point{(start_.x + last_.x) * 0.5f, (start_.y + last_.y) * 0.5f}; }
    float touchTime() const { return static_cast<float>(lastTime_ - startTime_); }
    float touchTimeDiff() const { return timeDiff_; }
    bool didTap() const { return tapped_; }

private:
    int startId_ = -1;
    Point start_;
    double startTime_ = 0.0;
    int lastId_ = -1;
    Point last_;
    double lastTime_ = 0.0;
    Point diff_;
    float timeDiff_ = 0.0f;
    bool tapped_ = false;
};

class ScrollView : public View, public AnimatorDelegate {
public:
    using View::init;
    explicit ScrollView(UiContext& ctx);
    // Init: HorizontalScrolling, VerticalScrolling, Paging; the content size defaults to the frame.
    void init(const aa::data::JsonNode& dict) override;
    void setDelegate(ScrollViewDelegate* d) { delegate_ = d; }
    void addSubview(View* v) override;   // subviews go into the content view
    View* contentView() { return content_.get(); }
    void setContentSize(Size s);
    Size contentSize() const { return contentSize_; }
    void setContentOffset(Point offset, bool animated);
    Point contentOffset() const { return offset_; }
    void setPageSize(Size s) { pageSize_ = s; }
    Size pageSize() const;
    void setPaging(bool on) { paging_ = on; }
    void setHorizontalScrolling(bool on) { horizontal_ = on; }
    void setVerticalScrolling(bool on) { vertical_ = on; }
    void setMaxRubberbandFactor(float f) { rubberband_ = f; }
    void setPageControlAreaLength(float len) { pageAreaLength_ = len; }
    void setScrollingByTap(bool on) { scrollingByTap_ = on; }
    int numberOfPages() const;
    int activePage() const;
    void setActivePage(int page, bool animated);
    void scrollToNextPageInDirection(Point velocity);

    void update(float dt) override;
    void touchesStarted(const TouchEvent& e) override;
    void touchesMovedInside(const TouchEvent& e) override;
    void touchesMovedOutside(const TouchEvent& e) override { touchesMovedInside(e); }
    void touchesFinishedInside(const TouchEvent& e) override;
    void touchesFinishedOutside(const TouchEvent& e) override { touchesFinishedInside(e); }
    void touchesCancel(const TouchEvent& e) override;
    // Remake-only: the desktop wheel / trackpad. A paging view turns one page per swipe (a swipe's samples
    // and its momentum tail are one stream; a new swipe shows as a reversal or a pause then a small sample —
    // the constants in scroll_view.cpp); a free-scrolling one follows every sample and ends the gesture like
    // a drag (the delegate's Started / FinishedDecelerating). A horizontal-only view also takes the wheel.
    bool wheelScrolled(Point delta) override;
    void animationFinished(int id) override;
    void animationCanceled(int id) override;

private:
    TouchEvent toContent(const TouchEvent& e) const;
    void endDragScrolling();
    void handleTapScrolling(Point p);
    bool isInLeft(Point p) const;
    bool isInRight(Point p) const;
    bool isInTop(Point p) const;
    bool isInBottom(Point p) const;

    std::unique_ptr<View> content_;
    EventHandler childEvents_;
    TouchFilter filter_;
    ScrollViewDelegate* delegate_ = nullptr;
    Size contentSize_;
    Point offset_;
    Point lastOffset_;
    Size pageSize_{-1.0f, -1.0f};
    Point velocity_;
    bool horizontal_ = true;
    bool vertical_ = true;
    bool paging_ = false;
    bool forwarding_ = false;
    bool scrollingByTap_ = false;
    float pageAreaLength_ = 0.0f;
    float rubberband_ = 1.0f;
    int scrollAnimation_ = 0;
    // Remake-only, the wheel (see wheelScrolled): a free-scrolling gesture in progress; the seconds since the
    // last sample; a paging view's locked stream (its direction and axis), the candidate's sum, the turn lock.
    bool wheelGesture_ = false;
    float wheelIdle_ = 1.0f;
    bool wheelStream_ = false;
    float wheelDir_ = 0.0f;
    bool wheelAxisX_ = true;
    float wheelAccum_ = 0.0f;
    float wheelLock_ = 0.0f;
};

// UI::PageControl: the page dots (ImageActive / ImageInactive, ImagePadding percent of the screen
// width) and the optional page number label.
class PageControl : public View {
public:
    using View::init;
    static constexpr int kMaxPages = 30;
    explicit PageControl(UiContext& ctx);
    void init(const aa::data::JsonNode& dict) override;
    void setPageCount(int n);
    int pageCount() const { return pageCount_; }
    void setActivePage(int page);
    int activePage() const { return activePage_; }
    bool isActivePage(int page) const { return activePage_ == page; }
    void setContentPadding(float percent);
    void setImageForState(const std::string& image, bool active, bool newContent);
    void setNewContentIndicator(int page, bool on);
    void setPageNumberVisible(bool on) {
        showNumber_ = on;
        dirty_ = true;
    }
    void setStartPageForNumbering(int page) { numberStart_ = page; }
    void update(float dt) override;
    // The dots' sizes/positions come from ctx_->resources->imageSize() (uiScale-dependent) and padding_
    // (screen-width-percent) at whatever moment dirty_ was last cleared — a live resize changes both
    // without touching pageCount_ or any other dirty_-setting setter, so nothing else re-arms it or
    // redoes padding_'s percent-to-px conversion; refreshPages() itself stays deferred to Update() (it
    // removes/re-adds every dot subview, unsafe mid-relayout-recursion).
    void recomputeAutoSize() override {
        padding_ = ctx_->screen.nativeWidth * 0.01f * paddingPercent_;
        dirty_ = true;
    }

private:
    void refreshPages();
    void updatePageSprite(int page);
    std::vector<std::unique_ptr<ImageView>> dots_;
    std::unique_ptr<OutlineLabelView> label_;
    std::string images_[4];   // inactive, active, new-inactive, new-active
    bool newContent_[kMaxPages] = {};
    int activePage_ = 0;
    int pageCount_ = 0;
    float padding_ = 0.0f;
    float paddingPercent_ = 0.0f;   // the un-scaled value behind padding_ (SetContentPadding's `percent`)
    bool dirty_ = true;
    bool showNumber_ = true;
    int numberStart_ = 0;
};

}  // namespace aa::ui
