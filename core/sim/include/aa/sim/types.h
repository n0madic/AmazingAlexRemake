// Basic types of the deterministic core (docs/10-architecture.md §5).
// Everything here mirrors the original's plain data: integer handles, float world coordinates in metres.
#pragma once

#include <Box2D/Common/b2Math.h>

#include <cstdint>

namespace aa::sim {

// World-space 2D vector. The core's public headers use Box2D's vector directly: the only consumers are
// aa_data, the tests and the platform layer, all of which link Box2D transitively (docs/10 §5).
using Vec2 = b2Vec2;

// st::ItemType::Enum (docs/03-game-items.md §1). Value 0 is unused; 40 is the legacy helicopter controller
// in level files (converted to RCController on import) and the editor's selection-area helper at run time.
enum class ItemType : int {
    None = 0,
    Shelf = 1,
    TennisBall = 2,
    BowlingBall = 3,
    SoccerBall = 4,
    Balloon = 5,
    Scissors = 6,
    Bucket = 7,
    Hook = 8,
    Rope = 9,
    CardboardBoxMedium = 10,
    CardboardBoxSmall = 11,
    FishBowl = 12,
    PiggyBank = 13,
    BoxingGlove = 14,
    Book = 15,
    EightBall = 16,
    Pipe = 17,
    Pipe90 = 18,
    Doll = 19,
    Skateboard = 20,
    Pulley = 21,
    Seesaw = 22,
    GoalStar = 23,
    Billboard = 24,
    Magnet = 25,
    Pinball = 26,
    PaperPlane = 27,
    Spring = 28,
    Dart = 29,
    HangingLamp = 30,
    WorldBound = 31,
    LaundryBasket = 32,
    Bumper = 33,
    Slingshot = 34,
    RCTruck = 35,
    RCController = 36,
    Trapdoor = 37,
    TrapdoorLever = 38,
    Helicopter = 39,
    SelectionArea = 40,
    BouncyBall = 41,
    ZipLine = 42,
};

constexpr int kItemTypeCount = 43;   // indices 0..42, as the original's template table
constexpr int kFirstItemType = 1;
constexpr int kLastItemType = 42;

// Name of an item type as used by the tools (uc_dump_physics.py ITEM_NAMES); "?" for an invalid value.
const char* itemTypeName(ItemType type);
inline bool isValidItemType(int type) { return type >= kFirstItemType && type <= kLastItemType; }

// st::PhysicsMode::Enum (docs/04-physics.md §2): the set-up world (editing, never stepped, every body
// dynamic, selection sensors) and the simulation world.
enum class PhysicsMode : int { SetUp = 0, Simulation = 1 };

// PhysicsObject flags byte (docs/04-physics.md §4). The template initialiser edits these bits per type.
namespace object_flags {
constexpr std::uint8_t kBase = 0x01;        // always set
constexpr std::uint8_t kDynamic = 0x02;     // falls; interpolated by LerpState
constexpr std::uint8_t kFixed = 0x04;       // set from level flag bit 0; cannot be picked up
constexpr std::uint8_t kFlippable = 0x08;   // has a flip button (asymmetric sprite: scissors, glove, doll, skateboard,
                                            // magnet, plane, dart, slingshot, truck, helicopter) [verified:
                                            // GameTouchHandler::Process tests this bit before the flip rect]
constexpr std::uint8_t kBreakable = 0x10;   // piggy bank
constexpr std::uint8_t kMagnetic = 0x20;    // attracted by the magnet
constexpr std::uint8_t kStabbable = 0x40;   // the dart sticks into it
constexpr std::uint8_t kReturnsToToolbox = 0x80;   // goes back into the toolbox strip when removed; cleared only for
                                                   // RCController and TrapdoorLever, whose truck / trapdoor returns
                                                   // instead [verified: FUN_000c3478 `ldrb [obj+0xc]; lsrs #7`]
constexpr std::uint8_t kTemplateDefault = 0x82;   // every template starts as 0x82 (static initialiser)
}  // namespace object_flags

// PhysicsObject state byte (docs/04-physics.md §4, docs/05 §5).
namespace object_state {
constexpr std::uint8_t kActivated = 0x01;   // balloon popped, piggy broken (goal type 5)
constexpr std::uint8_t kGhostTint = 0x02;   // the held item (and its attached ropes) while in ghost: drawn translucent
constexpr std::uint8_t kIsGhost = 0x04;     // a ghost copy (non-collidable, static, removed on ExitGhostState)
}

// Limits of the original's fixed arrays (GameState): ItemActionsNewSelection refuses a toolbox item when the
// object count would exceed 126 or the per-type item count is already 31 [verified].
constexpr int kMaxObjects = 126;
constexpr int kMaxItemsPerType = 31;
constexpr int kObjectCapacity = 128;         // vector reserve so references stay valid while adding

// Level item flags (docs/02-level-format.md §3.2).
namespace level_flags {
constexpr int kFixed = 1;
constexpr int kFlipped = 2;
}

// Attachment point kinds and masks (docs/04-physics.md §7).
namespace attachment_kind {
constexpr int kHook = 1;
constexpr int kHangable = 2;
constexpr int kRopeEnd = 4;
constexpr int kPipeEnd = 8;
}

// Attachment record state (docs/02-level-format.md §3.5).
namespace attachment_state {
constexpr int kFree = 0;
constexpr int kSnapped = 1;
constexpr int kAttached = 2;
}

// World geometry constants (docs/02-level-format.md §6).
constexpr float kPixelToMeters = 3.41f / 1024.0f;   // WorldStateUtils::GetPixelToMetersFactor
constexpr float kWorldWidth = 3.41f;
constexpr float kWorldHeight = 2.12459f;
// st::LevelLayout::LevelLayout's title: the localisation id of "My level" (a user level's default).
constexpr const char* kDefaultSandboxTitleId = "LEVEL_SHARE_CONTRAPTION_DEFAULT";
constexpr int kTreehouseBackground = 3;             // WorldBoundUtils::GetTypeForBackgroundIndex(i) == (i == 3)

}  // namespace aa::sim
