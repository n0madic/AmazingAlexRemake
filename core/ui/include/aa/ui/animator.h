// UI::Animator [verified: Animate (single view = absolute targets; view array = deltas), Update,
// Interpolate, the five easing curves, CancelAnimation]: animates a view's frame, angle, alpha, scale
// and pivot from its current values to the target over `duration` after `delay`, `repeat` times (0 =
// forever), calling the delegate's AnimationStarted / Finished / Canceled.
//
// Remake deviation: the original's Interpolate writes all five components every frame, whether or not
// the animation changes them — so an alpha-only fade keeps re-applying the frame it captured at Animate
// time, and two animations on the same view (a sidebar's slide and its fade) fight over each other's
// components (GameView::animationFinished's play_->setAlpha(1.0f) papers over exactly that). The remake
// writes only the components whose from and to differ (per view), so a value the animation never meant
// to drive keeps whatever something else (a live-resize relayout, a sibling animation) set it to.
#pragma once

#include "aa/ui/types.h"

#include <memory>
#include <vector>

namespace aa::ui {

class View;

// UI::AnimationParameters (0x34 bytes): frame, angle, alpha, scale, pivot, curve, delay, duration, repeat.
struct AnimationParameters {
    Rect frame;
    float angle = 0.0f;
    float alpha = 1.0f;
    float scale = 1.0f;
    Point pivot;
    int curve = 0;         // 0 linear, 1 ease-in (t²), 2 ease-out (1 − (1−t)²), 3 / 4 smoothstep
    float delay = 0.0f;
    float duration = 0.0f;
    int repeat = 1;        // 0 = loop forever

    // The current values of a view, with the given curve / duration.
    static AnimationParameters fromView(const View& view);
};

class AnimatorDelegate {
public:
    virtual ~AnimatorDelegate() = default;
    virtual void animationStarted(int) {}
    virtual void animationFinished(int) {}
    virtual void animationCanceled(int) {}
};

class Animator {
public:
    // Animate(View*, params, delegate): `params` are the absolute targets. Returns the animation id.
    int animate(View* view, const AnimationParameters& target, AnimatorDelegate* delegate);
    // Animate(Array<View*>, params, delegate): `delta` is added to every view's current values; a
    // component whose |delta| < 1e-4 stays as it is.
    int animate(const std::vector<View*>& views, const AnimationParameters& delta, AnimatorDelegate* delegate);
    // CancelAnimation(id): the delegate's AnimationCanceled, the item dropped (values stay where they are).
    bool cancelAnimation(int id);
    // FinishAnimation(id): jumps to the end (the finished callback follows on the next update). Because the
    // item is left in the list, a plain Update() afterwards re-applies these (now possibly stale) end values
    // — safe for the original show()/hide() callers (the view is already sitting at `to`), unsafe for a
    // caller that repositions the view afterwards (e.g. a relayout()). Use CompleteAnimation for that case.
    bool finishAnimation(int id);
    // CompleteAnimation(id): jumps to the end, removes the item, and fires the finished callback
    // synchronously (not deferred to the next Update()) — nothing is left to re-apply a stale value on a
    // later frame. Prefer this over FinishAnimation when the caller will reposition the view afterwards.
    bool completeAnimation(int id);
    // completeAnimations(root) (remake-only, for a live resize): CompleteAnimation for every item that
    // moves the *frame* of a view inside `root`'s subtree (`root` included) — the only component a
    // relayout rewrites that an in-flight animation would then drag back to a stale, pre-resize value.
    // Alpha / scale / angle fades are left running: with the per-component write above they never touch
    // the frame, and completing them would fire their delegates' chains (a level button's pulse restarts
    // itself, a button's zoom-in end is its press). Repeats until nothing matches, since a completed
    // item's delegate may start the next frame-moving one (GameView's close → showControls). Returns
    // how many items were completed.
    int completeAnimations(const View* root);
    void cancelAll();
    void update(float dt);
    bool isRunning(int id) const;
    int count() const { return static_cast<int>(items_.size()); }

private:
    struct Item {
        int id = 0;
        std::vector<View*> views;
        AnimatorDelegate* delegate = nullptr;
        std::vector<AnimationParameters> from;
        std::vector<AnimationParameters> to;
        float delayTime = 0.0f;
        float time = 0.0f;
        int repeats = 0;
        bool started = false;
    };
    static bool framesDiffer(const Rect& a, const Rect& b);
    static bool movesFrame(const Item& item, const View* root);
    void interpolate(Item& item);
    void apply(const Item& item, const std::vector<AnimationParameters>& values);
    std::vector<std::unique_ptr<Item>> items_;
    int nextId_ = 1;
};

}  // namespace aa::ui
