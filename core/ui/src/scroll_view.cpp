#include "aa/ui/scroll_view.h"

#include <algorithm>
#include <cmath>

namespace aa::ui {

namespace {
constexpr float kDragCancelDist2 = 400.0f;      // a 20 px drag cancels the child touch
constexpr float kTapMaxTime = 10.0f;
constexpr float kScrollDuration = 0.3f;          // 0x3e99999a
constexpr float kFlingFactor = 0.3f;
constexpr float kPageFlingVelocity = 1250.0f;    // px/s
constexpr int kCurveEaseOut = 2;
// Remake-only (the desktop wheel / trackpad), tuned on a macOS trackpad log (2026-09-15). A free-scrolling
// view's gesture ends after kWheelGestureGap seconds without a sample and moves kWheelStepRate of the screen
// height per unit. A paging view turns one page per swipe: the samples of a swipe and of its momentum tail
// (a second or two) arrive every ~16 ms with the same sign and, once the page is turned, are a locked stream.
// A new swipe shows as a reversal, or as a pause of ≥ kWheelStreamGap followed by a small sample
// (< kWheelRestartMax: the OS stops the momentum when the finger lands, the new swipe ramps up from ~0.2 —
// whereas a dropped frame inside the momentum resumes at the old magnitude). The candidate that follows
// turns the page once its samples sum to kWheelTurn (a mouse notch is 1.0 at once; the sparse 0.1-samples
// at a tail's very end never get there). kWheelTurnLock after a turn swallows the rest of that swipe — its
// finger-lift gap included — while still letting a swipe 0.4 s later through.
constexpr float kWheelGestureGap = 0.15f;
constexpr float kWheelStepRate = 0.04f;
constexpr float kWheelStreamGap = 0.03f;
constexpr float kWheelRestartMax = 1.0f;
constexpr float kWheelTurn = 1.0f;
constexpr float kWheelTurnLock = 0.35f;
}  // namespace

// --- TouchFilter -----------------------------------------------------------------------------------

void TouchFilter::reset() {
    startId_ = -1;
    start_ = Point{};
    startTime_ = 0.0;
    lastId_ = -1;
    last_ = Point{};
    lastTime_ = 0.0;
    diff_ = Point{};
    timeDiff_ = 0.0f;
    tapped_ = false;
}

void TouchFilter::notifyTouch(const TouchEvent& e) {
    startId_ = e.id;
    start_ = e.position;
    startTime_ = e.time;
    lastId_ = -1;
    last_ = e.position;
    lastTime_ = e.time;
    tapped_ = false;
}

void TouchFilter::notifyMove(const TouchEvent& e) {
    if (e.id != startId_) return;
    const Point prev = lastId_ == -1 ? start_ : last_;
    const double prevTime = lastId_ == -1 ? startTime_ : lastTime_;
    diff_ = Point{e.position.x - prev.x, e.position.y - prev.y};
    timeDiff_ = static_cast<float>(e.time - prevTime);
    lastId_ = e.id;
    last_ = e.position;
    lastTime_ = e.time;
}

void TouchFilter::notifyUp(const TouchEvent& e) {
    if (e.id != startId_) return;
    lastId_ = e.id;
    last_ = e.position;
    lastTime_ = e.time;
    startId_ = -1;
    lastId_ = -1;
    tapped_ = true;
}

// --- ScrollView ------------------------------------------------------------------------------------

ScrollView::ScrollView(UiContext& ctx) : View(ctx), content_(std::make_unique<View>(ctx)) {
    content_->setViewName("ScrollContent");
    childEvents_.setRootView(content_.get());
}

void ScrollView::init(const aa::data::JsonNode& dict) {
    View::init(dict);
    if (contentSize_.w == 0.0f || contentSize_.h == 0.0f) contentSize_ = size();
    content_->init();
    View::addSubview(content_.get());
    content_->setInteraction(false);
    setClipSubviews(true);
    if (dict.isNull()) return;
    if (dict.has("HorizontalScrolling")) horizontal_ = dict.getBool("HorizontalScrolling", true);
    if (dict.has("VerticalScrolling")) vertical_ = dict.getBool("VerticalScrolling", true);
    if (dict.has("Paging")) paging_ = dict.getBool("Paging", false);
}

void ScrollView::addSubview(View* v) { content_->addSubview(v); }

Size ScrollView::pageSize() const {
    if (pageSize_.w < 0.0f) return size();
    return pageSize_;
}

void ScrollView::setContentSize(Size s) {
    if (contentSize_.w == s.w && contentSize_.h == s.h) return;
    if (scrollAnimation_ != 0) {
        ctx_->animator->cancelAnimation(scrollAnimation_);
        scrollAnimation_ = 0;
        if (paging_) setActivePage(activePage(), true);
    }
    content_->setFrame(Rect{-offset_.x, -offset_.y, s.w, s.h});
    contentSize_ = s;
}

void ScrollView::setContentOffset(Point offset, bool animated) {
    if (!animated) {
        if (scrollAnimation_ != 0) {
            ctx_->animator->cancelAnimation(scrollAnimation_);
            scrollAnimation_ = 0;
        }
        offset_ = offset;
        return;
    }
    if (scrollAnimation_ != 0) {
        ctx_->animator->cancelAnimation(scrollAnimation_);
        scrollAnimation_ = 0;
    }
    AnimationParameters p = AnimationParameters::fromView(*content_);
    p.frame.x = -offset.x;
    p.frame.y = -offset.y;
    p.curve = kCurveEaseOut;
    p.duration = kScrollDuration;
    p.repeat = 1;
    scrollAnimation_ = ctx_->animator->animate(content_.get(), p, this);
}

int ScrollView::numberOfPages() const {
    const Size ps = pageSize();
    const float len = horizontal_ ? contentSize_.w : contentSize_.h;
    const float page = horizontal_ ? ps.w : ps.h;
    int n = page > 0.0f ? static_cast<int>(len / page) : 1;
    return n < 1 ? 1 : n;
}

int ScrollView::activePage() const {
    const Size ps = pageSize();
    const float page = horizontal_ ? ps.w : ps.h;
    const float off = horizontal_ ? offset_.x : offset_.y;
    int p = page > 0.0f ? static_cast<int>((off + page * 0.5f) / page) : 0;
    if (p < 0) p = 0;
    else if (p >= numberOfPages()) p = numberOfPages() - 1;
    return p;
}

void ScrollView::setActivePage(int page, bool animated) {
    if (!paging_) return;
    if (page < 0) page = 0;
    else if (page >= numberOfPages()) page = numberOfPages() - 1;
    const Size ps = pageSize();
    Point target = offset_;
    if (horizontal_) {
        target.x = std::min(static_cast<float>(page) * ps.w, contentSize_.w);
        if (target.x < 0.0f) target.x = 0.0f;
    } else {
        target.y = std::min(static_cast<float>(page) * ps.h, contentSize_.h);
        if (target.y < 0.0f) target.y = 0.0f;
    }
    setContentOffset(target, animated);
}

void ScrollView::scrollToNextPageInDirection(Point velocity) {
    const float v = horizontal_ ? velocity.x : velocity.y;
    int dir = 0;
    if (v > kPageFlingVelocity) dir = -1;
    else if (v < -kPageFlingVelocity) dir = 1;
    setActivePage(activePage() + dir, true);
}

void ScrollView::update(float dt) {
    View::update(dt);
    if (scrollAnimation_ != 0) {
        offset_ = Point{-content_->frame().x, -content_->frame().y};
    } else if (!paging_) {
        if (horizontal_) offset_.x = std::max(0.0f, std::min(offset_.x, contentSize_.w - frame_.w));
        if (vertical_) offset_.y = std::max(0.0f, std::min(offset_.y, contentSize_.h - frame_.h));
    } else {
        const Size ps = pageSize();
        if (horizontal_) {
            const float pad = ps.w * rubberband_ * 0.5f;
            offset_.x = std::max(-pad, std::min(offset_.x, (contentSize_.w - ps.w) + pad));
        } else if (vertical_) {
            const float pad = ps.h * rubberband_ * 0.5f;
            offset_.y = std::max(-pad, std::min(offset_.y, (contentSize_.h - ps.h) + pad));
        }
    }
    content_->setFrame(Rect{-offset_.x, -offset_.y, contentSize_.w, contentSize_.h});
    if (delegate_ && (lastOffset_.x != offset_.x || lastOffset_.y != offset_.y)) delegate_->scrollViewMoved(id());
    lastOffset_ = offset_;
    wheelIdle_ += dt;
    wheelLock_ = std::max(0.0f, wheelLock_ - dt);
    if (wheelGesture_ && wheelIdle_ >= kWheelGestureGap) {
        // A free-scrolling view settles like a released finger (a zero fling, so the delegate hears
        // FinishedDecelerating).
        wheelGesture_ = false;
        endDragScrolling();
    }
}

TouchEvent ScrollView::toContent(const TouchEvent& e) const {
    // The event position converted from the scene root into the content view's coordinates.
    TouchEvent c = e;
    const View* root = this;
    while (root->parent()) root = root->parent();
    c.position = content_->convertPointFromView(*root, e.position);
    c.previous = content_->convertPointFromView(*root, e.previous);
    return c;
}

bool ScrollView::isInLeft(Point p) const { return p.x >= 0.0f && p.x <= pageAreaLength_ && p.y >= 0.0f && p.y <= frame_.h; }
bool ScrollView::isInRight(Point p) const { return p.x >= frame_.w - pageAreaLength_ && p.x <= frame_.w && p.y >= 0.0f && p.y <= frame_.h; }
bool ScrollView::isInTop(Point p) const { return p.y >= 0.0f && p.y <= pageAreaLength_ && p.x >= 0.0f && p.x <= frame_.w; }
bool ScrollView::isInBottom(Point p) const { return p.y >= frame_.h - pageAreaLength_ && p.y <= frame_.h && p.x >= 0.0f && p.x <= frame_.w; }

void ScrollView::touchesStarted(const TouchEvent& e) {
    if (filter_.isHandling()) return;
    filter_.notifyTouch(e);
    if (!scrollingByTap_) {
        forwarding_ = true;
        setInteraction(false);
        childEvents_.touchesStarted(toContent(e));
        setInteraction(true);
    } else {
        // Tap scrolling: touches inside the page-control areas scroll; the rest goes to the content.
        bool area = false;
        const View* root = this;
        while (root->parent()) root = root->parent();
        const Point local = convertPointFromView(*root, e.position);
        if (horizontal_) area = isInLeft(local) || isInRight(local);
        else if (vertical_) area = isInTop(local) || isInBottom(local);
        forwarding_ = !area;
        if (forwarding_) {
            setInteraction(false);
            childEvents_.touchesStarted(toContent(e));
            setInteraction(true);
        }
    }
    if (scrollAnimation_ != 0) {
        ctx_->animator->cancelAnimation(scrollAnimation_);
        scrollAnimation_ = 0;
    }
}

void ScrollView::touchesMovedInside(const TouchEvent& e) {
    if (!filter_.isHandling(e)) return;
    filter_.notifyMove(e);
    if (forwarding_) {
        setInteraction(false);
        const bool fixedContent = contentSize_.w == frame_.w && contentSize_.h == frame_.h && rubberband_ == 0.0f;
        const Point d = filter_.drag();
        if (d.x * d.x + d.y * d.y <= kDragCancelDist2 || fixedContent) {
            childEvents_.touchesMoved(toContent(e));
        } else {
            childEvents_.touchesCancel(toContent(e));
            forwarding_ = false;
        }
        setInteraction(true);
    }
    const Point diff = filter_.dragDiff();
    const float dt = filter_.touchTimeDiff();
    if (horizontal_) {
        offset_.x -= diff.x;
        velocity_.x = dt > 0.0f ? diff.x / dt : 0.0f;
    }
    if (vertical_) {
        offset_.y -= diff.y;
        velocity_.y = dt > 0.0f ? diff.y / dt : 0.0f;
    }
}

void ScrollView::touchesFinishedInside(const TouchEvent& e) {
    if (!filter_.isHandling(e)) {
        if (scrollAnimation_ == 0 && paging_) setActivePage(activePage(), true);
        return;
    }
    filter_.notifyUp(e);
    const Point d = filter_.drag();
    if (filter_.touchTime() < kTapMaxTime && d.x * d.x + d.y * d.y < kDragCancelDist2) {
        velocity_ = Point{};
        if (scrollingByTap_) handleTapScrolling(filter_.tap());
    }
    endDragScrolling();
    if (forwarding_) {
        setInteraction(false);
        childEvents_.touchesFinished(toContent(e));
        setInteraction(true);
        forwarding_ = false;
    }
}

void ScrollView::touchesCancel(const TouchEvent& e) {
    if (!filter_.isHandling(e)) return;
    if (forwarding_) {
        setInteraction(false);
        childEvents_.touchesCancel(toContent(e));
        setInteraction(true);
        forwarding_ = false;
    }
    filter_.reset();
    endDragScrolling();
}

void ScrollView::endDragScrolling() {
    // EndDragScrolling [verified]: a fling (non-paging) or the page snap (paging).
    if (!paging_) {
        AnimationParameters p = AnimationParameters::fromView(*content_);
        velocity_.x = std::max(-frame_.w, std::min(velocity_.x, frame_.w));
        velocity_.y = std::max(-frame_.h, std::min(velocity_.y, frame_.h));
        p.frame.x += velocity_.x * kFlingFactor;
        p.frame.y += velocity_.y * kFlingFactor;
        p.curve = kCurveEaseOut;
        p.duration = kScrollDuration;
        p.repeat = 1;
        scrollAnimation_ = ctx_->animator->animate(content_.get(), p, this);
    } else {
        Point v = velocity_;
        const Point d = filter_.drag();
        const Size ps = pageSize();
        const float drag = std::fabs(horizontal_ ? d.x : d.y);
        const float half = (horizontal_ ? ps.w : ps.h) * 0.5f;
        if (drag > half) v = Point{};
        scrollToNextPageInDirection(v);
    }
    velocity_ = Point{};
    if (delegate_) delegate_->scrollViewStartedDecelerating(id());
}

void ScrollView::handleTapScrolling(Point p) {
    if (!filter_.didTap() || !paging_ || !scrollingByTap_) return;
    int page = -1;
    if (horizontal_) {
        if (isInLeft(p)) page = std::max(0, activePage() - 1);
        else if (isInRight(p)) page = std::min(numberOfPages() - 1, activePage() + 1);
    } else if (vertical_) {
        if (isInTop(p)) page = std::max(0, activePage() - 1);
        else if (isInBottom(p)) page = std::min(numberOfPages() - 1, activePage() + 1);
    }
    if (page >= 0) setActivePage(page, true);
}

bool ScrollView::wheelScrolled(Point delta) {
    Point move;
    if (horizontal_ && vertical_) move = delta;
    else if (horizontal_) move.x = delta.x != 0.0f ? delta.x : delta.y;
    else if (vertical_) move.y = delta.y;
    if (move.x == 0.0f && move.y == 0.0f) return false;
    if (filter_.isHandling()) return true;   // a finger / button is dragging: the wheel yields
    const float gap = wheelIdle_;
    wheelIdle_ = 0.0f;
    if (paging_) {
        // One page per swipe (see the constants above).
        const bool axisX = move.x != 0.0f;
        const float d = axisX ? move.x : move.y;
        if (wheelLock_ > 0.0f) return true;
        if (gap >= kWheelGestureGap || (gap >= kWheelStreamGap && (!wheelStream_ || std::fabs(d) < kWheelRestartMax))) {
            wheelStream_ = false;
            wheelAccum_ = 0.0f;
        }
        if (wheelStream_) {
            if (axisX != wheelAxisX_ || d * wheelDir_ > 0.0f) return true;
            wheelStream_ = false;   // a reversal: the momentum never changes sign
            wheelAccum_ = 0.0f;
        }
        if (wheelAccum_ * d < 0.0f || axisX != wheelAxisX_) wheelAccum_ = 0.0f;
        wheelAxisX_ = axisX;
        wheelAccum_ += d;
        if (std::fabs(wheelAccum_) < kWheelTurn) return true;
        // The page turn of a fling in the swipe's direction (positive = up / right = the previous page),
        // then the delegate as after a finger. Not EndDragScrolling: that reads the last finger's drag.
        const float fling = (wheelAccum_ > 0.0f ? 2.0f : -2.0f) * kPageFlingVelocity;
        wheelDir_ = wheelAccum_ > 0.0f ? 1.0f : -1.0f;
        wheelStream_ = true;
        wheelAccum_ = 0.0f;
        wheelLock_ = kWheelTurnLock;
        scrollToNextPageInDirection(horizontal_ ? Point{fling, 0.0f} : Point{0.0f, fling});
        velocity_ = Point{};
        if (delegate_) delegate_->scrollViewStartedDecelerating(id());
        return true;
    }
    if (scrollAnimation_ != 0) {
        ctx_->animator->cancelAnimation(scrollAnimation_);
        scrollAnimation_ = 0;
    }
    if (!wheelGesture_) {
        wheelGesture_ = true;
        if (delegate_) delegate_->scrollViewStartedDecelerating(id());
    }
    const float step = ctx_->screen.nativeHeight * kWheelStepRate;
    offset_.x -= move.x * step;
    offset_.y -= move.y * step;
    velocity_ = Point{};
    return true;
}

void ScrollView::animationFinished(int id) {
    if (id != scrollAnimation_) return;
    scrollAnimation_ = 0;
    if (delegate_) delegate_->scrollViewFinishedDecelerating(this->id());
}

void ScrollView::animationCanceled(int id) {
    if (id == scrollAnimation_) scrollAnimation_ = 0;
}

// --- PageControl -----------------------------------------------------------------------------------

PageControl::PageControl(UiContext& ctx) : View(ctx), label_(std::make_unique<OutlineLabelView>(ctx)) {
    for (int i = 0; i < kMaxPages; ++i) dots_.push_back(std::make_unique<ImageView>(ctx));
}

void PageControl::init(const aa::data::JsonNode& dict) {
    View::init(dict);
    if (!dict.isNull()) {
        if (dict.has("ImageActive")) images_[1] = dict.getString("ImageActive");
        if (dict.has("ImageInactive")) images_[0] = dict.getString("ImageInactive");
        if (dict.has("ImageNewContentActive")) images_[3] = dict.getString("ImageNewContentActive");
        if (dict.has("ImageNewContentInactive")) images_[2] = dict.getString("ImageNewContentInactive");
        if (dict.has("ImagePadding")) setContentPadding(dict.getFloat("ImagePadding"));
        const aa::data::JsonNode label = dict.optional("Label");
        if (label.isObject()) {
            label_->setViewName("Label{0}");
            label_->init(label);
        }
        if (dict.has("ShowPageNumber")) setPageNumberVisible(dict.getBool("ShowPageNumber", true));
    }
    for (auto& d : dots_) {
        d->init();
        d->setInteraction(false);
    }
    label_->setInteraction(false);
}

void PageControl::setContentPadding(float percent) {
    dirty_ = true;
    paddingPercent_ = percent;
    padding_ = ctx_->screen.nativeWidth * 0.01f * percent;
}

void PageControl::setImageForState(const std::string& image, bool active, bool newContent) {
    images_[(newContent ? 2 : 0) + (active ? 1 : 0)] = image;
    dirty_ = true;
}

void PageControl::setNewContentIndicator(int page, bool on) {
    if (page < 0 || page >= kMaxPages) return;
    newContent_[page] = on;
    dirty_ = true;
}

void PageControl::setPageCount(int n) {
    pageCount_ = std::min(n, kMaxPages);
    refreshPages();
}

void PageControl::setActivePage(int page) {
    if (activePage_ == page) return;
    const int old = activePage_;
    activePage_ = page;
    updatePageSprite(old);
    updatePageSprite(activePage_);
    dirty_ = true;
}

void PageControl::updatePageSprite(int page) {
    if (page < 0 || page >= kMaxPages) return;
    int which;
    if (!newContent_[page]) which = page == activePage_ ? 1 : 0;
    else which = page == activePage_ ? 3 : 2;
    dots_[static_cast<std::size_t>(page)]->setImage(images_[which], false);
}

void PageControl::refreshPages() {
    // RefreshPages [verified]: the dots are laid out around the view's centre, spaced by the inactive
    // sprite's width plus the padding; the active one bottom-aligned; the number label above the active dot.
    if (activePage_ >= pageCount_) activePage_ = 0;
    for (auto& d : dots_) d->setVisible(false);
    for (View* v : std::vector<View*>(subviews_)) removeSubview(v);
    if (activePage_ < numberStart_ || !showNumber_) {
        label_->setVisible(false);
    } else {
        label_->setNonLocalizedText(std::to_string(activePage_ - numberStart_ + 1));
        label_->setVisible(showNumber_);
    }
    const float n = static_cast<float>(pageCount_);
    const Size inactive = ctx_->resources->imageSize(images_[0]);
    const Size active = ctx_->resources->imageSize(images_[1]);
    const float dotW = inactive.w;
    const float dotH = active.h;
    const Point c = center();
    const float startX = (c.x - n * 0.5f * dotW) - (n - 1.0f) * 0.5f * padding_;
    for (int i = 0; i < pageCount_; ++i) {
        ImageView& d = *dots_[static_cast<std::size_t>(i)];
        updatePageSprite(i);
        d.resizeFrameToImage(true, true);
        const float fi = static_cast<float>(i);
        const Size s = d.size();
        const float x = startX + fi * (dotW + padding_) - (s.w - dotW) * 0.5f;
        const float y = (frame_.h - s.h) - (dotH - s.h) * 0.5f;
        d.setPosition(Point{x, y});
        if (i == activePage_) {
            // The number label: x = the active dot's x, y = dot y − label height + activeH / 2.05, the
            // division in double [verified: 0x113a30 vcvt.f64 / vdiv.f64 / vcvt.f32].
            label_->setPosition(Point{x, (y - label_->size().h) + static_cast<float>(static_cast<double>(dotH) / 2.05)});
        }
        d.setVisible(true);
        addSubview(&d);
    }
    addSubview(label_.get());
}

void PageControl::update(float dt) {
    View::update(dt);
    if (!dirty_) return;
    dirty_ = false;
    refreshPages();
}

}  // namespace aa::ui
