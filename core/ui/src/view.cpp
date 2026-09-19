#include "aa/ui/view.h"

#include "aa/ui/scene.h"

#include <algorithm>
#include <cmath>

namespace aa::ui {

int View::nextId_ = 1;

namespace {

float screenW(const UiContext& c) { return c.screen.nativeWidth; }
float screenH(const UiContext& c) { return c.screen.nativeHeight; }

}  // namespace

std::string localizedText(const UiContext& ctx, const std::string& id) {
    return ctx.localization ? ctx.localization->text(id) : id;
}

View::View(UiContext& ctx) : ctx_(&ctx), id_(nextId_++) {}

View::~View() = default;

// The dictionary-less Init: the subclasses' Init(dict) run with a null node (their subviews are built,
// no attributes read).
void View::init() { init(aa::data::JsonNode(nullptr, "")); }

void View::init(const aa::data::JsonNode& dict) {
    if (dict.isNull()) return;
    // BackgroundColor needs all four channels [verified].
    const aa::data::JsonNode bg = dict.optional("BackgroundColor");
    if (bg.isObject() && bg.has("R") && bg.has("G") && bg.has("B") && bg.has("A")) {
        setBackgroundColor(Color{static_cast<std::uint8_t>(bg.getInt("R")), static_cast<std::uint8_t>(bg.getInt("G")),
                                 static_cast<std::uint8_t>(bg.getInt("B")), static_cast<std::uint8_t>(bg.getInt("A"))});
    }
    if (dict.has("X")) frame_.x = static_cast<float>(dict.getInt("X"));
    if (dict.has("Y")) frame_.y = static_cast<float>(dict.getInt("Y"));
    if (dict.has("W")) frame_.w = static_cast<float>(dict.getInt("W"));
    if (dict.has("H")) frame_.h = static_cast<float>(dict.getInt("H"));
    const aa::data::JsonNode rel = dict.optional("Relative");
    if (rel.isObject()) {
        Point p;
        Size s;
        const bool hasX = rel.has("X");
        const bool hasY = rel.has("Y");
        const bool hasW = rel.has("W");
        const bool hasH = rel.has("H");
        if (hasX) p.x = rel.getFloat("X");
        if (hasY) p.y = rel.getFloat("Y");
        if (hasW) s.w = rel.getFloat("W");
        if (hasH) s.h = rel.getFloat("H");
        setRelative(hasX, hasY, hasW, hasH);
        setRelativePosition(p);
        setRelativeSize(s);
        const aa::data::JsonNode pad = rel.optional("Padding");
        if (pad.isObject()) {
            // SetRelativePadding: percentages of the screen size.
            float l = 0.0f, r = 0.0f, t = 0.0f, b = 0.0f;
            if (pad.has("Left")) l = pad.getFloat("Left");
            if (pad.has("Right")) r = pad.getFloat("Right");
            if (pad.has("Top")) t = pad.getFloat("Top");
            if (pad.has("Bottom")) b = pad.getFloat("Bottom");
            paddingSource_ = PaddingSource::kRelativePercent;
            paddingSpec_[0] = l;
            paddingSpec_[1] = r;
            paddingSpec_[2] = t;
            paddingSpec_[3] = b;
            recomputePadding();
        }
    }
    const aa::data::JsonNode pad = dict.optional("Padding");
    if (pad.isObject()) {
        float l = 0.0f, r = 0.0f, t = 0.0f, b = 0.0f;
        if (pad.has("Left")) l = static_cast<float>(pad.getInt("Left"));
        if (pad.has("Right")) r = static_cast<float>(pad.getInt("Right"));
        if (pad.has("Top")) t = static_cast<float>(pad.getInt("Top"));
        if (pad.has("Bottom")) b = static_cast<float>(pad.getInt("Bottom"));
        paddingSource_ = PaddingSource::kUiScalePixels;
        paddingSpec_[0] = l;
        paddingSpec_[1] = r;
        paddingSpec_[2] = t;
        paddingSpec_[3] = b;
        recomputePadding();
    }
    const aa::data::JsonNode anchor = dict.optional("Anchor");
    if (anchor.isObject()) {
        std::string selfH, selfV, targetH, targetV, nameH, nameV;
        const aa::data::JsonNode h = anchor.optional("H");
        if (h.isObject()) {
            selfH = h.getString("Self", "");
            const aa::data::JsonNode v = h.optional("View");
            if (v.isObject()) {
                nameH = v.getString("Name", "");
                targetH = v.getString("Target", "");
            }
        }
        const aa::data::JsonNode v = anchor.optional("V");
        if (v.isObject()) {
            selfV = v.getString("Self", "");
            const aa::data::JsonNode vv = v.optional("View");
            if (vv.isObject()) {
                nameV = vv.getString("Name", "");
                targetV = vv.getString("Target", "");
            }
        }
        setViewAnchor(Anchor{hAnchorFromString(selfH), vAnchorFromString(selfV)},
                      Anchor{hAnchorFromString(targetH), vAnchorFromString(targetV)}, nameH, nameV);
    }
}

void View::setPosition(Point p) {
    frame_.x = std::ceil(p.x);
    frame_.y = std::ceil(p.y);
}

void View::setSize(Size s) {
    frame_.w = std::floor(s.w + 0.5f);
    frame_.h = std::floor(s.h + 0.5f);
}

void View::setFrame(const Rect& r) {
    setPosition(Point{r.x, r.y});
    setSize(Size{r.w, r.h});
}

Point View::center() const { return Point{std::ceil(frame_.w * 0.5f), std::ceil(frame_.h * 0.5f)}; }

void View::setCenter(Point c) { setPosition(Point{std::ceil(c.x - frame_.w * 0.5f), std::ceil(c.y - frame_.h * 0.5f)}); }

Point View::globalPosition() const {
    Point p = position();
    for (const View* v = parent_; v; v = v->parent_) {
        p.x += v->frame_.x;
        p.y += v->frame_.y;
    }
    return p;
}

Rect View::realFrame() const {
    return Rect{std::ceil(frame_.x) + padding_[0], std::ceil(frame_.y) + padding_[2], std::ceil(scale_ * frame_.w) + padding_[1],
                std::ceil(scale_ * frame_.h) + padding_[3]};
}

void View::setPadding(float left, float right, float top, float bottom) {
    padding_[0] = left;
    padding_[1] = right;
    padding_[2] = top;
    padding_[3] = bottom;
}

void View::setRelative(bool x, bool y, bool w, bool h) {
    relativeX_ = x;
    relativeY_ = y;
    relativeW_ = w;
    relativeH_ = h;
}

void View::setRelativePosition(Point percent) {
    relativePos_ = percent;
    if (relativeX_) frame_.x = screenW(*ctx_) * 0.01f * percent.x;
    if (relativeY_) frame_.y = screenH(*ctx_) * 0.01f * percent.y;
}

void View::setRelativeSize(Size percent) {
    relativeSize_ = percent;
    if (relativeW_) frame_.w = screenW(*ctx_) * 0.01f * percent.w;
    if (relativeH_) frame_.h = screenH(*ctx_) * 0.01f * percent.h;
}

void View::recomputePadding() {
    switch (paddingSource_) {
    case PaddingSource::kRelativePercent:
        setPadding(screenW(*ctx_) * 0.01f * paddingSpec_[0], screenW(*ctx_) * 0.01f * paddingSpec_[1], screenH(*ctx_) * 0.01f * paddingSpec_[2],
                  screenH(*ctx_) * 0.01f * paddingSpec_[3]);
        break;
    case PaddingSource::kUiScalePixels:
        setPadding(paddingSpec_[0] * ctx_->screen.uiScale, paddingSpec_[1] * ctx_->screen.uiScale, paddingSpec_[2] * ctx_->screen.uiScale,
                  paddingSpec_[3] * ctx_->screen.uiScale);
        break;
    case PaddingSource::kNone: break;
    }
}

void View::recomputeRelativeFrame() {
    setRelativePosition(relativePos_);
    setRelativeSize(relativeSize_);
}

void View::recomputeRelativeFrameRecursive() {
    recomputeRelativeFrame();
    for (View* v : subviews_) {
        if (v) v->recomputeRelativeFrameRecursive();
    }
}

void View::recomputePaddingRecursive() {
    recomputePadding();
    for (View* v : subviews_) {
        if (v) v->recomputePaddingRecursive();
    }
}

void View::recomputeAutoSizeRecursive() {
    // Post-order (children before self): an "auto size from content" view (DialogBackground's width from
    // its top_ sprite, SlidingButton's from its button_) needs its children's own AutoResize/content sizes
    // already current when it derives its own — the reverse of the other three passes, which are all
    // purely top-down (percent-of-parent, not percent-of-content).
    for (View* v : subviews_) {
        if (v) v->recomputeAutoSizeRecursive();
    }
    recomputeAutoSize();
}

void View::reWrapRecursive() {
    reWrap();
    for (View* v : subviews_) {
        if (v) v->reWrapRecursive();
    }
}

void View::relayout() {
    // An in-flight animation that moves a frame inside this subtree would keep interpolating towards (and
    // end at) the target it captured before the resize, dragging the fresh geometry back a frame later —
    // snap it to its end first (the documented "snap, don't rebase" resize trade-off). Fades and pulses
    // never write the frame (Animator's per-component write) and are left running.
    if (ctx_->animator) ctx_->animator->completeAnimations(this);
    // The order matters, and mirrors construction: View::init's Relative.W/H percent-of-screen size first,
    // then the concrete init's content-derived sizing over it (ImageView::AutoResize from the sprite,
    // DialogBackground's width from its top piece, LevelCompletedView's panel from its Alex picture — all
    // of which may read a relative size just established), both before reWrap() (its wrap width comes
    // from frame_.w, already final at this point), and reWrap() before the anchors resolve —
    // updateViewAnchors only ever calls setPosition, never touches frame_.w/h, so an auto-resize-height
    // label must already be wrapped to its final height before it, or a sibling anchored to that height
    // (e.g. a message below a title) resolves against the stale, pre-wrap one. Every concrete view wraps
    // its labels' text before the owning view's own final updateViewAnchors(true, true) call.
    recomputeRelativeFrameRecursive();
    recomputeAutoSizeRecursive();
    recomputePaddingRecursive();
    reWrapRecursive();
    updateViewAnchors(true, true);
}

void View::setViewAnchor(Anchor self, Anchor target, const std::string& nameH, const std::string& nameV) {
    selfAnchor_ = self;
    targetAnchor_ = target;
    targetNameH_ = nameH;
    targetNameV_ = nameV;
}

void View::addSubview(View* v) {
    if (std::find(subviews_.begin(), subviews_.end(), v) != subviews_.end()) return;
    v->parent_ = this;
    subviews_.push_back(v);
    v->setParentScene(scene_);
}

void View::insertSubview(View* v, std::size_t index) {
    // AddSubview at a position: the original adds its views in draw order; the remake builds some of
    // them (GameView's dim view) after their later siblings.
    if (std::find(subviews_.begin(), subviews_.end(), v) != subviews_.end()) return;
    v->parent_ = this;
    if (index > subviews_.size()) index = subviews_.size();
    subviews_.insert(subviews_.begin() + static_cast<std::ptrdiff_t>(index), v);
    v->setParentScene(scene_);
}

void View::removeSubview(View* v) {
    const auto it = std::find(subviews_.begin(), subviews_.end(), v);
    if (it == subviews_.end()) return;
    (*it)->parent_ = nullptr;
    subviews_.erase(it);
}

View* View::findViewByName(const std::string& name) {
    if (name_ == name) return this;
    for (View* v : subviews_) {
        if (View* found = v->findViewByName(name)) return found;
    }
    return nullptr;
}

void View::setParentScene(Scene* scene) {
    scene_ = scene;
    for (View* v : subviews_) v->setParentScene(scene);
}

void View::updateViewAnchors(bool recurse, bool resolveTargets) {
    // 1. Resolve the anchor targets by name among the subviews of every ancestor [verified].
    if (resolveTargets && ((!targetNameH_.empty() && !targetH_) || (!targetNameV_.empty() && !targetV_))) {
        for (View* p = parent_; p; p = p->parent_) {
            const std::vector<View*> siblings = p->subviews_;
            for (View* s : siblings) {
                if (!s) continue;
                if (s->name_ == targetNameH_) targetH_ = s;
                if (s->name_ == targetNameV_) targetV_ = s;
                const bool need = (!targetNameH_.empty() && !targetH_) || (!targetNameV_.empty() && !targetV_);
                if (!need) break;
            }
            const bool need = (!targetNameH_.empty() && !targetH_) || (!targetNameV_.empty() && !targetV_);
            if (!need) break;
        }
    }
    float w = screenW(*ctx_);
    float h = screenH(*ctx_);
    if (targetH_) {
        if (resolveTargets) targetH_->updateViewAnchors(false, true);
        if (targetH_ == parent_) w = parent_->frame_.w;
    }
    if (targetV_) {
        if (resolveTargets) targetV_->updateViewAnchors(false, true);
        if (targetV_ == parent_) h = parent_->frame_.h;
    }
    const bool hTargetOther = targetH_ && targetH_ != parent_;
    const bool vTargetOther = targetV_ && targetV_ != parent_;
    // 2. The target anchor point.
    float x = 0.0f;
    switch (targetAnchor_.h) {
    case HAnchor::Left: x = hTargetOther ? targetH_->frame_.x + targetH_->padding_[0] : 0.0f; break;
    case HAnchor::Center:
        x = hTargetOther ? targetH_->frame_.x + targetH_->padding_[0] + (targetH_->frame_.w + targetH_->padding_[1]) * 0.5f : w * 0.5f;
        break;
    case HAnchor::Right:
        x = hTargetOther ? targetH_->frame_.x + targetH_->padding_[0] + targetH_->frame_.w + targetH_->padding_[1] : w;
        break;
    case HAnchor::Pivot: x = hTargetOther ? targetH_->pivot_.x : 0.0f; break;
    default: {
        const float px = relativeX_ ? 0.0f : frame_.x;
        if (selfAnchor_.h == HAnchor::Right) x = px + w;
        else if (selfAnchor_.h == HAnchor::Center) x = px + w * 0.5f;
        else x = px;
        break;
    }
    }
    float y = 0.0f;
    switch (targetAnchor_.v) {
    case VAnchor::Top: y = vTargetOther ? targetV_->frame_.y + targetV_->padding_[2] : 0.0f; break;
    case VAnchor::Center:
        y = vTargetOther ? targetV_->frame_.y + targetV_->padding_[2] + (targetV_->frame_.h + targetV_->padding_[3]) * 0.5f : h * 0.5f;
        break;
    case VAnchor::Bottom:
        y = vTargetOther ? targetV_->frame_.y + targetV_->padding_[2] + targetV_->frame_.h + targetV_->padding_[3] : h;
        break;
    case VAnchor::Pivot:
    case VAnchor::Baseline: y = vTargetOther ? targetV_->pivot_.y : 0.0f; break;
    default: {
        const float py = relativeY_ ? 0.0f : frame_.y;
        if (selfAnchor_.v == VAnchor::Center) y = py + h * 0.5f;
        else if (selfAnchor_.v == VAnchor::Bottom) y = py + h;
        else y = py;
        break;
    }
    }
    // 3. The self anchor offset; the percentage is multiplied by AnchorAspectCorrectionFactor only when
    //    a target view exists and the self anchor is LEFT / RIGHT (TOP / BOTTOM) [verified].
    const float fx = targetH_ ? ctx_->screen.anchorCorrectionX : 1.0f;
    const float fy = targetV_ ? ctx_->screen.anchorCorrectionY : 1.0f;
    switch (selfAnchor_.h) {
    case HAnchor::Left:
        if (relativeX_) x = x + w * 0.01f * (relativePos_.x * fx) + padding_[0];
        break;
    case HAnchor::Center:
        if (!relativeX_) x = x - (frame_.w + padding_[1]) * 0.5f;
        else x = (x - frame_.w * 0.5f) + w * 0.01f * relativePos_.x + (padding_[1] - padding_[0]) * 0.5f;
        break;
    case HAnchor::Right:
        if (!relativeX_) x = x - (frame_.w + padding_[1]);
        else x = ((x - frame_.w) - w * 0.01f * (relativePos_.x * fx)) - padding_[1];
        break;
    case HAnchor::Pivot:
        if (!relativeX_) x = x + (pivot_.x - (frame_.w + padding_[1])) * 0.5f;
        else x = (x - pivot_.x) + w * 0.01f * relativePos_.x + (padding_[1] - padding_[0]) * 0.5f;
        break;
    default: break;
    }
    switch (selfAnchor_.v) {
    case VAnchor::Top:
        if (relativeY_) y = y + h * 0.01f * (relativePos_.y * fy) + padding_[2];
        break;
    case VAnchor::Center:
        if (!relativeY_) y = y - (frame_.h + padding_[3]) * 0.5f;
        else y = (y - frame_.h * 0.5f) + h * 0.01f * relativePos_.y + (padding_[3] - padding_[2]) * 0.5f;
        break;
    case VAnchor::Bottom:
        if (!relativeY_) y = y - (frame_.h + padding_[3]);
        else y = ((y - frame_.h) - h * 0.01f * (relativePos_.y * fy)) - padding_[3];
        break;
    case VAnchor::Pivot:
    case VAnchor::Baseline:
        if (!relativeY_) y = y + (pivot_.y - (frame_.h + padding_[3])) * 0.5f;
        else y = (y - pivot_.y) + h * 0.01f * relativePos_.y + (padding_[3] - padding_[2]) * 0.5f;
        break;
    default: break;
    }
    if (selfAnchor_.h != HAnchor::None || selfAnchor_.v != VAnchor::None) setPosition(Point{x, y});
    if (recurse) {
        for (std::size_t i = subviews_.size(); i-- > 0;) {
            if (subviews_[i]) subviews_[i]->updateViewAnchors(true, true);
        }
    }
}

void View::baseDraw(Renderer& renderer, const Rect& rect) {
    if (!visible_ || !(scale_ > 0.0f)) return;
    // The angle sum and alpha product up the parents.
    float angle = 0.0f;
    float alpha = 1.0f;
    for (const View* v = this; v; v = v->parent_) {
        angle += v->angle_;
        alpha *= v->alpha_;
    }
    // The clip rectangle: the intersection of every clipping ancestor's real frame (in screen px).
    DrawState state;
    state.clip = Rect{0.0f, 0.0f, -1.0f, -1.0f};
    {
        float left = -10000.0f, top = -10000.0f, right = 10000.0f, bottom = 10000.0f;
        bool any = false;
        Point origin{rect.x, rect.y};
        for (const View* v = this; v; v = v->parent_) {
            if (v->isClippingSubviews()) {
                const Rect rf = v->realFrame();
                left = std::max(left, origin.x);
                top = std::max(top, origin.y);
                right = std::min(right, origin.x + rf.w);
                bottom = std::min(bottom, origin.y + rf.h);
                any = true;
            }
            origin.x -= v->frame_.x;
            origin.y -= v->frame_.y;
        }
        if (any) state.clip = Rect{left, top, right - left, bottom - top};
    }
    state.translate = Point{std::ceil((pivot_.x + rect.x) / scale_ - pivot_.x), std::ceil((pivot_.y + rect.y) / scale_ - pivot_.y)};
    state.pivot = pivot_;
    state.scale = scale_;
    state.angle = angle;
    state.alpha = alpha;
    renderer.setState(state);
    if (hasBackground_) {
        // DrawBackgroundColor [verified]: the context alpha slot is 0 for the fill; the colour's alpha is
        // multiplied by the view's own alpha (+0x14), not by the accumulated one.
        DrawState bg = state;
        bg.alpha = 0.0f;
        renderer.setState(bg);
        Color c = background_;
        c.a = static_cast<unsigned char>(static_cast<int>(static_cast<float>(c.a) * alpha_));
        renderer.drawColorRect(Rect{0.0f, 0.0f, frame_.w, frame_.h}, c);
        renderer.setState(state);
    }
    draw(renderer, rect);
    for (View* v : subviews_) {
        if (!v || !v->isVisible()) continue;
        const Rect child{rect.x + v->frame_.x, rect.y + v->frame_.y, v->frame_.w, v->frame_.h};
        v->baseDraw(renderer, child);
    }
}

void View::draw(Renderer&, const Rect&) {}

void View::update(float dt) {
    for (View* v : subviews_) {
        if (v) v->update(dt);
    }
}

void View::updateLocale() {
    for (View* v : subviews_) {
        if (v) v->updateLocale();
    }
}

bool View::isPointInView(Point p) const {
    return p.x >= 0.0f && p.x <= frame_.w && p.y >= 0.0f && p.y <= frame_.h;
}

Point View::convertPointFromView(const View& from, Point p) const {
    const Point a = from.globalPosition();
    const Point b = globalPosition();
    return Point{p.x + a.x - b.x, p.y + a.y - b.y};
}

View* View::hitTest(Point p) {
    if (!isPointInView(p)) return nullptr;
    View* current = this;
    Point point = p;
    for (;;) {
        const std::vector<View*>& subs = current->subviews_;
        View* hit = nullptr;
        Point hitPoint;
        for (std::size_t i = subs.size(); i-- > 0;) {
            View* child = subs[i];
            if (!child) continue;
            const Point cp = child->convertPointFromView(*current, point);
            if (child->isInteractable() && child->isVisible() && child->isPointInView(cp)) {
                hit = child;
                hitPoint = cp;
                break;
            }
        }
        if (!hit) return current;
        current = hit;
        point = hitPoint;
        if (current->subviews_.empty()) return current;
    }
}

// --- ImageView -------------------------------------------------------------------------------------

void ImageView::init(const aa::data::JsonNode& dict) {
    View::init(dict);
    if (dict.isNull()) return;
    if (dict.has("Background")) {
        // The full-screen aspect-fill background.
        setImage(dict.getString("Background"), false);
        setFrame(Rect{0.0f, 0.0f, screenW(*ctx_), screenH(*ctx_)});
        setDrawMode(DrawMode::Fill);
    }
    std::string name;
    bool localized = false;
    if (dict.has("LocalizedImage")) {
        name = dict.getString("LocalizedImage");
        localized = true;
    } else if (dict.has("Image")) {
        name = dict.getString("Image");
    }
    const std::string mode = dict.getString("DrawMode", "");
    if (mode == "TILE") setDrawMode(DrawMode::Tile);
    else if (mode == "FIT") setDrawMode(DrawMode::Fit);
    else if (mode == "SCALE") setDrawMode(DrawMode::Fill);
    else if (mode == "CENTER") setDrawMode(DrawMode::Center);
    if (dict.has("Angle")) setAngle(dict.getFloat("Angle"));
    if (name.empty()) return;
    setImage(name, localized);
    const bool autoResize = dict.getBool("AutoResize", false);
    autoResizeImageW_ = autoResize || dict.getBool("AutoResizeW", false);
    autoResizeImageH_ = autoResize || dict.getBool("AutoResizeH", false);
    if (autoResizeImageW_ || autoResizeImageH_) resizeFrameToImage(autoResizeImageW_, autoResizeImageH_);
}

void ImageView::setImage(const std::string& name, bool localized) {
    imageName_ = name;
    localized_ = localized;
    if (name.empty()) {
        imageSize_ = Size{};
        pivot_ = Point{};
        atlasPivot_ = Point{};
        isCompo_ = false;
        pending_ = false;
        return;
    }
    load();
}

void ImageView::load() {
    const std::string resolved = localized_ && ctx_->localization ? ctx_->localization->imageName(imageName_) : imageName_;
    const ResourceProxy& res = *ctx_->resources;
    if (res.isCompo(resolved)) {
        isCompo_ = true;
        pending_ = false;
    } else if (res.sprite(resolved).valid()) {
        isCompo_ = false;
        pending_ = false;
    } else {
        pending_ = true;
        return;
    }
    imageSize_ = res.imageSize(resolved);
    atlasPivot_ = res.imagePivot(resolved);
    pivot_ = atlasPivot_;
}

void ImageView::resizeFrameToImage(bool w, bool h) {
    Size s = size();
    if (w) s.w = imageSize_.w;
    if (h) s.h = imageSize_.h;
    setSize(s);
    // Any caller of ResizeFrameToImage(true, ...) wants this frame to track the image's native size —
    // the same intent as JSON's declarative AutoResize, just invoked imperatively (often from Setup()-style
    // code re-run on each refresh, not just Init()). Register it the same way so a live resize's uiScale
    // change (recomputeAutoSize) keeps it current even for a view whose own JSON never set AutoResize.
    autoResizeImageW_ = autoResizeImageW_ || w;
    autoResizeImageH_ = autoResizeImageH_ || h;
}

void ImageView::updateLocale() {
    View::updateLocale();
    if (!imageName_.empty()) setImage(imageName_, localized_);
}

void ImageView::recomputeAutoSize() {
    // AutoResize sizes the frame from the sprite's native pixel dimensions at the *current*
    // ctx_->resources scale (Game::applyScreenLayout keeps ResourceProxy::uiScale live on a resize) —
    // without this, a resized AutoResize view stays at whatever pixel size it loaded at construction,
    // regardless of how far the window has since moved from that scale.
    if (!autoResizeImageW_ && !autoResizeImageH_) return;
    if (!imageName_.empty()) {
        // Load() puts the sprite's own pivot back; a pivot a caller replaced (the view's centre, for a
        // zoom / pop animation) must scale with the image instead — GameView's travelling Alex would
        // otherwise zoom around the sprite's atlas pivot after a resize mid-level.
        const Size oldSize = imageSize_;
        const Point oldPivot = pivot_;
        const bool ownPivot = oldPivot.x != atlasPivot_.x || oldPivot.y != atlasPivot_.y;
        load();
        if (ownPivot) {
            pivot_ = Point{oldSize.w > 0.0f ? oldPivot.x * imageSize_.w / oldSize.w : oldPivot.x,
                           oldSize.h > 0.0f ? oldPivot.y * imageSize_.h / oldSize.h : oldPivot.y};
        }
    }
    resizeFrameToImage(autoResizeImageW_, autoResizeImageH_);
}

void ImageView::draw(Renderer& renderer, const Rect& rect) {
    View::draw(renderer, rect);
    if (imageName_.empty()) return;
    if (pending_) load();
    if (pending_) return;
    const ResourceProxy& res = *ctx_->resources;
    const std::string resolved = localized_ && ctx_->localization ? ctx_->localization->imageName(imageName_) : imageName_;
    const float k = res.uiScale();
    if (isCompo_) {
        // CompoSprite::draw at (0, 0) with the top-left anchor: every part at pivot + (dx, dy).
        const aa::data::CompoSprite* c = res.compo(resolved);
        if (!c) return;
        const Rect b = res.compoBounds(*c);
        for (const aa::data::CompoPart& p : c->parts) {
            const SpriteRef s = res.sprite(p.sprite);
            if (!s.valid()) continue;
            const float x = (-b.x + static_cast<float>(p.dx - s.sprite->pivotX)) * k;
            const float y = (-b.y + static_cast<float>(p.dy - s.sprite->pivotY)) * k;
            renderer.drawSprite(s, x, y, static_cast<float>(s.sprite->w) * k, static_cast<float>(s.sprite->h) * k);
        }
        return;
    }
    const SpriteRef s = res.sprite(resolved);
    if (!s.valid()) return;
    const float iw = imageSize_.w;
    const float ih = imageSize_.h;
    switch (drawMode_) {
    case DrawMode::Stretch: renderer.drawSprite(s, 0.0f, 0.0f, std::ceil(frame_.w), std::ceil(frame_.h)); break;
    case DrawMode::Fit:
    case DrawMode::Fill: {
        if (iw <= 0.0f || ih <= 0.0f) return;
        const float sx = frame_.w / iw;
        const float sy = frame_.h / ih;
        const float f = drawMode_ == DrawMode::Fit ? std::min(sx, sy) : std::max(sx, sy);
        const float w = static_cast<float>(static_cast<int>(std::ceil(iw * f)));
        const float h = static_cast<float>(static_cast<int>(std::ceil(ih * f)));
        renderer.drawSprite(s, std::ceil((frame_.w - w) * 0.5f), std::ceil((frame_.h - h) * 0.5f), w, h);
        break;
    }
    case DrawMode::Tile: {
        if (iw <= 0.0f || ih <= 0.0f) return;
        const int nx = static_cast<int>(std::ceil(frame_.w / iw));
        const int ny = static_cast<int>(std::ceil(frame_.h / ih));
        // The tiles are clipped to the view rectangle (the original narrows the context's clip).
        for (int i = 0; i < nx; ++i) {
            for (int j = 0; j < ny; ++j) {
                const float x = iw * static_cast<float>(i);
                const float y = ih * static_cast<float>(j);
                const float w = std::min(iw, frame_.w - x);
                const float h = std::min(ih, frame_.h - y);
                if (w <= 0.0f || h <= 0.0f) continue;
                // A partial tile: shrink the source rectangle proportionally.
                aa::data::UiSprite part = *s.sprite;
                part.w = static_cast<int>(std::lround(static_cast<double>(w / k)));
                part.h = static_cast<int>(std::lround(static_cast<double>(h / k)));
                SpriteRef ref;
                ref.sheet = s.sheet;
                ref.sprite = &part;
                renderer.drawSprite(ref, x, y, w, h);
            }
        }
        break;
    }
    case DrawMode::Center:
        renderer.drawSprite(s, std::ceil((frame_.w - iw) * 0.5f), std::ceil((frame_.h - ih) * 0.5f), iw, ih);
        break;
    }
}

// --- LabelView -------------------------------------------------------------------------------------

LabelView::LabelView(UiContext& ctx) : View(ctx) { setInteraction(false); }

void LabelView::init(const aa::data::JsonNode& dict) {
    View::init(dict);
    if (fontName_.empty()) fontName_ = "FONT_1";   // ResourceProxy::GetDefaultFontName
    if (dict.isNull()) return;
    if (dict.has("Font")) setFont(dict.getString("Font"));
    FontAnchorH h = FontAnchorH::Left;
    FontAnchorV v = FontAnchorV::Top;
    if (dict.has("FontAnchorH")) h = fontAnchorHFromString(dict.getString("FontAnchorH"));
    if (dict.has("FontAnchorV")) v = fontAnchorVFromString(dict.getString("FontAnchorV"));
    setAnchor(h, v);
    if (dict.getBool("AutoResize", false)) {
        autoResizeW_ = true;
        autoResizeH_ = true;
    }
    if (dict.getBool("AutoResizeW", false)) autoResizeW_ = true;
    if (dict.getBool("AutoResizeH", false)) autoResizeH_ = true;
    if (dict.has("Text")) setText(dict.getString("Text"));
    if (dict.has("TitleText")) setText(dict.getString("TitleText") + "_HD");
    if (dict.has("Wrapping")) setWordWrapping(dict.getBool("Wrapping", true));
}

void LabelView::setText(const std::string& id) {
    textId_ = id;
    text_ = localizedText(*ctx_, id);
    wrapText(text_);
}

void LabelView::setNonLocalizedText(const std::string& text) {
    textId_.clear();
    text_ = text;
    wrapText(text_);
}

void LabelView::reWrap() {
    if (!wordWrapping_) return;
    wrapText(text_);
}

void LabelView::updateLocale() {
    View::updateLocale();
    if (!textId_.empty()) setText(textId_);
}

namespace {

// The WrapText tokeniser: words, single spaces and line breaks ("\n" literal or a newline character).
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string word;
    bool escape = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\\' && !escape) {
            escape = true;
            continue;
        }
        if (escape && c == 'n') c = '\n';
        escape = false;
        if (c == '\n') {
            tokens.push_back(word);
            tokens.push_back("\n");
            word.clear();
        } else if (c == ' ') {
            tokens.push_back(word);
            tokens.push_back(" ");
            word.clear();
        } else {
            word.push_back(c);
        }
    }
    if (!word.empty()) tokens.push_back(word);
    return tokens;
}

// Drops the last code point of a UTF-8 string.
void dropLastChar(std::string& s) {
    if (s.empty()) return;
    std::size_t n = s.size();
    // Skip continuation bytes (10xxxxxx) back to the lead byte.
    do {
        --n;
    } while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80);
    s.resize(n);
}

}  // namespace

void LabelView::wrapText(const std::string& text) {
    lines_.clear();
    const BitmapFont* font = fontName_.empty() ? nullptr : ctx_->resources->font(fontName_);
    if (text.empty() || !font) {
        lines_.push_back("");
        return;
    }
    if (!autoResizeW_ && !(frame_.w > 0.0f)) {
        lines_.push_back("");
        return;
    }
    if (!autoResizeH_ && !(frame_.h > 0.0f)) {
        lines_.push_back("");
        return;
    }
    const float width = frame_.w;
    auto fits = [&](const std::string& s) { return autoResizeW_ || font->stringWidth(s) < width; };
    std::vector<std::string> tokens = tokenize(text);
    std::string line;
    bool stop = false;
    for (std::size_t i = 0; i < tokens.size() && !stop; ++i) {
        std::string word = tokens[i];
        if (word == "\n") {
            lines_.push_back(line);
            line.clear();
            continue;
        }
        if (word == " " || word.empty()) continue;
        // A word wider than the view is split; the remainder becomes the next token.
        if (!autoResizeW_ && !(font->stringWidth(word) < width)) {
            std::string head = word;
            while (!head.empty() && !(font->stringWidth(head) < width)) dropLastChar(head);
            if (head.empty()) head = word.substr(0, 1);
            const std::string rest = word.substr(head.size());
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(i) + 1, rest);
            word = head;
        }
        if (maxLetters_ > 0) {
            const std::string joined = line + word;
            if (static_cast<int>(decodeUtf8(joined).size()) > maxLetters_) {
                std::string cut = joined;
                while (static_cast<int>(decodeUtf8(cut).size()) > std::max(0, maxLetters_ - 3)) dropLastChar(cut);
                line = cut + "...";
                stop = true;
                break;
            }
        }
        const std::string candidate = line + word;
        if (!fits(candidate)) {
            if (maxRows_ > 0 && maxRows_ <= static_cast<int>(lines_.size()) + 1) {
                // The last allowed row: the current line gets an ellipsis that fits.
                std::string cut = line;
                while (!cut.empty() && !fits(cut + "...")) dropLastChar(cut);
                lines_.push_back(cut + "...");
                line.clear();
                stop = true;
                break;
            }
            lines_.push_back(line);
            line = word + " ";
        } else {
            line = candidate;
            if (i + 1 < tokens.size()) line += " ";
        }
    }
    if (!stop && !line.empty()) lines_.push_back(line);
    for (std::string& l : lines_) {
        while (!l.empty() && l.back() == ' ') l.pop_back();
    }
    if (lines_.empty()) lines_.push_back("");
    if (autoResizeW_ || autoResizeH_) {
        float maxW = 0.0f;
        float totalH = font->maxDescending();
        for (const std::string& l : lines_) {
            maxW = std::max(maxW, font->stringWidth(l));
            totalH += font->leading();
        }
        Rect f = frame_;
        if (autoResizeW_) f.w = maxW;
        if (autoResizeH_) f.h = totalH;
        setFrame(f);
    }
}

Point LabelView::lineOrigin(const BitmapFont& font, const Rect& rect) const {
    float x = 0.0f;
    if (anchorH_ == FontAnchorH::Center) x = rect.w * 0.5f;
    else if (anchorH_ == FontAnchorH::Right) x = rect.w;
    float y = 0.0f;
    const int n = static_cast<int>(lines_.size());
    if (anchorV_ == FontAnchorV::Center) {
        y = rect.h * 0.5f;
        if (wordWrapping_) y -= static_cast<float>(((n - 1) * static_cast<int>(font.data().leading)) / 2) * font.scale();
    } else if (anchorV_ == FontAnchorV::Bottom) {
        y = rect.h;
        if (wordWrapping_) y -= static_cast<float>((n - 1) * font.data().leading) * font.scale();
    }
    return Point{x, y};
}

void LabelView::draw(Renderer& renderer, const Rect& rect) {
    View::draw(renderer, rect);
    const BitmapFont* font = ctx_->resources->font(fontName_);
    if (!font) return;
    const Point o = lineOrigin(*font, rect);
    if (!wordWrapping_) {
        font->drawString(renderer, text_, o.x, o.y, anchorV_, anchorH_);
        return;
    }
    float y = o.y;
    for (const std::string& l : lines_) {
        font->drawString(renderer, l, o.x, y, anchorV_, anchorH_);
        y += font->leading();
    }
}

// --- OutlineLabelView ------------------------------------------------------------------------------

OutlineLabelView::OutlineLabelView(UiContext& ctx) : LabelView(ctx), inner_(std::make_unique<LabelView>(ctx)) {
    inner_->setViewName("OutlineInner");
    View::addSubview(inner_.get());
}

void OutlineLabelView::init(const aa::data::JsonNode& dict) {
    LabelView::init(dict);
    // The inner label shares the anchors, wrapping and auto-resize flags.
    inner_->setAnchor(anchorH_, anchorV_);
    inner_->setAutoResize(autoResizeW_, autoResizeH_);
    inner_->setWordWrapping(wordWrapping_);
    if (!textId_.empty()) inner_->setText(textId_);
    else if (!text_.empty()) inner_->setNonLocalizedText(text_);
}

void OutlineLabelView::updateOffsets() {
    if (outlineFontName_.empty()) return;
    const aa::data::FontOutline* o = ctx_->resources->outline(outlineFontName_);
    if (!o) return;
    offsetX_ = static_cast<int>(static_cast<float>(o->offsetX) * ctx_->screen.uiScale);
    offsetY_ = static_cast<int>(static_cast<float>(o->offsetY) * ctx_->screen.uiScale);
    inner_->setFrame(Rect{static_cast<float>(offsetX_), static_cast<float>(offsetY_), frame_.w - static_cast<float>(2 * offsetX_),
                          frame_.h - static_cast<float>(2 * offsetY_)});
}

void OutlineLabelView::setFont(const std::string& font) {
    // SetFont [verified]: the Fonts.xml entry gives the outline font and offsets; the inner label sits at
    // (offsetX, offsetY) with the fill font, this view draws the outline font.
    outlineFontName_ = font;
    const aa::data::FontOutline* o = ctx_->resources->outline(font);
    if (!o) {
        LabelView::setFont(font);
        inner_->setFont(font);
        return;
    }
    updateOffsets();
    inner_->setFont(font);
    LabelView::setFont(o->outlineFont);
}

void OutlineLabelView::setText(const std::string& id) {
    LabelView::setText(id);
    inner_->setAnchor(anchorH_, anchorV_);
    inner_->setAutoResize(autoResizeW_, autoResizeH_);
    inner_->setWordWrapping(wordWrapping_);
    inner_->setText(id);
}

void OutlineLabelView::setNonLocalizedText(const std::string& text) {
    LabelView::setNonLocalizedText(text);
    inner_->setAnchor(anchorH_, anchorV_);
    inner_->setAutoResize(autoResizeW_, autoResizeH_);
    inner_->setWordWrapping(wordWrapping_);
    inner_->setNonLocalizedText(text);
}

void OutlineLabelView::setSize(Size s) {
    Size cur = size();
    if (s.w == -1.0f) s.w = cur.w;
    if (s.h == -1.0f) s.h = cur.h;
    View::setSize(s);
    inner_->setSize(Size{frame_.w - static_cast<float>(2 * offsetX_), frame_.h - static_cast<float>(2 * offsetY_)});
    inner_->setPosition(Point{static_cast<float>(offsetX_), static_cast<float>(offsetY_)});
    updateViewAnchors(true, false);
}

void OutlineLabelView::reWrap() {
    // Redo offsetX_/offsetY_ (uiScale-dependent) and inner_'s frame (dependent on both those and this
    // view's own frame_, which a Relative.W/H resize moves without going through the virtual setSize()
    // above) before either label re-wraps against it.
    updateOffsets();
    LabelView::reWrap();
    inner_->reWrap();
}

void OutlineLabelView::setAlpha(float a) {
    View::setAlpha(a);
    inner_->setAlpha(1.0f);   // the product with the parent's alpha applies
}

void OutlineLabelView::setPosition(Point p) { View::setPosition(p); }

void OutlineLabelView::setScale(float s) {
    View::setScale(s);
    inner_->setScale(1.0f);
}

// --- HighlightLabelView ----------------------------------------------------------------------------

void HighlightLabelView::init(const aa::data::JsonNode& dict) {
    LabelView::init(dict);
    if (!dict.isNull() && dict.has("HilightFont")) setHighlightFont(dict.getString("HilightFont"));
}

void HighlightLabelView::wrapText(const std::string& text) {
    // The markers are not drawn: wrap the text without them so the line widths are right, then put the
    // markers back per line by tracking the highlight state through the original text.
    LabelView::wrapText(text);
}

float HighlightLabelView::segmentedWidth(const std::string& line, const BitmapFont& normal, const BitmapFont& highlight, bool& state) const {
    float width = 0.0f;
    std::string segment;
    auto flush = [&]() {
        if (segment.empty()) return;
        width += (state ? highlight : normal).stringWidth(segment);
        segment.clear();
    };
    for (char c : line) {
        if (c == '*') {
            flush();
            state = !state;
        } else {
            segment.push_back(c);
        }
    }
    flush();
    return width;
}

void HighlightLabelView::draw(Renderer& renderer, const Rect& rect) {
    View::draw(renderer, rect);
    const BitmapFont* normal = ctx_->resources->font(fontName_);
    if (!normal) return;
    const BitmapFont* highlight = highlightFont_.empty() ? normal : ctx_->resources->font(highlightFont_);
    if (!highlight) highlight = normal;
    const float leading = std::max(normal->leading(), highlight->leading());
    // The line origin uses the larger leading of the two fonts.
    float y = 0.0f;
    const int n = static_cast<int>(lines_.size());
    if (anchorV_ == FontAnchorV::Center) y = rect.h * 0.5f - (wordWrapping_ ? leading * static_cast<float>(n - 1) * 0.5f : 0.0f);
    else if (anchorV_ == FontAnchorV::Bottom) y = rect.h - (wordWrapping_ ? leading * static_cast<float>(n - 1) : 0.0f);
    bool state = false;
    for (const std::string& line : lines_) {
        bool measureState = state;
        const float width = segmentedWidth(line, *normal, *highlight, measureState);
        float x = 0.0f;
        if (anchorH_ == FontAnchorH::Center) x = rect.w * 0.5f - width * 0.5f;
        else if (anchorH_ == FontAnchorH::Right) x = rect.w - width;
        std::string segment;
        auto flush = [&]() {
            if (segment.empty()) return;
            const BitmapFont& f = state ? *highlight : *normal;
            f.drawString(renderer, segment, x, y, anchorV_, FontAnchorH::Left);
            x += f.stringWidth(segment) + f.tracking();
            segment.clear();
        };
        for (char c : line) {
            if (c == '*') {
                flush();
                state = !state;
            } else {
                segment.push_back(c);
            }
        }
        flush();
        y += leading;
    }
}

bool View::keyDown(int key) {
    for (std::size_t i = subviews_.size(); i-- > 0;) {
        View* v = subviews_[i];
        if (v && v->isInteractable() && v->isVisible() && v->keyDown(key)) return true;
    }
    return false;
}

}  // namespace aa::ui
