// Port of st::PhysicsObjectsUtils::InitializePhysicsObjectTemplates (Android build, 0xcf4b0).
// Every template starts as the static-initialised object (flags 0x82, half-size 1.0); the initialiser
// then edits fields per type. Expression shapes (multiplication order, float throughout — the function
// contains no double operation) are kept as compiled, because the results feed bit-exact fixture sizes.
#include "aa/sim/templates.h"

#include "aa/sim/float_bits.h"

#include <cmath>

namespace aa::sim {

namespace {

// Frame indices of GameItems.plist read by the initialiser (docs/03 §4).
namespace frame {
constexpr int kBalloon = 1;
constexpr int kBookBlue = 7;
constexpr int kBowlingBall = 12;
constexpr int kBucket = 26;
constexpr int kBumperOff = 28;
constexpr int kCardboardBoxMedium = 30;
constexpr int kCardboardBoxSmall = 31;
constexpr int kDart = 33;
constexpr int kFishBowl = 42;
constexpr int kHook = 57;
constexpr int kLampShade = 59;
constexpr int kLaundryBasketFront = 61;
constexpr int kMagnet = 62;
constexpr int kPaperPlane = 73;
constexpr int kPiggyBank = 74;
constexpr int kPipe = 79;
constexpr int kPipe90 = 80;
constexpr int kPulleyWheel = 88;
constexpr int kRCController = 90;
constexpr int kRCTruck = 94;
constexpr int kScissorsTop = 107;
constexpr int kSeesawArm = 108;
constexpr int kShelfTop = 112;
constexpr int kSkateboard = 113;
constexpr int kSlingshotFrameFront = 118;
constexpr int kSoccerBall = 121;
constexpr int kSpring = 123;
constexpr int kStar01 = 125;
constexpr int kTennisBall = 138;
constexpr int kTrapdoorLeft = 140;
constexpr int kTrapdoorLever = 141;
constexpr int kTrapdoorShelfLeft = 144;
}  // namespace frame

// (int)(|extent| - 2) converted back to float: the sprite's pixel size minus the 1-px trim on each side.
float trimmed(float extent) {
    const float t = std::fabs(extent) - 2.0f;
    return static_cast<float>(static_cast<int>(t));
}

// halfSize = trimmed * 0.5 * 3.41 * (1/1024), in this order (float).
float halfOf(float extent) { return trimmed(extent) * 0.5f * 3.41f * 0.0009765625f; }

float halfWidth(const FrameTable& frames, int index) { return halfOf(frames.at(index).width()); }
float halfHeight(const FrameTable& frames, int index) { return halfOf(frames.at(index).height()); }

AttachmentPoint point(Vec2 pos, Vec2 dir, int kind, int mask, bool positionOnly, int body = 0) {
    AttachmentPoint p;
    p.pos = pos;
    p.dir = dir;
    p.kind = kind;
    p.mask = mask;
    p.body = body;
    p.positionOnly = positionOnly;
    return p;
}

Vec2 normalized(float x, float y) {
    Vec2 v(x, y);
    v.Normalize();
    return v;
}

}  // namespace

TemplateTable initTemplates(const FrameTable& frames) {
    using namespace attachment_kind;
    constexpr int kHangableMask = kRopeEnd;             // hangable / hook points accept rope ends
    constexpr int kRopeMask = kHook | kHangable;
    constexpr int kPipeMask = kPipeEnd;
    const Vec2 kUp(0.0f, 1.0f), kDown(0.0f, -1.0f), kRight(1.0f, 0.0f), kLeft(-1.0f, 0.0f);

    TemplateTable table{};
    for (int i = 0; i < kItemTypeCount; ++i) {
        table[static_cast<std::size_t>(i)].type = static_cast<ItemType>(i);
    }
    table[0].type = ItemType::None;   // slot 0 is never initialised by the original
    auto& T = table;
    auto t = [&T](ItemType type) -> PhysicsObjectTemplate& { return T[static_cast<std::size_t>(type)]; };
    auto setFlags = [](PhysicsObjectTemplate& p, std::uint8_t keep, std::uint8_t set) {
        p.flags = static_cast<std::uint8_t>((p.flags & keep) | set);
    };
    auto orFlags = [](PhysicsObjectTemplate& p, std::uint8_t set) {
        p.flags = static_cast<std::uint8_t>(p.flags | set);
    };

    {   // 1 Shelf
        auto& p = t(ItemType::Shelf);
        setFlags(p, 0xbc, 0x41);
        p.halfSize = halfWidth(frames, frame::kShelfTop) * 0.98f;
    }
    {   // 2 TennisBall
        auto& p = t(ItemType::TennisBall);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kTennisBall) * 0.98f;
    }
    {   // 3 BowlingBall
        auto& p = t(ItemType::BowlingBall);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kBowlingBall);
    }
    {   // 4 SoccerBall
        auto& p = t(ItemType::SoccerBall);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kSoccerBall) * 1.05f;
    }
    {   // 5 Balloon
        auto& p = t(ItemType::Balloon);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kBalloon);
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(0.0f, -0.25f), kDown, kHangable, kHangableMask, true);
    }
    {   // 6 Scissors
        auto& p = t(ItemType::Scissors);
        setFlags(p, 0xf4, 0x09);
        p.halfSize = halfWidth(frames, frame::kScissorsTop);
    }
    {   // 7 Bucket
        auto& p = t(ItemType::Bucket);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kBucket) * 0.9f;
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(0.0f, 0.26f), kUp, kHangable, kHangableMask, true);
    }
    {   // 8 Hook
        auto& p = t(ItemType::Hook);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = halfWidth(frames, frame::kHook);
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(0.0f, -0.03f), kDown, kHook, kHangableMask, true);
    }
    {   // 9 Rope: link radius 0.03; the two end points get their positions from the rope bodies
        auto& p = t(ItemType::Rope);
        for (std::uint8_t& b : p.bodyFlags) b = static_cast<std::uint8_t>(b & ~body_flags::kGizmos);
        orFlags(p, 0x03);
        p.halfSize = 0.03f;
        p.attachmentCount = 2;
        p.attachments[0] = point(Vec2(0.0f, 0.0f), kRight, kRopeEnd, kRopeMask, true);
        p.attachments[1] = point(Vec2(0.0f, 0.0f), kRight, kRopeEnd, kRopeMask, true);
    }
    {   // 10 / 11 cardboard boxes
        auto& m = t(ItemType::CardboardBoxMedium);
        orFlags(m, 0x43);
        m.halfSize = halfWidth(frames, frame::kCardboardBoxMedium) * 0.9f;
        auto& s = t(ItemType::CardboardBoxSmall);
        orFlags(s, 0x43);
        s.halfSize = halfWidth(frames, frame::kCardboardBoxSmall) * 0.9f;
    }
    {   // 12 FishBowl
        auto& p = t(ItemType::FishBowl);
        p.bodyFlags[0] = static_cast<std::uint8_t>(p.bodyFlags[0] & ~body_flags::kGizmos);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kFishBowl) * 0.95f;
    }
    {   // 13 PiggyBank
        auto& p = t(ItemType::PiggyBank);
        setFlags(p, 0xe4, 0x13);
        p.halfSize = halfWidth(frames, frame::kPiggyBank) * 0.9f;
    }
    {   // 14 BoxingGlove
        auto& p = t(ItemType::BoxingGlove);
        setFlags(p, 0xf4, 0x09);
        p.halfSize = 0.2f;
    }
    {   // 15 Book: the template size comes from the BookBlue frame *height*; the body size from a
        // hard-coded table indexed by colour (items/book.cpp)
        auto& p = t(ItemType::Book);
        orFlags(p, 0x43);
        p.halfSize = halfHeight(frames, frame::kBookBlue);
    }
    {   // 16 EightBall
        auto& p = t(ItemType::EightBall);
        orFlags(p, 0x03);
        p.halfSize = floatFromBits(0x3d67e0f0u);   // ≈ 0.05661
    }
    {   // 17 Pipe: ends at ±(half + 0.0051), aligned snapping (positionOnly = 0)
        auto& p = t(ItemType::Pipe);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = halfWidth(frames, frame::kPipe) * 0.97f;
        p.attachmentCount = 2;
        p.attachments[0] = point(Vec2(p.halfSize + 0.0051f, 0.0f), kRight, kPipeEnd, kPipeMask, false);
        p.attachments[1] = point(Vec2(-p.halfSize - 0.0051f, 0.0f), kLeft, kPipeEnd, kPipeMask, false);
    }
    {   // 18 Pipe90
        auto& p = t(ItemType::Pipe90);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = halfWidth(frames, frame::kPipe90) * 0.97f;
        p.attachmentCount = 2;
        p.attachments[0] = point(Vec2(p.halfSize * 0.44f, p.halfSize * 0.967f), kUp, kPipeEnd, kPipeMask, false);
        p.attachments[1] = point(Vec2(p.halfSize * -0.985f, p.halfSize * -0.42f), kLeft, kPipeEnd, kPipeMask, false);
    }
    {   // 19 Doll
        auto& p = t(ItemType::Doll);
        orFlags(p, 0x4b);
        p.halfSize = 0.15f;
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(-0.06f, -0.01f), kRight, kHangable, kHangableMask, true);
    }
    {   // 20 Skateboard
        auto& p = t(ItemType::Skateboard);
        orFlags(p, 0x0b);
        p.halfSize = halfWidth(frames, frame::kSkateboard);
    }
    {   // 21 Pulley
        auto& p = t(ItemType::Pulley);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = halfWidth(frames, frame::kPulleyWheel);
    }
    {   // 22 Seesaw: full arm width
        auto& p = t(ItemType::Seesaw);
        setFlags(p, 0xfd, 0x01);
        const float half = halfWidth(frames, frame::kSeesawArm);
        p.halfSize = half + half;
    }
    {   // 23 GoalStar
        auto& p = t(ItemType::GoalStar);
        p.bodyFlags[0] = static_cast<std::uint8_t>(p.bodyFlags[0] & ~body_flags::kGizmos);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = halfWidth(frames, frame::kStar01) * 0.91f;
    }
    {   // 24 Billboard
        auto& p = t(ItemType::Billboard);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = 0.2f;
    }
    {   // 25 Magnet
        auto& p = t(ItemType::Magnet);
        setFlags(p, 0xf4, 0x09);
        p.halfSize = halfWidth(frames, frame::kMagnet);
    }
    {   // 26 Pinball (magnetic)
        auto& p = t(ItemType::Pinball);
        orFlags(p, 0x23);
        p.halfSize = floatFromBits(0x3d45c78au);   // ≈ 0.04829
    }
    {   // 27 PaperPlane (magnetic, stabbable, compound update)
        auto& p = t(ItemType::PaperPlane);
        orFlags(p, 0x6b);
        p.halfSize = halfWidth(frames, frame::kPaperPlane);
    }
    {   // 28 Spring: from the Spring frame height
        auto& p = t(ItemType::Spring);
        orFlags(p, 0x03);
        p.halfSize = halfHeight(frames, frame::kSpring);
    }
    {   // 29 Dart (the template also carries a default angle of -π/2, which every level overrides)
        auto& p = t(ItemType::Dart);
        orFlags(p, 0x2b);
        p.halfSize = halfWidth(frames, frame::kDart);
    }
    {   // 30 HangingLamp
        auto& p = t(ItemType::HangingLamp);
        orFlags(p, 0x43);
        p.halfSize = halfWidth(frames, frame::kLampShade);
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(0.0f, 0.2f), kRight, kHangable, kHangableMask, true);
    }
    {   // 31 WorldBound
        auto& p = t(ItemType::WorldBound);
        setFlags(p, 0xbc, 0x41);
        p.halfSize = 0.0f;
    }
    {   // 32 LaundryBasket
        auto& p = t(ItemType::LaundryBasket);
        orFlags(p, 0x03);
        p.halfSize = halfWidth(frames, frame::kLaundryBasketFront);
    }
    {   // 33 Bumper
        auto& p = t(ItemType::Bumper);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = halfWidth(frames, frame::kBumperOff);
    }
    {   // 34 Slingshot: from the SlingshotFrameFront frame height
        auto& p = t(ItemType::Slingshot);
        p.bodyFlags[1] = static_cast<std::uint8_t>(p.bodyFlags[1] & ~body_flags::kGizmos);
        setFlags(p, 0xf4, 0x09);
        p.halfSize = halfHeight(frames, frame::kSlingshotFrameFront);
    }
    {   // 35 RCTruck: half width + hitch offset; hitch point at (-halfSize, -0.02), direction (-1,-1)/√2
        auto& p = t(ItemType::RCTruck);
        orFlags(p, 0x0b);
        p.halfSize = halfWidth(frames, frame::kRCTruck) + floatFromBits(0x3d23ae15u);   // 0.03996094
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(-p.halfSize, -0.02f), normalized(-1.0f, -1.0f), kHangable, kHangableMask, true);
    }
    {   // 36 RCController: bit 7 cleared
        auto& p = t(ItemType::RCController);
        setFlags(p, 0x7f, 0x03);
        p.halfSize = halfWidth(frames, frame::kRCController);
    }
    {   // 37 Trapdoor: 2 · (door half width + shelf half width)
        auto& p = t(ItemType::Trapdoor);
        setFlags(p, 0xfd, 0x01);
        const float sum = halfWidth(frames, frame::kTrapdoorLeft) + halfWidth(frames, frame::kTrapdoorShelfLeft);
        p.halfSize = sum + sum;
    }
    {   // 38 TrapdoorLever: from the lever frame height; bit 7 cleared
        auto& p = t(ItemType::TrapdoorLever);
        setFlags(p, 0x7d, 0x01);
        p.halfSize = halfHeight(frames, frame::kTrapdoorLever);
    }
    {   // 39 Helicopter
        auto& p = t(ItemType::Helicopter);
        orFlags(p, 0x0b);
        p.halfSize = 0.225f;
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(-0.09f, -0.084f), normalized(0.0f, -1.0f), kHangable, kHangableMask, true);
    }
    {   // 40 SelectionArea (editor helper)
        auto& p = t(ItemType::SelectionArea);
        setFlags(p, 0xbd, 0x01);
        p.halfSize = 0.0f;
    }
    {   // 41 BouncyBall
        auto& p = t(ItemType::BouncyBall);
        orFlags(p, 0x03);
        p.halfSize = 0.04f;
    }
    {   // 42 ZipLine: template size stays 1.0; the trolley hook point is on body 2
        auto& p = t(ItemType::ZipLine);
        p.bodyFlags[0] = static_cast<std::uint8_t>(p.bodyFlags[0] & ~body_flags::kGizmos);
        p.bodyFlags[1] = static_cast<std::uint8_t>(p.bodyFlags[1] & ~body_flags::kGizmos);
        setFlags(p, 0xfd, 0x01);
        p.halfSize = 1.0f;
        p.attachmentCount = 1;
        p.attachments[0] = point(Vec2(0.0f, -0.17f), normalized(0.0f, -1.0f), kHangable, kHangableMask, true, 2);
    }
    return table;
}

}  // namespace aa::sim
