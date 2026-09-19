#include "aa/ui/button.h"

#include "aa/ui/scene.h"

#include <cmath>

namespace aa::ui {

namespace {
constexpr int kUiButtonPush = 3;
constexpr int kUiButtonRelease = 4;
constexpr float kButtonSoundVolume = 0.2f;    // 0x3e4ccccd
constexpr float kZoomOutScale = 1.15f;
constexpr float kZoomOutDuration = 0.1f;      // 0x3dcccccd
constexpr float kZoomInDuration = 0.05f;      // 0x3d4ccccd
constexpr int kZoomCurve = 4;
constexpr float kSelectedHold = 0.1f;         // +0x7dc: the selected state's minimum display time
constexpr float kMenuSlideDuration = 0.3f;    // 0x3e99999a
constexpr float kPi = 3.1415927f;
}  // namespace

int Button::processedTouchId_ = -1;

Button::Button(UiContext& ctx)
    : View(ctx), background_(std::make_unique<ImageView>(ctx)), stateImage_(std::make_unique<ImageView>(ctx)),
      overlay_(std::make_unique<ImageView>(ctx)), label_(std::make_unique<OutlineLabelView>(ctx)) {}

void Button::initSubviews(const aa::data::JsonNode& dict) {
    View::init(dict);
    background_->init();
    background_->setInteraction(false);
    stateImage_->init();
    stateImage_->setInteraction(false);
    overlay_->init();
    overlay_->setInteraction(false);
    overlay_->setVisible(false);
    label_->init();
    label_->setInteraction(false);
    label_->setVisible(false);
    label_->setAutoResize(false, true);
    addSubview(background_.get());
    addSubview(stateImage_.get());
    addSubview(label_.get());
    addSubview(overlay_.get());
}

void Button::init(const aa::data::JsonNode& dict) {
    initSubviews(dict);
    if (dict.isNull()) return;
    resizeParent_ = dict.getBool("ResizeParent", true);
    if (dict.has("ImageBackground") && dict.optional("ImageBackground").isString()) setBackground(dict.getString("ImageBackground"));
    auto states = [&](const char* normal, const char* disabled, const char* selected, const char* highlighted, bool localized) {
        if (dict.has(normal)) {
            setImageForState(dict.getString(normal), button_state::kNormal, localized);
            if (!dict.has(disabled)) setImageForState(dict.getString(normal), button_state::kDisabled, localized);
            if (!isBackgroundSet()) {
                // ResizeFrameToImage(normal, resizeParent): the frame takes the normal image's size.
                const std::string name = localized && ctx_->localization ? ctx_->localization->imageName(dict.getString(normal)) : dict.getString(normal);
                const Size s = ctx_->resources->imageSize(name);
                if (resizeParent_) setFrame(Rect{frame_.x, frame_.y, s.w, s.h});
                stateImage_->setFrame(Rect{0.0f, 0.0f, s.w, s.h});
            }
        }
        if (dict.has(selected)) setImageForState(dict.getString(selected), button_state::kSelected, localized);
        if (dict.has(highlighted)) setImageForState(dict.getString(highlighted), button_state::kHighlighted, localized);
        if (dict.has(disabled)) setImageForState(dict.getString(disabled), button_state::kDisabled, localized);
    };
    states("ImageStateNormal", "ImageStateDisabled", "ImageStateSelected", "ImageStateHighlighted", false);
    states("LocalizedImageStateNormal", "LocalizedImageStateDisabled", "LocalizedImageStateSelected", "LocalizedImageStateHighlighted", true);
    if (dict.has("TextFont")) setFont(dict.getString("TextFont"));
    if (dict.has("TextStateNormal")) {
        const std::string t = dict.getString("TextStateNormal");
        setTextForState(t, button_state::kNormal);
        if (!dict.has("TextStateDisabled")) setTextForState(t, button_state::kDisabled);
        if (!dict.has("TextStateSelected")) setTextForState(t, button_state::kSelected);
        if (!dict.has("TextStateHighlighted")) setTextForState(t, button_state::kHighlighted);
    }
    if (dict.has("TextStateSelected")) setTextForState(dict.getString("TextStateSelected"), button_state::kSelected);
    if (dict.has("TextStateHighlighted")) setTextForState(dict.getString("TextStateHighlighted"), button_state::kHighlighted);
    if (dict.has("TextStateDisabled")) setTextForState(dict.getString("TextStateDisabled"), button_state::kDisabled);
}

void Button::setState(int state) {
    state_ = state;
    if (state != button_state::kDisabled) {
        if (changeAlpha_) setAlpha(1.0f);
        if (zoomOutAnimation_ != 0) {
            ctx_->animator->cancelAnimation(zoomOutAnimation_);
            zoomOutAnimation_ = 0;
        }
        zoomIn();
        setInteraction(true);
        dirty_ = true;
        return;
    }
    if (changeAlpha_) setAlpha(0.5f);
    setInteraction(false);
    dirty_ = true;
}

void Button::setImageForState(const std::string& image, int state, bool localized) {
    stateImages_[static_cast<std::size_t>(state)] = image;
    stateLocalized_[static_cast<std::size_t>(state)] = localized;
    dirty_ = true;
}

void Button::setBackground(const std::string& image, bool localized) {
    backgroundName_ = image;
    backgroundLocalized_ = localized;
    background_->setImage(image, localized);
    resizeFrameToBackground();
    dirty_ = true;
}

void Button::resizeFrameToBackground() {
    const Size s = background_->imageSize();
    setFrame(Rect{frame_.x, frame_.y, s.w, s.h});
    background_->resizeFrameToImage(true, true);
}

void Button::recomputeAutoSize() {
    // Mirrors Init's own sizing branches, against the ctx_->resources scale current now rather than at
    // construction; Update()'s dirty_ refresh redoes stateImage_ / overlay_ (it already calls
    // ImageView::setImage(), which re-reads ctx_->resources->imageSize() itself) once marked dirty here —
    // background_ needs its own SetImage redone first since ResizeFrameToBackground only re-reads its
    // (otherwise stale) cached size.
    if (isBackgroundSet()) {
        background_->setImage(backgroundName_, backgroundLocalized_);
        resizeFrameToBackground();
    } else if (!stateImages_[button_state::kNormal].empty()) {
        const std::string& raw = stateImages_[button_state::kNormal];
        const std::string name =
            stateLocalized_[button_state::kNormal] && ctx_->localization ? ctx_->localization->imageName(raw) : raw;
        const Size s = ctx_->resources->imageSize(name);
        if (resizeParent_) setFrame(Rect{frame_.x, frame_.y, s.w, s.h});
        stateImage_->setFrame(Rect{0.0f, 0.0f, s.w, s.h});
    }
    dirty_ = true;
}

void Button::setOverlayForState(const std::string& image, int state, Point offsetPercent) {
    stateOverlays_[static_cast<std::size_t>(state)] = image;
    overlayOffsetPercent_[static_cast<std::size_t>(state)] = offsetPercent;
    dirty_ = true;
}

void Button::setTextForState(const std::string& textId, int state) { stateTexts_[static_cast<std::size_t>(state)] = textId; }

void Button::setFont(const std::string& font) {
    textFont_ = font;
    label_->setFont(font);
    label_->setAnchor(FontAnchorH::Center, FontAnchorV::Center);
}

void Button::setScale(float s) {
    View::setScale(s);
    overlay_->setScale(s);
    stateImage_->setScale(s);
    background_->setScale(s);
    dirty_ = true;
}

bool Button::isPointInView(Point p) const {
    // IsPointInView: while highlighted / selected the hit area is twice as large around the centre.
    const float f = (state_ == button_state::kHighlighted || state_ == button_state::kSelected) ? 2.0f : 1.0f;
    const Point c = center();
    const Point q{(p.x - c.x) / f + c.x, (p.y - c.y) / f + c.y};
    return View::isPointInView(q);
}

std::vector<View*> Button::zoomViews() {
    std::vector<View*> views{background_.get()};
    if (!animateOnlyBackground_) {
        views.push_back(stateImage_.get());
        views.push_back(overlay_.get());
    }
    return views;
}

void Button::zoomOut() {
    if (zoomOutAnimation_ != 0) return;
    AnimationParameters d;
    d.frame = Rect{};
    d.angle = 0.0f;
    d.alpha = 0.0f;
    d.pivot = Point{};
    d.scale = (kZoomOutScale - background_->scale()) - (1.0f - scale_);
    d.curve = kZoomCurve;
    d.duration = kZoomOutDuration;
    d.repeat = 1;
    zoomOutAnimation_ = ctx_->animator->animate(zoomViews(), d, this);
}

void Button::zoomIn() {
    if (zoomInAnimation_ != 0) return;
    AnimationParameters d;
    d.alpha = 0.0f;
    d.angle = 0.0f;
    d.scale = (1.0f - background_->scale()) - (1.0f - scale_);
    d.curve = kZoomCurve;
    d.duration = kZoomInDuration;
    d.repeat = 1;
    zoomInAnimation_ = ctx_->animator->animate(zoomViews(), d, this);
}

void Button::playPress() {
    if (!silent_ && ctx_->sounds) ctx_->sounds->playUiSound(kUiButtonPush, kButtonSoundVolume);
}

void Button::playRelease() {
    if (!silent_ && ctx_->sounds) ctx_->sounds->playUiSound(kUiButtonRelease, kButtonSoundVolume);
}

void Button::touchesStarted(const TouchEvent& e) {
    if (processedTouchId_ != -1) return;
    if (state_ != button_state::kNormal) return;
    processedTouchId_ = e.id;
    dirty_ = true;
    trackedTouch_ = e.id;
    state_ = button_state::kHighlighted;
    zoomOut();
    playPress();
}

void Button::touchesFinishedInside(const TouchEvent& e) {
    if (e.id != processedTouchId_) return;
    if (state_ == button_state::kDisabled) {
        processedTouchId_ = -1;
        return;
    }
    trackedTouch_ = -1;
    dirty_ = true;
    if (state_ == button_state::kNormal) {
        // Remake fix: the original returns here with the static touch id still claimed, so a button reset
        // to Normal while pressed (ChapterSelectionView::Refresh on a page change) deadlocks every button
        // until the process ends. Release the id instead.
        processedTouchId_ = -1;
        return;
    }
    if (!stateImages_[button_state::kSelected].empty()) selectedTimer_ = kSelectedHold;
    state_ = button_state::kSelected;
    if (zoomOutAnimation_ == 0) zoomIn();
    playRelease();
    if (delegate_) delegate_->buttonAboutToBePressed(id());
}

void Button::touchesFinishedOutside(const TouchEvent& e) {
    if (e.id != processedTouchId_) return;
    if (state_ != button_state::kDisabled && e.id == trackedTouch_) {
        processedTouchId_ = -1;
        if (state_ != button_state::kNormal) state_ = button_state::kNormal;
        trackedTouch_ = -1;
        dirty_ = true;
        if (zoomOutAnimation_ != 0) {
            ctx_->animator->cancelAnimation(zoomOutAnimation_);
            zoomOutAnimation_ = 0;
        }
        zoomIn();
        return;
    }
    processedTouchId_ = -1;
}

void Button::touchesMovedEnter(const TouchEvent& e) {
    if (e.id != processedTouchId_) return;
    if (state_ == button_state::kDisabled || e.id != trackedTouch_) return;
    dirty_ = true;
    if (state_ == button_state::kNormal) state_ = button_state::kHighlighted;
    if (zoomInAnimation_ != 0) {
        ctx_->animator->cancelAnimation(zoomInAnimation_);
        zoomInAnimation_ = 0;
    }
    zoomOut();
    playPress();
}

void Button::touchesMovedExit(const TouchEvent& e) {
    if (e.id != processedTouchId_) return;
    if (state_ == button_state::kDisabled || e.id != trackedTouch_) return;
    if (state_ != button_state::kNormal) state_ = button_state::kNormal;
    dirty_ = true;
    if (zoomOutAnimation_ != 0) {
        ctx_->animator->cancelAnimation(zoomOutAnimation_);
        zoomOutAnimation_ = 0;
    }
    zoomIn();
}

void Button::touchesCancel(const TouchEvent& e) {
    if (e.id != processedTouchId_) return;
    setState(button_state::kNormal);
    processedTouchId_ = -1;
    trackedTouch_ = -1;
}

void Button::animationFinished(int animationId) {
    // Button::AnimationFinished [verified]: the zoom-in's end completes a selected press (state → normal,
    // the delegate's ButtonPressed); the zoom-out's end starts the zoom-in unless still highlighted.
    if (zoomInAnimation_ == animationId) {
        zoomInAnimation_ = 0;
        if (state_ == button_state::kSelected) {
            state_ = button_state::kNormal;
            if (delegate_) {
                delegateCalled();
                delegate_->buttonPressed(id());
            }
            processedTouchId_ = -1;   // remake fix: the original releases the id only with a delegate
        }
    } else if (zoomOutAnimation_ == animationId) {
        zoomOutAnimation_ = 0;
        if (state_ != button_state::kHighlighted) zoomIn();
    }
    dirty_ = true;
}

void Button::update(float dt) {
    View::update(dt);
    if (dirty_) {
        dirty_ = false;
        const std::size_t s = static_cast<std::size_t>(state_);
        const std::string& image = stateImages_[s];
        if (!image.empty()) {
            stateImage_->setImage(image, stateLocalized_[s]);
            stateImage_->resizeFrameToImage(true, true);
            // The state image's pivot sits at the button's centre.
            const Point c = center();
            const Point p = stateImage_->pivot();
            stateImage_->setPosition(Point{c.x - p.x, c.y - p.y});
        }
        const std::string& overlay = stateOverlays_[s];
        if (overlay.empty()) {
            overlay_->setVisible(false);
        } else {
            overlay_->setImage(overlay, false);
            overlay_->resizeFrameToImage(true, true);
            overlay_->setPivot(overlay_->center());
            const Point c = center();
            const Point pct = overlayOffsetPercent_[s];
            const Point o{pct.x * frame_.w * 0.01f, pct.y * frame_.h * 0.01f};
            overlay_->setCenter(Point{c.x + o.x, c.y + o.y});
            overlay_->setVisible(true);
        }
        if (isBackgroundSet()) {
            background_->resizeFrameToImage(true, true);
            background_->setPivot(background_->center());
            const Point c = center();
            const Point p = stateImage_->pivot();
            stateImage_->setPosition(Point{c.x - p.x, c.y - p.y});
        }
        const std::string& text = stateTexts_[s];
        if (text.empty()) {
            label_->setVisible(false);
        } else {
            label_->setSize(Size{frame_.w * 0.8f, -1.0f});
            label_->setText(text);
            label_->setCenter(center());
            label_->setVisible(true);
        }
    }
    if (state_ == button_state::kSelected) {
        if (selectedTimer_ <= 0.0f) {
            if (selectedTimer_ < 0.0f) {
                selectedTimer_ = 0.0f;
                dirty_ = true;
            }
        } else {
            selectedTimer_ -= dt;
        }
    }
}

void Button::draw(Renderer& renderer, const Rect& rect) {
    if (!rotateBackground_) background_->setAngle(-angle());
    View::draw(renderer, rect);
}

// --- ToggleButton ----------------------------------------------------------------------------------

void ToggleButton::init(const aa::data::JsonNode& dict) {
    Button::init(dict);
    if (dict.isNull()) return;
    if (dict.has("ImageStateNormalOn")) {
        setToggleImage(dict.getString("ImageStateNormalOn"), button_state::kNormal, true);
        setToggleImage(dict.getString("ImageStateNormalOn"), button_state::kDisabled, true);
    }
    if (dict.has("ImageStateNormalOff")) {
        setToggleImage(dict.getString("ImageStateNormalOff"), button_state::kNormal, false);
        setToggleImage(dict.getString("ImageStateNormalOff"), button_state::kDisabled, false);
    }
    if (dict.has("ImageStateSelectedOn")) setToggleImage(dict.getString("ImageStateSelectedOn"), button_state::kSelected, true);
    if (dict.has("ImageStateSelectedOff")) setToggleImage(dict.getString("ImageStateSelectedOff"), button_state::kSelected, false);
    if (dict.has("ImageStateHighlightedOn")) setToggleImage(dict.getString("ImageStateHighlightedOn"), button_state::kHighlighted, true);
    if (dict.has("ImageStateHighlightedOff")) setToggleImage(dict.getString("ImageStateHighlightedOff"), button_state::kHighlighted, false);
    if (dict.has("ImageStateDisabledOn")) setToggleImage(dict.getString("ImageStateDisabledOn"), button_state::kDisabled, true);
    if (dict.has("ImageStateDisabledOff")) setToggleImage(dict.getString("ImageStateDisabledOff"), button_state::kDisabled, false);
    // The frame follows the "on" normal image (no ImageBackground: the ImageState* keys are absent).
    if (!isBackgroundSet() && !images_[button_state::kNormal].empty()) {
        const Size s = ctx_->resources->imageSize(images_[button_state::kNormal]);
        setFrame(Rect{frame_.x, frame_.y, s.w, s.h});
    }
    toggleDirty_ = true;
}

void ToggleButton::recomputeAutoSize() {
    Button::recomputeAutoSize();   // a no-op here when ImageBackground / ImageStateNormal are absent, as they are for a toggle
    // Mirrors Init's own tail: the frame follows the "on" normal image at the current ctx_->resources scale.
    if (!isBackgroundSet() && !images_[button_state::kNormal].empty()) {
        const Size s = ctx_->resources->imageSize(images_[button_state::kNormal]);
        setFrame(Rect{frame_.x, frame_.y, s.w, s.h});
    }
    toggleDirty_ = true;
}

void ToggleButton::setToggleImage(const std::string& image, int state, bool on) {
    // ToggleButton::SetImageForState(image, state, off) [verified]: the "On" images fill slots 0..3 and
    // show while unchecked; the "Off" images fill slots 4..7 and show while checked.
    images_[static_cast<std::size_t>(state + (on ? 0 : 4))] = image;
    toggleDirty_ = true;
}

void ToggleButton::update(float dt) {
    if (toggleDirty_) {
        for (int s = 0; s < 4; ++s) {
            const std::string& img = images_[static_cast<std::size_t>(s + (checked_ ? 4 : 0))];
            Button::setImageForState(img, s, false);
        }
        toggleDirty_ = false;
    }
    Button::update(dt);
}

// --- SlidingButton ---------------------------------------------------------------------------------

SlidingButton::SlidingButton(UiContext& ctx) : View(ctx), button_(std::make_unique<Button>(ctx)), menu_(std::make_unique<View>(ctx)) {}

void SlidingButton::init(const aa::data::JsonNode& dict) {
    // SlidingButton::Init(dict) [verified: 0x1122d8 + disassembly]: the slider's own View::Init reads the
    // dictionary (position / anchors); the inner button is built bare (Button::Init(), AnimateOnlyBackground)
    // and takes only the image keys — ImageBackground (the slider's frame then copies the background's size,
    // SetBackground), ImageState* / LocalizedImageState* (Normal 1, Selected 3, Highlighted 2, Disabled 0);
    // "Direction": "DOWN" → 1, "UP" → 0, the constructor's default 0.
    View::init(dict);
    button_->init();
    button_->setDelegate(this);
    button_->setAnimateOnlyBackground(true);
    button_->setViewName("SlidingMainButton");
    menu_->init();
    menu_->setInteraction(false);
    menu_->setVisible(false);
    menu_->setViewName("SlidingMenu");
    addSubview(menu_.get());
    addSubview(button_.get());
    if (dict.isNull()) return;
    if (dict.has("ImageBackground")) setBackground(dict.getString("ImageBackground"));
    const struct { const char* key; int state; bool localized; } images[] = {
        {"ImageStateNormal", button_state::kNormal, false},           {"ImageStateSelected", button_state::kSelected, false},
        {"ImageStateHighlighted", button_state::kHighlighted, false}, {"ImageStateDisabled", button_state::kDisabled, false},
        {"LocalizedImageStateNormal", button_state::kNormal, true},   {"LocalizedImageStateSelected", button_state::kSelected, true},
        {"LocalizedImageStateHighlighted", button_state::kHighlighted, true}, {"LocalizedImageStateDisabled", button_state::kDisabled, true},
    };
    for (const auto& img : images) {
        if (dict.has(img.key)) button_->setImageForState(dict.getString(img.key), img.state, img.localized);
    }
    if (dict.has("Direction")) {
        const std::string direction = dict.getString("Direction");
        if (direction == "DOWN") down_ = true;
        else if (direction == "UP") down_ = false;
    }
}

void SlidingButton::setBackground(const std::string& image) {
    // SetBackground [verified]: the inner button's background; the slider's frame takes its size.
    button_->setBackground(image);
    setFrame(Rect{frame_.x, frame_.y, button_->frame().w, button_->frame().h});
}

void SlidingButton::recomputeAutoSize() {
    // button_ is a subview, so recomputeAutoSizeRecursive() (post-order) has already refreshed its own
    // AutoResize size by the time this runs.
    if (button_->isBackgroundSet()) setFrame(Rect{frame_.x, frame_.y, button_->frame().w, button_->frame().h});
}

void SlidingButton::relayout() {
    // The menu slide (a single-view absolute-target animation) targets openMenuFrame()/closedMenuFrame(),
    // both derived from this view's own frame_ — View::relayout() completes it (the "snap in-flight
    // animations to the endpoint" resize trade-off); then redo menu_'s frame and its buttons' layout, both
    // stale otherwise: neither is Relative/Anchor-driven, and Update() only redoes layoutMenuButtons() on
    // the very first frame or mid-slide.
    View::relayout();
    menu_->setFrame(menuOpen_ ? openMenuFrame() : closedMenuFrame());
    layoutMenuButtons();
}

void SlidingButton::addMenuButton(Button* b) {
    menu_->addSubview(b);
    layoutMenuButtons();
}

void SlidingButton::layoutMenuButtons() {
    // LayoutMenuButtons [verified: 0x11307c]: `itemH` = the item height when set, else the slider's frame
    // height; the buttons are centred horizontally. UP (direction 0): button i at (i + 0.5)·itemH from the
    // menu's top, clamped to the menu height; DOWN (1): at menuH − (i + 0.5)·itemH, clamped to 0 — the
    // last-added button sits nearest the main button and they emerge from under it as the menu grows.
    const float itemH = itemHeight_ == 0.0f ? frame_.h : itemHeight_;
    const Rect m = menu_->frame();
    const std::vector<View*> buttons = menu_->subviews();
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        View* b = buttons[i];
        const float fi = static_cast<float>(i);
        Point c{m.w * 0.5f, 0.0f};
        if (down_) {
            c.y = m.h - (fi + 0.5f) * itemH;
            if (c.y < 0.0f) c.y = 0.0f;
        } else {
            c.y = (fi + 0.5f) * itemH;
            if (!(c.y < m.h)) c.y = m.h;
        }
        b->setCenter(c);
    }
}

Rect SlidingButton::openMenuFrame() const {
    // ShowMenu [verified: 0x11363c]: the menu starts as {0, h/2, w, 0} (the main button's centre line) and
    // opens to {0, h/2, w, h/2 + n·itemH} downwards or {0, −n·itemH, w, h/2 + n·itemH} upwards.
    const float itemH = itemHeight_ == 0.0f ? frame_.h : itemHeight_;
    const float n = static_cast<float>(menu_->subviews().size());
    const float h = frame_.h * 0.5f + itemH * n;
    return down_ ? Rect{0.0f, frame_.h * 0.5f, frame_.w, h} : Rect{0.0f, -itemH * n, frame_.w, h};
}

Rect SlidingButton::closedMenuFrame() const { return Rect{0.0f, frame_.h * 0.5f, frame_.w, 0.0f}; }

void SlidingButton::showMenu(bool animated) {
    if (animation_ != 0) {
        ctx_->animator->cancelAnimation(animation_);
        animation_ = 0;
    }
    const Rect target = openMenuFrame();
    if (!animated) {
        menu_->setFrame(target);
        layoutMenuButtons();
        menuOpen_ = true;
        menu_->setInteraction(true);
        menu_->setVisible(true);
        button_->setAngle(kPi);
        return;
    }
    menu_->setVisible(true);
    menu_->setFrame(closedMenuFrame());
    AnimationParameters p = AnimationParameters::fromView(*menu_);
    p.frame = target;
    p.curve = 2;
    p.duration = kMenuSlideDuration;
    p.repeat = 1;
    animation_ = ctx_->animator->animate(menu_.get(), p, this);
    AnimationParameters b = AnimationParameters::fromView(*button_);
    b.angle = kPi;
    b.curve = 0;
    b.duration = kMenuSlideDuration;
    b.repeat = 1;
    ctx_->animator->animate(button_.get(), b, nullptr);
    menuOpen_ = true;
}

void SlidingButton::hideMenu(bool animated) {
    // HideMenu [verified: 0x1133c4]: back to the closed frame, the main button's angle to 0.
    if (animation_ != 0) {
        ctx_->animator->cancelAnimation(animation_);
        animation_ = 0;
    }
    const Rect target = closedMenuFrame();
    if (!animated) {
        menu_->setFrame(target);
        layoutMenuButtons();
        menuOpen_ = false;
        menu_->setInteraction(false);
        menu_->setVisible(false);
        button_->setAngle(0.0f);
        return;
    }
    AnimationParameters p = AnimationParameters::fromView(*menu_);
    p.frame = target;
    p.curve = 1;
    p.duration = kMenuSlideDuration;
    p.repeat = 1;
    animation_ = ctx_->animator->animate(menu_.get(), p, this);
    AnimationParameters b = AnimationParameters::fromView(*button_);
    b.angle = 0.0f;
    b.curve = 0;
    b.duration = kMenuSlideDuration;
    b.repeat = 1;
    ctx_->animator->animate(button_.get(), b, nullptr);
    menuOpen_ = false;
    menu_->setInteraction(false);
}

void SlidingButton::buttonPressed(int id) {
    if (id == button_->id()) {
        if (animation_ != 0) return;
        if (!menuOpen_) showMenu(true);
        else hideMenu(true);
        if (delegate_) delegate_->buttonPressed(this->id());
        return;
    }
    if (delegate_) delegate_->buttonPressed(id);
}

void SlidingButton::animationFinished(int id) {
    if (id != animation_) return;
    animation_ = 0;
    if (menuOpen_) menu_->setInteraction(true);
    else menu_->setVisible(false);
    layoutMenuButtons();
}

void SlidingButton::animationCanceled(int id) {
    if (id == animation_) animation_ = 0;
}

void SlidingButton::update(float dt) {
    View::update(dt);
    if (!laidOut_) {
        layoutMenuButtons();
        laidOut_ = true;
    }
    if (animation_ != 0) layoutMenuButtons();
}

bool SlidingButton::isPointInView(Point p) const {
    if (View::isPointInView(p)) return true;
    if (!menuOpen_ && animation_ == 0) return false;
    const Rect m = menu_->frame();
    return p.x >= m.x && p.x <= m.x + m.w && p.y >= m.y && p.y <= m.y + m.h;
}

}  // namespace aa::ui
