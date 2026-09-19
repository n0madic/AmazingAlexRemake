// The copyable game state (st::WorldState, docs/10-architecture.md §5.1): items, physics objects and the
// handle table. No pointers: bodies are slot indices into a PhysicsWorld, which is rebuilt from this state
// on every mode transition exactly as the original does.
#pragma once

#include "aa/sim/animations.h"
#include "aa/sim/templates.h"
#include "aa/sim/types.h"

#include <algorithm>

#include <array>
#include <cstdint>
#include <vector>

namespace aa::sim {

class PhysicsWorld;

// st::Handle (docs/02 §3.1): type << 26 | generation << 12 | slot, stored as a signed 32-bit int.
struct Handle {
    static constexpr int kTypeShift = 26;
    static constexpr int kGenerationShift = 12;
    static constexpr int kSlotBits = 12;
    static constexpr int kSlotMask = (1 << kSlotBits) - 1;
    static constexpr int kGenerationMask = (1 << (kTypeShift - kGenerationShift)) - 1;

    static ItemType typeOf(int handle) {
        return static_cast<ItemType>(static_cast<std::uint32_t>(handle) >> kTypeShift);
    }
    static int slotOf(int handle) { return handle & kSlotMask; }
    static int generationOf(int handle) {
        return (static_cast<std::uint32_t>(handle) >> kGenerationShift) & kGenerationMask;
    }
    static int make(ItemType type, int generation, int slot) {
        return static_cast<int>((static_cast<std::uint32_t>(type) << kTypeShift) |
                                (static_cast<std::uint32_t>(generation & kGenerationMask) << kGenerationShift) |
                                static_cast<std::uint32_t>(slot & kSlotMask));
    }
};

// Per-item game state (st::GameItem; the per-type block returned by HandleManager::Get). Only the fields
// the core needs so far: the type-specific word at +8 (book colour, world-bound variant, billboard hint,
// controller links), the end vector of two-ended items (rope / slingshot / zip line), the scissors cut
// angle and the glove button height. A new item starts with the per-type defaults of st::ItemInfos
// (GameItemCollectionUtils::InsertWithHandle copies that block) — see GameItem::defaults.
struct GameItem {
    static constexpr float kDefaultCutAngle = 0.26179934f;     // Scissors+0xC: 15° (0x3e860a90)
    static constexpr float kGloveButtonHeightPx = 36.0f;       // BoxingGlove+0x20: full button height
    static constexpr float kIdleTimerMin = 3.0f;               // DAT_0029179c / DAT_00283174
    static constexpr float kIdleTimerMax = 10.0f;

    ItemType type = ItemType::None;
    int handle = 0;
    int objectIndex = -1;         // index into WorldState::objects
    std::int32_t stateWord = 0;   // Book: colour 0..3; WorldBound: 1 = Treehouse floor; Billboard: hint
    Vec2 endVector{0.0f, 0.0f};   // second end relative to the centre (Rope, ZipLine); the pouch (Slingshot)
    float cutAngle = 0.0f;        // Scissors+0xC: half-opening angle of the blades
    // BoxingGlove+0xC..+0x24: the trigger button height interpolator (36 → 8 px in 0.06 s when hit;
    // its value at +0x20 is the sprite height the renderer draws).
    CubicInterpolator gloveButton;
    float buttonHeightPx() const { return gloveButton.value; }
    // Helicopter / RCTruck / Trapdoor: the controller or lever is still part of the item (`Helicopter+8`,
    // `Truck+8`, `Trapdoor+8`): set by GameItemUtils::SetInitialState(fromToolbox) when the item is taken
    // out of the toolbox, cleared by the first ManipulationEnded, which splits the controller off into an
    // item of its own and pairs the two through stateWord [verified: TruckUtils::ManipulationEnded]. Legacy
    // layouts (version <= 6) set it too; the importer normalises every level to version 7.
    bool builtInController = false;
    // Rope+0x10: the end-to-end distance joint RopeUtils::AttachmentChanged creates while both ends are
    // attached (PhysicsWorld joint slot, -1 when none).
    int ropeJoint = -1;
    // Slingshot+0x1C: seconds since the last stretch / release sound (SlingshotUtils::UpdateSetUpMode).
    float setUpTimer = 0.0f;
    // --- simulation state (docs/03 §2, docs/11 §5), zero unless the type uses it -----------------
    // GoalStar+8 / +0xC: 0 uncollected, 1 collecting (the 0.4 s shrink, `timer` counts up), 2 removed.
    int starState = 0;
    float starTimer = 0.0f;
    // Balloon+8 popped, +0xC the pop animation countdown (0.15 s, then action 7).
    bool popped = false;
    float popTimer = 0.0f;
    // Bumper+8 on, +0xC the "on" countdown (0.18 s).
    int bumperOn = 0;
    float bumperTimer = 0.0f;
    // BoxingGlove+8: 0 armed, 1 punching (the button was hit), 2 retracted (BoxingGloveUtils::Retract);
    // +0x24 the lattice hinge angle UpdateArmGeometry computed last (what the renderer draws).
    int gloveState = 0;
    float latticeAngle = 0.0f;
    // Scissors+8: 0 open, 1 closing (cutAngle shrinks by 2 rad/s), 2 closed / cut done. The idle snip
    // animation (docs/11 §5, FUN_000e4314): +0x10 step (−1 … 4, one ScissorsXX frame per 0.04 s), +0x14
    // its timer, +0x18 snipping, +0x1C the snip angle, +0x20 the snip phase, +0x24 its direction.
    int scissorsState = 0;
    int snipStep = 0;
    float snipTimer = 0.0f;
    bool snipping = false;
    float snipAngle = 0.0f;
    float snipPhase = 0.0f;
    float snipDirection = 0.0f;
    // Dart+9: stuck into a stabbable item (action 17 made the joint). The idle wobble (DartUtils::
    // UpdateSetUpMode, the same shape as the scissors' snip): +8 wobbling, +0xC angle, +0x10 phase, +0x14
    // direction, +0x18 timer.
    bool stuck = false;
    bool wobbling = false;
    float wobbleAngle = 0.0f;
    float wobblePhase = 0.0f;
    float wobbleDirection = 0.0f;
    float wobbleTimer = 0.0f;
    // Helicopter+0x10: the rotor runs (RadioControllerUtils::Update → TurnOn / TurnOff); +0x14 the
    // throttle (50–100 N), +0x1C / +0x20 the rotor speed and phase, +0x24 / +0x28 the tail rotor's.
    bool heliOn = false;
    float heliThrottle = 0.0f;
    float rotorSpeed = 0.0f;
    float rotorPhase = 0.0f;
    float tailSpeed = 0.0f;
    float tailPhase = 0.0f;
    // Slingshot+8 fired, +0x14 the pouch velocity (the pouch springs back to (−0.01, 0.076) after the
    // launch), +0x20 the handle of the launched object (SlingshotUtils::ShouldCollide).
    bool fired = false;
    Vec2 pouchVelocity{0.0f, 0.0f};
    int loadedHandle = 0;
    // PiggyBank+8: seconds since Break (the POW frames for 0.2 s).
    float piggyTimer = 0.0f;
    // TrapdoorLever+0xC unlocked (the paired trapdoor — stateWord — was opened), +0xD the move sound played.
    bool leverUnlocked = false;
    bool leverSounded = false;
    // Seesaw+8: the direction (±1) of the last SeesawMove sound.
    int seesawDirection = 0;
    // RadioController+0x10: the button is pressed (translation below −0.03 m); the paired truck /
    // helicopter is in stateWord (RadioController+8).
    bool buttonPressed = false;
    // Magnet+8 pulling (something is attracted this substep), +0xC the nearest attracted distance², +0x14
    // the pulse frame timer (1/30 s), +0x18 the pulse frame 0..5.
    bool magnetPulling = false;
    float magnetMinDist2 = 0.0f;
    float magnetTimer = 0.0f;
    int magnetFrame = 0;
    // The looping-sound clip handle SoundRenderer::Render keeps in the block (Skateboard+8, Magnet+0x10,
    // ZipLine+0x10, RadioController+0x14; -1 = none) and the controller's current clip id (+0x18: start /
    // loop / end, 0 = none). Excluded from the conformance dumps (docs/10 §11 item 12 d).
    int clipHandle = -1;
    int clipId = 0;

    // The st::ItemInfos default block for a type (rope end (0, -0.6), slingshot pouch (-0.11, 0.03),
    // zip-line end (1, -0.2), scissors 15°, glove button 36 px, billboard hint 1).
    static GameItem defaults(ItemType type);
    // GameItemUtils::SetInitialState: the book colour from the handle, the built-in controller flag of a
    // toolbox truck / trapdoor / helicopter, and the first idle timer (3–10 s) of a dart's wobble / a
    // scissors' snip. The original seeds that timer's Random with the item block's *address*; the remake
    // (and the oracle, which overrides the block) seed it with the handle — a documented deviation
    // (docs/10 §11), presentation only until the GameState Random is consumed by the animation.
    void setInitialState(bool fromToolbox);
};

// Attachment record of an object (docs/04 §7): the template point plus the connection state.
struct AttachmentRecord {
    AttachmentPoint point;
    int state = attachment_state::kFree;
    int otherObject = -1;
    int otherPoint = -1;
    int joint = -1;               // joint slot in the PhysicsWorld, -1 when none
};

// st::PhysicsObject (docs/04 §4), 1:1 with the original's fields; body pointers replaced by slots.
struct PhysicsObject {
    static constexpr int kMaxBodies = 16;
    static constexpr int kMaxJoints = 16;
    static constexpr int kMaxAttachments = PhysicsObjectTemplate::kMaxAttachments;

    ItemType type = ItemType::None;
    int index = -1;               // index in the collection
    int handle = 0;
    std::uint8_t flags = object_flags::kTemplateDefault;
    std::uint8_t state = 0;
    Vec2 position{0.0f, 0.0f};
    float angle = 0.0f;
    Vec2 scale{1.0f, 1.0f};       // scale.x = -1 when flipped
    float halfSize = 1.0f;        // template size ("r")
    int attachmentCount = 0;
    std::array<AttachmentRecord, kMaxAttachments> attachments{};
    int bodyCount = 0;
    std::array<int, kMaxBodies> bodies{};   // PhysicsWorld body slots; [0] = main body
    // PhysicsWorld joint slots of the item's own joints, in the order the original stores its b2Joint*
    // fields (per type: seesaw pivot, glove prismatic, wheel joints, rope links…); attachment joints are
    // in the attachment records.
    int jointCount = 0;
    std::array<int, kMaxJoints> joints{};
    // PhysicsObject+0x90..0x92: per-body flag bytes (body_flags): bit 0 selectable — read by the pick query
    // through the fixture user data (rope end circles carry body index + 1) and cleared by
    // RopeUtils::AttachmentChanged for a snapped / attached end; bit 1 gizmos — byte 0 decides whether a
    // tapped / dropped item keeps the rotation ring and flip button [verified].
    static constexpr int kSelectableBytes = PhysicsObjectTemplate::kBodyFlagBytes;
    std::array<std::uint8_t, kSelectableBytes> selectable{{body_flags::kDefault, body_flags::kDefault, body_flags::kDefault}};
    bool showsGizmos() const { return (selectable[0] & body_flags::kGizmos) != 0; }

    bool flipped() const { return scale.x < 0.0f; }
    bool valid() const { return (flags & object_flags::kBase) != 0; }   // cleared by InvalidateItem
    bool isGhost() const { return (state & object_state::kIsGhost) != 0; }
    // Flip sign "s" of docs/03: +1, or -1 when the item is flipped.
    float flipSign() const { return scale.x < 0.0f ? -1.0f : 1.0f; }
    bool isDynamic() const { return (flags & object_flags::kDynamic) != 0; }
    bool isFixed() const { return (flags & object_flags::kFixed) != 0; }

    // Construct from a template, as PhysicsObjectsUtils::Add copies the template record.
    static PhysicsObject fromTemplate(const PhysicsObjectTemplate& t);
    void addBody(int slot);
    void addJoint(int slot);
};

// Live handle table (st::HandleManager, 0x1000 entries × {next free slot, generation, active}): a slot's
// generation starts at 1 and is bumped by every Add, so a stale handle (an older generation) is rejected;
// the free slots form a list (0 → 1 → 2 → …, a removed slot pushed back at the head), and Add takes its head
// [verified: HandleEntry, Reset, Add, Set, Remove, Get].
class HandleManager {
public:
    static constexpr int kSlotCount = 1 << Handle::kSlotBits;

    HandleManager();
    // HandleManager::Set: register a handle chosen by the level file (WorldStateUtils::AddItemWithHandle);
    // the slot leaves the free list and takes the handle's generation.
    void addWithHandle(int handle, int itemIndex);
    // HandleManager::Add: the free list's head with its generation bumped.
    int add(ItemType type, int itemIndex);
    // HandleManager::Remove: the slot goes back to the head of the free list; a stale generation is ignored.
    void remove(int handle);
    // Item index for a handle, -1 if the handle is not live.
    int lookup(int handle) const;
    // HandleManager::Update: the item moved inside the collection (type-sorted insert / removal).
    void setItemIndex(int handle, int itemIndex);
    // The slot add() would take (the free list's head); throws when the table is full.
    int freeSlot() const;

private:
    struct Slot {
        int next = 0;          // the next free slot while in the free list
        int generation = 1;
        int itemIndex = -1;
        bool live = false;
    };
    static constexpr int kEndOfList = -1;
    void unlink(int slot);
    std::vector<Slot> slots_;
    int freeHead_ = 0;
};

// st::WorldState. `items` is the original's GameItemCollection: sorted by type, a new item goes to the end
// of its type's run (GameItemCollectionUtils::Insert) — the renderer and LevelLayoutUtils iterate it in that
// order. `objects` is the PhysicsObjectCollection in insertion order; removal is swap-with-last
// (PhysicsObjectsUtils::Remove). Both vectors are reserved to kObjectCapacity so references stay valid
// while items are added (the original uses fixed arrays).
struct WorldState {
    std::vector<GameItem> items;
    std::vector<PhysicsObject> objects;
    HandleManager handles;

    WorldState();

    // Add an item with its physics object from the template table under a level-chosen handle
    // (WorldStateUtils::AddItemWithHandle). Returns the item index.
    int addItemWithHandle(const TemplateTable& templates, int handle, Vec2 center, float angle);
    // WorldStateUtils::AddNewItem: a fresh handle, the object at `center` with the template angle + `angle`,
    // GameItemUtils::SetInitialState(fromToolbox) (book colour from the handle, built-in controller flag).
    // No physics: the caller creates it. Returns the item index.
    int addNewItem(const TemplateTable& templates, ItemType type, Vec2 center, float angle, bool fromToolbox);
    // WorldStateUtils::RemoveInvalidItems: drops every object whose base flag was cleared by InvalidateItem
    // (swap-with-last; the moved object's index, item link, body user data and the attachment records that
    // point at it are fixed). `world` may be null when no bodies exist.
    void removeInvalidItems(PhysicsWorld* world);
    // Number of live items of a type (the per-type count GameState keeps).
    int typeCount(ItemType type) const;
    GameItem* findItem(int handle);
    const GameItem* findItem(int handle) const;
    GameItem& itemOf(const PhysicsObject& obj) { return items[static_cast<std::size_t>(handles.lookup(obj.handle))]; }
    const GameItem& itemOf(const PhysicsObject& obj) const {
        return items[static_cast<std::size_t>(handles.lookup(obj.handle))];
    }

private:
    int insertItem(GameItem item);
    void removeItemAt(int itemIndex);
    void reindexItems(int from);
    void removeObject(int objectIndex, PhysicsWorld* world);
};

}  // namespace aa::sim
