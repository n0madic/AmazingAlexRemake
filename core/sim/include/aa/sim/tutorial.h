// The Classroom tutorial hand (docs/05 §5) [verified: st::TutorialState / Hand / ControlledItem, the hand
// states Setposstate / Setimagestate / Waitstate / Fadestate / Movestate (LinearPath, CircularPath) /
// Setdragitemstate / Setorientationitemstate, TutorialUtils::Start / Update / Stop / Reset,
// tutorial_should_run, click_position_tutorial / fetch_all_items_tutorial / fetch_item_rotate_tutorial,
// tutorial_chap0_level0..6, toolboxIdxToWorld — decompile + disassembly, 2026-09-14].
//
// The script is a list of hand states run in a loop (step mod count); each state edits the TutorialState
// and advances the step. The "controlled items" are ghost copies of toolbox items the hand drags: the
// original writes them into the GameState's extra render table (GameState+0x31a0c; only the Shelf and
// the Book are drawn there, at 50 % grey); the remake exposes them in RenderState::tutorialGhosts.
#pragma once

#include "aa/sim/types.h"

#include <vector>

namespace aa::sim {

// st::Hand (TutorialState+0x18): position (world m), alpha, angle, image (0 = pointer, 1 = pressed).
struct TutorialHand {
    Vec2 pos{0.0f, 0.0f};
    float alpha = 0.0f;
    float angle = 0.0f;
    int image = 0;
};

// st::ControlledItem (0x14 bytes): type, position, angle, visible.
struct TutorialControlledItem {
    int type = 0;
    Vec2 pos{0.0f, 0.0f};
    float angle = 0.0f;
    bool visible = false;
};

// One hand state; the original's polymorphic TutorialHandState objects flattened into a record.
struct TutorialStep {
    enum class Kind : int { SetPos, SetImage, Wait, Fade, MoveLinear, MoveCircular, SetDragItem, SetOrientationItem };
    Kind kind = Kind::Wait;
    Vec2 a{0.0f, 0.0f};      // SetPos: the position; MoveLinear: from; MoveCircular: centre
    Vec2 b{0.0f, 0.0f};      // MoveLinear: to
    float duration = 0.0f;   // Wait: the time; Fade / Move: the duration
    float from = 0.0f;       // Fade: from alpha; MoveCircular: start angle
    float to = 0.0f;         // Fade: to alpha; MoveCircular: swept angle
    float radius = 0.0f;     // MoveCircular
    int image = 0;           // SetImage
    int slot = -1;           // SetDragItem / SetOrientationItem: controlled item index (−1 = none)
    int type = 0;            // SetDragItem / SetOrientationItem: item type
};

// st::TutorialState (0x64 bytes).
struct TutorialState {
    int step = 0;                     // +0
    float time = 0.0f;                // +8: seconds within the current state
    int itemCount = 0;                // +0x14: controlled items (the toolbox item count)
    TutorialHand hand;                // +0x18
    int dragSlot = -1;                // +0x2c
    int orientSlot = -1;              // +0x30
    int dragType = 0;                 // +0x34
    int orientType = 0;               // +0x38
    std::vector<TutorialControlledItem> items;   // +0x3c
    std::vector<TutorialStep> steps;  // +0x48
    bool running = false;             // +0x54
};

// What the script builders read from the game (the GameState slice they touch).
struct TutorialToolboxSlot {
    ItemType type = ItemType::None;
    int amount = 0;
    Vec2 world{0.0f, 0.0f};   // toolboxIdxToWorld(slot)
};

struct TutorialContext {
    int locationIndex = -1;                  // LocationInfo+0 (GameState+0x834)
    int levelIndex = -1;                     // GameState+0x23a4
    std::vector<TutorialToolboxSlot> slots;  // the strip, in order
    int itemCount = 0;                       // ToolboxUtils::GetItemCount
    bool hasPlayButton = false;              // level 0: the ButtonPlay centre in world metres
    Vec2 playButtonWorld{0.0f, 0.0f};
};

// tutorial_should_run: location 0 only; a non-empty toolbox, or level 0 with an empty one.
bool tutorialShouldRun(const TutorialContext& ctx);
// TutorialUtils::Start: Stop, then the level's script when tutorial_should_run (levels 0..6).
void tutorialStart(TutorialState& state, const TutorialContext& ctx);
// TutorialUtils::Update: one hand state per frame, the dragged / oriented items, the time.
void tutorialUpdate(float dt, TutorialState& state);
// TutorialUtils::Stop: the run flag off, the step reset, the controlled items dropped.
void tutorialStop(TutorialState& state);

// The script builders (exposed for the tests).
std::vector<TutorialStep> clickPositionTutorial(Vec2 from, Vec2 to);
struct TutorialTargets {
    std::vector<Vec2> shelf;   // key 1, popped from the back
    std::vector<Vec2> book;    // key 0xf
};
std::vector<TutorialStep> fetchAllItemsTutorial(Vec2 start, const TutorialTargets& targets, const TutorialContext& ctx);
std::vector<TutorialStep> fetchItemRotateTutorial(Vec2 start, Vec2 target, float angle, const TutorialContext& ctx);

}  // namespace aa::sim
