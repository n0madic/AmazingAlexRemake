// UI::DialogBackground / InfoDialog / MessageDialog [verified: DialogBackground::Init / SetWide,
// InfoDialog::Init / ButtonPressed / KeyDown / SetMessage / Show / Hide, MessageDialog::Init /
// ButtonPressed / KeyDown / SetTitle / SetMessage, the delegates' vtables — decompile + disassembly,
// 2026-09-14]. A dialog is a full-screen view built from the scene's dialog dictionary (`DialogType`,
// Title / Message) and the type's layout in ui/scenes/Dialogs.json: the three-piece background (the
// "Wide" pieces for a LegalDialog), the labels, the buttons. Without a delegate a button hides the dialog.
#pragma once

#include "aa/ui/button.h"
#include "aa/ui/view.h"

#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

// The stretchable message box: ImageTop / ImageMiddle (tiled, sized to the rest) / ImageBottom; the frame
// takes the top piece's width and keeps its dictionary height.
class DialogBackground : public View {
public:
    using View::init;
    explicit DialogBackground(UiContext& ctx) : View(ctx) {}
    void setWide(bool wide) { wide_ = wide; }
    void init(const aa::data::JsonNode& dict) override;
    // middle_'s height compensates for top_ / bottom_ (fixed, sprite-sized) against this view's own
    // height (screen-relative) — Init derives it once; relayout() redoes it against the current height.
    void relayout() override;
    // This view's own width (W: -1 in JSON — not Relative-driven) comes from top_'s sprite width, which a
    // live resize's AutoResize change moves; recomputeAutoSize() runs post-order (children first) after
    // the relative-frame pass, so top_->size() is already current here and the (relative) height it
    // re-sets gets setSize's rounding — must happen before updateViewAnchors resolves this view's own
    // (HCENTER-anchored) position from that width.
    void recomputeAutoSize() override;
    ImageView* top() { return top_; }
    ImageView* middle() { return middle_; }
    ImageView* bottom() { return bottom_; }

private:
    bool wide_ = false;
    ImageView* top_ = nullptr;
    ImageView* middle_ = nullptr;
    ImageView* bottom_ = nullptr;
    std::vector<std::unique_ptr<View>> owned_;
};

class InfoDialogDelegate {
public:
    virtual ~InfoDialogDelegate() = default;
    virtual void messageConfirmed(int dialogId) = 0;
};

class MessageDialogDelegate {
public:
    virtual ~MessageDialogDelegate() = default;
    virtual void messageConfirmed(int dialogId) = 0;
    virtual void messageCanceled(int dialogId) = 0;
};

// One message and one confirm button (the legal prompt: DialogType LegalDialog → the wide background and
// the scene's Message dictionary re-initialises the label, sized to the background's inner width).
class InfoDialog : public View, public ButtonDelegate {
public:
    using View::init;
    explicit InfoDialog(UiContext& ctx);
    // `dict` is the scene's dialog entry, `dialogs` the "Dialogs" node of Dialogs.json.
    void init(const aa::data::JsonNode& dict, const aa::data::JsonNode& dialogs);
    void setDelegate(InfoDialogDelegate* d) { delegate_ = d; }
    void setMessage(const std::string& textId);
    void show() { setVisible(true); }
    void hide() { setVisible(false); }
    void buttonPressed(int id) override;
    bool keyDown(int key) override;
    void relayout() override;
    Button* confirmButton() { return confirm_; }
    LabelView* message() { return message_; }
    DialogBackground* background() { return background_; }

private:
    InfoDialogDelegate* delegate_ = nullptr;
    View* shield_ = nullptr;
    DialogBackground* background_ = nullptr;
    Button* confirm_ = nullptr;
    LabelView* message_ = nullptr;
    // Init only fits message_ to background_'s width (fitMessage) when the scene's own dict overrides
    // Message — otherwise message_ keeps whatever its default (type-level) Message dict gave it, which
    // relayout() must not override.
    bool messageFitToBackground_ = false;
    std::vector<std::unique_ptr<View>> owned_;
};

// Title + message with a confirm button (single) or confirm + cancel; the back key presses the cancel
// button (the confirm one when single).
class MessageDialog : public View, public ButtonDelegate {
public:
    using View::init;
    MessageDialog(UiContext& ctx, bool single);
    void init(const aa::data::JsonNode& dict, const aa::data::JsonNode& dialogs);
    void setDelegate(MessageDialogDelegate* d) { delegate_ = d; }
    void setTitle(const std::string& textId);
    void setMessage(const std::string& textId);
    void show() { setVisible(true); }
    void hide() { setVisible(false); }
    void buttonPressed(int id) override;
    bool keyDown(int key) override;
    void relayout() override;
    Button* confirmButton() { return confirm_; }
    Button* cancelButton() { return cancel_; }
    OutlineLabelView* title() { return title_; }
    OutlineLabelView* message() { return message_; }
    DialogBackground* background() { return background_; }
    bool isSingle() const { return single_; }

private:
    // The one-time title/message overlap fix-up (Init's tail) mutates title_'s own Relative.Y percent in
    // place, so relayout() needs the pre-correction baseline to redo it from scratch instead of compounding
    // a stale, pre-resize shift on top of itself.
    void applyTitleOverlapCorrection();
    float titleBaseRelativeY_ = 0.0f;
    bool single_;
    MessageDialogDelegate* delegate_ = nullptr;
    View* shield_ = nullptr;
    DialogBackground* background_ = nullptr;
    Button* confirm_ = nullptr;
    Button* cancel_ = nullptr;
    OutlineLabelView* title_ = nullptr;
    OutlineLabelView* message_ = nullptr;
    std::vector<std::unique_ptr<View>> owned_;
};

}  // namespace aa::ui
