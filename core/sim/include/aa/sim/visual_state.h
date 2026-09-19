// st::VisualWorldState — the presentation state that lives outside WorldState (docs/11 §5): goal markers
// with their appear animation, the star spin, the RC signal waves, and the sparkle / confetti particles
// (approximate by decision, docs/10 §1).
#pragma once

#include "aa/sim/animations.h"
#include "aa/sim/level.h"
#include "aa/sim/render_state.h"
#include "aa/sim/world_state.h"

#include <vector>

namespace aa::sim {

struct VisualState {
    std::vector<RenderMarker> markers;
    int markerCount = 0;            // VisualWorldState+0: the goal's target count (0 while a touch hides the markers)
    bool markersBehindItems = false;
    float markerTimer = 0.0f;       // VisualWorldState+0x134: seconds since SetGoalMarkers
    float markerLastStep = 0.0f;    // +0x138: the last step's time (the 0.4 s stagger)

    // Star spin entries (+0x2238c, 4 × {handle, timer, frame}).
    struct StarSpin {
        int handle = 0;
        float timer = 0.0f;
        int frame = 0;
    };
    std::vector<StarSpin> stars;
    Random spinRandom;              // the original draws lrand48 for the first frame / phase

    // RC waves per RadioController / RCTruck handle (item block fields in the original).
    struct WaveSet {
        int handle = 0;
        std::vector<RenderWave> waves;
    };
    std::vector<WaveSet> waveSets;

    std::vector<RenderParticle> particles;
    Random particleRandom;

    // VisualWorldStateUtils::SetGoalMarkers: one marker per live goal target (circle, or a cross for goal
    // type 5) at the target object's position, plus the goal's own marker (arrow / side arrows) at the
    // clamped (width, height) point. Every marker starts at frameStep -1; the timers restart.
    void setGoalMarkers(const Goal& goal, const WorldState& state);
    // VisualWorldStateUtils::UpdateGoals: the markers follow their targets; 0.7 s after SetGoalMarkers
    // the first marker steps through frames 0..4 (1/15 s each), the next one 0.4 s later, and so on.
    void updateGoals(float dt, const Goal& goal, const WorldState& state);
    // The end state of the appear animation (frameStep 4 for every marker).
    void revealGoalMarkers();
    // A touch in set-up (doFrame): the marker counts and the timers go to 0, nothing is drawn until
    // setGoalMarkers lays the markers out again (5 s after the last touch, Session::frameTail).
    void hideGoalMarkers();

    // VisualWorldStateUtils::UpdateStars: every 1/12 s the next of the 12 spin frames; a star seen for the
    // first time gets a random frame and phase (lrand48 in the original — here the state's own Random).
    void updateStars(float dt, const WorldState& state);
    int starFrame(int handle) const;

    // RadioControllerUtils::UpdateAnimation / TruckUtils::UpdateAnimation: the signal waves.
    void updateWaves(float dt, const WorldState& state);
    const std::vector<RenderWave>* wavesOf(int handle) const;

    // The approximate effects: a sparkle burst (star collected, scissors cut), a pop / break puff and the
    // level-complete confetti.
    void startSparkles(Vec2 at, int count, float speed);
    void startConfetti(Vec2 at);
    void updateParticles(float dt);
    // VisualWorldStateUtils::PartialReset + a new run: waves, spins and particles dropped.
    void resetRun();
};

}  // namespace aa::sim
