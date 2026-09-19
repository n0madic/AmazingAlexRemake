// The read-only snapshot the platform renderer draws from (docs/10-architecture.md §5.4, docs/11): items in
// collection order with their body transforms, the goal markers, the camera and the background. Value
// semantics — the renderer never touches WorldState or the Box2D world.
#pragma once

#include "aa/sim/types.h"
#include "aa/sim/world_state.h"

#include <array>
#include <cstdint>
#include <vector>

namespace aa::sim {

// One RC signal wave (RadioControllerUtils / TruckUtils::UpdateAnimation, docs/11 §5).
struct RenderWave {
    float x = 0.0f;       // distance from the antenna (drawn on both sides, mirrored)
    float scale = 0.0f;
    float alpha = 0.0f;
    float age = 0.0f;
};

// A sparkle / confetti particle (approximate, docs/10 §1): position, velocity, remaining life, colour.
struct RenderParticle {
    Vec2 position{0.0f, 0.0f};
    Vec2 velocity{0.0f, 0.0f};
    float life = 0.0f;
    float maxLife = 0.0f;
    float size = 0.0f;
    int colour = 0;       // 0 sparkle (white), 1..5 confetti colours
};

// World transform of one body (b2Body::GetPosition / GetAngle).
struct RenderBody {
    Vec2 position{0.0f, 0.0f};
    float angle = 0.0f;
};

constexpr std::array<bool, PhysicsObject::kMaxBodies> allRopeSegments() {
    std::array<bool, PhysicsObject::kMaxBodies> a{};
    for (bool& b : a) b = true;
    return a;
}

struct RenderItem {
    ItemType type = ItemType::None;
    int objectIndex = -1;
    int handle = 0;
    Vec2 position{0.0f, 0.0f};    // PhysicsObject position / angle / scale (flip = scale.x -1)
    float angle = 0.0f;
    Vec2 scale{1.0f, 1.0f};
    float halfSize = 1.0f;        // template size "r"
    std::uint8_t flags = 0;       // object_flags
    std::uint8_t state = 0;       // object_state (ghost tint bit 1)
    std::int32_t stateWord = 0;   // GameItem+8 (book colour, billboard hint, controller link…)
    bool builtInController = false;   // legacy Truck/Trapdoor/Helicopter with the controller as an own body
    Vec2 endVector{0.0f, 0.0f};   // rope / zip-line far end, slingshot pouch (item-local)
    int bodyCount = 0;
    std::array<RenderBody, PhysicsObject::kMaxBodies> bodies{};
    int attachmentCount = 0;
    std::array<AttachmentRecord, PhysicsObject::kMaxAttachments> attachments{};
    // Rope: whether the link bodies k and k+1 (in RopeRenderUtils::CalculateBodyIndices order) still share a
    // joint — RopeRenderUtils::AddIndices draws the segment only then (a cut rope shows the gap). All true
    // unless renderState() found a missing joint.
    std::array<bool, PhysicsObject::kMaxBodies> ropeSegments = allRopeSegments();
    float latticeAngle = 0.0f;    // BoxingGlove+0x24: scissor-lattice hinge angle (docs/03 §14)
    float buttonHeightPx = 0.0f;  // BoxingGlove+0x20: trigger button sprite height
    bool held = false;            // the manipulated item (RenderState::heldIndex points at it)
    // --- simulation animation state (docs/11 §5), straight from the item block ------------------
    bool popped = false;          // Balloon+8; popTimer = the 0.15 s countdown → frames 2..5
    float popTimer = 0.0f;
    float piggyTimer = 0.0f;      // PiggyBank+8: POW frames 69..72 while < 0.2 s (broken items only)
    bool magnetPulling = false;   // Magnet+8: the pulse frame 63 + magnetFrame at (0, 0.19)
    int magnetFrame = 0;
    float rotorPhase = 0.0f;      // Helicopter+0x20: rotor frame 46 + (int(phase·10) mod 10)
    float tailPhase = 0.0f;       // Helicopter+0x28: the tail rotor's rotation
    int bumperOn = 0;             // Bumper+8: frame 29 while on
    int snipStep = -1;            // Scissors+0x10: −1 none, 0..4 → frame 101 + step
    bool snipping = false;        // Scissors+0x18: Sparkle03 scaled by snipPhase instead
    float snipPhase = 0.0f;
    float cutAngle = 0.0f;        // Scissors+0xC
    int starFrame = 0;            // the spin frame 0..11 (VisualState)
    std::vector<RenderWave> waves;   // RC controller / truck signal waves (VisualState)

    bool flipped() const { return scale.x < 0.0f; }
    float flipSign() const { return scale.x < 0.0f ? -1.0f : 1.0f; }
    bool isFixed() const { return (flags & object_flags::kFixed) != 0; }
};

// VisualWorldState goal marker (docs/11 §3 step 9): kind 1 circle on a target, 6 cross, 7 arrow at the goal
// angle, 2/3/4/5 side arrows (right/left/up/down), 8 down arrow at an unclamped point. frameStep is the
// appear animation step: -1 = not shown yet (RenderWorld skips it), 0..4 = goal_*_1..5 frames.
struct RenderMarker {
    int kind = 0;
    Vec2 position{0.0f, 0.0f};
    float angle = 0.0f;
    int frameStep = -1;
    float stepTimer = 0.0f;   // VisualWorldState marker +0xC: the appear animation's step timer
    int targetIndex = -1;     // the goal target this marker follows (kinds 1 / 6), -1 for the goal's own
};

// st::Camera (docs/11 §1): centre in virtual play-field pixels (512, 319 = the middle), the zoom, and the
// edge auto-scroll state of CameraUtils::Update (+0x14 timer, +0x18 corner flag).
struct Camera {
    static constexpr float kDefaultCenterX = 512.0f;
    static constexpr float kDefaultCenterY = 319.0f;

    Vec2 centerPx{kDefaultCenterX, kDefaultCenterY};
    float zoom = 1.0f;
    float edgeTimer = 0.0f;
    bool cornerFlag = false;
};

// The held-item overlay (GameRenderState+0x2c..+0x44, docs/11 §2): what renderFrame draws over the world
// for the manipulated item.
struct ManipulationOverlay {
    static constexpr int kNone = 0;
    static constexpr int kTranslation = 1;   // TranslationGizmoBig scaled by animScale (selection animation)
    static constexpr int kGizmos = 2;        // RotationGizmo_iPhone at angle + gizmoAngle (+ FlipGizmo for flippable items)
    static constexpr int kInvalid = 3;       // InvalidSelection (the fixed-item buzz)

    int state = kNone;
    int objectIndex = -1;                    // object index (RenderState::items carries objectIndex)
    Vec2 position{0.0f, 0.0f};               // GameItemUtils::GetSelectedPos: the object position, or the grabbed
                                             // body's for ropes and zip lines (GameRenderState+0x34)
    float angle = 0.0f;                      // the object angle (the rotation gizmo turns with the item)
    float gizmoAngle = 0.0f;                 // renderFrame's slow spin phase
    float animScale = 1.0f;                  // ManipulationAnimation::gizmoScale
    bool inGhost = false;                    // ghost colour (0.8784, 0.2667, 0) instead of (0.6431, 0.7843, 0.9333)
    bool showFlip = false;                   // the flip button (object flag bit 3)
    Vec2 flipButtonPos{0.0f, 0.0f};          // world position of the flip button
};

// The fixed-item buzz: the item flashes / shakes for `t` ∈ [0, 1] of the interpolator at a random angle.
struct RenderBuzz {
    int handle = 0;
    float amplitude = 0.0f;
    float angle = 0.0f;
};

// The toolbox strip in native px (y up), for ToolboxRenderer::Render (docs/11 §7).
struct RenderToolboxSlot {
    ItemType type = ItemType::None;
    int amount = 0;
    float widthPx = 0.0f;
    float heightPx = 0.0f;
    float scale = 1.0f;
};
struct RenderToolbox {
    bool visible = false;
    float spriteScale = 1.0f; // UIElements frame px → native px (the profile scale, Session::toolboxScale)
    float x = 0.0f;           // button centre
    float y = 0.0f;
    float ejectLength = 0.0f; // how far the strip is out (px); the slots are scissored to it
    float scroll = 0.0f;
    float buttonScale = 1.0f; // the press animation (1 → 1.2)
    std::vector<RenderToolboxSlot> slots;
};

// CameraUtils::GetClampedCenter: keeps the visible rectangle inside the 1024x638 play field.
Vec2 clampedCameraCenter(const Camera& camera, Vec2 centerPx);

struct TutorialGhost {
    ItemType type = ItemType::None;
    Vec2 position{0.0f, 0.0f};
    float angle = 0.0f;
    float halfSize = 1.0f;   // the type's template "r" (the original copies the template)
};

struct RenderState {
    PhysicsMode mode = PhysicsMode::SetUp;
    std::vector<RenderItem> items;       // item order (type-sorted, as the original's collection)
    std::vector<RenderMarker> markers;
    bool markersBehindItems = false;     // VisualWorldState+0xc
    Camera camera;
    int backgroundIndex = 0;
    // The sandbox background change (RenderWorld, docs/11 §2): the new background is drawn at x =
    // backgroundSlide metres, the previous one (previousBackground ≥ 0) at backgroundSlide − 3.41 while
    // the slide is non-zero.
    float backgroundSlide = 0.0f;
    int previousBackground = -1;
    int heldIndex = -1;                  // index into `items` of the held item (drawn last), -1 = none
    ManipulationOverlay overlay;
    RenderBuzz buzz;
    RenderToolbox toolbox;
    std::vector<RenderParticle> particles;   // sparkles / confetti (approximate)
    bool completed = false;                  // controller state 6: the world is frozen after the countdown
    // The tutorial hand's ghost items (GameState+0x31a0c entries with mode 2 / 3): Shelf and Book copies
    // drawn at 50 % grey, 50 % alpha under the hand; other types are listed but the original draws nothing.
    std::vector<TutorialGhost> tutorialGhosts;
};

}  // namespace aa::sim
