#include "aa/sim/visual_state.h"

#include "aa/sim/math_utils.h"
#include "items/item_common.h"

#include <algorithm>

namespace aa::sim {

namespace {

constexpr int kFinalFrameStep = 4;
constexpr float kArrowMarkerOffset = 0.392699f;   // Pi / 8, subtracted from the goal angle (kind 7)
constexpr float kMarkerAppearDelay = 0.7f;
constexpr float kMarkerStagger = 0.4f;
constexpr float kMarkerStepTime = 0.06666667f;     // 1/15 s
constexpr float kMarkerRestT = 0.2f;
constexpr float kMarkerRestBelow = 0.1f;
constexpr int kStarFrames = 12;
constexpr float kStarFrameTime = 0.083333336f;     // 1/12 s
constexpr int kMaxWaves = 3;
constexpr float kWaveSpawnAge = 0.33333334f;
constexpr float kControllerWaveScale0 = 0.1f;
constexpr float kControllerWaveX0 = 0.01f;
constexpr float kTruckWaveScale0 = 1.2f;           // 0x3f99999a
constexpr float kTruckWaveX0 = 0.13f;              // 0x3e051eb8
constexpr float kWaveScaleRate = 1.1f;
constexpr float kWaveSpeed = 0.12f;                // 0x3df5c28f
constexpr float kSparkleLife = 0.5f;
constexpr float kConfettiLife = 2.5f;
constexpr float kConfettiGravity = 1.5f;

float clampWorldX(float x) {
    if (x < 0.0f) return 0.0f;
    return x > kWorldWidth ? kWorldWidth : x;
}

float clampWorldY(float y) {
    if (y < 0.0f) return 0.0f;
    return y > kWorldHeight ? kWorldHeight : y;
}

}  // namespace

void VisualState::setGoalMarkers(const Goal& goal, const WorldState& state) {
    markers.clear();
    markerCount = goal.itemCount;
    markerTimer = 0.0f;
    markerLastStep = 0.0f;
    for (int i = 0; i < goal.itemCount && i < Goal::kMaxTargets; ++i) {
        const int handle = goal.type == 7 ? goal.itemHandles2[static_cast<std::size_t>(i)]
                                          : goal.itemHandles[static_cast<std::size_t>(i)];
        const GameItem* item = state.findItem(handle);
        if (item == nullptr) continue;   // a dead handle leaves its record inactive
        const PhysicsObject& obj = state.objects[static_cast<std::size_t>(item->objectIndex)];
        RenderMarker m;
        m.kind = goal.type == 5 ? 6 : 1;
        m.position = obj.position;
        m.targetIndex = i;
        markers.push_back(m);
    }
    RenderMarker m;
    switch (goal.type) {
    case 2:
    case 7:
        m.kind = 7;
        m.position = Vec2(clampWorldX(goal.width), clampWorldY(goal.height));
        m.angle = goal.angle * items::kDegToRad - kArrowMarkerOffset;
        break;
    case 3:
        m.kind = 8;
        m.position = Vec2(goal.width, goal.height);   // the one unclamped marker
        break;
    case 6:
        m.kind = 5;
        m.position = Vec2(clampWorldX(goal.width), clampWorldY(goal.height));
        m.angle = items::kPi;
        break;
    case 8:
        m.kind = 4;
        m.position = Vec2(clampWorldX(goal.width), clampWorldY(goal.height));
        break;
    case 9:
        m.kind = 3;
        m.position = Vec2(clampWorldX(goal.width), clampWorldY(goal.height));
        m.angle = items::kPi * 0.5f;
        break;
    case 10:
        m.kind = 2;
        m.position = Vec2(clampWorldX(goal.width), clampWorldY(goal.height));
        m.angle = items::kPi * 1.5f;
        break;
    default:
        return;
    }
    markers.push_back(m);
}

void VisualState::revealGoalMarkers() {
    for (RenderMarker& m : markers) m.frameStep = kFinalFrameStep;
}

void VisualState::hideGoalMarkers() {
    // doFrame (a touch in set-up): the marker counts and the timers go to 0 — SetGoalMarkers lays them
    // out again 5 s after the last touch (Session::frameTail).
    markers.clear();
    markerCount = 0;
    markerTimer = 0.0f;
    markerLastStep = 0.0f;
}

void VisualState::updateGoals(float dt, const Goal& goal, const WorldState& state) {
    // VisualWorldStateUtils::UpdateGoals [verified: decompile].
    markerTimer = markerTimer + dt;
    for (RenderMarker& m : markers) {
        if (m.kind == 0) continue;
        if ((m.kind == 1 || m.kind == 6) && m.targetIndex >= 0) {
            const int handle = goal.type == 7 ? goal.itemHandles2[static_cast<std::size_t>(m.targetIndex)]
                                              : goal.itemHandles[static_cast<std::size_t>(m.targetIndex)];
            if (const GameItem* item = state.findItem(handle)) {
                m.position = state.objects[static_cast<std::size_t>(item->objectIndex)].position;
            }
        }
        if (kMarkerAppearDelay <= markerTimer && kMarkerStagger <= markerTimer - markerLastStep) {
            if (m.frameStep < kFinalFrameStep) {
                m.stepTimer = dt + m.stepTimer;
                markerLastStep = m.stepTimer;
                if (m.stepTimer < kMarkerStepTime) return;
                m.frameStep = m.frameStep + 1;
                m.stepTimer = 0.0f;
                return;
            }
            if (m.stepTimer < kMarkerRestBelow) {
                m.stepTimer = kMarkerRestT;
                markerLastStep = markerTimer;
            }
        }
    }
}

void VisualState::updateStars(float dt, const WorldState& state) {
    // VisualWorldStateUtils::UpdateStars [verified: decompile]: the table is rebuilt from the live stars.
    std::vector<StarSpin> next;
    for (const GameItem& item : state.items) {
        if (item.type != ItemType::GoalStar) continue;
        StarSpin s;
        s.handle = item.handle;
        bool found = false;
        for (const StarSpin& old : stars) {
            if (old.handle == item.handle) {
                s = old;
                found = true;
                break;
            }
        }
        if (!found) {
            // lrand48 % 12, then lrand48 · 2^-31 / 12.
            s.frame = static_cast<int>(spinRandom.customRand() % static_cast<std::uint32_t>(kStarFrames));
            s.timer = static_cast<float>(spinRandom.customRand()) * 3.0517578e-05f * kStarFrameTime;
        } else {
            s.timer = s.timer - dt;
            if (!(s.timer > 0.0f)) {
                s.timer = kStarFrameTime;
                s.frame = s.frame + 1;
                if (s.frame >= kStarFrames) s.frame = 0;
            }
        }
        next.push_back(s);
    }
    stars.swap(next);
}

int VisualState::starFrame(int handle) const {
    for (const StarSpin& s : stars) {
        if (s.handle == handle) return s.frame;
    }
    return 0;
}

void VisualState::updateWaves(float dt, const WorldState& state) {
    // RadioControllerUtils::UpdateAnimation / TruckUtils::UpdateAnimation [verified: decompile + disassembly]: the
    // controller's waves grow and travel outward while its button is pressed; the paired truck's waves
    // shrink and travel inward (received signal).
    std::vector<WaveSet> next;
    for (const GameItem& item : state.items) {
        if (item.type != ItemType::RCController && item.type != ItemType::RCTruck) continue;
        WaveSet set;
        set.handle = item.handle;
        for (const WaveSet& old : waveSets) {
            if (old.handle == item.handle) set.waves = old.waves;
        }
        const bool controller = item.type == ItemType::RCController;
        bool pressed = false;
        if (controller) {
            pressed = item.buttonPressed;
        } else if (const GameItem* paired = state.findItem(item.stateWord)) {
            pressed = paired->buttonPressed;
        }
        std::vector<RenderWave>& w = set.waves;
        if (controller) {
            for (RenderWave& wave : w) {
                wave.age = dt + wave.age;
                wave.scale = kControllerWaveScale0 + wave.age * kWaveScaleRate;
                wave.x = wave.x + dt * kWaveSpeed;
                wave.alpha = 1.0f - wave.age;
            }
        } else if (!w.empty() && w.front().age < 1.0f) {
            for (RenderWave& wave : w) {
                wave.age = dt + wave.age;
                wave.scale = kTruckWaveScale0 + wave.age * -kWaveScaleRate;
                wave.x = wave.x - dt * kWaveSpeed;
                wave.alpha = wave.age + 0.0f;
            }
        }
        while (!w.empty() && !(w.front().age < 1.0f)) w.erase(w.begin());
        if (pressed && static_cast<int>(w.size()) < kMaxWaves && (w.empty() || w.back().age >= kWaveSpawnAge)) {
            RenderWave wave;
            wave.scale = controller ? kControllerWaveScale0 : kTruckWaveScale0;
            wave.alpha = controller ? 1.0f : 0.0f;
            wave.x = controller ? kControllerWaveX0 : kTruckWaveX0;
            w.push_back(wave);
        }
        next.push_back(set);
    }
    waveSets.swap(next);
}

const std::vector<RenderWave>* VisualState::wavesOf(int handle) const {
    for (const WaveSet& s : waveSets) {
        if (s.handle == handle) return &s.waves;
    }
    return nullptr;
}

void VisualState::startSparkles(Vec2 at, int count, float speed) {
    for (int i = 0; i < count; ++i) {
        RenderParticle p;
        const float a = particleRandom.getFloat(-kGamePi, kGamePi);
        const float v = particleRandom.getFloat(speed * 0.4f, speed);
        p.position = at;
        p.velocity = Vec2(cosF(a) * v, sinF(a) * v);
        p.maxLife = p.life = particleRandom.getFloat(kSparkleLife * 0.6f, kSparkleLife);
        p.size = particleRandom.getFloat(0.015f, 0.03f);
        p.colour = 0;
        particles.push_back(p);
    }
}

void VisualState::startConfetti(Vec2 at) {
    for (int i = 0; i < 80; ++i) {
        RenderParticle p;
        p.position = Vec2(at.x + particleRandom.getFloat(-0.3f, 0.3f), at.y + particleRandom.getFloat(-0.1f, 0.1f));
        p.velocity = Vec2(particleRandom.getFloat(-1.2f, 1.2f), particleRandom.getFloat(0.8f, 2.4f));
        p.maxLife = p.life = particleRandom.getFloat(kConfettiLife * 0.5f, kConfettiLife);
        p.size = particleRandom.getFloat(0.02f, 0.035f);
        p.colour = 1 + particleRandom.getInt(0, 4);
        particles.push_back(p);
    }
}

void VisualState::updateParticles(float dt) {
    for (RenderParticle& p : particles) {
        p.life = p.life - dt;
        if (p.colour != 0) p.velocity.y = p.velocity.y - kConfettiGravity * dt;
        p.position = Vec2(p.position.x + p.velocity.x * dt, p.position.y + p.velocity.y * dt);
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const RenderParticle& p) { return p.life <= 0.0f; }),
                    particles.end());
}

void VisualState::resetRun() {
    stars.clear();
    waveSets.clear();
    particles.clear();
}

}  // namespace aa::sim
