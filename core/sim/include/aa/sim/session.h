// One loaded level: the core API the platform layer talks to (docs/10-architecture.md §5.4). Since M3 the
// session runs the set-up mode of GameScreenController::doFrame — pick, drag, snap, rotate, flip, ghost,
// toolbox, undo, play/stop/restart, edge auto-scroll — on native pointer pixels; since M4 the simulation
// branch runs GameScreen::UpdateSimulation (docs/04 §3), the world-side actions, goals and the completion
// sequence.
#pragma once

#include "aa/sim/action.h"
#include "aa/sim/animations.h"
#include "aa/sim/ghost.h"
#include "aa/sim/goal_state.h"
#include "aa/sim/level.h"
#include "aa/sim/physics_world.h"
#include "aa/sim/render_state.h"
#include "aa/sim/screen_layout.h"
#include "aa/sim/simulation.h"
#include "aa/sim/sound_sink.h"
#include "aa/sim/templates.h"
#include "aa/sim/toolbox.h"
#include "aa/sim/tutorial.h"
#include "aa/sim/touch.h"
#include "aa/sim/visual_state.h"
#include "aa/sim/world_state.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace aa::sim {

// GameMode::Enum of the original: 0 campaign, 1 sandbox editor, 2 World-of-Contraptions play, 3 friend
// solution, 4 test play (sandbox), 5 sandbox toolbox step. Campaign, Sandbox, TestPlay and SandboxToolbox
// are implemented (docs/05 §1); the online modes 2 / 3 exist so the mode checks read like the original.
enum class GameMode : int { Campaign = 0, Sandbox = 1, WorldOfContraptions = 2, FriendSolution = 3, TestPlay = 4, SandboxToolbox = 5 };

// The item types the editor toolbox offers (GameProgress+0x160 + 0x10·type, docs/05 §4), indexed by ItemType.
using UnlockedItems = std::array<bool, kItemTypeCount>;

// LevelLayoutUtils::Apply for the world part: every layout item becomes an item + physics object under
// its stored handle, with the fixed flag / flip scale and the per-type item state (docs/02 §3). A level
// without a WorldBound item gets one first (as the harness does), so body 0 is always the world bound.
void applyLayout(const Level& level, const TemplateTable& templates, WorldState& state);

// GamePhysicsUtils::CreateDynamicPhysics + CreateAttachments: createPhysics for every object in collection
// order, then the attachment joints and the rope adjustments (docs/04 §2, §7).
void createWorldPhysics(WorldState& state, PhysicsWorld& world, PhysicsMode mode);

// LevelLayoutUtils::Get: the layout of the live state — one item per object in collection order (ghost
// copies and the SelectionArea included, as the original does), attachment records by object index, the
// toolbox counts; header fields from `header`.
Level levelFromState(const WorldState& state, const Level& header, const Toolbox& toolbox);

// st::UndoQueue (0x45c08 bytes): `top` = the newest snapshot, `count` = the current one; 32 layouts, the
// oldest dropped when full. prepareForNewLevel resets it and pushes the base layout (count 0 = nothing to
// undo); every edited frame pushes; undo / redo move `count` and restore that layout.
struct UndoQueue {
    static constexpr int kCapacity = 32;
    static constexpr int kMaxCount = 0x1f;

    int top = -1;
    int count = -1;
    std::array<Level, kCapacity> layouts{};

    void reset();
    void add(const Level& layout);
};

// The sound / feedback events the session produces (action 13, the buzz, the goal / star / completion
// milestones of a play run); drained by the platform.
struct SessionEvent {
    enum class Kind { Sound, Buzz, GoalComplete, StarCollected, LevelCompleted };
    Kind kind = Kind::Sound;
    int soundId = 0;
    Vec2 position{0.0f, 0.0f};
    float volume = 1.0f;
};

class Session {
public:
    // The template table is shared, read-only and must outlive the session (built once at start-up).
    explicit Session(const TemplateTable& templates);

    // GameStateUtils::CreateNew + GameScreenController::prepareForNewLevel (docs/05 §2): layout →
    // WorldState → set-up world with attachments; campaign items fixed; the toolbox from the level; the
    // SelectionArea object; the base layout and the undo base snapshot; goal markers; camera reset.
    // Mode Sandbox: the editor toolbox (ToolboxUtils::SetFull over the unlocked types, minus what the
    // level holds) becomes the active strip and nothing is fixed (SandboxView::Show un-fixes the level's
    // items after playNewLevel). GameStateUtils::CreateNewSandbox = load(Level{}, Sandbox).
    void load(const Level& level, GameMode mode = GameMode::Campaign, SceneRecorder* recorder = nullptr);
    // The GameProgress item unlocks the editor strip lists (all set by default: tools and tests).
    void setUnlockedItems(const UnlockedItems& unlocked) { unlocked_ = unlocked; }
    const UnlockedItems& unlockedItems() const { return unlocked_; }

    // --- the sandbox editor (GameScreenController::setMode and friends, docs/05 §1) ----------------
    // setMode(m) [verified]: 1 → 4 snapshots the layout and fixes every object (test play); 1 → 5 stashes
    // the undo queue, remembers the layout and the object handles, empties the level strip (the active
    // toolbox from now on) and fixes the stars; 5 → x undoes every move, restores the undo queue and
    // un-fixes; 4 → 1 un-fixes. The editor toolbox is the active strip in modes 1 / 4.
    void setMode(GameMode mode);
    // WorldStateUtils::MarkAllObjectsFixed / MarkAllObjectsNotFixed / MarkAllStarsFixed.
    void markAllObjectsFixed();
    void markAllObjectsNotFixed();
    void markAllStarsFixed();
    // SandboxView's background button: GameState+0x23a8 = index, GameScreenController::backgroundChanged(1):
    // the new background slides in from the right over 0.5 s (renderState().backgroundSlide), the level's
    // `tested` flag clears. The world bound keeps its shape until the level is reloaded (as the original).
    void setBackground(int index);
    // LevelLayoutUtils::Get of the live state (the sandbox file's content: title / author / background /
    // tested from the header, the level strip as the toolbox list).
    Level sandboxLevel() const { return levelFromState(state_, level_, toolbox_); }
    // LevelLayout.tested (GameState+0x25b0): GameScreenController::isShareAllowed — set by a completed test
    // play, cleared by every edit in the editor.
    bool tested() const { return level_.tested; }
    // The editor's own strip (GameScreenController+0xc2094) and whether it is the active one.
    const Toolbox& editorToolbox() const { return editorToolbox_; }
    bool editorToolboxActive() const { return editorActive_; }
    // The handles moved into the strip in the toolbox step (GameScreenController+0x29b4).
    const std::vector<int>& removedHandles() const { return removedHandles_; }

    // The original's DestroyWorld → CreateWorld → CreateDynamicPhysics → CreateAttachments on the current
    // WorldState — not a reload: rope ends already moved onto their attached objects stay where they are.
    // (play / stop go through the layout snapshot instead, as toggleSimulation does.) The recorder, like
    // load()'s, covers this construction only.
    void rebuildWorld(PhysicsMode mode, SceneRecorder* recorder = nullptr);

    const Level& level() const { return level_; }
    const WorldState& state() const { return state_; }
    PhysicsWorld* world() const { return world_.get(); }
    PhysicsMode physicsMode() const { return physicsMode_; }
    GameMode gameMode() const { return gameMode_; }
    VisualState& visual() { return visual_; }
    const VisualState& visual() const { return visual_; }
    const TemplateTable& templates() const { return templates_; }

    // Camera in virtual play-field pixels (docs/11 §1); the centre is clamped as CameraUtils does.
    const Camera& camera() const { return camera_; }
    void setCameraCenter(Vec2 centerPx);
    void setCameraZoom(float zoom);

    // --- viewport and coordinates (docs/11 §1) ----------------------------------------------------
    // The window's ScreenLayout: the touch handler, the toolbox strip and the camera work in native
    // pixels (y up), and screenToWorld needs the letterbox numbers. Call whenever the window resizes.
    void setViewport(const ScreenLayout& layout);
    const ScreenLayout& viewport() const { return layout_; }
    // The UIElements sizes of the toolbox strip (per profile, unscaled; the session scales them to the
    // viewport). Defaults to the 2048X1536 profile numbers when never set.
    void setToolboxFrameSizes(const ToolboxFrameSizes& sizes);
    // st::screenToWorld: native px (y up) → world metres through the camera.
    Vec2 screenToWorld(Vec2 nativePx) const;
    // WorldPtToScreenPt + CameraUtils::ScreenToPixelPos: world metres → native px (y up).
    Vec2 worldToScreen(Vec2 world) const;
    // Phones scroll the camera towards a held item near an edge (CameraUtils::Update); tablets do not
    // (DeviceParams::IsTablet gates the block in doFrame) [verified]. Default: phone behaviour.
    void setTablet(bool tablet) { isTablet_ = tablet; }

    // --- input (TouchUtils::QueueTouches*): native px with y DOWN as the window reports it; the y flip
    // to the original's y-up convention happens here. `id` distinguishes fingers / buttons.
    void pointerDown(int id, Vec2 nativePxYDown);
    void pointerMove(int id, Vec2 nativePxYDown);
    void pointerUp(int id, Vec2 nativePxYDown);
    void pointerCancel(int id);

    // --- direct commands (desktop conveniences that queue what a touch would) -----------------------
    // Rotates the held item by `deltaAngle` (through the touch-state angle and action 5, so the next
    // move keeps the new angle).
    void rotateHeld(float deltaAngle);
    // Action 6 on the held / gizmo item.
    void flipHeld();
    // Action 8 for toolbox slot `slot` at the strip's slot centre (the strip must be visible).
    void takeFromToolbox(int slot);
    // Action 9: the held item returns to the toolbox.
    void returnHeld();
    // Action 0x1B: scroll the strip by `deltaPx`.
    void scrollToolbox(float deltaPx);
    // The strip button's release (touch state 0xE): the strip retracts / extends (Toolbox::buttonState).
    void toggleToolbox();
    // Queues `a` as an item update or the contact listener would (the conformance scripts' `goal`).
    void queueAction(const Action& a) { queue_.add(a); }
    void undo();
    void redo();
    bool canUndo() const;   // GameScreenController::isActionEnabled(0)
    bool canRedo() const;   // isActionEnabled(1)
    // toggleSimulation halves: play = the layout snapshot + simulation world; stop = back to that
    // layout's set-up world. restart = the base layout.
    // `recorder` (the conformance dump tool) covers the simulation world's construction only.
    void play(SceneRecorder* recorder = nullptr);
    void stop();
    void restart();
    // The pause-menu entry (GameScene::SetPaused): drops the held item back where it is / into the toolbox.
    void pause();
    // GameView::ButtonPressed / SetPaused / ReturnFromSolutions: the gizmo off and TutorialUtils::Stop.
    void stopTutorial();
    // The touch state's held item (object index) or -1; the state machine value.
    int heldObject() const;
    const TouchState& touchState() const { return touch_; }
    // Mutable access for the tests that force the unreachable two-finger states (docs/05 §2).
    TouchState& touchState() { return touch_; }
    const Toolbox& toolbox() const { return editorActive_ ? editorToolbox_ : toolbox_; }
    const GhostState& ghost() const { return ghost_; }
    const UndoQueue& undoQueue() const { return undo_; }
    bool isManipulationActive() const;
    bool edited() const { return edited_; }

    // GameScreenController::doFrame (docs/05 §5, docs/04 §3), one frame of `wallDt` seconds: the set-up
    // branch in state 2, the simulation branch in state 4 (the accumulator fed with wallDt × 0.8, the
    // 1/120 s substeps, the world-side actions, goal evaluation, the completion countdown, the runaway
    // stop and the 5 s no-motion auto-stop), the common tail.
    void advance(float wallDt);

    // --- simulation state (GameScreenController / GameState fields), for the HUD and the dump tools ---
    int controllerState() const { return controllerState_; }   // 2 set-up, 4 simulation, 6 completed
    float accumulator() const { return accumulator_; }         // +0xc28f4
    float playTime() const { return playTime_; }               // GameState+0
    const GoalState& goalState() const { return goalState_; }
    const Random& random() const { return random_; }          // GameState+0x575e0
    bool completing() const { return completing_; }            // +0xc998c: the completion countdown runs
    Vec2 levelCompletePos() const { return levelCompletePos_; } // +0xc9990: where the completion effect started

    // --- the Classroom tutorial (docs/05 §5) ----------------------------------------------------
    // The location / level the session plays (LocationInfo+0, GameState+0x23a4): tutorial_should_run.
    void setTutorialContext(int locationIndex, int levelIndex);
    // Level 0's script points at the ButtonPlay view: its centre in native px (y down, as the window
    // reports it). Without it the level-0 script does not start.
    void setTutorialPlayButton(Vec2 nativePxYDown);
    const TutorialState& tutorial() const { return tutorial_; }
    bool stopRequested() const { return stopRequested_; }      // +0xc99b8: the next frame stops the run
    int substepCount() const { return substep_; }              // physics substeps since load
    // The render copy's pose of an object (GamePhysicsUtils::LerpState: the lerp of the two last physics
    // states for dynamic-flag objects, the live pose otherwise) — what renderState() reports in simulation.
    RenderPose renderPose(int objectIndex) const;
    // The wall clock in seconds, as TimeUtils::GetAbsoluteTime: only seeds the bouncy ball's random impact
    // sound. The dump tools leave it 0 (the harness clock).
    void setAbsoluteTime(double seconds) {
        timeSeed_ = static_cast<int>(seconds);
        simContext_.timeSeed = timeSeed_;   // the listener reads the context's copy
    }
    // Called after every physics substep (GetStateFromPhysics done, item updates of the step not yet run)
    // with the running substep count — the trajectory dump of the conformance tool.
    void setSubstepObserver(std::function<void(int)> observer) { substepObserver_ = std::move(observer); }

    // The actions processed during the last advance() (ids of docs/05 §7), for tests and tools; cleared
    // by the call.
    std::vector<Action> drainActions();
    // The same with the substep each action was processed in (-1 = before / after the substep loop).
    struct ProcessedAction {
        Action action;
        int substep = -1;
    };
    std::vector<ProcessedAction> drainProcessedActions();
    // Sounds / buzz produced since the last call.
    std::vector<SessionEvent> drainEvents();
    // The audio interface of SoundRenderer::Render (the looping clips); null = headless. Must outlive
    // the session or be cleared before it goes away.
    void setSoundSink(SoundSink* sink) { soundSink_ = sink; }

    // Snapshot for the renderer (docs/10 §5.4): items in item order with body transforms straight from the
    // Box2D bodies, the held-item overlay, the toolbox strip, markers, camera and background.
    RenderState renderState() const;

private:
    struct Transitions {
        CubicInterpolator toolboxX;      // GameScreenTransitions+0: the strip button x (native px)
        CubicInterpolator toolboxEject;  // +0x18: the eject length while retracting
        CubicInterpolator background;    // +0xb0: the sandbox background slide (virtual px, 1024 → 0)
    };

    void prepareForNewLevel();
    void buildEditorToolbox();
    void updateSandboxToolboxLayout(int handle);
    int physIndexFromHandle(int handle) const;
    int physicsIndexSBOriginalToCurrent(int original) const;
    int physicsIndexSBCurrentToOriginal(int current) const;
    Toolbox& tb() { return editorActive_ ? editorToolbox_ : toolbox_; }
    const Toolbox& tb() const { return editorActive_ ? editorToolbox_ : toolbox_; }
    void createSelectionAreaObject();
    void restoreGameState(const Level& layout, PhysicsMode mode, SceneRecorder* recorder = nullptr);
    void saveUndoState();
    void toggleSimulation(SceneRecorder* recorder = nullptr);
    void applyToolboxFromLayout(const Level& layout);
    void displayToolbox();
    void retractToolbox();
    float toolboxY() const;
    float toolboxOnscreenX() const;
    float toolboxOffscreenX() const;
    float toolboxScale() const;

    void processActions();
    bool processSimulationAction(const Action& a);
    void processSimulationTouches();
    void doFrame(float dt);
    void advanceSimulation(float dt, bool& recurse);
    float updateSimulation(float acc);
    void startLevelCompleteSequence();
    void setCompletedState();
    void renderSounds();         // SoundRenderer::Render (sound_renderer.cpp)
    void stopLoopingSounds();    // SoundRenderer::StopLoopingSounds
    void frameTail();
    bool itemActionsForSelectedAddable(const Action& a, bool ended);
    void itemActionsForSelectedNormal(const Action& a, bool ended);
    void itemActionsForSelectedCompletion(const Action& a);
    void itemActionsNewSelection(const Action& a);
    bool itemActionsMisc(const Action& a, Action& requeue);
    void endManipulationForActiveItem(int itemIndex, bool& ended);
    void releaseHeldItems();
    void updateGhostAnimation(float dt);
    void updateFlippingAnimation(float dt);
    void updateManipulationAnimation(float dt);
    void updateGhostManipulation();
    void saveGoodState(int itemIndex);
    void exitGhostState(int itemIndex);
    void revertGhostState(int itemIndex);
    void updateCamera(float dt);
    void resetTouchState();

    const TemplateTable& templates_;
    Level level_;
    GameMode gameMode_ = GameMode::Campaign;
    PhysicsMode physicsMode_ = PhysicsMode::SetUp;
    WorldState state_;
    std::unique_ptr<PhysicsWorld> world_;
    VisualState visual_;
    Camera camera_;
    ScreenLayout layout_ = ScreenLayout::compute(1024, 768);
    bool isTablet_ = false;

    Touches touches_;
    TouchState touch_;
    double now_ = 0.0;            // the touch timestamps: seconds accumulated from advance()
    ActionQueue queue_;
    std::vector<ProcessedAction> processed_;
    int processingSubstep_ = -1;   // the substep processActions runs in (-1 outside the loop)
    std::vector<SessionEvent> events_;
    SoundSink* soundSink_ = nullptr;
    int lastActionId_ = -1;       // GameScreenController+0xc99c0

    Toolbox toolbox_;                  // GameState+0x2937c: the level's strip (LevelLayoutUtils::Get / Apply)
    Toolbox editorToolbox_;            // GameScreenController+0xc2094: every unlocked item (modes 1 / 4)
    bool editorActive_ = false;        // +0xc25b8 points at the editor toolbox
    UnlockedItems unlocked_;           // GameProgress item unlock bytes (all set by default)
    Level readyLayout_;                // +0x5a4: the layout when the toolbox step began
    Level testPlayLayout_;             // +0xc72a0: the layout when test play began
    UndoQueue undoBackup_;             // +0x79c90: the editor's undo queue while in the toolbox step
    std::vector<int> handlesAtReady_;  // +0x29a4: object handles in collection order at 1 → 5
    std::vector<int> removedHandles_;  // +0x29b4: handles moved into the strip in mode 5
    int previousBackground_ = -1;      // GameResources+0x64b8: the background sliding out (−1 = none)
    ToolboxFrameSizes toolboxSizes_;   // per profile, unscaled
    Transitions transitions_;
    ManipulationAnimation manipAnim_;
    FlippingAnimation flipAnim_;
    GhostState ghost_;
    GhostAnimation ghostAnim_;
    BuzzState buzz_;
    Random random_;
    float gizmoPhase_ = 0.0f;     // GameScreenController+0xc28fc: the rotation gizmo's slow spin
    float totalTime_ = 0.0f;      // +0xc9970: seconds since the level started (the wobble's clock)

    int activeHandle_ = -1;       // +0xc99bc
    bool adding_ = false;         // +0xc99b9: a new toolbox item is being animated in
    bool removing_ = false;       // +0xc99ba: action 9 processed, removal animation running
    bool edited_ = false;         // +0xc996d
    bool snapping_ = true;        // +0xc25bd
    bool paused_ = true;          // +0xc996c: set-up (true) / simulation running (false)
    bool inputEnabled_ = true;    // UI::SceneManager::SetUserInteractionEnabled while flipping
    int swallowedPointer_ = -1;   // SceneManager+0x80: the pointer that went down while input was disabled
    int controllerState_ = 2;     // +0x31c80: 2 set-up, 3 set-up→simulation, 4 simulation, 5 simulation→set-up,
                                  // 6 completed

    // --- simulation (docs/04 §3) ---------------------------------------------------------------
    float accumulator_ = 0.0f;        // +0xc28f4: unconsumed simulation time
    WorldState prevState_;            // +0x29c0: the state before the last substep (the lerp's start)
    std::vector<RenderPose> renderPoses_;   // the render copy's poses by object index
    float playTime_ = 0.0f;           // GameState+0: seconds of simulation shown on the stopwatch
    GoalState goalState_;             // GameState+0x808
    bool completing_ = false;         // +0xc998c: startLevelCompleteSequence ran, the countdown is on
    float completionTimer_ = 0.0f;    // +0xc9988: seconds until setCompletedState
    Vec2 levelCompletePos_{0.0f, 0.0f};   // +0xc9990
    float idleTime_ = 0.0f;           // +0xc9980: seconds without a moving body (5 s → stop)
    float markerReshowTimer_ = 0.0f;  // +0xc9984: seconds since a touch hid the goal markers (5 s → re-laid)
    TutorialState tutorial_;          // GameState+0x57398
    bool tutorialStarted_ = false;    // GameScreenController+0x29b0: Start ran once for this level
    int tutorialLocation_ = -1;
    int tutorialLevel_ = -1;
    bool tutorialHasPlayButton_ = false;
    Vec2 tutorialPlayButtonPx_{0.0f, 0.0f};   // native px, y up
    TutorialContext tutorialContext() const;
    void startTutorial();
    bool stopRequested_ = false;      // +0xc99b8: a simulation touch; the next frame stops the run
    int substep_ = 0;
    float lastDt_ = 0.0f;             // the frame dt, for the tail's idle timer
    SimulationContext simContext_;    // what the contact listener reaches (pointers to the members above)
    int timeSeed_ = 0;                // TimeUtils::GetAbsoluteTime for the bouncy-ball sound (setAbsoluteTime)
    std::function<void(int)> substepObserver_;

    UndoQueue undo_;
    Level baseLayout_;            // +0xc2aa0: the layout at prepareForNewLevel (restart)
    Level playLayout_;            // +0xc4ea0: the layout at play (stop returns to it)
};

}  // namespace aa::sim
