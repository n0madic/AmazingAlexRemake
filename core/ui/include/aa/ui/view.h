// UI::View and the image / label views (docs/06 §1) [verified: View::View / Init / UpdateViewAnchors /
// BaseDraw / HitTest / SetPosition / SetSize / GetRealFrame, ImageView::Init / Load / Draw, LabelView::Init /
// Draw / WrapText, OutlineLabelView, HighlightLabelView — decompile + disassembly, 2026-09-14].
// A view has a frame in its parent's px (position ceiled, size rounded), a pivot, scale, angle and alpha,
// the four-way padding, the anchor description (self / target anchors, target view names) and the
// percent-of-screen relative position and size; subviews draw and hit-test in reverse order of addition.
#pragma once

#include "aa/data/json.h"
#include "aa/game/localization.h"
#include "aa/ui/renderer.h"
#include "aa/ui/resources.h"
#include "aa/ui/types.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

class Animator;
class Scene;

// What every view reaches through its scene: the resources, the screen numbers, the localisation, a
// sound player (UI clips) and the animator.
class SoundPlayer {
public:
    virtual ~SoundPlayer() = default;
    virtual void playUiSound(int audioId, float volume) = 0;
};

struct UiContext {
    ResourceProxy* resources = nullptr;
    ScreenParams screen;
    const aa::game::Localization* localization = nullptr;
    SoundPlayer* sounds = nullptr;
    Animator* animator = nullptr;
};

class View {
public:
    explicit View(UiContext& ctx);
    virtual ~View();
    View(const View&) = delete;
    View& operator=(const View&) = delete;

    UiContext& ctx() const { return *ctx_; }

    // View::Init(DataDictionary): BackgroundColor {R,G,B,A}, X/Y/W/H (ints), Relative {X,Y,W,H,Padding},
    // Padding {Left,Right,Top,Bottom}, Anchor {H {Self, View {Name, Target}}, V {…}}.
    virtual void init(const aa::data::JsonNode& dict);
    // Init with no dictionary (a plain container).
    void init();

    // --- frame ---------------------------------------------------------------------------------
    Point position() const { return Point{frame_.x, frame_.y}; }
    Size size() const { return Size{frame_.w, frame_.h}; }
    Rect frame() const { return frame_; }
    virtual void setPosition(Point p);        // ceilf
    virtual void setSize(Size s);             // floorf(x + 0.5)
    virtual void setFrame(const Rect& r);     // setPosition + setSize
    Point center() const;                     // ceil(w/2), ceil(h/2)
    void setCenter(Point c);                  // ceil(c − size/2)
    Point globalPosition() const;             // the sum of the positions up the parents
    // GetRealFrame: (ceil x + left, ceil y + top, ceil(scale·w) + right, ceil(scale·h) + bottom).
    Rect realFrame() const;
    Point pivot() const { return pivot_; }
    virtual void setPivot(Point p) { pivot_ = p; }
    float scale() const { return scale_; }
    virtual void setScale(float s) { scale_ = s; }
    float alpha() const { return alpha_; }
    virtual void setAlpha(float a) { alpha_ = a; }
    float angle() const { return angle_; }
    virtual void setAngle(float a) { angle_ = a; }
    void setPadding(float left, float right, float top, float bottom);
    const float* padding() const { return padding_; }
    void setRelative(bool x, bool y, bool w, bool h);
    void setRelativePosition(Point percent);   // frame.xy = screen × 0.01 × percent for the relative axes
    Point relativePosition() const { return relativePos_; }
    void setRelativeSize(Size percent);
    // relayout(): re-derives everything View::init(dict) computed once from the screen size — the
    // relative frame, the percent / uiScale padding, the anchored position and the wrapped text — after
    // ctx_->screen changed underneath a live view tree. Concrete views that also bake geometry from the
    // screen in their constructor (sidebar slide endpoints, per-item grids, …) override this to redo that
    // work too, calling View::relayout() first.
    virtual void relayout();
    void setViewAnchor(Anchor self, Anchor target, const std::string& nameH, const std::string& nameV);
    void setBackgroundColor(Color c) {
        background_ = c;
        hasBackground_ = true;
    }
    void setClipSubviews(bool clip) { clipSubviews_ = clip; }
    bool isClippingSubviews() const { return clipSubviews_; }

    // --- tree ----------------------------------------------------------------------------------
    virtual void addSubview(View* v);
    void insertSubview(View* v, std::size_t index);
    void removeSubview(View* v);
    void clearSubviews() { subviews_.clear(); }
    const std::vector<View*>& subviews() const { return subviews_; }
    View* parent() const { return parent_; }
    View* findViewByName(const std::string& name);
    void setViewName(const std::string& name) { name_ = name; }
    const std::string& viewName() const { return name_; }
    int id() const { return id_; }
    void setParentScene(Scene* scene);
    Scene* parentScene() const { return scene_; }

    // --- visibility / interaction --------------------------------------------------------------
    virtual void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }
    virtual void setInteraction(bool on) { interactable_ = on; }
    bool isInteractable() const { return interactable_; }

    // --- layout --------------------------------------------------------------------------------
    // UpdateViewAnchors(recurse, resolveTargets): resolves the target views by name among the ancestors'
    // subviews, computes the anchored position and, when `recurse`, lays out the subviews (last first).
    virtual void updateViewAnchors(bool recurse, bool resolveTargets);

    // --- drawing -------------------------------------------------------------------------------
    // BaseDraw(rect): the accumulated transform, the background colour, draw(), then the visible
    // subviews with their frames offset by `rect`.
    void baseDraw(Renderer& renderer, const Rect& rect);
    virtual void draw(Renderer& renderer, const Rect& rect);
    virtual void update(float dt);
    virtual void updateLocale();

    // --- touches (the EventHandler's dispatch) -------------------------------------------------
    // HitTest(point in this view's coordinates): the deepest interactable visible view under the point.
    View* hitTest(Point p);
    virtual bool isPointInView(Point p) const;
    Point convertPointFromView(const View& from, Point p) const;
    virtual void touchesStarted(const TouchEvent&) {}
    virtual void touchesFinishedInside(const TouchEvent&) {}
    virtual void touchesFinishedOutside(const TouchEvent&) {}
    virtual void touchesMovedInside(const TouchEvent&) {}
    virtual void touchesMovedOutside(const TouchEvent&) {}
    virtual void touchesMovedEnter(const TouchEvent&) {}
    virtual void touchesMovedExit(const TouchEvent&) {}
    virtual void touchesCancel(const TouchEvent&) {}
    // WheelScrolled(delta): a remake-only hook for the desktop mouse wheel / trackpad (no touch
    // equivalent in the original). `delta` is raylib's wheel move (x for a two-finger horizontal swipe,
    // y for the wheel; positive = up / right). The SceneManager offers it to the view under the pointer
    // and then its ancestors; true when consumed.
    virtual bool wheelScrolled(Point) { return false; }
    // KeyDown(key): true when consumed. The remake maps Esc to the original's back key. View::KeyDown
    // offers the key to the subviews from the topmost down, the interactable and visible ones only, and
    // stops at the first that consumes it [verified: 0x10872c] — a dialog on top of a view takes the key.
    virtual bool keyDown(int key);
    // reWrap(): re-wraps whatever text/content a view cached at its last size (LabelView's line list).
    // The base does nothing; promoted from LabelView-only so relayout() can dispatch it through a View*.
    virtual void reWrap() {}
    // recomputeAutoSize(): re-derives a frame size that was computed from content rather than from
    // Relative.W/H — a sprite/font's native pixel dimensions at the ctx_->resources scale current when the
    // view was constructed (ImageView::AutoResize; that scale changes on a live resize, Game::
    // applyScreenLayout calls ResourceProxy::setUiScale), or a child's / sibling's size (DialogBackground's
    // width from its top piece). relayout() runs it after the relative-frame pass (as construction does:
    // View::init's Relative sizing, then the concrete init's content sizing over it) and post-order, so a
    // child's own content size is already current. The base does nothing.
    virtual void recomputeAutoSize() {}

protected:
    // Where padding_ came from, so relayout() can redo the same formula against the current screen: the
    // Relative/Padding percent-of-screen block, or the top-level Padding uiScale-px block (View::init).
    enum class PaddingSource { kNone, kRelativePercent, kUiScalePixels };
    void recomputePadding();
    // Re-invokes setRelativePosition(relativePos_) / setRelativeSize(relativeSize_): both are already
    // gated on relativeX_/Y_/W_/H_, so safe to call unconditionally.
    void recomputeRelativeFrame();
    void recomputeRelativeFrameRecursive();
    void recomputePaddingRecursive();
    void recomputeAutoSizeRecursive();
    void reWrapRecursive();

    UiContext* ctx_;
    Rect frame_;
    Point pivot_;
    float scale_ = 1.0f;
    float alpha_ = 1.0f;
    float angle_ = 0.0f;
    float padding_[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // left, right, top, bottom
    PaddingSource paddingSource_ = PaddingSource::kNone;
    float paddingSpec_[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // the un-scaled values behind padding_
    Anchor selfAnchor_;
    Anchor targetAnchor_;
    std::string targetNameH_;
    std::string targetNameV_;
    View* targetH_ = nullptr;
    View* targetV_ = nullptr;
    bool relativeX_ = false;
    bool relativeY_ = false;
    bool relativeW_ = false;
    bool relativeH_ = false;
    Point relativePos_;
    Size relativeSize_;
    std::vector<View*> subviews_;
    View* parent_ = nullptr;
    Scene* scene_ = nullptr;
    int id_ = 0;
    bool visible_ = true;
    bool interactable_ = true;
    bool clipSubviews_ = false;
    bool hasBackground_ = false;
    Color background_;
    std::string name_ = "DefaultName";

    static int nextId_;
};

// UI::ImageView: a sprite or composite, drawn at the view size (stretch), tiled (TILE), aspect-fitted,
// aspect-filled (the full-screen `Background`) or centred at its own size.
class ImageView : public View {
public:
    using View::init;
    enum class DrawMode : int { Stretch = 0, Fit = 1, Fill = 2, Tile = 3, Center = 4 };

    explicit ImageView(UiContext& ctx) : View(ctx) {}
    void init(const aa::data::JsonNode& dict) override;
    // SetImage(name, localized): resolves the sprite / composite size and pivot (Load).
    void setImage(const std::string& name, bool localized = false);
    const std::string& imageName() const { return imageName_; }
    Size imageSize() const { return imageSize_; }
    // ResizeFrameToImage(w, h): the frame takes the image's width / height.
    void resizeFrameToImage(bool w, bool h);
    void setDrawMode(DrawMode mode) { drawMode_ = mode; }
    void draw(Renderer& renderer, const Rect& rect) override;
    void updateLocale() override;
    void recomputeAutoSize() override;

private:
    void load();
    std::string imageName_;
    bool localized_ = false;
    bool isCompo_ = false;
    bool pending_ = false;   // the image was not found at SetImage time (Load retried at draw)
    Size imageSize_;
    // The sprite's own pivot as Load last resolved it (scaled): recomputeAutoSize() tells a pivot a caller
    // replaced (typically the view's centre) from the sprite's by comparing against this.
    Point atlasPivot_;
    DrawMode drawMode_ = DrawMode::Stretch;
    // AutoResize/AutoResizeW/AutoResizeH (View::init's local w/h, kept here so relayout() can redo
    // ResizeFrameToImage against the current ctx_->resources scale — see recomputeAutoSize()).
    bool autoResizeImageW_ = false;
    bool autoResizeImageH_ = false;
};

// UI::LabelView: bitmap text with the font anchors, word wrapping (`WrapText`), MaxRows / MaxLetters and
// auto-resize to the text extents.
class LabelView : public View {
public:
    using View::init;
    explicit LabelView(UiContext& ctx);
    void init(const aa::data::JsonNode& dict) override;
    // SetText(id): the localised text of `id` (or the id itself when unknown).
    virtual void setText(const std::string& id);
    // SetNonLocalizedText.
    virtual void setNonLocalizedText(const std::string& text);
    const std::string& text() const { return text_; }
    virtual void setFont(const std::string& font) { fontName_ = font; }
    const std::string& fontName() const { return fontName_; }
    void setAnchor(FontAnchorH h, FontAnchorV v) {
        anchorH_ = h;
        anchorV_ = v;
    }
    void setAutoResize(bool w, bool h) {
        autoResizeW_ = w;
        autoResizeH_ = h;
    }
    void setMaxRows(int rows) { maxRows_ = rows; }
    void setMaxLetters(int letters) { maxLetters_ = letters; }
    void setWordWrapping(bool on) { wordWrapping_ = on; }
    bool isWordWrapping() const { return wordWrapping_; }
    const std::vector<std::string>& lines() const { return lines_; }
    void reWrap() override;
    void draw(Renderer& renderer, const Rect& rect) override;
    void updateLocale() override;

protected:
    // WrapText(text): the line list and, with auto-resize, the frame size.
    virtual void wrapText(const std::string& text);
    // The line origin of Draw: x from the H anchor, y from the V anchor and the line count.
    Point lineOrigin(const BitmapFont& font, const Rect& rect) const;

    std::string fontName_;
    std::string text_;
    std::string textId_;
    FontAnchorH anchorH_ = FontAnchorH::Left;
    FontAnchorV anchorV_ = FontAnchorV::Top;
    int maxLetters_ = -1;
    int maxRows_ = -1;
    bool autoResizeW_ = false;
    bool autoResizeH_ = false;
    bool wordWrapping_ = true;
    std::vector<std::string> lines_;
};

// UI::OutlineLabelView: the outline font drawn by this view, the fill font by an inner label offset by the
// Fonts.xml outline offsets (docs/06 §2).
class OutlineLabelView : public LabelView {
public:
    using View::init;
    explicit OutlineLabelView(UiContext& ctx);
    void init(const aa::data::JsonNode& dict) override;
    void setText(const std::string& id) override;
    void setNonLocalizedText(const std::string& text) override;
    void setFont(const std::string& font) override;
    void setSize(Size s) override;   // -1 keeps the current dimension; the inner label shrinks by 2·offset
    void reWrap() override;          // both labels: the fill would otherwise keep its lines from before a resize
    void setAlpha(float a) override;
    void setPosition(Point p) override;
    void setScale(float s) override;
    LabelView& inner() { return *inner_; }

private:
    // offsetX_/offsetY_ + inner_'s frame, both derived from outlineFontName_ / frame_ at the current
    // ctx_->screen.uiScale — re-derived by reWrap() so a live resize (which changes uiScale, and moves
    // frame_.w/h for a Relative.W/H view without going through the virtual setSize() below) keeps them
    // current.
    void updateOffsets();
    std::unique_ptr<LabelView> inner_;
    std::string outlineFontName_;
    int offsetX_ = 0;
    int offsetY_ = 0;
};

// UI::HighlightLabelView: `*word*` segments drawn with the highlight font (the loading-screen tips).
class HighlightLabelView : public LabelView {
public:
    using View::init;
    explicit HighlightLabelView(UiContext& ctx) : LabelView(ctx) {}
    void init(const aa::data::JsonNode& dict) override;
    void setHighlightFont(const std::string& font) { highlightFont_ = font; }
    void draw(Renderer& renderer, const Rect& rect) override;

protected:
    void wrapText(const std::string& text) override;

private:
    float segmentedWidth(const std::string& line, const BitmapFont& normal, const BitmapFont& highlight, bool& state) const;
    std::string highlightFont_;
};

// Parses the `Text` attribute family of the label dictionaries.
std::string localizedText(const UiContext& ctx, const std::string& id);

}  // namespace aa::ui
