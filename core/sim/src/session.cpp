// GameScreenController's set-up mode (docs/05 §5, docs/05 §7): doFrame, processActions and the ItemActions*
// handlers, endManipulationForActiveItem / releaseHeldItems, the ghost / flipping / manipulation animation
// updates, GhostManipulationUtils, the undo queue, prepareForNewLevel / restoreGameState / toggleSimulation /
// restartLevel, the toolbox transitions and CameraUtils::Update. Every constant and order is the original's
// [verified: decompile + disassembly, notes in docs/05 §5].
#include "aa/sim/session.h"

#include "aa/sim/attachments.h"
#include "aa/sim/coords.h"
#include "aa/sim/filters.h"
#include "aa/sim/goals.h"
#include "aa/sim/interaction.h"
#include "aa/sim/items/items.h"
#include "aa/sim/math_utils.h"
#include "touch_handler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aa::sim {

namespace {

// The per-type item state LevelLayoutUtils::Apply copies from the layout item (docs/02 §3.3–§3.4).
void applyItemState(const Level& level, const LevelItem& li, GameItem& item) {
    switch (item.type) {
    case ItemType::WorldBound:
        item.stateWord = level.backgroundIndex == kTreehouseBackground ? 1 : 0;
        break;
    case ItemType::Rope:
    case ItemType::Slingshot:
    case ItemType::ZipLine:
        // Apply cases 9 / 0x22 / 0x2a copy the layout's ropeEnd as it is: it is stored relative to the
        // centre (docs/02 §3.4).
        item.endVector = li.ropeEnd;
        break;
    case ItemType::Book:
    case ItemType::Billboard:
    case ItemType::RCTruck:
    case ItemType::RCController:
    case ItemType::Trapdoor:
    case ItemType::TrapdoorLever:
    case ItemType::Helicopter:
        item.stateWord = li.itemData;
        break;
    default:
        break;
    }
}

// floatMod(m, x) = x − m·floorf(x / m), as the binary evaluates it (unfused).
float floatMod(float m, float x) {
    const float f = std::floor(x / m);
    return x - m * f;
}

constexpr float kTwoPi = 6.2831855f;
constexpr float kDragGizmoSpeed = 0.785398006439209f;   // DAT_0028df4c = Pi / 4
constexpr float kIdleGizmoSpeed = 0.3f;
constexpr float kToolboxEjectSpeed = 1000.0f;
constexpr float kToolboxScrollSpring = 100.0f;
constexpr float kToolboxButtonSpeed = 3.0f;
constexpr float kToolboxButtonPressedScale = 1.2f;
constexpr float kToolboxDisplayFraction = 0.75f;
constexpr float kCameraEdgePx = 100.0f;
constexpr float kCameraEdgeDelay = 0.4f;
constexpr float kCameraEdgeSpeed = 300.0f;
constexpr float kCameraCornerX = 100.0f;   // DAT_0028302c
constexpr float kCameraCornerY = 60.0f;    // DAT_00283030
constexpr float kEpsilon = 0.0001f;        // st::Epsilon
constexpr float kBuzzAmplitudeIn = 0.1f;
constexpr float kBuzzAmplitudeOut = 1.0f;
constexpr float kFlipHalfTime = 0.075f;
constexpr float kFlipTime = 0.15f;
constexpr float kCompletionCampaign = 2.05f;         // 0x40033333
constexpr float kCompletionTestPlay = 1.5f;          // 0x3fc00000
constexpr float kCompletionFriendSolution = 3.35f;   // 0x40566666
constexpr float kIdleStopSeconds = 5.0f;             // doFrame: no moving body for 5 s → toggleSimulation
constexpr float kMarkerReshowSeconds = 5.0f;   // +0xc9984 threshold (doFrame's set-up tail)
constexpr float kTimeScale = 1.0f;                   // GameState+4, written by the constructor only [verified]

const CurvePoint kAddingCurve[4] = {{0.0f, 0.6f}, {0.6f, 1.15f}, {0.8f, 0.9f}, {1.0f, 1.0f}};   // DAT_0028ee68

// The default UIElements sizes of the 2048X1536 profile (ToolboxUtils::InitializeButtonSizesFromTextures
// reads them from the atlas; aa_data supplies the real table).
ToolboxFrameSizes defaultToolboxSizes() {
    ToolboxFrameSizes s;
    for (std::size_t i = 0; i < s.iconWidth.size(); ++i) {
        s.iconWidth[i] = 110.0f;
        s.iconHeight[i] = 110.0f;
    }
    return s;
}

}  // namespace

void applyLayout(const Level& level, const TemplateTable& templates, WorldState& state) {
    bool hasBound = false;
    for (const LevelItem& li : level.items) hasBound = hasBound || li.type == ItemType::WorldBound;
    // Attachment records name other items by layout index; the auto-added bound shifts them by one.
    const int offset = hasBound ? 0 : 1;
    if (!hasBound) {
        // Every shipped level carries its world bound; a layout without one gets it first (body 0) under a
        // slot no level item uses, so the level's own handles register unchanged afterwards.
        int slot = 0;
        for (bool taken = true; taken; ++slot) {
            taken = false;
            for (const LevelItem& li : level.items) taken = taken || Handle::slotOf(li.handle) == slot;
        }
        // Generation 1, like HandleManager's own first Add: a generation-0 handle would let `lookup(0)` —
        // an item's "no pair" state word — hit the bound instead of reporting no item.
        const int handle = Handle::make(ItemType::WorldBound, 1, slot - 1);
        const int i = state.addItemWithHandle(templates, handle, Vec2(0.0f, 0.0f), 0.0f);
        state.items[static_cast<std::size_t>(i)].stateWord = level.backgroundIndex == kTreehouseBackground ? 1 : 0;
    }
    for (const LevelItem& li : level.items) {
        // LevelLayoutUtils::Apply rejects layouts whose handles carry no valid type (docs/02 §3.1).
        if (!isValidItemType(static_cast<int>(Handle::typeOf(li.handle)))) {
            throw std::invalid_argument("applyLayout: item handle without a valid type");
        }
        // A zip line normalises its end vector (anchor B, the prismatic axis): a zero one (a missing or
        // hand-edited `ropeEnd`) would seed NaN into the world.
        if (Handle::typeOf(li.handle) == ItemType::ZipLine && length(li.ropeEnd) <= 0.0f) {
            throw std::invalid_argument("applyLayout: zip line with a zero-length rope end");
        }
        const int i = state.addItemWithHandle(templates, li.handle, li.center, li.angle);
        GameItem& item = state.items[static_cast<std::size_t>(i)];
        PhysicsObject& obj = state.objects[static_cast<std::size_t>(item.objectIndex)];
        if (li.fixed()) obj.flags = static_cast<std::uint8_t>(obj.flags | object_flags::kFixed);
        if (li.flipped()) obj.scale.x = -1.0f;
        applyItemState(level, li, item);
        // Apply copies the object's attachment count of records from the layout item, whatever their state.
        for (int k = 0; k < obj.attachmentCount; ++k) {
            const LevelAttachment& la = li.attachments[static_cast<std::size_t>(k)];
            AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
            rec.state = la.state;
            rec.otherObject = la.objectIndex >= 0 ? la.objectIndex + offset : -1;
            rec.otherPoint = la.index;
            rec.joint = -1;
        }
    }
    // Attached records must point at an existing object's attachment point (hand-edited / sandbox layouts).
    // Snapped ones are taken as they come: shipped levels carry stale snapped records (02_Room/TruckTrap
    // names object 16 of 16) that the original keeps, and the set-up dumps compare them.
    // TODO: bounds-check snapped records at their consumers (unsnap, removeObject, the ghost) for
    // hand-edited layouts; a record with an unknown state is rejected here.
    const int objectCount = static_cast<int>(state.objects.size());
    for (const PhysicsObject& obj : state.objects) {
        for (int k = 0; k < obj.attachmentCount; ++k) {
            const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
            if (rec.state != attachment_state::kFree && rec.state != attachment_state::kSnapped && rec.state != attachment_state::kAttached) {
                throw std::invalid_argument("applyLayout: attachment record with an unknown state");
            }
            if (rec.state != attachment_state::kAttached) continue;
            if (rec.otherObject < 0 || rec.otherObject >= objectCount ||
                rec.otherPoint < 0 || rec.otherPoint >= state.objects[static_cast<std::size_t>(rec.otherObject)].attachmentCount) {
                throw std::invalid_argument("applyLayout: attachment record points outside the layout");
            }
        }
    }
}

void createWorldPhysics(WorldState& state, PhysicsWorld& world, PhysicsMode mode) {
    for (std::size_t i = 0; i < state.objects.size(); ++i) {
        PhysicsObject& obj = state.objects[i];
        GameItem& item = state.itemOf(obj);
        obj.bodyCount = 0;
        obj.jointCount = 0;
        for (AttachmentRecord& rec : obj.attachments) rec.joint = -1;
        createPhysics(obj, item, world, mode);
        // BoxingGloveUtils::CreatePhysics ends with UpdateArmGeometry, which stores the lattice angle
        // (BoxingGlove+0x24) the renderer draws.
        if (obj.type == ItemType::BoxingGlove && obj.bodyCount >= 2) {
            item.latticeAngle = items::gloveLatticeAngle(world.body(obj.bodies[0])->GetPosition(), world.body(obj.bodies[1])->GetPosition());
        }
    }
    for (GameItem& item : state.items) item.ropeJoint = -1;
    createAttachments(state, world);
}

Level levelFromState(const WorldState& state, const Level& header, const Toolbox& toolbox) {
    Level layout;
    layout.version = 7;
    layout.title = header.title;
    layout.description = header.description;
    layout.authorName = header.authorName;
    layout.backgroundIndex = header.backgroundIndex;
    layout.goal = header.goal;
    layout.rewardId = header.rewardId;
    layout.tested = header.tested;
    for (const PhysicsObject& obj : state.objects) {
        const GameItem& item = state.itemOf(obj);
        LevelItem li;
        li.type = obj.type;
        li.handle = obj.handle;
        li.center = obj.position;
        li.angle = obj.angle;
        li.attachmentCount = obj.attachmentCount;
        if (obj.isFixed()) li.flags |= level_flags::kFixed;
        if (obj.scale.x < 0.0f) li.flags |= level_flags::kFlipped;
        for (int k = 0; k < obj.attachmentCount && k < LevelItem::kMaxAttachments; ++k) {
            const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
            LevelAttachment& la = li.attachments[static_cast<std::size_t>(k)];
            la.state = rec.state;
            la.objectIndex = rec.otherObject;
            la.index = rec.otherPoint;
        }
        switch (obj.type) {
        case ItemType::Rope:
        case ItemType::Slingshot:
        case ItemType::ZipLine:
            li.ropeEnd = item.endVector;
            break;
        case ItemType::Book:
        case ItemType::Billboard:
        case ItemType::RCTruck:
        case ItemType::RCController:
        case ItemType::Trapdoor:
        case ItemType::TrapdoorLever:
        case ItemType::Helicopter:
            li.itemData = item.stateWord;
            break;
        default:
            break;
        }
        layout.items.push_back(li);
    }
    for (int i = 0; i < toolbox.slotCount; ++i) {
        const ToolboxStripSlot& s = toolbox.slots[static_cast<std::size_t>(i)];
        layout.toolbox.push_back({s.type, s.amount});
    }
    return layout;
}

void UndoQueue::reset() {
    top = -1;
    count = -1;
}

void UndoQueue::add(const Level& layout) {
    top = count;
    if (top == kMaxCount) {
        for (int i = 0; i + 1 < kCapacity; ++i) layouts[static_cast<std::size_t>(i)] = layouts[static_cast<std::size_t>(i + 1)];
        count = count - 1;
        top = top - 1;
    }
    top = top + 1;
    layouts[static_cast<std::size_t>(top)] = layout;
    count = top;
}

Session::Session(const TemplateTable& templates) : templates_(templates), toolboxSizes_(defaultToolboxSizes()) {
    unlocked_.fill(true);
    toolbox_.sizes = toolboxSizes_.scaled(toolboxScale());
    editorToolbox_.sizes = toolbox_.sizes;
    // The original's GameScreenController outlives the levels: RetractToolbox has left the strip x off screen
    // long before the first level's DisplayToolbox starts (a fresh GameScreenTransitions holds 0). The strip
    // therefore always glides in from the right.
    toolbox_.x = toolboxOffscreenX();
    editorToolbox_.x = toolbox_.x;
    transitions_.toolboxX.value = toolbox_.x;
}

// --- level set-up --------------------------------------------------------------------------------

void Session::load(const Level& level, GameMode mode, SceneRecorder* recorder) {
    // The new state and world are built aside and committed together: a layout that applyLayout or
    // createWorldPhysics rejects (a hand-edited sandbox file) leaves the previous level intact instead of a
    // null world next to a half-filled state — the caller reports the error and keeps advancing.
    WorldState state;
    applyLayout(level, templates_, state);
    auto world = std::make_unique<PhysicsWorld>(recorder);
    world->installContactListener(PhysicsMode::SetUp);
    createWorldPhysics(state, *world, PhysicsMode::SetUp);
    level_ = level;
    gameMode_ = mode;
    physicsMode_ = PhysicsMode::SetUp;
    state_ = std::move(state);
    world_ = std::move(world);
    applyToolboxFromLayout(level_);
    visual_ = VisualState{};
    visual_.setGoalMarkers(level_.goal, state_);
    camera_ = Camera{};
    paused_ = true;
    controllerState_ = 2;
    goalState_ = GoalState{};
    playTime_ = 0.0f;
    accumulator_ = 0.0f;
    substep_ = 0;
    renderPoses_.clear();
    // The sandbox bookkeeping of the previous level (the original's controller keeps it; nothing reads it
    // before the next 1 → 5).
    removedHandles_.clear();
    handlesAtReady_.clear();
    undoBackup_.reset();
    previousBackground_ = -1;
    transitions_.background = CubicInterpolator{};
    prepareForNewLevel();
}

void Session::prepareForNewLevel() {
    // GameScreenController::prepareForNewLevel [verified: disassembly 0xb6e34].
    snapping_ = true;
    tutorialStarted_ = false;   // playNewLevel: +0x29b0 = 0 (TutorialUtils::Start runs once per level)
    tutorialStop(tutorial_);    // remake safety: a script still running from the previous level ends here
    if (gameMode_ == GameMode::Sandbox) {
        // The editor branch: the controller's own strip lists every unlocked item and becomes the active
        // toolbox (+0xc25b8), its eject length reset; nothing is fixed here.
        buildEditorToolbox();
        editorActive_ = true;
        editorToolbox_.ejectLength = 0.0f;
    } else {
        // WorldStateUtils::MarkAllObjectsFixed: every level item is fixed in the campaign; toolbox items
        // are the only movable ones (a saved solution's items are un-fixed by
        // MarkAllSolutionItemsFromToolboxNotFixed — M5). The level's strip is the active toolbox.
        for (PhysicsObject& obj : state_.objects) obj.flags = static_cast<std::uint8_t>(obj.flags | object_flags::kFixed);
        editorActive_ = false;
    }
    // GameScreenTransitionsUtils::Reset leaves the strip transitions alone; a still-running glide carries
    // its current x into the new level's toolbox [verified: prepareForNewLevel tests `+0xc1c98` (active)].
    if (transitions_.toolboxX.active) tb().x = transitions_.toolboxX.value;
    queue_.clear();
    undo_.reset();
    baseLayout_ = levelFromState(state_, level_, toolbox_);
    createSelectionAreaObject();
    resetTouchState();
    touches_ = Touches{};
    buzz_ = BuzzState{};
    edited_ = false;
    swallowedPointer_ = -1;
    saveUndoState();
    totalTime_ = 0.0f;
    gizmoPhase_ = 0.0f;
    // startLevelWithGoals → setSimulationToSetUpTransitionState → DisplayToolbox.
    displayToolbox();
}

void Session::buildEditorToolbox() {
    // ToolboxUtils::SetFull(editor, progress, 0x20, max(3 − stars, 0)) [verified: 0xe1040]: a GoalStar slot
    // first when stars are left, then the fixed type order, one slot of 0x20 per unlocked type; then
    // prepareForNewLevel's mode-1 loop [verified: 0xb7128..0xb71c8]: every slot's amount becomes the cap
    // (3 for the star, 0x20 otherwise) minus the count already in the level, the slot removed at 0.
    static constexpr int kFullAmount = 0x20;
    static constexpr int kStarCap = 3;
    static constexpr ItemType kOrder[] = {
        ItemType::Shelf,      ItemType::TennisBall, ItemType::SoccerBall, ItemType::Book,          ItemType::CardboardBoxMedium,
        ItemType::CardboardBoxSmall, ItemType::LaundryBasket, ItemType::Balloon, ItemType::Scissors, ItemType::EightBall,
        ItemType::Pipe,       ItemType::Pipe90,     ItemType::BoxingGlove, ItemType::Hook,         ItemType::Rope,
        ItemType::Bucket,     ItemType::BowlingBall, ItemType::Seesaw,    ItemType::Slingshot,    ItemType::Dart,
        ItemType::Spring,     ItemType::Skateboard, ItemType::Pinball,    ItemType::Bumper,       ItemType::Doll,
        ItemType::Trapdoor,   ItemType::PiggyBank,  ItemType::RCTruck,    ItemType::HangingLamp,  ItemType::PaperPlane,
        ItemType::Magnet,     ItemType::BouncyBall, ItemType::Helicopter, ItemType::ZipLine};
    Toolbox& t = editorToolbox_;
    t.slotCount = 0;
    const int starsLeft = kStarCap - state_.typeCount(ItemType::GoalStar);
    if (starsLeft > 0) t.appendSlot(ItemType::GoalStar, starsLeft);
    for (ItemType type : kOrder) {
        if (unlocked_[static_cast<std::size_t>(type)]) t.appendSlot(type, kFullAmount);
    }
    for (int type = 1; type < kItemTypeCount; ++type) {
        const int slot = t.getSlotIndexForType(static_cast<ItemType>(type));
        if (slot < 0) continue;
        const int cap = type == static_cast<int>(ItemType::GoalStar) ? kStarCap : kFullAmount;
        const int left = cap - state_.typeCount(static_cast<ItemType>(type));
        if (left <= 0) t.removeSlot(slot);
        else t.slots[static_cast<std::size_t>(slot)].amount = left;
    }
}

void Session::createSelectionAreaObject() {
    for (const PhysicsObject& obj : state_.objects) {
        if (obj.type == ItemType::SelectionArea) return;
    }
    const int itemIndex = state_.addNewItem(templates_, ItemType::SelectionArea, Vec2(0.0f, 0.0f), 0.0f, false);
    const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
    createPhysics(state_.objects[static_cast<std::size_t>(object)], state_.items[static_cast<std::size_t>(itemIndex)], *world_,
                  PhysicsMode::SetUp);
}

void Session::applyToolboxFromLayout(const Level& layout) {
    // LevelLayoutUtils::Apply's tail: the slots rebuilt from the layout list, the eject length reset.
    toolbox_.slotCount = 0;
    for (const auto& slot : layout.toolbox) toolbox_.appendSlot(slot.type, slot.amount);
    toolbox_.ejectLength = 0.0f;
}

void Session::rebuildWorld(PhysicsMode mode, SceneRecorder* recorder) {
    world_.reset();
    physicsMode_ = mode;
    world_ = std::make_unique<PhysicsWorld>(recorder);
    simContext_ = SimulationContext{&state_, world_.get(), &queue_, &level_.goal, &goalState_, timeSeed_};
    world_->installContactListener(mode, &simContext_);
    createWorldPhysics(state_, *world_, physicsMode_);
    controllerState_ = mode == PhysicsMode::SetUp ? 2 : 4;
}

// Every interaction record that indexes the object collection or drives an item by handle: the touch state,
// the ghost copy and its glide, the selection / flip animations. The original keeps them across
// restoreGameState (they live outside the WorldState that PartialReset clears) and reads the stale indices
// from its fixed 126-slot arrays — a zeroed slot with no gizmo bit; the remake's vectors have no such slot, so
// the state is reset together with the world (a remake deviation, docs/10 §11).
void Session::resetTouchState() {
    touch_ = TouchState{};
    touches_.events.clear();
    ghost_ = GhostState{};
    ghostAnim_ = GhostAnimation{};
    manipAnim_.reset();
    flipAnim_ = FlippingAnimation{};
    activeHandle_ = -1;
    adding_ = false;
    removing_ = false;
    lastActionId_ = -1;
    inputEnabled_ = true;
}

void Session::restoreGameState(const Level& layout, PhysicsMode mode, SceneRecorder* recorder) {
    // GameScreenController::restoreGameState: DestroyWorld, PartialReset, Apply, CreateWorld,
    // CreateDynamicPhysics, CreateAttachments, DisplayToolbox, goal markers.
    resetTouchState();
    playTime_ = 0.0f;          // GameState+0 = 0
    stopLoopingSounds();       // SoundRenderer::StopLoopingSounds before DestroyWorld
    goalState_ = GoalState{};  // LevelLayoutUtils::Apply constructs a fresh GoalState
    completing_ = false;       // setSetUpToSimulationTransitionState / setSimulationState clear +0xc998c
    completionTimer_ = 0.0f;
    renderPoses_.clear();
    world_.reset();
    state_ = WorldState{};
    applyLayout(layout, templates_, state_);
    applyToolboxFromLayout(layout);
    physicsMode_ = mode;
    world_ = std::make_unique<PhysicsWorld>(recorder);   // the recorder only covers this construction
    simContext_ = SimulationContext{&state_, world_.get(), &queue_, &level_.goal, &goalState_, timeSeed_};
    world_->installContactListener(mode, &simContext_);
    createWorldPhysics(state_, *world_, mode);
    displayToolbox();
    visual_ = VisualState{};
    visual_.setGoalMarkers(level_.goal, state_);
    controllerState_ = mode == PhysicsMode::SetUp ? 2 : 4;
}

void Session::saveUndoState() { undo_.add(levelFromState(state_, level_, toolbox_)); }

// --- the sandbox editor (docs/05 §1) ------------------------------------------------------------------

void Session::markAllObjectsFixed() {
    for (PhysicsObject& obj : state_.objects) obj.flags = static_cast<std::uint8_t>(obj.flags | object_flags::kFixed);
}

void Session::markAllObjectsNotFixed() {
    for (PhysicsObject& obj : state_.objects) obj.flags = static_cast<std::uint8_t>(obj.flags & ~object_flags::kFixed);
}

void Session::markAllStarsFixed() {
    for (PhysicsObject& obj : state_.objects) {
        if (obj.type == ItemType::GoalStar) obj.flags = static_cast<std::uint8_t>(obj.flags | object_flags::kFixed);
    }
}

void Session::setMode(GameMode mode) {
    // GameScreenController::setMode [verified: disassembly 0xb9e70].
    const GameMode from = gameMode_;
    if (from == GameMode::Sandbox) {
        if (mode == GameMode::TestPlay) {
            editorActive_ = true;
            testPlayLayout_ = levelFromState(state_, level_, toolbox_);
            markAllObjectsFixed();
        } else if (mode == GameMode::SandboxToolbox) {
            undoBackup_ = undo_;
            undo_.reset();
            removedHandles_.clear();
            readyLayout_ = levelFromState(state_, level_, toolbox_);
            handlesAtReady_.clear();
            for (const PhysicsObject& obj : state_.objects) handlesAtReady_.push_back(obj.handle);
            saveUndoState();
            // The level's strip takes over at the editor strip's position (+4 / +8 copied), emptied.
            toolbox_.y = editorToolbox_.y;
            toolbox_.x = editorToolbox_.x;
            editorActive_ = false;
            toolbox_.removeAllSlots();
            markAllStarsFixed();
        }
    } else if (from == GameMode::SandboxToolbox) {
        // undoLastMove until isActionEnabled(0) fails: the layout at the start of the step comes back.
        while (controllerState_ == 2 && undo_.count > 0) {
            undo_.count = undo_.count - 1;
            restoreGameState(undo_.layouts[static_cast<std::size_t>(undo_.count)], PhysicsMode::SetUp);
        }
        undo_.reset();
        undoBackup_.reset();
        markAllObjectsNotFixed();
        editorActive_ = true;
    } else if (from == GameMode::TestPlay) {
        editorActive_ = true;
        if (mode == GameMode::Sandbox) markAllObjectsNotFixed();
    }
    gameMode_ = mode;
}

void Session::setBackground(int index) {
    // SandboxView::ButtonPressed(ButtonBackground) + GameScreenController::backgroundChanged(1) [verified]:
    // the previous background keeps drawing while the new one slides in from 1024 virtual px (0.5 s), and
    // the level is no longer "tested".
    previousBackground_ = level_.backgroundIndex;
    level_.backgroundIndex = index;
    transitions_.background.start(1024.0f, 0.0f, 0.5f);
    level_.tested = false;
    // A remake deviation (docs/10 §11 item 14 (k)): the original leaves the world bound as it is until the
    // next restoreGameState rebuilds it from the layout (whose background index LevelLayoutUtils::Get copies
    // from GameState+0x23a8 [verified: 0xca3f4]) — the Treehouse floor hole appears or goes only at the next
    // test play. Here the bound's physics is rebuilt at once, as WorldBoundUtils::CreatePhysics would.
    for (GameItem& item : state_.items) {
        if (item.type != ItemType::WorldBound || item.objectIndex < 0) continue;
        item.stateWord = index == kTreehouseBackground ? 1 : 0;
        if (!world_) continue;
        PhysicsObject& obj = state_.objects[static_cast<std::size_t>(item.objectIndex)];
        world_->destroyPhysics(obj);
        createPhysics(obj, item, *world_, physicsMode_);
    }
}

int Session::physIndexFromHandle(int handle) const {
    for (std::size_t i = 0; i < handlesAtReady_.size(); ++i) {
        if (handlesAtReady_[i] == handle) return static_cast<int>(i);
    }
    return -1;
}

int Session::physicsIndexSBOriginalToCurrent(int original) const {
    // The original index minus the removed items before it; −1 for a removed one [verified: 0xb8058].
    int current = 0;
    for (std::size_t i = 0; i < handlesAtReady_.size(); ++i) {
        bool removed = false;
        for (int h : removedHandles_) removed = removed || h == handlesAtReady_[i];
        if (static_cast<int>(i) == original) return removed ? -1 : current;
        if (!removed) ++current;
    }
    return -1;
}

int Session::physicsIndexSBCurrentToOriginal(int current) const {
    int cur = 0;
    for (std::size_t i = 0; i < handlesAtReady_.size(); ++i) {
        bool removed = false;
        for (int h : removedHandles_) removed = removed || h == handlesAtReady_[i];
        if (removed) continue;
        if (cur == current) return static_cast<int>(i);
        ++cur;
    }
    return -1;
}

void Session::updateSandboxToolboxLayout(int handle) {
    // GameScreenController::UpdateSandboxToolboxLayout [verified: 0xb8144]: the ready layout without the
    // removed items (LevelLayoutUtils::StripItemHandle), every attachment record that pointed at a removed
    // item cleared (CleanAttachments), the surviving attachment indices remapped, the world rebuilt from it
    // with the strip preserved across restoreGameState (LevelLayoutUtils::Apply would rebuild the slots).
    (void)handle;   // the item being re-added: its own records are cleaned by the loop below like any other
    Level out = readyLayout_;
    for (int removed : removedHandles_) {
        const int physIdx = physIndexFromHandle(removed);
        Level next;
        next.title = out.title;
        next.description = out.description;
        next.authorName = out.authorName;
        next.backgroundIndex = out.backgroundIndex;
        next.toolbox = out.toolbox;
        next.goal = out.goal;
        next.rewardId = out.rewardId;
        next.tested = out.tested;
        next.version = out.version;
        for (const LevelItem& li : out.items) {
            if (li.handle != removed) next.items.push_back(li);
        }
        for (LevelItem& li : next.items) {
            for (int a = 0; a < li.attachmentCount; ++a) {
                LevelAttachment& rec = li.attachments[static_cast<std::size_t>(a)];
                if (rec.objectIndex == physIdx) {
                    rec.state = attachment_state::kFree;
                    rec.objectIndex = -1;
                    rec.index = -1;
                }
            }
        }
        out = next;
    }
    for (LevelItem& li : out.items) {
        for (int a = 0; a < li.attachmentCount; ++a) {
            LevelAttachment& rec = li.attachments[static_cast<std::size_t>(a)];
            rec.objectIndex = physicsIndexSBOriginalToCurrent(rec.objectIndex);
        }
    }
    const Toolbox saved = toolbox_;
    restoreGameState(out, PhysicsMode::SetUp);
    toolbox_ = saved;
    // A remake deviation (docs/10 §11 item 14 (o)): the original takes the ready layout before
    // MarkAllStarsFixed (0xba014 < 0xba114) and never re-fixes after this rebuild, so from the first strip
    // move on its stars are movable in the step; the port keeps them fixed, as 1 → 5 left them.
    markAllStarsFixed();
}

bool Session::canUndo() const { return controllerState_ == 2 && undo_.count > 0; }
bool Session::canRedo() const { return controllerState_ == 2 && undo_.count < undo_.top; }

void Session::undo() {
    // isActionEnabled(0) only checks the counter; the touch guard is the remake's (the original's undo button
    // belongs to the UI layer, and a rebuilt world under a live touch state — even a pending tap — would index
    // stale objects). The UI buttons are disabled with the touches while the flip animation runs.
    if (!canUndo() || touch_.state != touch_state::kIdle || !inputEnabled_) return;
    undo_.count = undo_.count - 1;
    restoreGameState(undo_.layouts[static_cast<std::size_t>(undo_.count)], PhysicsMode::SetUp);
}

void Session::redo() {
    if (!canRedo() || touch_.state != touch_state::kIdle || !inputEnabled_) return;
    undo_.count = undo_.count + 1;
    restoreGameState(undo_.layouts[static_cast<std::size_t>(undo_.count)], PhysicsMode::SetUp);
}

void Session::toggleSimulation(SceneRecorder* recorder) {
    // The ghost guard is the remake's: LevelLayoutUtils::Get copies every object, so a snapshot taken while
    // the ghost glide still runs would carry the ghost copy into the simulation as a real item.
    if (isManipulationActive() || ghost_.inGhost) return;
    if (!paused_) {
        paused_ = true;
        restoreGameState(playLayout_, PhysicsMode::SetUp);
        controllerState_ = 2;   // setSimulationToSetUpTransitionState → (doFrame case 5) setSetUpState
    } else {
        paused_ = false;
        playLayout_ = levelFromState(state_, level_, toolbox_);
        accumulator_ = 0.0f;   // +0xc28f4
        restoreGameState(playLayout_, PhysicsMode::Simulation, recorder);
        prevState_ = state_;
        retractToolbox();
        activeHandle_ = -1;
        controllerState_ = 4;   // setSetUpToSimulationTransitionState → (case 3) setSimulationState
        // doFrame case 3 [verified]: the frame that completes the transition runs setSimulationState and
        // then one more TutorialUtils::Update (campaign / test-play, while running). Approximated here in
        // the toggling frame with its dt (the port takes case 3 at once); inert in practice — the play
        // button's ButtonPressed has already stopped the script.
        if ((gameMode_ == GameMode::Campaign || gameMode_ == GameMode::TestPlay) && tutorial_.running) tutorialUpdate(lastDt_, tutorial_);
        if (gameMode_ != GameMode::FriendSolution) tb().buttonState = 1;
    }
    idleTime_ = 0.0f;   // +0xc9980, both halves
}

void Session::play(SceneRecorder* recorder) {
    if (paused_ && inputEnabled_) toggleSimulation(recorder);
}

void Session::toggleToolbox() {
    if (tb().buttonState == 0) tb().buttonState = 1;
    else if (tb().buttonState == 1) tb().buttonState = 0;
}

void Session::pause() {
    // UI::GameScene::SetPaused(true) in the set-up state: a frame with dt 0, the gizmos off, the held item
    // released (releaseHeldItems: a fresh toolbox item or a ghost goes back; a plain drag is left to the
    // touch layer), one more zero frame so its actions run before the pause menu opens. The menu then takes
    // the screen and the fingers are cancelled (TouchUtils::QueueTouchesCancelled), which drops a plain
    // drag where it is.
    if (controllerState_ != 2) return;
    advance(0.0f);
    stopTutorial();
    releaseHeldItems();
    advance(0.0f);
    for (const TouchRecord& r : touches_.records) {
        if (r.id != -1) touches_.cancelled(r.id);
    }
    advance(0.0f);
}

void Session::stop() {
    if (!paused_ && inputEnabled_) toggleSimulation();
}

void Session::stopTutorial() {
    // The block at the head of GameView::ButtonPressed (every button, any state), in GameScene::SetPaused
    // (state 2) and GameView::ReturnFromSolutions [verified]: the gizmo object cleared (+0x5738c = −1, its
    // render-table entry hidden), then TutorialUtils::Stop.
    touch_.gizmoObject = -1;
    tutorialStop(tutorial_);
}

void Session::startLevelCompleteSequence() {
    // GameScreenController::startLevelCompleteSequence: the toolbar button and the stopwatch retract, the
    // goal markers hide, the confetti / stars effect starts at the goal marker (LevelCompletedEffectUtils —
    // approximate, docs/10 §1) and the countdown to setCompletedState starts: 2.05 s in the campaign
    // (1.5 s in test play, 3.35 s for a friend's solution) [verified]. The physics keeps running meanwhile.
    if (completing_) return;
    completing_ = true;
    if (gameMode_ == GameMode::TestPlay) completionTimer_ = kCompletionTestPlay;
    else if (gameMode_ == GameMode::FriendSolution) completionTimer_ = kCompletionFriendSolution;
    else completionTimer_ = kCompletionCampaign;
    visual_.hideGoalMarkers();
    // LevelCompletedEffectUtils::Start: confetti at the goal marker (approximate). The camera does not
    // zoom (ZoomCameraOut belongs to the editor / share paths, not to the completion) [verified].
    Vec2 center(kWorldWidth * 0.5f, kWorldHeight * 0.5f);
    if (level_.goal.type >= 2) center = Vec2(level_.goal.width, level_.goal.height);
    levelCompletePos_ = center;   // +0xc9990: getLevelCompletePos (the result popup's start)
    visual_.startConfetti(center);
    events_.push_back({SessionEvent::Kind::GoalComplete, goalState_.collectedStars, Vec2(0.0f, 0.0f), 1.0f});
}

void Session::setCompletedState() {
    completing_ = false;
    stopLoopingSounds();
    if (gameMode_ == GameMode::TestPlay) {
        // The test-play branch [verified: setCompletedState 0xba3cc, setEditorState 0xba204]: the layout
        // from before the run comes back, the level counts as tested, everything is fixed (the solution
        // file the original writes here has no offline reader), then setEditorState: mode 1, the stop
        // half of toggleSimulation, nothing fixed. State 6 is never entered.
        restoreGameState(testPlayLayout_, PhysicsMode::SetUp);
        level_.tested = true;
        markAllObjectsFixed();
        setMode(GameMode::Sandbox);
        toggleSimulation();
        markAllObjectsNotFixed();
        return;
    }
    // GameScreenController::setCompletedState (campaign branch): controller state 6 — the world freezes
    // in place (doFrame's default branch only drains the queue), the looping sounds stop. MarkLevelAsDone,
    // the star tally and the saves are the platform's (GameScene on SessionEvent::LevelCompleted).
    controllerState_ = 6;
    events_.push_back({SessionEvent::Kind::LevelCompleted, goalState_.collectedStars, Vec2(0.0f, 0.0f), 1.0f});
}

void Session::restart() {
    // GameScreenController::restartLevel(true): the campaign restores the base layout; modes 1 / 5 empty the
    // level instead [verified: 0xb9884..0xb9a1c] — restoreGameState of a default-constructed LevelLayout
    // (Apply copies its header: the default title id, no author / description, background 0), the level's
    // strip replaced by a fresh Toolbox, a WorldBound item of state 0 (the plain floor) added and given its
    // physics, the manipulation animation reset, then startLevelWithGoals(false) → the set-up state. The
    // editor strip is not rebuilt (its amounts stay as they were). No editor button reaches this branch.
    if (!inputEnabled_) return;
    if (gameMode_ == GameMode::Sandbox || gameMode_ == GameMode::SandboxToolbox) {
        Level empty;
        empty.title = kDefaultSandboxTitleId;
        empty.backgroundIndex = 0;
        level_.title = empty.title;
        level_.description.clear();
        level_.authorName.clear();
        level_.backgroundIndex = 0;
        level_.tested = false;
        previousBackground_ = -1;
        transitions_.background = CubicInterpolator{};
        restoreGameState(empty, PhysicsMode::SetUp);   // applyLayout adds the world bound (state 0) itself
        toolbox_ = Toolbox{};   // st::Toolbox::Toolbox: x / y = 0 too (memcpy of a fresh one, 0xb98e4)
        toolbox_.sizes = toolboxSizes_.scaled(toolboxScale());   // the remake's runtime constants, not part of the memcpy
    } else {
        restoreGameState(baseLayout_, PhysicsMode::SetUp);
    }
    paused_ = true;
    queue_.clear();
    undo_.count = 0;
    controllerState_ = 2;
    touch_ = TouchState{};
    activeHandle_ = -1;
    adding_ = false;
    removing_ = false;
    ghost_ = GhostState{};
    ghostAnim_ = GhostAnimation{};
}

// --- camera / viewport ---------------------------------------------------------------------------

void Session::setCameraCenter(Vec2 centerPx) { camera_.centerPx = clampedCameraCenter(camera_, centerPx); }

void Session::setCameraZoom(float zoom) {
    camera_.zoom = zoom;
    camera_.centerPx = clampedCameraCenter(camera_, camera_.centerPx);
}

Vec2 clampedCameraCenter(const Camera& camera, Vec2 centerPx) {
    const float halfW = Camera::kDefaultCenterX / camera.zoom;
    const float halfH = Camera::kDefaultCenterY / camera.zoom;
    Vec2 out = centerPx;
    if ((1024.0f - halfW) - centerPx.x < 0.0f) out.x = 1024.0f - halfW;
    if (centerPx.x - halfW < 0.0f) out.x = halfW;
    if ((638.0f - halfH) - centerPx.y < 0.0f) out.y = 638.0f - halfH;
    if (centerPx.y - halfH < 0.0f) out.y = halfH;
    return out;
}

float Session::toolboxScale() const { return layout_.uiScale(); }

void Session::setViewport(const ScreenLayout& layout) {
    const bool changed = layout.width != layout_.width || layout.height != layout_.height;
    if (!changed) {
        layout_ = layout;
        return;
    }
    // A resize (the original ships one layout per device): the strip keeps its side — off screen stays off,
    // on screen stays on — and a running glide restarts towards the re-derived target.
    const bool wasOff = !(transitions_.toolboxX.value < toolboxOffscreenX());
    const bool glidingIn = transitions_.toolboxX.active && transitions_.toolboxX.to < transitions_.toolboxX.from;
    layout_ = layout;
    toolbox_.sizes = toolboxSizes_.scaled(toolboxScale());
    editorToolbox_.sizes = toolbox_.sizes;
    for (int i = 0; i < tb().slotCount; ++i) {
        ToolboxStripSlot& s = tb().slots[static_cast<std::size_t>(i)];
        const ToolboxStripSlot fresh = tb().makeSlot(s.type, s.amount);
        s.widthPx = fresh.widthPx;
        s.heightPx = fresh.heightPx;
    }
    tb().y = toolboxY();
    if (transitions_.toolboxX.active) {
        CubicInterpolator& g = transitions_.toolboxX;
        const float span = g.to - g.from;
        const float fraction = span != 0.0f ? (g.value - g.from) / span : 1.0f;
        const float remaining = g.duration - g.t;
        const float target = glidingIn ? toolboxOnscreenX() : toolboxOffscreenX();
        const float from = glidingIn ? toolboxOffscreenX() : toolboxOnscreenX();
        g.start(from + (target - from) * fraction, target, remaining > 0.0f ? remaining : 0.0f);
        tb().x = g.value;
    } else {
        tb().x = wasOff ? toolboxOffscreenX() : toolboxOnscreenX();
        transitions_.toolboxX.value = tb().x;
    }
}

void Session::setToolboxFrameSizes(const ToolboxFrameSizes& sizes) {
    toolboxSizes_ = sizes;
    toolbox_.sizes = toolboxSizes_.scaled(toolboxScale());
    editorToolbox_.sizes = toolbox_.sizes;
}

Vec2 Session::screenToWorld(Vec2 nativePx) const { return aa::sim::screenToWorld(layout_, camera_, nativePx); }

Vec2 Session::worldToScreen(Vec2 world) const {
    return screenToPixelPos(layout_, camera_, worldPtToScreenPt(layout_, world));
}

float Session::toolboxY() const {
    return (layout_.floor - 39.0f) * 0.04f + 2.0f + tb().sizes.buttonHeight * 0.5f;
}

float Session::toolboxOnscreenX() const {
    const float w = static_cast<float>(layout_.width);
    return (w - layout_.letterBoxFrameWidth * layout_.pixelScale) - tb().sizes.buttonWidth * 0.7f;
}

float Session::toolboxOffscreenX() const {
    const float w = static_cast<float>(layout_.width);
    return (w - layout_.letterBoxFrameWidth * layout_.pixelScale) + tb().sizes.buttonWidth * 0.7f;
}

void Session::displayToolbox() {
    // GameScreenTransitionsUtils::DisplayToolbox: y from the floor, x glides on screen in 0.6 s.
    tb().y = toolboxY();
    transitions_.toolboxX.start(transitions_.toolboxX.value, toolboxOnscreenX(), 0.6f);
}

void Session::retractToolbox() {
    transitions_.toolboxEject.start(tb().ejectLength, 0.0f, 0.3f);
    tb().buttonState = 0;
    transitions_.toolboxX.start(transitions_.toolboxX.value, toolboxOffscreenX(), 0.3f);
}

// --- input ---------------------------------------------------------------------------------------

// UI::SceneManager::TouchesStarted/Moved/Finished [verified]: while user interaction is disabled (the flip
// animation) a Began is only remembered (+0x80) and that pointer's Moved / Ended are dropped until its Ended
// clears the memory; touches that began earlier keep reaching the handler (the flip tap's own release).
void Session::pointerDown(int id, Vec2 px) {
    if (!inputEnabled_) {
        swallowedPointer_ = id;
        return;
    }
    touches_.began(id, Vec2(px.x, static_cast<float>(layout_.height) - px.y), now_);
    // A desktop convenience: an item taken out by takeFromToolbox() hangs on no finger; the next pointer
    // becomes its finger (its moves drag the item relative to where it went down).
    if (touch_.state == touch_state::kDragging && touch_.primaryTouch < 0 && touch_.selectedObject >= 0) {
        for (int i = 0; i < Touches::kMaxTouches; ++i) {
            if (touches_.records[static_cast<std::size_t>(i)].id == id) touch_.primaryTouch = i;
        }
        touch_.startPos = touch_.targetPos;
        // The Began just queued must not re-run the pick (earlier moves of the frame stay queued).
        if (!touches_.events.empty() && touches_.events.back().kind == TouchEvent::kBegan) touches_.events.pop_back();
    }
}

void Session::pointerMove(int id, Vec2 px) {
    if (id == swallowedPointer_) return;
    touches_.moved(id, Vec2(px.x, static_cast<float>(layout_.height) - px.y), now_);
}

void Session::pointerUp(int id, Vec2 px) {
    if (id == swallowedPointer_) {
        swallowedPointer_ = -1;
        return;
    }
    touches_.ended(id, Vec2(px.x, static_cast<float>(layout_.height) - px.y), now_);
}

void Session::pointerCancel(int id) { touches_.cancelled(id); }

int Session::heldObject() const {
    switch (touch_.state) {
    case touch_state::kDragging:
    case touch_state::kRingRotate:
    case touch_state::kFromToolbox:
    case touch_state::kReturning:
        return touch_.selectedObject;
    default:
        return -1;
    }
}

bool Session::isManipulationActive() const {
    const int s = touch_.state;
    return !(s == touch_state::kIdle || s == touch_state::kPending || s == touch_state::kPan || s == touch_state::kPinchZoom);
}

void Session::rotateHeld(float deltaAngle) {
    const int s = touch_.state;
    if (s != touch_state::kDragging && s != touch_state::kRingRotate && s != touch_state::kFromToolbox) return;
    if (touch_.selectedObject < 0) return;
    touch_.angleStart = touch_.angleStart + deltaAngle;
    touch_.angleCurrent = touch_.angleCurrent + deltaAngle;
    queue_.add(Action(action::kRotateSelected, state_.objects[static_cast<std::size_t>(touch_.selectedObject)].handle));
}

void Session::flipHeld() {
    int object = heldObject();
    if (object < 0) object = touch_.gizmoObject;
    if (object < 0 || (touch_.state != touch_state::kIdle && touch_.state != touch_state::kDragging)) return;
    const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
    if ((obj.flags & object_flags::kFlippable) == 0) return;
    queue_.add(Action(action::kFlipSelected, obj.handle));
    if (touch_.state == touch_state::kIdle) touch_.state = touch_state::kFlipping;
}

void Session::takeFromToolbox(int slot) {
    if (touch_.state != touch_state::kIdle || slot < 0 || slot >= tb().slotCount) return;
    const ToolboxStripSlot& s = tb().slots[static_cast<std::size_t>(slot)];
    if (s.amount == 0) return;
    Action a8(action::kNewFromToolbox, 0);
    a8.payload = static_cast<int>(s.type);
    queue_.add(a8);
    const Vec2 center = tb().getCenterForSlot(slot);
    const Vec2 w = screenToWorld(Vec2(tb().x + center.x, tb().y + center.y));
    touch_.state = touch_state::kFromToolbox;
    touch_.primaryTouch = -1;
    touch_.startPos = w;
    touch_.targetPos = w;
    touch_.bodyIndex = 0;
    touch_.angleStart = 0.0f;
    touch_.angleCurrent = 0.0f;
}

void Session::returnHeld() {
    const int object = heldObject();
    if (object < 0) return;
    queue_.add(Action(action::kReturnToToolbox, state_.objects[static_cast<std::size_t>(object)].handle));
    touch_.gizmoObject = -1;
    touch_.state = touch_state::kReturning;
    touch_.slingshotObject = -1;
    touch_.primaryTouch = -1;
    touch_.secondaryTouch = -1;
    touch_.rotationTouch = -1;
}

void Session::scrollToolbox(float deltaPx) { queue_.add(Action::scroll(Vec2(deltaPx, 0.0f))); }

// --- doFrame -------------------------------------------------------------------------------------

void Session::advance(float wallDt) {
    // The actions this frame processes (drainActions) — doFrame may recurse into itself for the same
    // frame, so the list is cleared here, outside the recursion.
    processed_.clear();
    doFrame(wallDt);
}

void Session::doFrame(float wallDt) {
    // doFrame's first statement: +0xc99b8 (a simulation touch of the previous frame) → toggleSimulation
    // (test play returns to mode 1 first — M6).
    if (stopRequested_) {
        stopRequested_ = false;
        if (gameMode_ == GameMode::TestPlay) setMode(GameMode::Sandbox);
        toggleSimulation();
    }
    const float dt = wallDt;
    lastDt_ = dt;
    now_ += static_cast<double>(dt);
    totalTime_ = totalTime_ + dt;
    // GameScreenTransitionsUtils::Update(dt, transitions, touch state): the toolbox eject interpolator,
    // then x while it is idle; the buzz interpolator (GameScreenTransitions+0x48) with the rule that hides
    // the InvalidSelection cross (+0xa8 handle): once the rise is over it goes while the finger still holds
    // (state 9) or when the amplitude is below 1; an idle amplitude at 1 starts the 1 s decay; during a
    // decay the cross goes at 0.8 [verified: decompile + disassembly].
    transitions_.toolboxEject.update(dt);
    if (!transitions_.toolboxEject.active) transitions_.toolboxX.update(dt);
    buzz_.amplitude.update(dt);
    {
        CubicInterpolator& amp = buzz_.amplitude;
        bool clear = false;
        if (!amp.active) {
            clear = touch_.state == touch_state::kBuzz || amp.value < 1.0f;
            if (!clear) {
                amp.start(amp.value, 0.0f, kBuzzAmplitudeOut);
                clear = !amp.active;
            }
        }
        if (clear) buzz_.handle = 0;
        else if (amp.to <= 0.0f && amp.value <= 0.8f) buzz_.handle = 0;
    }
    // ToolboxAnimationUtils::Update (the slot pop-in) is a no-op: nothing in the binary calls
    // ToolboxAnimationUtils::Display, so the state at GameScreenController+0xc1d60 never activates [verified].
    // Toolbox button scale: towards 1.2 while pressed, 1 otherwise, 3 per second.
    {
        const float target = tb().buttonPressed ? kToolboxButtonPressedScale : 1.0f;
        const float diff = target - tb().buttonScale;
        const float dir = diff > 0.0f ? kToolboxButtonSpeed : (diff < 0.0f ? -kToolboxButtonSpeed : 0.0f);
        float s = tb().buttonScale + dt * dir;
        if (!(s < 1.0f) && s != 1.0f) {
            if (!(s < kToolboxButtonPressedScale)) s = kToolboxButtonPressedScale;
        } else {
            s = 1.0f;
        }
        tb().buttonScale = s;
    }
    // TutorialUtils::Update runs at the head of every doFrame while the script is running (campaign /
    // test-play modes) [verified]; the state-3 → 4 transition repeats it once (toggleSimulation).
    if ((gameMode_ == GameMode::Campaign || gameMode_ == GameMode::TestPlay) && tutorial_.running) tutorialUpdate(dt, tutorial_);
    if (controllerState_ == 4) {
        // --- case 4 (simulation) -----------------------------------------------------------------
        bool recurse = false;
        advanceSimulation(dt, recurse);
        if (recurse) {
            // setCompletedState changed the state: doFrame runs again for the same dt and this call's
            // tail is skipped (`goto LAB_000cb840`) [verified].
            doFrame(wallDt);
            return;
        }
        frameTail();
        return;
    }
    if (controllerState_ != 2) {
        // The default branch (state 6, completed; the transition states): the queue drained, the render
        // copy a plain copy, then the tail.
        processActions();
        frameTail();
        return;
    }
    // --- case 2 (set-up) -------------------------------------------------------------------------
    updateGhostAnimation(dt);
    updateFlippingAnimation(dt);
    updateManipulationAnimation(dt);
    if (touch_.holdTimer > 0.0f) touch_.holdTimer = touch_.holdTimer - dt;
    PhysicsObject selectedBefore;
    bool haveSelectedBefore = false;
    if (touch_.selectedObject != -1) {
        selectedBefore = state_.objects[static_cast<std::size_t>(touch_.selectedObject)];
        haveSelectedBefore = true;
    }
    const bool activeBefore = isManipulationActive();
    {
        TouchContext ctx{touches_, touch_, state_, *world_, camera_, tb(), queue_, events_, layout_, gameMode_, isTablet_};
        processTouches(ctx);
    }
    (void)activeBefore;
    // Toolbox eject easing (skipped while the x transition runs).
    if (!transitions_.toolboxX.active) {
        float target = 0.0f;
        if (tb().buttonState == 1) {
            const float w = static_cast<float>(layout_.width);
            const float maxLen = (w - (layout_.letterBoxFrameWidth + layout_.letterBoxFrameWidth) * layout_.pixelScale) * kToolboxDisplayFraction;
            const float full = tb().ejectLengthFromDisplayLength(tb().totalSlotsWidth());
            if (tb().slotCount != 0) target = full;
            if (maxLen <= target) target = maxLen;
        }
        const float diff = target - tb().ejectLength;
        bool arrived = true;   // the strip is at its target this frame (also when it never moved)
        if (diff == 0.0f || diff < 0.0f) {
            if (diff < 0.0f) {
                tb().ejectLength = tb().ejectLength + -1.0f * kToolboxEjectSpeed * dt;
                if (!(target - tb().ejectLength < 0.0f)) tb().ejectLength = target;
                arrived = !(target - tb().ejectLength < 0.0f);
            }
        } else {
            tb().ejectLength = tb().ejectLength + 1.0f * kToolboxEjectSpeed * dt;
            if (!(0.0f < target - tb().ejectLength)) tb().ejectLength = target;
            arrived = !(0.0f < target - tb().ejectLength);
        }
        // TutorialUtils::Start the first time the strip rests at its target after a new level
        // (GameScreenController+0x29b0) [verified: doFrame].
        if (arrived && !tutorialStarted_ && (gameMode_ == GameMode::Campaign || gameMode_ == GameMode::TestPlay)) {
            startTutorial();
            tutorialStarted_ = true;
        }
        // The scroll spring back into range (not while the finger holds the strip).
        if (touch_.state != touch_state::kToolboxTouch && touch_.state != touch_state::kToolboxScroll) {
            const float maxScroll = tb().totalSlotsWidth() - tb().getDisplayWidth();
            float& scroll = tb().scroll;
            if (scroll < 0.0f) {
                float s = std::sqrt(std::fabs(0.0f - scroll)) * kToolboxScrollSpring * dt + scroll;
                if (0.0f <= s) s = 0.0f;
                scroll = s;
            } else if (maxScroll < scroll) {
                const float s = scroll + -(std::sqrt(std::fabs(maxScroll - scroll)) * kToolboxScrollSpring) * dt;
                scroll = s <= maxScroll ? maxScroll : s;
            }
        }
    }
    // CameraUtils::Update (phones only) and the item follow.
    updateCamera(dt);
    processActions();
    // GameScreen::UpdatePaused: b2World::Step(0, 1, 1) refreshes the contacts of the set-up world every
    // frame (IsColliding reads them), then the slingshot / scissors / dart set-up timers.
    world_->collideOnly();
    for (GameItem& item : state_.items) {
        if (item.type == ItemType::Slingshot) item.setUpTimer = item.setUpTimer + dt;
    }
    items::updateScissorsSetUpMode(dt, state_, random_);
    items::updateDartsSetUpMode(dt, state_, random_);
    processActions();
    camera_.centerPx = clampedCameraCenter(camera_, camera_.centerPx);
    // doFrame: the goal markers animate while no touch is in progress; a touch clears them (the marker
    // counts and timers and the +0xc9984 re-show timer go to 0, TutorialUtils::Stop) and the tail
    // lays them out again 5 s after the last touch [verified: decompile].
    if (touch_.state == touch_state::kIdle) {
        visual_.updateGoals(dt, level_.goal, state_);
    } else {
        visual_.hideGoalMarkers();
        markerReshowTimer_ = 0.0f;
        tutorialStop(tutorial_);
    }
    // GhostManipulationUtils::Update for the held item, then the attachment sounds of this frame.
    const int ts = touch_.state;
    if (ts == touch_state::kDragging || ts == touch_state::kTwoFingerPending || ts == touch_state::kTwoFingerRotate ||
        ts == touch_state::kRingRotate) {
        if (touch_.selectedObject != -1) {
            updateGhostManipulation();
            if (!ghost_.inGhost && haveSelectedBefore && touch_.selectedObject != -1) {
                playAttachmentSounds(state_.objects[static_cast<std::size_t>(touch_.selectedObject)], selectedBefore, queue_);
            }
        }
    }
    // renderFrame's gizmo phase: a slow wobble while the gizmos show, a steady spin while dragging.
    if (gameMode_ != GameMode::SandboxToolbox) {
        const int s = touch_.state;
        if (s == touch_state::kIdle && touch_.gizmoObject != -1) {
            gizmoPhase_ = floatMod(kTwoPi, gizmoPhase_ + dt * kIdleGizmoSpeed * cosF(totalTime_ * 1.5f));
        } else if (s == touch_state::kDragging || s == touch_state::kFromToolbox || s == touch_state::kReturning) {
            gizmoPhase_ = floatMod(kTwoPi, gizmoPhase_ + dt * kDragGizmoSpeed);
        }
    }
    frameTail();
}

void Session::frameTail() {
    // The common tail (LAB_000cb4b8): LevelCompletedEffectUtils::Update (effects), the transitions applied
    // to the toolbox, UpdateGoals (WP6), renderFrame, [state 4] StopRunawayObjects, RemoveInvalidItems, the
    // prev copy, saveUndoState, [state 4] the 5 s no-motion auto-stop [verified: doFrame].
    // LevelCompletedEffectUtils::Update (the confetti) and renderFrame's GameScreen::UpdateAnimations (the
    // star spin, the RC waves) run in every state — the completed screen keeps animating; the sparkles
    // (UpdateSparkles, state 4 only in the original) share the particle list and run here too.
    visual_.updateStars(lastDt_, state_);
    visual_.updateWaves(lastDt_, state_);
    visual_.updateParticles(lastDt_);
    if (transitions_.toolboxEject.active) tb().ejectLength = transitions_.toolboxEject.value;
    else if (transitions_.toolboxX.active) tb().x = transitions_.toolboxX.value;
    transitions_.background.update(lastDt_);
    if (!transitions_.background.active) previousBackground_ = -1;
    if (controllerState_ == 4) stopRunawayObjects(state_, *world_);
    // The render copy was taken before the removal: keep its poses with their objects across the swap.
    std::vector<std::pair<int, RenderPose>> posesByHandle;
    if (!renderPoses_.empty()) {
        posesByHandle.reserve(state_.objects.size());
        for (std::size_t i = 0; i < state_.objects.size() && i < renderPoses_.size(); ++i) {
            posesByHandle.emplace_back(state_.objects[i].handle, renderPoses_[i]);
        }
    }
    state_.removeInvalidItems(world_.get());
    if (!posesByHandle.empty()) {
        renderPoses_.assign(state_.objects.size(), RenderPose{});
        for (std::size_t i = 0; i < state_.objects.size(); ++i) {
            const PhysicsObject& obj = state_.objects[i];
            renderPoses_[i] = {obj.position, obj.angle};
            for (const auto& [handle, pose] : posesByHandle) {
                if (handle == obj.handle) {
                    renderPoses_[i] = pose;
                    break;
                }
            }
        }
    }
    if (controllerState_ == 4 || controllerState_ == 6) prevState_ = state_;   // memcpy(+0x29c0 ← ws)
    if (edited_ && gameMode_ != GameMode::Sandbox && gameMode_ != GameMode::SandboxToolbox) {
        saveUndoState();
        edited_ = false;
    }
    if (controllerState_ == 4) {
        // No body moved for 5 s: the run stops by itself (toggleSimulation; test play → mode 1 first).
        if (!hasMovingObjects(state_, *world_)) {
            idleTime_ = idleTime_ + lastDt_;
            if (idleTime_ > kIdleStopSeconds) {
                if (gameMode_ == GameMode::TestPlay) setMode(GameMode::Sandbox);
                toggleSimulation();
            }
        } else {
            idleTime_ = 0.0f;
        }
    } else if (controllerState_ == 2 && (gameMode_ == GameMode::Campaign || gameMode_ == GameMode::TestPlay)) {
        // The goal markers a touch hid come back 5 s after the last touch (SetGoalMarkers, the appear
        // animation replays, then TutorialUtils::Start) [verified: decompile].
        if (visual_.markerCount > 0) {
            markerReshowTimer_ = 0.0f;
        } else {
            markerReshowTimer_ = markerReshowTimer_ + lastDt_;
            if (markerReshowTimer_ > kMarkerReshowSeconds) {
                visual_.setGoalMarkers(level_.goal, state_);
                startTutorial();
            }
        }
    }
}

void Session::processSimulationTouches() {
    // GameSimulationTouchHandler::Process [verified]: only the first pending touch event of the frame is
    // looked at (the ring is emptied afterwards); a touch that begins queues 0x15 — the run stops on the
    // next frame — or 0x18 (skip the completion animations) once the level-complete effect runs. The
    // in-world GameButtons and the button-press sounds are M5 UI; the state-2 slow-motion drag of the
    // original is dead code (nothing sets that state).
    if (!touches_.events.empty()) {
        const TouchEvent& first = touches_.events.front();
        if (first.kind == TouchEvent::kBegan) {
            queue_.add(Action(completing_ ? action::kSkipAnims : action::kSimulationTouch, 0));
        }
    }
    touches_.events.clear();
}

void Session::advanceSimulation(float dt, bool& recurse) {
    // doFrame case 4 [verified: decompile + disassembly]. The frame-time ring buffer (+0xc25c0) feeds
    // statistics only.
    processSimulationTouches();
    processActions();
    const float scaled = dt * kTimeScale;
    const int starsBefore = goalState_.collectedStars;
    if (!paused_) {
        accumulator_ = updateSimulation(accumulator_ + scaled * kSimulationSpeed);
        if (goalState_.collectedStars != starsBefore) {
            events_.push_back({SessionEvent::Kind::StarCollected, goalState_.collectedStars, Vec2(0.0f, 0.0f), 1.0f});
        }
    } else {
        renderPoses_.resize(state_.objects.size());
        for (std::size_t i = 0; i < state_.objects.size(); ++i) {
            renderPoses_[i] = {state_.objects[i].position, state_.objects[i].angle};
        }
    }
    renderSounds();   // SoundRenderer::Render on the live world state, before the three-star test
    // Three stars end the level like the goal only in game modes 2 / 4 (World of Contraptions, test play)
    // [verified: `getMode() == 2 || == 4` guards the GoalState+4 == 3 test]; the campaign needs the goal.
    if ((gameMode_ == GameMode::WorldOfContraptions || gameMode_ == GameMode::TestPlay) && goalState_.collectedStars == 3) {
        goalState_.reached = true;
        startLevelCompleteSequence();
    }
    if (!completing_) {
        playTime_ = playTime_ + scaled;
    } else {
        completionTimer_ = completionTimer_ - scaled;
        if (completionTimer_ <= 0.0f) {
            setCompletedState();
            recurse = true;
            return;
        }
    }
}

float Session::updateSimulation(float acc) {
    // GameScreen::UpdateSimulation [verified: disassembly]. The trailing updates receive the accumulator as
    // it was *before* the substep loop (`s18`), the lerp and the return value the remainder (`s16`).
    const float acc0 = acc;
    float remaining = acc;
    if (remaining >= kSimulationStep) {
        do {
            prevState_ = state_;
            // The item updates in the original order (Balloon, BoxingGlove, RadioController, Helicopter,
            // Slingshot, Magnet, PaperPlane, Spring, Bumper, TrapdoorLever, Seesaw).
            items::updateBalloons(kSimulationStep, state_, *world_, queue_);
            items::updateBoxingGloves(kSimulationStep, state_, *world_);
            items::updateRadioControllers(kSimulationStep, state_, *world_, queue_);
            items::updateHelicopters(kSimulationStep, state_, *world_);
            items::updateSlingshots(kSimulationStep, state_, *world_, queue_);
            items::updateMagnets(kSimulationStep, state_, *world_);
            items::updatePaperPlanes(kSimulationStep, state_, *world_);
            items::updateSprings(kSimulationStep, state_, *world_, queue_);
            items::updateBumpers(kSimulationStep, state_, *world_, queue_);
            items::updateTrapdoorLevers(kSimulationStep, state_, *world_, queue_);
            items::updateSeesaws(kSimulationStep, state_, *world_, queue_);
            world_->step();
            getStateFromPhysics(state_, *world_);
            remaining = remaining - kSimulationStep;
            ++substep_;
            if (substepObserver_) substepObserver_(substep_);
            items::updateScissors(kSimulationStep, state_, *world_, random_, queue_);
            items::updateGoalStars(kSimulationStep, state_, *world_, goalState_, queue_);
            processingSubstep_ = substep_;
            processActions();
            processingSubstep_ = -1;
        } while (remaining >= kSimulationStep);
    }
    items::updatePiggyBanks(acc0, state_);
    items::updateDartsSetUpMode(acc0, state_, random_);
    if (!goalState_.reached) {
        updateGoalState(acc0, goalState_, level_.goal, state_, *world_);
        if (isGoalComplete(goalState_, level_.goal, state_, *world_)) queue_.add(Action(action::kGoalComplete, 0));
    }
    lerpState(renderPoses_, prevState_, state_, remaining / kSimulationStep);
    return remaining;
}

RenderPose Session::renderPose(int objectIndex) const {
    if (controllerState_ == 4 && objectIndex >= 0 && objectIndex < static_cast<int>(renderPoses_.size())) {
        return renderPoses_[static_cast<std::size_t>(objectIndex)];
    }
    const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(objectIndex)];
    return {obj.position, obj.angle};
}

void Session::updateCamera(float dt) {
    if (isTablet_) return;
    if (touch_.state != touch_state::kDragging || touch_.selectedObject == -1) {
        camera_.cornerFlag = false;
        camera_.edgeTimer = 0.0f;
        return;
    }
    // CameraUtils::Update(dt, camera, true, itemScreenPos, overToolbox, queue)
    const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(touch_.selectedObject)];
    Vec2 fingerPx(0.0f, 0.0f);
    if (touch_.primaryTouch >= 0) fingerPx = touches_.records[static_cast<std::size_t>(touch_.primaryTouch)].currentPx;
    const bool overToolbox = tb().getToolboxRectangle().contains(fingerPx);
    const Vec2 before = camera_.centerPx;
    const Vec2 itemScreen = worldPtToScreenPt(layout_, obj.position);
    {
        Camera& cam = camera_;
        const float halfW = 512.0f / cam.zoom;
        const float halfH = 319.0f / cam.zoom;
        const Vec2 topLeft(cam.centerPx.x - halfW, halfH + cam.centerPx.y);
        const Vec2 bottomRight(halfW + cam.centerPx.x, cam.centerPx.y - halfH);
        const Vec2 tl = screenToPixelPos(layout_, cam, topLeft);
        const Vec2 br = screenToPixelPos(layout_, cam, bottomRight);
        const Vec2 item = screenToPixelPos(layout_, cam, itemScreen);
        bool nearLeft = false;
        bool inZone;
        if (item.x > tl.x + kCameraEdgePx) {
            inZone = (br.x - kCameraEdgePx <= item.x) || (item.y <= br.y + kCameraEdgePx) || !(item.y < tl.y - kCameraEdgePx);
        } else {
            nearLeft = true;
            inZone = true;
        }
        bool counting = false;
        if (inZone && !overToolbox) {
            cam.edgeTimer = cam.edgeTimer + dt;
            counting = true;
        } else {
            cam.edgeTimer = 0.0f;
        }
        if (!cam.cornerFlag) {
            if (counting) {
                const float scrolledUp = cam.centerPx.y - 319.0f / cam.zoom;
                if ((kCameraCornerX > item.x && kCameraCornerY > item.y && scrolledUp > 0.0f) ||
                    (!(item.y > 20.0f) && !(scrolledUp > 0.0f))) {
                    cam.cornerFlag = true;
                    queue_.add(Action(action::kCornerEnter, 0));
                    return;
                }
            }
        } else if (item.y > 90.0f) {
            cam.cornerFlag = false;
            queue_.add(Action(action::kCornerLeave, 0));
            return;
        }
        if (cam.edgeTimer < kCameraEdgeDelay) return;
        const float dx = itemScreen.x - cam.centerPx.x;
        const float dy = itemScreen.y - cam.centerPx.y;
        const float len = length(Vec2(dx, dy));
        if (len < kEpsilon) return;
        float fx = 1.0f;
        if (!nearLeft && (br.x - kCameraEdgePx) > item.x) fx = 0.0f;
        float fy = 1.0f;
        if (!(br.y + kCameraEdgePx >= item.y) && (tl.y - kCameraEdgePx) > item.y) fy = 0.0f;
        const Vec2 target(cam.centerPx.x + dt * kCameraEdgeSpeed * (dx / len) * fx, cam.centerPx.y + dt * kCameraEdgeSpeed * (dy / len) * fy);
        cam.centerPx = clampedCameraCenter(cam, target);
    }
    // The item follows the camera (action 4).
    const float mx = camera_.centerPx.x - before.x;
    const float my = camera_.centerPx.y - before.y;
    if (my * my + mx * mx > kEpsilon) {
        const float f = kPixelToMeters;
        touch_.targetPos.x = mx * f + touch_.targetPos.x;
        touch_.targetPos.y = my * f + touch_.targetPos.y;
        touch_.startPos.x = mx * f + touch_.startPos.x;
        touch_.startPos.y = my * f + touch_.startPos.y;
        queue_.add(Action(action::kMoveSelected, obj.handle));
    }
}

// --- processActions and the handlers ------------------------------------------------------------

void Session::processActions() {
    if (queue_.empty()) return;
    std::vector<Action> actions;
    actions.swap(queue_.actions);
    Action requeue;
    bool requeueSet = false;
    bool ended = false;
    // GameScreenController::processActions re-reads the count every iteration: an action queued while one
    // is processed (the sounds of a Break, the removal of a popped balloon) runs in the same pass.
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const Action a = actions[i];
        // GameScreen::ProcessSimulationAction consumes the world-side ids (7, 12, 13, 17–19) first.
        if (processSimulationAction(a)) {
            processed_.push_back({a, processingSubstep_});
            if (!queue_.empty()) {
                actions.insert(actions.end(), queue_.actions.begin(), queue_.actions.end());
                queue_.clear();
            }
            continue;
        }
        if (a.id == action::kGoalComplete) {
            // ItemActionsMisc case 0xb (campaign / friend solution, not yet reached) [verified].
            if ((gameMode_ == GameMode::Campaign || gameMode_ == GameMode::FriendSolution) && !goalState_.reached) {
                goalState_.reached = true;
                startLevelCompleteSequence();
            }
            lastActionId_ = a.id;
            processed_.push_back({a, processingSubstep_});
            continue;
        }
        if (a.id == action::kSimulationTouch) {
            stopRequested_ = true;   // +0xc99b8
            lastActionId_ = a.id;
            processed_.push_back({a, processingSubstep_});
            continue;
        }
        if (a.id == action::kSkipAnims) {
            // skipLevelCompletedAnims: the effect is skipped, the countdown is not (LevelCompletedEffectUtils::Skip).
            lastActionId_ = a.id;
            processed_.push_back({a, processingSubstep_});
            continue;
        }
        // (The third condition of the original — the level-change background slide — never holds here.)
        if (activeHandle_ == a.handle || ghostAnim_.state == 1) {
            if (!removing_) ended = itemActionsForSelectedAddable(a, ended);
            if (!adding_ && !removing_) itemActionsForSelectedNormal(a, ended);
            else itemActionsForSelectedCompletion(a);
            if (!adding_ && !removing_) requeueSet = itemActionsMisc(a, requeue) || requeueSet;
        } else {
            if (!adding_ || activeHandle_ == -1) {
                if (!removing_) itemActionsNewSelection(a);
                if (!adding_ && !removing_) requeueSet = itemActionsMisc(a, requeue) || requeueSet;
            }
        }
        lastActionId_ = a.id;
        processed_.push_back({a, processingSubstep_});
    }
    if (requeueSet) queue_.add(requeue);
}

bool Session::processSimulationAction(const Action& a) {
    // GameScreen::ProcessSimulationAction [verified]: returns true when the id is consumed.
    switch (a.id) {
    case action::kRemoveItem: {
        // WorldStateUtils::InvalidateItem: attachments removed, physics destroyed, the base flag cleared;
        // the frame tail's RemoveInvalidItems drops the object.
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex >= 0) invalidateItem(state_, *world_, itemIndex);
        return true;
    }
    case action::kPlaySound:
        events_.push_back({SessionEvent::Kind::Sound, a.payload, a.position, a.value});
        // The effects the original starts next to these sounds (SparkleEffectUtils::Start at a collected
        // star, the pop / break puffs of FUN_000c9558), approximated as particle bursts (docs/10 §1).
        if (a.payload >= sound::kStarCollected && a.payload <= sound::kStarCollected + 2) visual_.startSparkles(a.position, 16, 1.2f);
        else if (a.payload == sound::kBalloonPop) visual_.startSparkles(a.position, 10, 1.6f);
        else if (a.payload == sound::kPiggyBreak) visual_.startSparkles(a.position, 12, 1.0f);
        return true;
    case action::kBreak: {
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex >= 0) items::breakItem(state_, *world_, itemIndex, Vec2(a.payloadFloat(), a.value), queue_);
        return true;
    }
    case action::kAttachSharpObject:
        items::attachSharpObject(state_, *world_, a.handle, a.payload, a.valueInt(), a.extra, a.position, a.amount, queue_);
        return true;
    case action::kForceToItem:
        items::forceToItem(state_, *world_, a.handle, a.extra, Vec2(a.payloadFloat(), a.value), a.position);
        return true;
    case action::kForceToRadius:
        items::forceToRadius(state_, *world_, a.position, a.payloadFloat(), a.value);
        return true;
    default:
        return false;
    }
}

bool Session::itemActionsForSelectedAddable(const Action& a, bool ended) {
    if (a.id == action::kMoveSelected) {
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex < 0) return ended;
        if (activeHandle_ != a.handle) {
            endManipulationForActiveItem(itemIndex, ended);
            activeHandle_ = a.handle;
        }
        updateItemPos(state_, *world_, state_.items[static_cast<std::size_t>(itemIndex)].objectIndex, touch_, snapping_, queue_);
    } else if (a.id == action::kReturnToToolbox) {
        // The step's stars stay (action 9 refused for type 23). The original sets its removing flag before
        // the refusal (0xb8938 < 0xb8958); the port refuses first, so the flag stays clear (the touch's
        // kReturning would still stand) — unreachable while the stars are fixed (docs/10 §11 item 14 (o)).
        if (gameMode_ == GameMode::SandboxToolbox && Handle::typeOf(a.handle) == ItemType::GoalStar) return ended;
        removing_ = true;
        if (gameMode_ == GameMode::Sandbox) level_.tested = false;
        manipAnim_.startRemoving(a.handle);
        events_.push_back({SessionEvent::Kind::Sound, sound::kUIItemRemoved, Vec2(0.0f, 0.0f), 0.2f});
    } else if (a.id == action::kManipulationEnded) {
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex < 0) return ended;
        endManipulationForActiveItem(itemIndex, ended);
        if (gameMode_ == GameMode::Sandbox) level_.tested = false;   // GameState+0x25b0 / +0x25b1 cleared
        const int i2 = state_.handles.lookup(a.handle);
        if (i2 >= 0) {
            const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(state_.items[static_cast<std::size_t>(i2)].objectIndex)];
            events_.push_back({SessionEvent::Kind::Sound, sound::kUIItemDeselected, obj.position, 1.0f});
        }
    }
    return ended;
}

void Session::itemActionsForSelectedNormal(const Action& a, bool ended) {
    if (a.id == action::kRotateSelected) {
        if (gameMode_ == GameMode::SandboxToolbox) return;
        if (gameMode_ == GameMode::Sandbox) level_.tested = false;
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex < 0) return;
        updateItemAngle(state_, *world_, state_.items[static_cast<std::size_t>(itemIndex)].objectIndex, touch_.angleCurrent);
    } else if (a.id == action::kFlipSelected) {
        if (gameMode_ == GameMode::SandboxToolbox) return;
        if (gameMode_ == GameMode::Sandbox) level_.tested = false;
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex < 0) return;
        const GameItem& item = state_.items[static_cast<std::size_t>(itemIndex)];
        flipAnim_.start(state_.objects[static_cast<std::size_t>(item.objectIndex)].scale.x, item.handle);
        inputEnabled_ = false;
    } else if (a.id == action::kManipulationStarted && !ended && ghostAnim_.state != 1) {
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex < 0) return;
        const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
        manipulationStarted(state_, *world_, object, touch_.bodyIndex);
        saveGoodState(itemIndex);
        snapping_ = true;   // setDefaultSnappingOptions
    }
}

void Session::itemActionsForSelectedCompletion(const Action& a) {
    if (a.id != action::kRemovalFinished) return;
    adding_ = false;
    removing_ = false;
    activeHandle_ = -1;
    const int itemIndex = state_.handles.lookup(a.handle);
    if (itemIndex >= 0 && state_.items[static_cast<std::size_t>(itemIndex)].objectIndex != -1) {
        const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
        const Vec2 screenPx = worldToScreen(state_.objects[static_cast<std::size_t>(object)].position);
        std::vector<int> removedObjects;
        const int related = removeRelatedItems(state_, *world_, itemIndex, tb(), screenPx);
        const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
        if (obj.type != ItemType::None && obj.handle != 0) {
            exitGhostState(itemIndex);
            detachRopesFromRemovedObject(state_, *world_, object, tb(), [this](Vec2 w) { return worldToScreen(w); }, &removedObjects);
            returnObjectToToolbox(state_, *world_, object, tb(), screenPx);
        }
        if (gameMode_ == GameMode::SandboxToolbox) {
            // The toolbox step [verified: ItemActionsForSelectedCompletion 0xbd32c]: the object, its related
            // item and the ropes it freed leave the ready layout — their handles (by the original physics
            // index, highest current index first) join the removed list, then the world is rebuilt from it.
            removedObjects.push_back(object);
            if (related != 0) {
                const int relatedIndex = state_.handles.lookup(related);
                if (relatedIndex >= 0) removedObjects.push_back(state_.items[static_cast<std::size_t>(relatedIndex)].objectIndex);
            }
            std::sort(removedObjects.begin(), removedObjects.end(), [](int x, int y) { return x > y; });
            for (int current : removedObjects) {
                const int original = physicsIndexSBCurrentToOriginal(current);
                if (original >= 0) removedHandles_.push_back(handlesAtReady_[static_cast<std::size_t>(original)]);
            }
            updateSandboxToolboxLayout(0);
        }
        touch_.state = touch_state::kIdle;
        touch_.selectedObject = -1;
    }
    edited_ = true;
}

void Session::itemActionsNewSelection(const Action& a) {
    if (a.id == action::kSelected) {
        const int itemIndex = state_.handles.lookup(a.handle);
        if (itemIndex < 0) return;
        const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
        activeHandle_ = a.handle;
        adding_ = false;
        if (object != touch_.gizmoObject) manipAnim_.startSelection(a.handle);
        events_.push_back({SessionEvent::Kind::Sound, sound::kUIItemSelected, state_.objects[static_cast<std::size_t>(object)].position, 0.3f});
    } else if (a.id == action::kNewFromToolbox) {
        adding_ = true;
        if (gameMode_ == GameMode::SandboxToolbox) {
            // The toolbox step [verified: ItemActionsNewSelection 0xb904c]: the last item of that type moved
            // into the strip (and its related item) comes back into the layout at its ready position; the
            // strip loses one and the add animation starts on it.
            const ItemType type = static_cast<ItemType>(a.payload);
            int handle = 0;
            for (std::size_t i = removedHandles_.size(); i-- > 0;) {
                if (Handle::typeOf(removedHandles_[i]) == type) {
                    handle = removedHandles_[i];
                    removedHandles_.erase(removedHandles_.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
            }
            if (handle == 0) return;
            for (const LevelItem& li : readyLayout_.items) {
                if (li.handle != handle) continue;
                // LevelLayoutUtils::GetRelatedItem: the itemData of the paired types (35..39) is the partner.
                const int t = static_cast<int>(li.type);
                if (t >= static_cast<int>(ItemType::RCTruck) && t <= static_cast<int>(ItemType::Helicopter) && li.itemData != 0) {
                    for (std::size_t i = removedHandles_.size(); i-- > 0;) {
                        if (removedHandles_[i] == li.itemData) {
                            removedHandles_.erase(removedHandles_.begin() + static_cast<std::ptrdiff_t>(i));
                            break;
                        }
                    }
                }
                break;
            }
            updateSandboxToolboxLayout(handle);
            tb().removeItem(type);
            manipAnim_.startAdding(handle);
            return;
        }
        if (gameMode_ == GameMode::Sandbox) level_.tested = false;
        if (touch_.state == touch_state::kFromToolbox) {
            const ItemType type = static_cast<ItemType>(a.payload);
            const int needed = (type == ItemType::Helicopter || type == ItemType::RCTruck || type == ItemType::Trapdoor) ? 2 : 1;
            if (needed + static_cast<int>(state_.objects.size()) > kMaxObjects || state_.typeCount(type) > kMaxItemsPerType) {
                touch_.state = touch_state::kIdle;
                touch_.selectedObject = -1;
                adding_ = false;
                return;
            }
            const int itemIndex = state_.addNewItem(templates_, type, touch_.startPos, 0.0f, true);
            const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
            createPhysics(state_.objects[static_cast<std::size_t>(object)], state_.items[static_cast<std::size_t>(itemIndex)], *world_,
                          PhysicsMode::SetUp);
            PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
            obj.scale = Vec2(0.6f, 0.6f);
            tb().removeItem(obj.type);
            const int previousAnim = manipAnim_.state;
            activeHandle_ = obj.handle;
            touch_.state = touch_state::kDragging;
            touch_.angleStart = obj.angle;
            touch_.selectedObject = obj.index;
            touch_.angleCurrent = obj.angle;
            if (previousAnim != 0) {
                const int prev = state_.handles.lookup(manipAnim_.handle);
                if (prev >= 0) state_.objects[static_cast<std::size_t>(state_.items[static_cast<std::size_t>(prev)].objectIndex)].scale = Vec2(1.0f, 1.0f);
            }
            manipAnim_.startAdding(obj.handle);
            events_.push_back({SessionEvent::Kind::Sound, sound::kUIItemAdded, Vec2(0.0f, 0.0f), 0.2f});
            snapping_ = true;
            camera_.cornerFlag = true;   // the original writes Camera+0x18 here
            displayToolbox();
        }
    }
}

bool Session::itemActionsMisc(const Action& a, Action& requeue) {
    switch (a.id) {
    case action::kFixedBuzz: {
        events_.push_back({SessionEvent::Kind::Sound, sound::kUISelectBuzz, Vec2(0.0f, 0.0f), 0.2f});
        buzz_.amplitude.start(buzz_.amplitude.value, 1.0f, kBuzzAmplitudeIn);
        buzz_.angle = random_.getFloat(-kGamePi, kGamePi);
        buzz_.handle = a.handle;
        events_.push_back({SessionEvent::Kind::Buzz, 0, Vec2(0.0f, 0.0f), 1.0f});
        return false;
    }
    case action::kFixedBuzzEnd:
        buzz_.amplitude.start(buzz_.amplitude.value, 0.0f, kBuzzAmplitudeOut);
        return false;
    case action::kToolboxScroll:
        if (!removing_ && !adding_) {
            if (touch_.state == touch_state::kToolboxScroll) {
                tb().scroll = tb().scroll + a.position.x;
            } else {
                const float v = a.position.x * 0.95f * 0.95f;
                if (1.0f < std::fabs(v)) {
                    tb().scroll = tb().scroll + v;
                    requeue = Action::scroll(Vec2(v, a.position.y));
                    return true;
                }
            }
        }
        return false;
    default:
        return false;
    }
}

void Session::endManipulationForActiveItem(int itemIndex, bool& ended) {
    adding_ = false;
    ended = true;
    const GameItem& item = state_.items[static_cast<std::size_t>(itemIndex)];
    const int object = item.objectIndex;
    if (manipAnim_.state != 0) {
        PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
        obj.scale = Vec2(obj.scale.x < 0.0f ? -1.0f : 1.0f, 1.0f);
    }
    if (!ghost_.inGhost) {
        edited_ = true;
        manipulationEnded(state_, *world_, templates_, object);
        ghost_ = GhostState{};
    } else {
        if (ghostAnim_.state != 0) {
            const int animItem = state_.handles.lookup(ghostAnim_.handle);
            if (animItem >= 0) setItemPos(state_, *world_, state_.items[static_cast<std::size_t>(animItem)].objectIndex, ghostAnim_.position);
            ghostAnim_.state = 0;
        }
        const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
        if (!ghost_.hasGoodState || ghost_.ghostObject < 0) {
            ghostAnim_.start(obj.position, Vec2(obj.position.x, -0.1f), item.handle);
        } else {
            ghostAnim_.start(obj.position, state_.objects[static_cast<std::size_t>(ghost_.ghostObject)].position, item.handle);
        }
    }
    if (object != touch_.gizmoObject) manipAnim_.startDeselection(state_.items[static_cast<std::size_t>(itemIndex)].handle);
}

void Session::releaseHeldItems() {
    if (activeHandle_ == -1) return;
    if (!adding_ && !removing_) {
        const int itemIndex = state_.handles.lookup(activeHandle_);
        if (itemIndex < 0) return;
        const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
        const int handle = activeHandle_;
        if (ghostAnim_.state == 0 || 0.0f <= ghostAnim_.to.y) {
            ghostAnim_.state = 0;
            if (ghost_.hasGoodState && ghost_.ghostObject >= 0) {
                revertGhostState(itemIndex);
                manipulationEnded(state_, *world_, templates_, object);
                edited_ = true;
                queue_.add(Action(lastActionId_ != action::kManipulationEnded ? action::kManipulationEnded : action::kNoOp, handle));
                return;
            }
            if (lastActionId_ == action::kManipulationEnded) {
                activeHandle_ = -1;
                return;
            }
            if (!adding_) return;
            queue_.add(Action(action::kReturnToToolbox, handle));
        } else {
            ghostAnim_.state = 0;
            queue_.add(Action(action::kReturnToToolbox, handle));
        }
        touch_.state = touch_state::kReturning;
        touch_.selectedObject = object;
        touch_.gizmoObject = -1;
    } else {
        adding_ = false;
        removing_ = false;
        const int itemIndex = state_.handles.lookup(activeHandle_);
        if (itemIndex >= 0) {
            const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
            const Vec2 screenPx = worldToScreen(state_.objects[static_cast<std::size_t>(object)].position);
            removeRelatedItems(state_, *world_, itemIndex, tb(), screenPx);
            const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
            if (obj.type != ItemType::None && obj.handle != 0) {
                exitGhostState(itemIndex);
                detachRopesFromRemovedObject(state_, *world_, object, tb(), [this](Vec2 w) { return worldToScreen(w); });
                returnObjectToToolbox(state_, *world_, object, tb(), screenPx);
            }
            touch_.state = touch_state::kIdle;
            touch_.selectedObject = -1;
        }
        activeHandle_ = -1;
    }
}

void Session::updateGhostAnimation(float dt) {
    if (ghostAnim_.state == 0) return;
    ghostAnim_.update(dt);
    const int itemIndex = state_.handles.lookup(ghostAnim_.handle);
    if (itemIndex < 0) {
        ghostAnim_.state = 0;
        return;
    }
    const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
    if (ghostAnim_.state == 1) {
        setItemPos(state_, *world_, object, ghostAnim_.position);
    } else if (ghostAnim_.state == 2) {
        ghostAnim_.state = 0;
        const int handle = state_.objects[static_cast<std::size_t>(object)].handle;
        if (!ghost_.hasGoodState || ghost_.ghostObject < 0) {
            queue_.add(Action(action::kReturnToToolbox, handle));
            touch_.state = touch_state::kReturning;
            touch_.selectedObject = object;
            touch_.gizmoObject = -1;
        } else {
            revertGhostState(itemIndex);
            manipulationEnded(state_, *world_, templates_, object);
            edited_ = true;
            queue_.add(Action(lastActionId_ == action::kManipulationEnded ? action::kNoOp : action::kManipulationEnded, handle));
        }
    }
}

void Session::updateFlippingAnimation(float dt) {
    if (flipAnim_.state == 0) return;
    // FlippingAnimationUtils::Update
    if (flipAnim_.state == 1) {
        const float prev = flipAnim_.t;
        flipAnim_.t = prev + dt;
        const int itemIndex = state_.handles.lookup(flipAnim_.handle);
        if (itemIndex < 0) {
            flipAnim_.state = 0;
            inputEnabled_ = true;
            return;
        }
        const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
        {
            PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
            const bool colliding = world_->isColliding(obj);
            obj.state = static_cast<std::uint8_t>((obj.state & ~object_state::kGhostTint) | (colliding ? object_state::kGhostTint : 0));
        }
        if (kFlipHalfTime <= flipAnim_.t && prev < kFlipHalfTime) flipItem(state_, *world_, object, queue_);
        const bool scissors = Handle::typeOf(flipAnim_.handle) == ItemType::Scissors;
        if (flipAnim_.t < kFlipTime) {
            const float c = cosF((flipAnim_.t * kGamePi) / kFlipTime);
            flipAnim_.scale = -(flipAnim_.sign * c);
            if (scissors) flipAnim_.scale = std::fabs(-(flipAnim_.sign * c));
        } else {
            const float a = std::fabs(flipAnim_.startScale);
            const float f = scissors ? 1.0f : flipAnim_.sign;
            flipAnim_.state = 2;
            flipAnim_.scale = f * a;
            PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
            if ((obj.state & object_state::kGhostTint) == 0 || flipAnim_.flippedBack) {
                obj.state = static_cast<std::uint8_t>(obj.state & ~object_state::kGhostTint);
                flipAnim_.flippedBack = false;
            } else {
                flipAnim_.start(flipAnim_.scale, flipAnim_.handle);
                flipAnim_.flippedBack = true;
            }
        }
    }
    // updateFlippingAnimation: the animated scale onto the object.
    const int itemIndex = state_.handles.lookup(flipAnim_.handle);
    if (itemIndex < 0) return;
    PhysicsObject& obj = state_.objects[static_cast<std::size_t>(state_.items[static_cast<std::size_t>(itemIndex)].objectIndex)];
    if (flipAnim_.state == 1) {
        obj.scale.x = flipAnim_.scale;
    } else if (flipAnim_.state == 2) {
        flipAnim_.state = 0;
        obj.scale.x = flipAnim_.scale;
        inputEnabled_ = true;
        if (touch_.state == touch_state::kFlipping) touch_.state = touch_state::kIdle;
    }
}

void Session::updateManipulationAnimation(float dt) {
    // ManipulationAnimationUtils::Update, then doFrame copies the scale onto the item.
    const int before = manipAnim_.state;
    manipAnim_.t = dt + manipAnim_.t;
    ManipulationAnimation& m = manipAnim_;
    switch (m.state) {
    case ManipulationAnimation::kAdding: {
        const float r = m.t / 0.3f;
        const float v = curveValueAt(r, kAddingCurve, 4);
        m.scale = v;
        m.gizmoScale = v;
        if (1.0f <= r) {
            m.scale = 1.0f;
            m.gizmoScale = 1.0f;
            m.state = ManipulationAnimation::kNone;
        }
        break;
    }
    case ManipulationAnimation::kRemoving: {
        const float r = m.t / 0.15f;
        m.scale = cubicInterp(1.0f, 0.2f, r);
        if (1.0f <= r) {
            queue_.add(Action(action::kRemovalFinished, m.handle));
            m.reset();
        }
        break;
    }
    case ManipulationAnimation::kAddingThenRemoving: {
        const float r = m.t / 0.3f;
        const float v = curveValueAt(r, kAddingCurve, 4);
        m.scale = v;
        m.gizmoScale = v;
        if (1.0f <= r) {
            m.scale = 1.0f;
            m.state = ManipulationAnimation::kNone;
            m.startRemoving(m.handle);
        }
        break;
    }
    case ManipulationAnimation::kSelection:
    case ManipulationAnimation::kDeselection: {
        const bool sel = m.state == ManipulationAnimation::kSelection;
        const float r = m.t / (sel ? 0.1f : 0.07f);
        m.gizmoScale = cubicInterp(sel ? 0.6f : 1.0f, sel ? 1.0f : 0.6f, r);
        if (1.0f <= r) {
            m.gizmoScale = 1.0f;
            m.state = ManipulationAnimation::kNone;
        }
        break;
    }
    default:
        break;
    }
    if (before != 0) {
        const int itemIndex = state_.handles.lookup(m.handle);
        if (itemIndex >= 0) {
            PhysicsObject& obj = state_.objects[static_cast<std::size_t>(state_.items[static_cast<std::size_t>(itemIndex)].objectIndex)];
            const float sx = obj.scale.x < 0.0f ? -1.0f : 1.0f;
            const float sy = obj.scale.y < 0.0f ? -1.0f : 1.0f;
            obj.scale = Vec2(m.scale * sx, m.scale * sy);
        }
    }
}

// --- GhostManipulationUtils --------------------------------------------------------------------------

void Session::saveGoodState(int itemIndex) {
    ghost_.hasGoodState = true;
    ghost_.goodItem = state_.items[static_cast<std::size_t>(itemIndex)];
    ghost_.goodObject = state_.objects[static_cast<std::size_t>(ghost_.goodItem.objectIndex)];
}

void Session::updateGhostManipulation() {
    // GhostManipulationUtils::Update(ghost, item, obj, false, state, queue, enabled = mode != 5)
    const bool enabled = gameMode_ != GameMode::SandboxToolbox;
    const int object = touch_.selectedObject;
    const int itemIndex = state_.handles.lookup(state_.objects[static_cast<std::size_t>(object)].handle);
    if (itemIndex < 0) return;
    bool colliding = world_->isColliding(state_.objects[static_cast<std::size_t>(object)]);
    if (!colliding) {
        const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
        for (int k = 0; k < obj.attachmentCount; ++k) {
            const AttachmentRecord& rec = obj.attachments[static_cast<std::size_t>(k)];
            if (rec.state == attachment_state::kFree) continue;
            const PhysicsObject& other = state_.objects[static_cast<std::size_t>(rec.otherObject)];
            if (other.type != ItemType::Rope) continue;
            if (world_->isColliding(other)) {
                colliding = true;
                break;
            }
        }
    }
    if (!colliding && enabled) {
        if (!ghost_.inGhost) {
            saveGoodState(itemIndex);
            return;
        }
        playAttachmentSounds(state_.objects[static_cast<std::size_t>(object)], ghost_.goodObject, queue_);
        exitGhostState(itemIndex);
        saveGoodState(itemIndex);
        return;
    }
    if (ghost_.inGhost) return;
    ghost_.inGhost = true;
    if (!ghost_.hasGoodState) return;
    PhysicsObject& live = state_.objects[static_cast<std::size_t>(object)];
    live.state = static_cast<std::uint8_t>(live.state | object_state::kGhostTint);
    ghost_.ropeCount = 0;
    for (int k = 0; k < live.attachmentCount; ++k) {
        const AttachmentRecord& rec = live.attachments[static_cast<std::size_t>(k)];
        if (rec.state == attachment_state::kAttached && state_.objects[static_cast<std::size_t>(rec.otherObject)].type == ItemType::Rope &&
            ghost_.ropeCount < GhostState::kMaxRopes) {
            ghost_.ropes[static_cast<std::size_t>(ghost_.ropeCount)] = rec.otherObject;
            ghost_.ropeCount = ghost_.ropeCount + 1;
        }
    }
    const ItemType type = live.type;
    if (ghost_.ropeCount == 0) {
        if (enabled) {
            if (type != ItemType::Rope) {
                // The 10-step bisection between the current and the last good position.
                const Vec2 original = live.position;
                Vec2 cur = original;
                Vec2 good = ghost_.goodObject.position;
                for (int i = 0; i < 10; ++i) {
                    const Vec2 mid(cur.x + (good.x - cur.x) * 0.5f, cur.y + (good.y - cur.y) * 0.5f);
                    setItemPos(state_, *world_, object, mid);
                    world_->collideOnly();
                    if (!world_->isColliding(state_.objects[static_cast<std::size_t>(object)])) good = mid;
                    else cur = mid;
                }
                setItemPos(state_, *world_, object, original);
                world_->collideOnly();
                ghost_.goodObject.position = good;
            }
        }
    }
    // The ghost copy at the good pose.
    const Vec2 goodPos = ghost_.goodObject.position;
    const int copyItem = state_.addNewItem(templates_, type, goodPos, 0.0f, false);
    copySetUpData(state_.items[static_cast<std::size_t>(copyItem)], state_.items[static_cast<std::size_t>(itemIndex)]);
    const int copyObject = state_.items[static_cast<std::size_t>(copyItem)].objectIndex;
    {
        PhysicsObject& copy = state_.objects[static_cast<std::size_t>(copyObject)];
        const PhysicsObject& src = state_.objects[static_cast<std::size_t>(object)];
        copy.angle = ghost_.goodObject.angle;
        copy.scale = src.scale;
        copy.state = static_cast<std::uint8_t>(copy.state | object_state::kIsGhost);
        createPhysics(copy, state_.items[static_cast<std::size_t>(copyItem)], *world_, PhysicsMode::SetUp);
        world_->setCollisionFilter(copy, filters::kNonCollidable);
    }
    const int attachmentCount = state_.objects[static_cast<std::size_t>(object)].attachmentCount;
    for (int k = 0; k < attachmentCount; ++k) {
        const AttachmentRecord& liveRec = state_.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)];
        const AttachmentRecord& goodRec = ghost_.goodObject.attachments[static_cast<std::size_t>(k)];
        if (liveRec.state == attachment_state::kFree || goodRec.state != attachment_state::kAttached || type == ItemType::Rope) continue;
        const int ropeObject = goodRec.otherObject;
        if (ropeObject < 0 || ropeObject >= static_cast<int>(state_.objects.size()) ||
            state_.objects[static_cast<std::size_t>(ropeObject)].type != ItemType::Rope) {
            continue;
        }
        PhysicsObject& rope = state_.objects[static_cast<std::size_t>(ropeObject)];
        rope.state = static_cast<std::uint8_t>(rope.state | object_state::kGhostTint);
        const GameItem ropeItem = state_.itemOf(rope);
        const int ropeCopyItem = state_.addNewItem(templates_, ItemType::Rope, rope.position, 0.0f, false);
        copySetUpData(state_.items[static_cast<std::size_t>(ropeCopyItem)], ropeItem);
        const int ropeCopyObject = state_.items[static_cast<std::size_t>(ropeCopyItem)].objectIndex;
        const Vec2 end = attachmentPosWS(state_.objects[static_cast<std::size_t>(copyObject)], *world_, k);
        items::ropeSetEndPosition(state_.items[static_cast<std::size_t>(ropeCopyItem)], state_.objects[static_cast<std::size_t>(ropeCopyObject)],
                                  goodRec.otherPoint, end);
        createPhysics(state_.objects[static_cast<std::size_t>(ropeCopyObject)], state_.items[static_cast<std::size_t>(ropeCopyItem)], *world_,
                      PhysicsMode::SetUp);
        world_->setCollisionFilter(state_.objects[static_cast<std::size_t>(ropeCopyObject)], filters::kNonCollidable);
        attach(state_, *world_, copyObject, k, ropeCopyObject, goodRec.otherPoint);
    }
    ghost_.ghostObject = copyObject;
}

void Session::exitGhostState(int itemIndex) {
    // GhostManipulationUtils::ExitGhostState
    if (!ghost_.inGhost) {
        ghost_ = GhostState{};
        return;
    }
    const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
    for (int r = 0; r < ghost_.ropeCount; ++r) {
        const int ropeObject = ghost_.ropes[static_cast<std::size_t>(r)];
        bool stillAttached = false;
        const PhysicsObject& live = state_.objects[static_cast<std::size_t>(object)];
        for (int k = 0; k < live.attachmentCount; ++k) {
            const AttachmentRecord& rec = live.attachments[static_cast<std::size_t>(k)];
            if (rec.state == attachment_state::kAttached && rec.otherObject == ropeObject) stillAttached = true;
        }
        if (stillAttached) continue;
        const int ropeItem = state_.handles.lookup(state_.objects[static_cast<std::size_t>(ropeObject)].handle);
        if (ropeItem >= 0) invalidateItem(state_, *world_, ropeItem);
    }
    if (ghost_.ghostObject != -1) {
        const int g = ghost_.ghostObject;
        int ghostItem = state_.handles.lookup(state_.objects[static_cast<std::size_t>(g)].handle);
        if (state_.objects[static_cast<std::size_t>(g)].type == ItemType::Rope) {
            PhysicsObject& ghostObj = state_.objects[static_cast<std::size_t>(g)];
            for (int k = 0; k < ghostObj.attachmentCount; ++k) ghostObj.attachments[static_cast<std::size_t>(k)] = AttachmentRecord{};
        } else {
            std::vector<int> ropesToEnd;
            const int n = state_.objects[static_cast<std::size_t>(g)].attachmentCount;
            for (int k = 0; k < n; ++k) {
                const AttachmentRecord rec = state_.objects[static_cast<std::size_t>(g)].attachments[static_cast<std::size_t>(k)];
                if (rec.state == attachment_state::kAttached) {
                    if (state_.objects[static_cast<std::size_t>(rec.otherObject)].type == ItemType::Rope) {
                        const int ropeItem = state_.handles.lookup(state_.objects[static_cast<std::size_t>(rec.otherObject)].handle);
                        if (state_.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)].state == attachment_state::kAttached) {
                            if (ropeItem >= 0) invalidateItem(state_, *world_, ropeItem);
                        } else {
                            detach(state_, *world_, g, k);
                            if (ropeItem >= 0) ropesToEnd.push_back(ropeItem);
                        }
                    }
                } else if (rec.state == attachment_state::kSnapped) {
                    unsnap(state_, g, k);
                }
                state_.objects[static_cast<std::size_t>(g)].attachments[static_cast<std::size_t>(k)] = AttachmentRecord{};
            }
            if (ghostItem >= 0) invalidateItem(state_, *world_, ghostItem);
            ghost_.ghostObject = -1;
            for (const int ropeItem : ropesToEnd) {
                PhysicsObject& rope = state_.objects[static_cast<std::size_t>(state_.items[static_cast<std::size_t>(ropeItem)].objectIndex)];
                items::ropeSetSelectionCollisionFilters(rope, *world_);
                items::ropeManipulationEnded(state_, *world_, state_.items[static_cast<std::size_t>(ropeItem)], rope);
            }
            ghostItem = -1;
        }
        if (ghost_.ghostObject != -1 && ghostItem >= 0) invalidateItem(state_, *world_, ghostItem);
        ghost_.ghostObject = -1;
    }
    PhysicsObject& live = state_.objects[static_cast<std::size_t>(object)];
    live.state = static_cast<std::uint8_t>(live.state & ~object_state::kGhostTint);
    for (int k = 0; k < live.attachmentCount; ++k) {
        const AttachmentRecord& rec = live.attachments[static_cast<std::size_t>(k)];
        if (rec.state == attachment_state::kAttached) {
            PhysicsObject& other = state_.objects[static_cast<std::size_t>(rec.otherObject)];
            other.state = static_cast<std::uint8_t>(other.state & ~object_state::kGhostTint);
        }
    }
    ghost_ = GhostState{};
}

void Session::revertGhostState(int itemIndex) {
    // GhostManipulationUtils::RevertGhostState: the ghost pose and set-up data back onto the item, the
    // ghost's snaps / rope attachments re-applied, then ExitGhostState.
    const int object = state_.items[static_cast<std::size_t>(itemIndex)].objectIndex;
    const int g = ghost_.ghostObject;
    setItemPos(state_, *world_, object, state_.objects[static_cast<std::size_t>(g)].position);
    updateItemAngle(state_, *world_, object, state_.objects[static_cast<std::size_t>(g)].angle);
    copySetUpData(state_.items[static_cast<std::size_t>(itemIndex)], ghost_.goodItem);
    int ropeSlot = 0;
    const int n = state_.objects[static_cast<std::size_t>(g)].attachmentCount;
    for (int k = 0; k < n; ++k) {
        const AttachmentRecord grec = state_.objects[static_cast<std::size_t>(g)].attachments[static_cast<std::size_t>(k)];
        if (grec.state == attachment_state::kSnapped) {
            snap(state_, object, k, grec.otherObject, grec.otherPoint);
        } else if (grec.state == attachment_state::kAttached) {
            if (state_.objects[static_cast<std::size_t>(grec.otherObject)].type == ItemType::Rope) {
                const GameItem& copyItem = state_.itemOf(state_.objects[static_cast<std::size_t>(grec.otherObject)]);
                if (ropeSlot < ghost_.ropeCount) {
                    const int origObject = ghost_.ropes[static_cast<std::size_t>(ropeSlot)];
                    ++ropeSlot;
                    PhysicsObject& orig = state_.objects[static_cast<std::size_t>(origObject)];
                    GameItem& origItem = state_.itemOf(orig);
                    origItem.endVector = copyItem.endVector;
                    items::updateRopeLinksFromExtremes(orig, origItem, *world_);
                }
            }
        } else if (state_.objects[static_cast<std::size_t>(object)].attachments[static_cast<std::size_t>(k)].state == attachment_state::kSnapped) {
            unsnap(state_, object, k);
        }
    }
    exitGhostState(itemIndex);
}

// --- render state --------------------------------------------------------------------------------

void Session::setTutorialContext(int locationIndex, int levelIndex) {
    tutorialLocation_ = locationIndex;
    tutorialLevel_ = levelIndex;
}

void Session::setTutorialPlayButton(Vec2 nativePxYDown) {
    tutorialHasPlayButton_ = true;
    tutorialPlayButtonPx_ = Vec2(nativePxYDown.x, static_cast<float>(layout_.height) - nativePxYDown.y);
}

TutorialContext Session::tutorialContext() const {
    TutorialContext ctx;
    ctx.locationIndex = tutorialLocation_;
    ctx.levelIndex = tutorialLevel_;
    ctx.itemCount = tb().getItemCount();
    for (int i = 0; i < tb().slotCount; ++i) {
        TutorialToolboxSlot slot;
        slot.type = tb().slots[static_cast<std::size_t>(i)].type;
        slot.amount = tb().slots[static_cast<std::size_t>(i)].amount;
        // toolboxIdxToWorld [verified]: the slot centre relative to the button, x clamped to <= 0.
        Vec2 c = tb().getCenterForSlot(i);
        if (!(c.x < 0.0f)) c.x = 0.0f;
        slot.world = screenToWorld(Vec2(tb().x + c.x, tb().y + c.y));
        ctx.slots.push_back(slot);
    }
    ctx.hasPlayButton = tutorialHasPlayButton_;
    if (tutorialHasPlayButton_) ctx.playButtonWorld = screenToWorld(tutorialPlayButtonPx_);
    return ctx;
}

void Session::startTutorial() { tutorialStart(tutorial_, tutorialContext()); }

RenderState Session::renderState() const {
    RenderState rs;
    rs.mode = physicsMode_;
    rs.backgroundIndex = level_.backgroundIndex;
    rs.backgroundSlide = transitions_.background.active ? transitions_.background.value * (kWorldWidth / 1024.0f) : 0.0f;
    rs.previousBackground = transitions_.background.active ? previousBackground_ : -1;
    rs.camera = camera_;
    rs.markers = visual_.markers;
    rs.markersBehindItems = visual_.markersBehindItems;
    rs.particles = visual_.particles;
    rs.completed = controllerState_ == 6;
    if (tutorial_.running) {
        for (const TutorialControlledItem& it : tutorial_.items) {
            if (!it.visible) continue;
            TutorialGhost g;
            g.type = static_cast<ItemType>(it.type);
            g.position = it.pos;
            g.angle = it.angle;
            if (it.type > 0 && it.type < kItemTypeCount) g.halfSize = templates_[static_cast<std::size_t>(it.type)].halfSize;
            rs.tutorialGhosts.push_back(g);
        }
    }
    rs.items.reserve(state_.items.size());
    for (const GameItem& item : state_.items) {
        if (item.objectIndex < 0) continue;
        const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(item.objectIndex)];
        if (!obj.valid()) continue;
        RenderItem ri;
        ri.type = obj.type;
        ri.objectIndex = obj.index;
        ri.handle = obj.handle;
        // renderFrame draws the render copy: LerpState's poses for the dynamic objects in simulation.
        const RenderPose pose = renderPose(obj.index);
        ri.position = pose.position;
        ri.angle = pose.angle;
        ri.scale = obj.scale;
        ri.halfSize = obj.halfSize;
        ri.flags = obj.flags;
        ri.state = obj.state;
        ri.stateWord = item.stateWord;
        ri.builtInController = item.builtInController;
        ri.endVector = item.endVector;
        ri.bodyCount = obj.bodyCount;
        for (int k = 0; k < obj.bodyCount; ++k) {
            const int slot = obj.bodies[static_cast<std::size_t>(k)];
            const b2Body* body = (world_ && slot >= 0) ? world_->body(slot) : nullptr;
            RenderBody& rb = ri.bodies[static_cast<std::size_t>(k)];
            if (body != nullptr) {
                rb.position = Vec2(body->GetPosition().x, body->GetPosition().y);
                rb.angle = body->GetAngle();
            }
        }
        ri.attachmentCount = obj.attachmentCount;
        ri.attachments = obj.attachments;
        if (obj.type == ItemType::Rope && world_ && obj.bodyCount >= 3) {
            // RopeRenderUtils::AddIndices [verified]: for each consecutive pair the first joint edge of the
            // first body that leads to the second decides (its joint non-null → the segment is drawn).
            int order[PhysicsObject::kMaxBodies];
            items::ropeBodyIndices(obj.bodyCount, order);
            for (int k = 0; k + 2 < obj.bodyCount; ++k) {
                const b2Body* a = world_->body(obj.bodies[static_cast<std::size_t>(order[k])]);
                const b2Body* b = world_->body(obj.bodies[static_cast<std::size_t>(order[k + 1])]);
                bool drawn = false;
                for (const b2JointEdge* je = a ? a->GetJointList() : nullptr; je; je = je->next) {
                    if (je->other == b) {
                        drawn = je->joint != nullptr;
                        break;
                    }
                }
                ri.ropeSegments[static_cast<std::size_t>(k)] = drawn;
            }
        }
        ri.popped = item.popped;
        ri.popTimer = item.popTimer;
        ri.piggyTimer = item.piggyTimer;
        ri.magnetPulling = item.magnetPulling;
        ri.magnetFrame = item.magnetFrame;
        ri.rotorPhase = item.rotorPhase;
        ri.tailPhase = item.tailPhase;
        ri.bumperOn = item.bumperOn;
        ri.snipStep = item.snipStep;
        ri.snipping = item.snipping;
        ri.snipPhase = item.snipPhase;
        ri.cutAngle = item.cutAngle;
        if (obj.type == ItemType::GoalStar) ri.starFrame = visual_.starFrame(item.handle);
        if (obj.type == ItemType::RCController || obj.type == ItemType::RCTruck) {
            if (const std::vector<RenderWave>* w = visual_.wavesOf(item.handle)) ri.waves = *w;
        }
        if (obj.type == ItemType::BoxingGlove && obj.bodyCount >= 2) {
            // Simulation: BoxingGlove+0x24 as the last Update left it; set-up: from the live bodies
            // (the set-up drag re-creates the bodies without an Update).
            ri.latticeAngle = controllerState_ == 2 ? items::gloveLatticeAngle(ri.bodies[0].position, ri.bodies[1].position)
                                                    : item.latticeAngle;
            ri.buttonHeightPx = item.buttonHeightPx();
        }
        rs.items.push_back(ri);
    }
    // renderFrame: the overlay state from the touch state machine (the gizmo phase advances in advance()).
    ManipulationOverlay& ov = rs.overlay;
    int object = -1;
    switch (touch_.state) {
    case touch_state::kIdle:
        if (gameMode_ != GameMode::SandboxToolbox && touch_.gizmoObject != -1) {
            ov.state = ManipulationOverlay::kGizmos;
            object = touch_.gizmoObject;
            ov.gizmoAngle = floatMod(kTwoPi, gizmoPhase_);
        }
        break;
    case touch_state::kPending:
        if (gameMode_ != GameMode::SandboxToolbox && touch_.selectedObject != -1 &&
            !state_.objects[static_cast<std::size_t>(touch_.selectedObject)].isFixed()) {
            ov.gizmoAngle = floatMod(kTwoPi, gizmoPhase_);
            ov.state = ManipulationOverlay::kTranslation;
            object = touch_.selectedObject;
        }
        break;
    case touch_state::kDragging:
    case touch_state::kFromToolbox:
    case touch_state::kReturning:
        if (gameMode_ != GameMode::SandboxToolbox) {
            ov.gizmoAngle = floatMod(kTwoPi, gizmoPhase_);
            ov.state = ManipulationOverlay::kTranslation;
            object = touch_.selectedObject;
        }
        break;
    case touch_state::kTwoFingerPending:
    case touch_state::kTwoFingerRotate:
    case touch_state::kRingRotate:
        if (gameMode_ != GameMode::SandboxToolbox) {
            ov.state = ManipulationOverlay::kGizmos;
            object = touch_.selectedObject;
            ov.gizmoAngle = floatMod(kTwoPi, gizmoPhase_);
        }
        break;
    case touch_state::kBuzz:
        ov.state = ManipulationOverlay::kInvalid;
        object = touch_.selectedObject;
        break;
    default:
        break;
    }
    if (object == -1) {
        if (manipAnim_.state != 0) {
            const int i = state_.handles.lookup(manipAnim_.handle);
            if (i >= 0) {
                object = state_.items[static_cast<std::size_t>(i)].objectIndex;
                ov.state = ManipulationOverlay::kTranslation;
            }
        } else if (ghostAnim_.state != 0) {
            const int i = state_.handles.lookup(ghostAnim_.handle);
            if (i >= 0) {
                object = state_.items[static_cast<std::size_t>(i)].objectIndex;
                ov.state = ManipulationOverlay::kNone;
            }
        }
    }
    if (buzz_.handle != 0) {
        const int i = state_.handles.lookup(buzz_.handle);
        if (i >= 0) {
            object = state_.items[static_cast<std::size_t>(i)].objectIndex;
            ov.state = ManipulationOverlay::kInvalid;
        }
    }
    ov.objectIndex = object;
    ov.animScale = manipAnim_.gizmoScale;
    ov.inGhost = ghost_.inGhost;
    if (object >= 0 && object < static_cast<int>(state_.objects.size())) {
        const PhysicsObject& obj = state_.objects[static_cast<std::size_t>(object)];
        // GameItemUtils::GetSelectedPos(obj, touch body index): ropes and zip lines report the grabbed body.
        ov.position = obj.position;
        if ((obj.type == ItemType::Rope || obj.type == ItemType::ZipLine) && touch_.bodyIndex >= 0 &&
            touch_.bodyIndex < obj.bodyCount && world_) {
            const b2Body* b = world_->body(obj.bodies[static_cast<std::size_t>(touch_.bodyIndex)]);
            if (b) ov.position = Vec2(b->GetPosition().x, b->GetPosition().y);
        }
        ov.angle = obj.angle;
        ov.showFlip = (obj.flags & object_flags::kFlippable) != 0;
        ov.flipButtonPos = Vec2(obj.position.x + cosF(kGamePi * 0.25f) * 0.4f, sinF(kGamePi * 0.25f) * 0.4f + obj.position.y);
        for (std::size_t i = 0; i < rs.items.size(); ++i) {
            if (rs.items[i].objectIndex == object) {
                rs.heldIndex = static_cast<int>(i);
                rs.items[i].held = true;
            }
        }
    }
    if (controllerState_ == 4) ov.state = ManipulationOverlay::kNone;
    rs.buzz.handle = buzz_.handle;
    rs.buzz.amplitude = buzz_.amplitude.value;
    rs.buzz.angle = buzz_.angle;
    // The toolbox strip.
    RenderToolbox& out = rs.toolbox;
    // GameRenderState+0x24 (renderFrame's local_60 = 1): the strip is always drawn; it hides by sliding off.
    out.visible = true;
    out.spriteScale = toolboxScale();
    out.x = tb().x;
    out.y = tb().y;
    out.ejectLength = tb().ejectLength;
    out.scroll = tb().scroll;
    out.buttonScale = tb().buttonScale;
    for (int i = 0; i < tb().slotCount; ++i) {
        const ToolboxStripSlot& s = tb().slots[static_cast<std::size_t>(i)];
        out.slots.push_back({s.type, s.amount, s.widthPx, s.heightPx, s.scale});
    }
    return rs;
}

std::vector<Action> Session::drainActions() {
    std::vector<Action> out;
    out.reserve(processed_.size());
    for (const ProcessedAction& p : processed_) out.push_back(p.action);
    processed_.clear();
    return out;
}

std::vector<Session::ProcessedAction> Session::drainProcessedActions() {
    std::vector<ProcessedAction> out;
    out.swap(processed_);
    return out;
}

std::vector<SessionEvent> Session::drainEvents() {
    std::vector<SessionEvent> out;
    out.swap(events_);
    return out;
}

}  // namespace aa::sim
