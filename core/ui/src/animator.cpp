#include "aa/ui/animator.h"

#include "aa/ui/view.h"

#include <cmath>

namespace aa::ui {

namespace {

constexpr float kDeltaEpsilon = 1e-4f;   // 0x38d1b717

// The five easing functions of the interpolator [verified: disassembly at 0xed834..0xed93c].
float ease(int curve, float a, float b, float t) {
    switch (curve) {
    case 1: {
        const float s = t * t;
        return (1.0f - s) * a + b * s;
    }
    case 2: {
        const float u = 1.0f - t;
        const float s = 1.0f - u * u;
        return (1.0f - s) * a + b * s;
    }
    case 3: {
        const float s = t * t * (3.0f - (t + t));
        return (1.0f - s) * a + b * s;
    }
    case 4: {
        const float s = -2.0f * t * t * t + 3.0f * t * t;
        return a * (1.0f - s) + s * b;
    }
    default: return (1.0f - t) * a + b * t;
    }
}

}  // namespace

AnimationParameters AnimationParameters::fromView(const View& v) {
    AnimationParameters p;
    p.frame = v.frame();
    p.angle = v.angle();
    p.alpha = v.alpha();
    p.scale = v.scale();
    p.pivot = v.pivot();
    return p;
}

int Animator::animate(View* view, const AnimationParameters& target, AnimatorDelegate* delegate) {
    auto item = std::make_unique<Item>();
    item->id = nextId_++;
    item->views.push_back(view);
    item->delegate = delegate;
    item->to.push_back(target);
    AnimationParameters from = AnimationParameters::fromView(*view);
    from.repeat = 1;
    item->from.push_back(from);
    const int id = item->id;
    items_.push_back(std::move(item));
    return id;
}

int Animator::animate(const std::vector<View*>& views, const AnimationParameters& delta, AnimatorDelegate* delegate) {
    auto item = std::make_unique<Item>();
    item->id = nextId_++;
    item->delegate = delegate;
    for (View* v : views) {
        item->views.push_back(v);
        AnimationParameters from = AnimationParameters::fromView(*v);
        from.repeat = 1;
        AnimationParameters to = from;
        to.curve = delta.curve;
        to.delay = delta.delay;
        to.duration = delta.duration;
        to.repeat = delta.repeat;
        auto add = [](float base, float d) { return std::fabs(d) >= kDeltaEpsilon ? base + d : base; };
        to.frame.x = add(from.frame.x, delta.frame.x);
        to.frame.y = add(from.frame.y, delta.frame.y);
        to.frame.w = add(from.frame.w, delta.frame.w);
        to.frame.h = add(from.frame.h, delta.frame.h);
        to.angle = add(from.angle, delta.angle);
        to.alpha = add(from.alpha, delta.alpha);
        to.scale = add(from.scale, delta.scale);
        if (std::fabs(delta.pivot.x) >= kDeltaEpsilon) {
            to.pivot.x = from.pivot.x + delta.pivot.x;
            to.pivot.y = from.pivot.y + delta.pivot.y;
        }
        item->from.push_back(from);
        item->to.push_back(to);
    }
    const int id = item->id;
    items_.push_back(std::move(item));
    return id;
}

bool Animator::cancelAnimation(int id) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i]->id != id) continue;
        std::unique_ptr<Item> item = std::move(items_[i]);
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
        if (item->delegate) item->delegate->animationCanceled(id);
        return true;
    }
    return false;
}

bool Animator::finishAnimation(int id) {
    for (auto& item : items_) {
        if (item->id != id) continue;
        item->delayTime = item->to[0].delay;
        item->time = item->to[0].duration;
        interpolate(*item);
        return true;
    }
    return false;
}

bool Animator::completeAnimation(int id) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i]->id != id) continue;
        std::unique_ptr<Item> item = std::move(items_[i]);
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
        item->delayTime = item->to[0].delay;
        item->time = item->to[0].duration;
        apply(*item, item->to);
        if (item->delegate) item->delegate->animationFinished(id);
        return true;
    }
    return false;
}

void Animator::cancelAll() {
    while (!items_.empty()) cancelAnimation(items_.front()->id);
}

bool Animator::isRunning(int id) const {
    for (const auto& item : items_) {
        if (item->id == id) return true;
    }
    return false;
}

bool Animator::movesFrame(const Item& item, const View* root) {
    for (std::size_t i = 0; i < item.views.size(); ++i) {
        if (!framesDiffer(item.from[i].frame, item.to[i].frame)) continue;
        for (const View* v = item.views[i]; v; v = v->parent()) {
            if (v == root) return true;
        }
    }
    return false;
}

int Animator::completeAnimations(const View* root) {
    // Bounded: a delegate chain that keeps spawning frame-moving items (none today) must not spin forever.
    constexpr int kMaxCompletions = 64;
    int completed = 0;
    while (completed < kMaxCompletions) {
        int id = 0;
        for (const auto& item : items_) {
            if (!movesFrame(*item, root)) continue;
            id = item->id;
            break;
        }
        if (id == 0 || !completeAnimation(id)) break;
        ++completed;
    }
    return completed;
}

bool Animator::framesDiffer(const Rect& a, const Rect& b) { return a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h; }

void Animator::apply(const Item& item, const std::vector<AnimationParameters>& values) {
    for (std::size_t i = 0; i < item.views.size(); ++i) {
        View* v = item.views[i];
        const AnimationParameters& a = item.from[i];
        const AnimationParameters& b = item.to[i];
        const AnimationParameters& p = values[i];
        if (framesDiffer(a.frame, b.frame)) v->setFrame(p.frame);
        if (a.alpha != b.alpha) v->setAlpha(p.alpha);
        if (a.scale != b.scale) v->setScale(p.scale);
        if (a.angle != b.angle) v->setAngle(p.angle);
        if (a.pivot.x != b.pivot.x || a.pivot.y != b.pivot.y) v->setPivot(p.pivot);
    }
}

void Animator::interpolate(Item& item) {
    for (std::size_t i = 0; i < item.views.size(); ++i) {
        const AnimationParameters& a = item.from[i];
        const AnimationParameters& b = item.to[i];
        float t = b.duration > 0.0f ? item.time / b.duration : 1.0f;
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;
        const int c = b.curve;
        View* v = item.views[i];
        if (framesDiffer(a.frame, b.frame)) {
            v->setFrame(Rect{ease(c, a.frame.x, b.frame.x, t), ease(c, a.frame.y, b.frame.y, t), ease(c, a.frame.w, b.frame.w, t),
                             ease(c, a.frame.h, b.frame.h, t)});
        }
        if (a.alpha != b.alpha) v->setAlpha(ease(c, a.alpha, b.alpha, t));
        if (a.scale != b.scale) v->setScale(ease(c, a.scale, b.scale, t));
        if (a.angle != b.angle) v->setAngle(ease(c, a.angle, b.angle, t));
        if (a.pivot.x != b.pivot.x || a.pivot.y != b.pivot.y) v->setPivot(Point{ease(c, a.pivot.x, b.pivot.x, t), ease(c, a.pivot.y, b.pivot.y, t)});
    }
}

void Animator::update(float dt) {
    // Animator::Update [verified]: per item, the delay first; then the time advances, clamped to the
    // duration; at the end the final values are set, the delegate is told, and the item repeats or goes.
    for (std::size_t i = 0; i < items_.size();) {
        Item& item = *items_[i];
        const AnimationParameters& p = item.to[0];
        if (item.delayTime < p.delay) {
            item.delayTime += dt;
            ++i;
            continue;
        }
        if (!item.started) {
            if (item.delegate) item.delegate->animationStarted(item.id);
            item.started = true;
        }
        if (item.time < p.duration) {
            item.time = std::min(item.time + dt, p.duration);
            interpolate(item);
            ++i;
            continue;
        }
        apply(item, item.to);
        item.repeats += 1;
        const int id = item.id;
        AnimatorDelegate* delegate = item.delegate;
        if (p.repeat == 0 || item.repeats < p.repeat) {
            item.time = item.time - p.duration + dt;
            interpolate(item);
            if (delegate) delegate->animationFinished(id);
            ++i;
            continue;
        }
        // The item is removed before the delegate runs, so a delegate that starts a new animation
        // does not disturb the iteration.
        std::unique_ptr<Item> done = std::move(items_[i]);
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(i));
        if (delegate) delegate->animationFinished(id);
    }
}

}  // namespace aa::ui
