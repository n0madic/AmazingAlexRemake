#include "aa/ui/scene.h"

#include <algorithm>
#include <cmath>

namespace aa::ui {

namespace {

// BORDER_BORDER.png's outer columns (x 0 of LEFT, x 399 of RIGHT) are this one colour top to bottom, so the
// fills meet the sprites without a seam.
constexpr Color kLetterBoxFillColour{28, 99, 158, 255};
constexpr const char* kBorderSprites[] = {"BORDERIMAGE_LEFT", "BORDERIMAGE_RIGHT"};

}  // namespace

void drawLetterBoxBackdrop(Renderer& renderer, const UiContext& ctx, const aa::sim::ScreenLayout& layout) {
    const aa::sim::ScreenLayout::Fills fills = layout.letterBoxFills();
    if (fills.count == 0) return;   // the world covers the screen
    DrawState state;   // screen px, no clipping (a zero-width clip would hide everything)
    state.clip = Rect{0.0f, 0.0f, -1.0f, -1.0f};
    renderer.setState(state);
    for (int i = 0; i < fills.count; ++i) {
        const aa::sim::ScreenLayout::Fill& r = fills.rects[i];
        renderer.drawColorRect(Rect{static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.w), static_cast<float>(r.h)},
                               kLetterBoxFillColour);
    }
    if (!layout.letterBox || !ctx.resources) return;
    for (int i = 0; i < 2; ++i) {
        const SpriteRef s = ctx.resources->sprite(kBorderSprites[i]);
        if (!s.valid()) continue;
        const Size size = ctx.resources->imageSize(kBorderSprites[i]);
        const aa::sim::ScreenLayout::SpriteRect r = layout.borderSpriteRect(i == 1, size.w, size.h);
        renderer.drawSprite(s, std::ceil(r.x), r.y, std::ceil(r.w), r.h);
    }
}

// --- EventHandler ----------------------------------------------------------------------------------

bool EventHandler::touchesStarted(const TouchEvent& e) {
    if (!root_) return false;
    View* hit = root_->hitTest(e.position);
    if (!hit) return false;
    hit->touchesStarted(e);
    started_[e.id] = hit;
    current_[e.id] = hit;
    return true;
}

bool EventHandler::touchesMoved(const TouchEvent& e) {
    if (!root_) return false;
    View* s = started_.count(e.id) ? started_[e.id] : nullptr;
    // The original tests the started view with the raw event position (no conversion) [verified].
    if (s && s->isPointInView(e.position)) {
        s->touchesMovedInside(e);
        current_[e.id] = s;
        return true;
    }
    View* hit = root_->hitTest(e.position);
    View* cur = current_.count(e.id) ? current_[e.id] : nullptr;
    if (!hit) {
        // Remake extension: a position outside every view (the desktop pointer dragged off the window —
        // the original never saw one) still leaves the view under the touch and moves outside the started
        // one, so a pressed Button un-highlights instead of freezing in that state.
        if (cur) cur->touchesMovedExit(e);
        current_.erase(e.id);
        if (s) s->touchesMovedOutside(e);
        return s != nullptr;
    }
    if (cur == hit) {
        cur->touchesMovedInside(e);
    } else {
        if (cur) cur->touchesMovedExit(e);
        hit->touchesMovedEnter(e);
        current_[e.id] = hit;
    }
    if (s && s != hit) s->touchesMovedOutside(e);
    return true;
}

bool EventHandler::touchesFinished(const TouchEvent& e) {
    if (!root_) return false;
    View* s = started_.count(e.id) ? started_[e.id] : nullptr;
    if (s && s->isPointInView(e.position)) {
        s->touchesFinishedInside(e);
        started_.erase(e.id);
        current_.erase(e.id);
        return true;
    }
    View* hit = root_->hitTest(e.position);
    if (!hit) {
        // Remake extension: a release outside every view (the desktop pointer let go off the window) still
        // ends the started view's gesture — otherwise a Button keeps its Highlighted state and the static
        // touch id, swallowing the next press anywhere.
        if (s) s->touchesFinishedOutside(e);
        started_.erase(e.id);
        current_.erase(e.id);
        return s != nullptr;
    }
    hit->touchesFinishedInside(e);
    if (s && s != hit) s->touchesFinishedOutside(e);
    started_.erase(e.id);
    current_.erase(e.id);
    return true;
}

void EventHandler::touchesCancel(const TouchEvent& e) {
    View* s = started_.count(e.id) ? started_[e.id] : nullptr;
    View* c = current_.count(e.id) ? current_[e.id] : nullptr;
    if (s) s->touchesCancel(e);
    if (c && c != s) c->touchesCancel(e);
    started_.erase(e.id);
    current_.erase(e.id);
}

void EventHandler::purgeTouches(View* view) {
    if (!view) return;
    for (View* v : view->subviews()) purgeTouches(v);
    for (auto it = started_.begin(); it != started_.end();) {
        if (it->second == view) {
            TouchEvent e;
            e.id = it->first;
            view->touchesCancel(e);
            it = started_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = current_.begin(); it != current_.end();) {
        if (it->second == view) {
            TouchEvent e;
            e.id = it->first;
            view->touchesCancel(e);
            it = current_.erase(it);
        } else {
            ++it;
        }
    }
}

// --- Scene -----------------------------------------------------------------------------------------

void Scene::init() {
    root_ = std::make_unique<View>(*ctx_);
    root_->setViewName("SceneRoot");
    root_->setFrame(Rect{0.0f, 0.0f, ctx_->screen.nativeWidth, ctx_->screen.nativeHeight});
    root_->setParentScene(this);
    state_ = scene_state::kInactive;
    initialized_ = true;
}

void Scene::resizeRootAndMainView(View* mainView) {
    if (!root_) return;
    root_->setFrame(Rect{0.0f, 0.0f, ctx_->screen.nativeWidth, ctx_->screen.nativeHeight});
    if (mainView) mainView->setFrame(root_->frame());
}

void Scene::relayout(int, int) {
    resizeRootAndMainView(nullptr);
    if (root_) root_->relayout();
}

void Scene::update(float dt) {
    if (state_ == scene_state::kActivating) state_ = scene_state::kActive;
    else if (state_ == scene_state::kInactivating) state_ = scene_state::kInactive;
    if (root_) root_->update(dt);
}

void Scene::draw(Renderer& renderer) {
    if (root_) root_->baseDraw(renderer, root_->frame());
}

bool Scene::keyDown(int key) {
    // Scene::KeyDown forwards to the root view's subviews (the game views override KeyDown).
    if (!root_) return false;
    for (std::size_t i = root_->subviews().size(); i-- > 0;) {
        View* v = root_->subviews()[i];
        if (v && v->isVisible() && v->keyDown(key)) return true;
    }
    return false;
}

View* Scene::hitTest(Point p) const {
    if (!root_ || state_ != scene_state::kActive) return nullptr;
    return root_->hitTest(p);
}

// --- SceneManager ----------------------------------------------------------------------------------

void SceneManager::registerScene(std::unique_ptr<Scene> scene) {
    scene->setManager(this);
    scenes_.push_back(std::move(scene));
}

Scene* SceneManager::scene(const std::string& name) const {
    for (const auto& s : scenes_) {
        if (name == s->name()) return s.get();
    }
    return nullptr;
}

void SceneManager::relayoutAll(int width, int height) {
    for (const auto& s : scenes_) s->relayout(width, height);
}

bool SceneManager::pushScene(const std::string& name) {
    if (inTransition()) nonSimultaneousTransition(lastDt_);
    Scene* s = scene(name);
    if (!s) return false;
    if (!stack_.empty()) inactivating_ = stack_.back();
    stack_.push_back(s);
    popping_ = false;
    activating_ = s;
    return true;
}

bool SceneManager::popScene() {
    if (inTransition()) nonSimultaneousTransition(lastDt_);
    if (stack_.empty()) return false;
    inactivating_ = stack_.back();
    stack_.pop_back();
    popping_ = true;
    if (!stack_.empty()) activating_ = stack_.back();
    return true;
}

bool SceneManager::setRootScene(const std::string& name) {
    Scene* s = scene(name);
    if (!s) return false;
    if (inTransition()) nonSimultaneousTransition(lastDt_);
    if (!stack_.empty()) inactivating_ = stack_.back();
    stack_.clear();
    stack_.push_back(s);
    popping_ = true;
    activating_ = s;
    return true;
}

bool SceneManager::insertScene(int index, const std::string& name) {
    Scene* s = scene(name);
    if (!s) return false;
    if (index < 0) index = 0;
    if (index > static_cast<int>(stack_.size())) index = static_cast<int>(stack_.size());
    stack_.insert(stack_.begin() + index, s);
    if (!s->initialized()) s->init();
    return true;
}

bool SceneManager::popScenesUntil(const std::string& name) {
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        if (name != stack_[i]->name()) continue;
        if (i + 1 == stack_.size()) return true;
        inactivating_ = stack_.back();
        activating_ = stack_[i];
        stack_.resize(i + 1);
        popping_ = true;
        return true;
    }
    return false;
}

bool SceneManager::removeScene(const std::string& name) {
    // RemoveScene [verified: 0xff160 + disassembly]: only the scenes between the root and the top of the
    // stack (indices 1 .. count − 2) are erased, without any transition — a middle scene is already
    // inactive. The top scene is never removed: LevelCompletedView's "back to the books" path relies on
    // this (RemoveScene(GameScene) with the game on top is a no-op, PushScene(LevelLoadingScene) then
    // stacks the loading scene a second time and PopScenesUntil(ChapterSelectionScene) clears both).
    bool removed = false;
    for (std::size_t i = 1; i + 1 < stack_.size();) {
        if (name == stack_[i]->name()) {
            stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(i));
            removed = true;
        } else {
            ++i;
        }
    }
    return removed;
}

void SceneManager::update(float dt) {
    lastDt_ = dt;
    ctx_->animator->update(dt);
    if (!simultaneous_) {
        nonSimultaneousTransition(dt);
        return;
    }
    simultaneousTransition(dt);
}

void SceneManager::nonSimultaneousTransition(float dt) {
    // NonSimultaneousTransition [verified]: the leaving scene first (AboutToInactivate + Inactivate
    // when still active; InactivationComplete once inactive), then the entering one.
    if (inactivating_) {
        const int s = inactivating_->state();
        if (s == scene_state::kActive) {
            inactivating_->aboutToInactivate();
            inactivating_->inactivate();
        } else if (s != scene_state::kInactivating) {
            if (s == scene_state::kInactive) {
                events_.purgeTouches(inactivating_->view());
                inactivating_->inactivationComplete();
                inactivating_ = nullptr;
            }
        }
        if (inactivating_) {
            inactivating_->update(dt);
            return;
        }
    }
    if (!activating_) {
        if (!stack_.empty()) stack_.back()->update(dt);
        return;
    }
    const int s = activating_->state();
    if (s != scene_state::kActive) {
        if (s != scene_state::kActivating) {
            if (s == scene_state::kInactive) {
                activating_->aboutToActivate();
                events_.setRootView(activating_->view());
                activating_->activate();
            }
        }
        activating_->update(dt);
        return;
    }
    activating_->activationComplete();
    activating_ = nullptr;
}

void SceneManager::simultaneousTransition(float dt) {
    // SimultaneousTransition [verified]: both scenes transition in the same frames.
    if (inactivating_) {
        const int s = inactivating_->state();
        if (s == scene_state::kActive || s == scene_state::kActivating) {
            if (s == scene_state::kActivating) {
                inactivating_->setState(scene_state::kActive);
                inactivating_->activationComplete();
            }
            inactivating_->aboutToInactivate();
            inactivating_->inactivate();
        }
    }
    if (activating_) {
        const int s = activating_->state();
        if (s == scene_state::kActive) {
            const bool waiting = inactivating_ && inactivating_->state() != scene_state::kInactive;
            if (!waiting) {
                if (inactivating_) {
                    events_.purgeTouches(inactivating_->view());
                    inactivating_->inactivationComplete();
                }
                inactivating_ = nullptr;
                activating_->activationComplete();
                activating_ = nullptr;
            }
        } else if (s == scene_state::kInactive || s == scene_state::kInactivating) {
            if (s == scene_state::kInactivating) {
                activating_->setState(scene_state::kInactive);
                activating_->inactivationComplete();
            }
            activating_->aboutToActivate();
            events_.setRootView(activating_->view());
            activating_->activate();
        }
    }
    if (inactivating_) inactivating_->update(dt);
    if (activating_) activating_->update(dt);
    if (!inactivating_ && !activating_ && !stack_.empty()) stack_.back()->update(dt);
}

void SceneManager::draw(Renderer& renderer) {
    // Draw [verified]: the active scene, or during a transition the leaving scene under the entering one
    // (the reverse while popping).
    if (!activating_) {
        if (!stack_.empty()) stack_.back()->draw(renderer);
        return;
    }
    if (!inactivating_) {
        activating_->draw(renderer);
        return;
    }
    Scene* below = inactivating_;
    Scene* above = activating_;
    if (popping_) std::swap(below, above);
    below->draw(renderer);
    above->draw(renderer);
}

void SceneManager::updateLocale() {
    for (const auto& s : scenes_) s->updateLocale();
}

void SceneManager::touchesStarted(const TouchEvent& e) {
    if (!interactionEnabled_) {
        swallowedTouch_ = e.id;
        return;
    }
    if (stack_.empty() || activating_) return;
    if (events_.touchesStarted(e)) return;
    // Unhandled by the view tree: the scene's own hook (nothing in the shipped scenes).
}

void SceneManager::touchesMoved(const TouchEvent& e) {
    if (swallowedTouch_ == e.id) return;
    if (stack_.empty() || activating_) return;
    events_.touchesMoved(e);
}

void SceneManager::touchesFinished(const TouchEvent& e) {
    if (swallowedTouch_ == e.id) {
        swallowedTouch_ = -1;
        return;
    }
    if (stack_.empty() || activating_) return;
    events_.touchesFinished(e);
}

void SceneManager::touchesCancel(const TouchEvent& e) {
    if (swallowedTouch_ == e.id) swallowedTouch_ = -1;
    events_.touchesCancel(e);
}

bool SceneManager::wheelScrolled(Point position, Point delta) {
    if (!interactionEnabled_) return false;
    if (stack_.empty() || activating_) return false;
    View* root = events_.rootView();
    if (!root) return false;
    for (View* v = root->hitTest(position); v; v = v->parent()) {
        if (v->wheelScrolled(delta)) return true;
    }
    return false;
}

void SceneManager::pause(bool paused) {
    if (activating_) activating_->setPaused(paused);
    if (inactivating_) inactivating_->setPaused(paused);
    if (!stack_.empty()) stack_.back()->setPaused(paused);
}

bool SceneManager::keyPressed(int key) {
    if (!interactionEnabled_) return false;
    if (stack_.empty() || activating_) return false;
    if (stack_.back()->keyDown(key)) return true;
    if (key == kKeyBack || key == kKeyAndroidBack) {
        if (stack_.size() < 2) return false;
        popScene();
        return true;
    }
    return false;
}

}  // namespace aa::ui
