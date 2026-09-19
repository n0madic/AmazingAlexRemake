#include "aa/ui/dialogs.h"

#include "aa/ui/scene.h"

namespace aa::ui {

namespace {

constexpr float kTitleGap = 10.0f;   // MessageDialog::Init: the title keeps 10 px above the message

template <class T, class... Args>
T* make(std::vector<std::unique_ptr<View>>& owned, Args&&... args) {
    auto v = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = v.get();
    owned.push_back(std::move(v));
    return raw;
}

bool isBackKey(int key) { return key == SceneManager::kKeyBack || key == SceneManager::kKeyAndroidBack; }

// SetMessage [verified]: after the text, the label's width = the background's inner width, re-wrapped.
void fitMessage(LabelView& message, const DialogBackground& background) {
    const float* pad = message.padding();
    message.setSize(Size{background.size().w - pad[0] - pad[1], message.size().h});   // a plain LabelView keeps no "-1" height
    message.reWrap();
}

}  // namespace

// --- DialogBackground -------------------------------------------------------------------------------

void DialogBackground::init(const aa::data::JsonNode& dict) {
    // DialogBackground::Init [verified]: View::Init(dict); ImageTop / ImageMiddle / ImageBottom (the
    // "Wide" variants when SetWide), added middle, bottom, top; the frame's width from the top piece; the
    // middle piece's height = the frame's height minus the top and bottom pieces; anchors resolved.
    View::init(dict);
    const std::string suffix = wide_ ? "Wide" : "";
    top_ = make<ImageView>(owned_, *ctx_);
    top_->setViewName("ImageTop" + suffix);
    top_->init(dict.optional(("ImageTop" + suffix).c_str()));
    middle_ = make<ImageView>(owned_, *ctx_);
    middle_->setViewName("ImageMiddle" + suffix);
    middle_->init(dict.optional(("ImageMiddle" + suffix).c_str()));
    bottom_ = make<ImageView>(owned_, *ctx_);
    bottom_->setViewName("ImageBottom" + suffix);
    bottom_->init(dict.optional(("ImageBottom" + suffix).c_str()));
    addSubview(middle_);
    addSubview(bottom_);
    addSubview(top_);
    setSize(Size{top_->size().w, size().h});
    middle_->setSize(Size{middle_->size().w, size().h - top_->size().h - bottom_->size().h});
    updateViewAnchors(true, false);
}

void DialogBackground::recomputeAutoSize() {
    setSize(Size{top_->size().w, size().h});
}

void DialogBackground::relayout() {
    // recomputeAutoSize()'s setSize also rounds the height the relative-frame pass wrote unrounded, the
    // way Init's own tail does before its final updateViewAnchors — a sibling (e.g. ImageBottom) anchored
    // to this view's bottom edge resolves against the rounded height, as at construction.
    View::relayout();
    middle_->setSize(Size{middle_->size().w, size().h - top_->size().h - bottom_->size().h});
}

// --- InfoDialog -------------------------------------------------------------------------------------

InfoDialog::InfoDialog(UiContext& ctx) : View(ctx) {}

void InfoDialog::init(const aa::data::JsonNode& dict, const aa::data::JsonNode& dialogs) {
    // InfoDialog::Init [verified]: View::Init(the scene's entry); the type's layout from Dialogs.json;
    // an inert full-frame "InvisibleBackground"; the background wide for a LegalDialog; Background/Message;
    // ConfirmButton; the scene's own Message dictionary re-initialises the label, sized to the background's
    // inner width and re-wrapped.
    View::init(dict);
    const std::string type = dict.getString("DialogType", "InfoDialog");
    const aa::data::JsonNode base = dialogs.optional(type.c_str());
    shield_ = make<View>(owned_, *ctx_);
    shield_->setViewName("InvisibleBackground");
    shield_->init();
    shield_->setFrame(Rect{0.0f, 0.0f, frame_.w, frame_.h});
    shield_->setInteraction(false);
    const aa::data::JsonNode bg = base.optional("Background");
    background_ = make<DialogBackground>(owned_, *ctx_);
    background_->setViewName("Background");
    background_->setWide(type == "LegalDialog");
    background_->init(bg);
    message_ = make<LabelView>(owned_, *ctx_);
    message_->setViewName("Message");
    message_->init(bg.optional("Message"));
    confirm_ = make<Button>(owned_, *ctx_);
    confirm_->setViewName("ConfirmButton");
    confirm_->init(base.optional("ConfirmButton"));
    confirm_->setDelegate(this);
    const aa::data::JsonNode own = dict.optional("Message");
    messageFitToBackground_ = own.isObject();
    if (messageFitToBackground_) {
        message_->init(own);
        fitMessage(*message_, *background_);
    }
    background_->addSubview(message_);
    addSubview(shield_);
    addSubview(background_);
    addSubview(confirm_);
    updateViewAnchors(true, true);
}

void InfoDialog::setMessage(const std::string& textId) { message_->setText(textId); }

void InfoDialog::buttonPressed(int id) {
    if (!delegate_) {
        hide();
        return;
    }
    if (id == confirm_->id()) delegate_->messageConfirmed(this->id());
}

bool InfoDialog::keyDown(int key) {
    if (View::keyDown(key)) return true;
    if (isBackKey(key)) {
        buttonPressed(confirm_->id());
        return true;
    }
    return false;
}

void InfoDialog::relayout() {
    View::relayout();
    // shield_ is a plain full-frame View sized once from this view's own frame_ at Init — resync it to
    // the frame_ View::relayout() just resolved (Relative.W/H=100%), or it stays at its construction size
    // forever (stale hit-testing / dimming past that point).
    if (shield_) shield_->setFrame(Rect{0.0f, 0.0f, frame_.w, frame_.h});
    // background_ is reached only through this explicit call — see MainMenuView::relayout.
    if (background_) background_->relayout();
    // message_ was fit to background_'s width at Init time (fitMessage) only when the scene overrode
    // Message — background_'s own width just moved (DialogBackground::recomputeAutoSize), so redo it, or
    // message_ stays at its construction-time width (and wrap) forever.
    if (messageFitToBackground_) fitMessage(*message_, *background_);
    // confirm_ is a sibling of background_ (not one of its children), Anchor-driven off its edge by name —
    // View::relayout()'s own anchor pass above already resolved it, but against background_'s height
    // before DialogBackground::relayout()'s rounding fixup; redo it now that background_ is final.
    if (confirm_) confirm_->updateViewAnchors(true, false);
}

// --- MessageDialog ----------------------------------------------------------------------------------

MessageDialog::MessageDialog(UiContext& ctx, bool single) : View(ctx), single_(single) {}

void MessageDialog::init(const aa::data::JsonNode& dict, const aa::data::JsonNode& dialogs) {
    // MessageDialog::Init [verified]: like InfoDialog with Background/Title + Background/Message
    // (OutlineLabelViews), SingleConfirmButton or ConfirmButton + CancelButton, the scene's Title /
    // Message ids; after the anchors the title moves up (in screen percent) when it would come closer
    // than 10 px to the message.
    View::init(dict);
    const std::string type = dict.getString("DialogType", "InfoDialog");
    const aa::data::JsonNode base = dialogs.optional(type.c_str());
    shield_ = make<View>(owned_, *ctx_);
    shield_->setViewName("InvisibleBackground");
    shield_->init();
    shield_->setFrame(Rect{0.0f, 0.0f, frame_.w, frame_.h});
    shield_->setInteraction(false);
    const aa::data::JsonNode bg = base.optional("Background");
    background_ = make<DialogBackground>(owned_, *ctx_);
    background_->setViewName("Background");
    background_->init(bg);
    title_ = make<OutlineLabelView>(owned_, *ctx_);
    title_->setViewName("Title");
    title_->init(bg.optional("Title"));
    message_ = make<OutlineLabelView>(owned_, *ctx_);
    message_->setViewName("Message");
    message_->init(bg.optional("Message"));
    confirm_ = make<Button>(owned_, *ctx_);
    confirm_->setViewName(single_ ? "SingleConfirmButton" : "ConfirmButton");
    confirm_->init(base.optional(single_ ? "SingleConfirmButton" : "ConfirmButton"));
    confirm_->setDelegate(this);
    if (!single_) {
        cancel_ = make<Button>(owned_, *ctx_);
        cancel_->setViewName("CancelButton");
        cancel_->init(base.optional("CancelButton"));
        cancel_->setDelegate(this);
    }
    if (dict.has("Title")) setTitle(dict.getString("Title"));
    if (dict.has("Message")) setMessage(dict.getString("Message"));
    background_->addSubview(title_);
    background_->addSubview(message_);
    addSubview(shield_);
    addSubview(background_);
    addSubview(confirm_);
    if (cancel_) addSubview(cancel_);
    updateViewAnchors(true, true);
    titleBaseRelativeY_ = title_->relativePosition().y;
    applyTitleOverlapCorrection();
}

// applyTitleOverlapCorrection [verified: MessageDialog::Init's tail]: when the title would come closer
// than kTitleGap px to the message, it moves up by the overlap, expressed in screen percent (Relative.Y).
// Always starts from titleBaseRelativeY_ (the as-loaded percent, before any correction) so it can be
// re-run — the correction itself mutates title_'s own Relative.Y in place, and re-measuring against an
// already-corrected baseline would compound a stale shift instead of redoing it.
void MessageDialog::applyTitleOverlapCorrection() {
    // Title is Anchor-driven (VCENTER against Background, whose own height is a fraction of the screen's),
    // so SetRelativePosition alone — which writes frame_.y on a plain percent-of-screen-height basis — does
    // not land title_ at its true rendered position; re-run its anchor so titleBottom below, and the frame_
    // this leaves behind, match what a subsequent UpdateViewAnchors pass (or a fresh construction) would
    // produce.
    Point rel = title_->relativePosition();
    rel.y = titleBaseRelativeY_;
    title_->setRelativePosition(rel);
    title_->updateViewAnchors(true, false);
    const float messageTop = message_->position().y;
    const float titleBottom = title_->position().y + title_->size().h + kTitleGap;
    if (titleBottom > messageTop) {
        rel.y = rel.y - (titleBottom - messageTop) * 100.0f / ctx_->screen.nativeHeight;
        title_->setRelativePosition(rel);
        title_->updateViewAnchors(true, false);
    }
}

void MessageDialog::relayout() {
    View::relayout();
    // shield_ is a plain full-frame View sized once from this view's own frame_ at Init — resync it to
    // the frame_ View::relayout() just resolved (Relative.W/H=100%), or it stays at its construction size
    // forever (stale hit-testing / dimming past that point).
    if (shield_) shield_->setFrame(Rect{0.0f, 0.0f, frame_.w, frame_.h});
    // background_ is reached only through this explicit call — see MainMenuView::relayout.
    if (background_) background_->relayout();
    // message_'s width was fit to background_'s at SetMessage/Init time (fitMessage) — background_'s own
    // width just moved (DialogBackground::recomputeAutoSize), so redo it, or message_ stays at its
    // construction-time width (and wrap) forever.
    fitMessage(*message_, *background_);
    applyTitleOverlapCorrection();
    // confirm_ / cancel_ are siblings of background_ (not its children), Anchor-driven off its edge by
    // name — View::relayout()'s own anchor pass above already resolved them, but against background_'s
    // height before DialogBackground::relayout()'s rounding fixup; redo it now that background_ is final.
    if (confirm_) confirm_->updateViewAnchors(true, false);
    if (cancel_) cancel_->updateViewAnchors(true, false);
}

void MessageDialog::setTitle(const std::string& textId) { title_->setText(textId); }

void MessageDialog::setMessage(const std::string& textId) {
    message_->setText(textId);
    fitMessage(*message_, *background_);
}

void MessageDialog::buttonPressed(int id) {
    if (!delegate_) {
        hide();
        return;
    }
    if (id == confirm_->id()) delegate_->messageConfirmed(this->id());
    else if (cancel_ && id == cancel_->id()) delegate_->messageCanceled(this->id());
}

bool MessageDialog::keyDown(int key) {
    if (View::keyDown(key)) return true;
    if (isBackKey(key)) {
        buttonPressed(single_ || !cancel_ ? confirm_->id() : cancel_->id());
        return true;
    }
    return false;
}

}  // namespace aa::ui
