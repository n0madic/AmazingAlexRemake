#include "item_parts.h"

#include "aa/sim/items/items.h"

#include <cmath>
#include <vector>

namespace aa::platform::render {

using aa::sim::ItemType;
using aa::sim::RenderItem;
using aa::sim::Vec2;

namespace {

// GameItems frame indices (docs/03 §4).
namespace fr {
constexpr int kEightBall = 0, kBalloon = 1, kBowlingBall = 12, kBouncyBall = 11, kBoxingGlove = 13,
              kGloveAttachment = 14, kGloveButton = 15, kGloveButtonSupport = 16, kGloveHingeBig = 17,
              kGloveHingeSmall = 18, kGlovePlateNE = 19, kGlovePlateNW = 20, kGlovePlunger = 23, kGloveSpring = 24,
              kGloveStand = 25, kBucket = 26, kBucketHandle = 27, kBumperOff = 28, kBumperOn = 29,
              kCardboardBoxMedium = 30, kCardboardBoxSmall = 31, kCoins = 32, kDart = 33, kDollBackArm = 34,
              kDollBackLeg = 35, kDollBody = 36, kDollDress = 37, kDollFrontArm = 38, kDollFrontLeg = 39,
              kDollHead = 40, kFish = 41, kFishBowl = 42, kFishBowlHighlight = 43, kHelicopter = 45,
              kHelicopterRotor01 = 46, kHelicopterTailRotor = 56, kHook = 57, kLampShade = 59, kLaundryBasketBack = 60,
              kLaundryBasketFront = 61, kMagnet = 62, kBook = 7, kPiggyBank = 74, kPiggyBankPiece1 = 75,
              kPaperPlane = 73, kPinball = 78, kPipe = 79, kPipe90 = 80, kPipe90Back = 81, kPipe90Bracket = 82, kPipeBack = 83,
              kPipeBracketLeft = 84, kPipeBracketRight = 85, kPulleyClevis = 86, kPulleyScrew = 87, kPulleyWheel = 88,
              kRCAntenna = 89, kRCController = 90, kRCControllerButton = 91, kRCHelicopterController = 92,
              kRCHitch = 93, kRCTruck = 94, kRCTruckWheel = 95, kRopeDollFront = 96, kRopeKnot = 97,
              kRopeSegment = 98, kScissorsBottom = 106, kScissorsTop = 107, kSeesawArm = 108,
              kSeesawFulcrum = 109, kBillboardShelf = 110, kShelfBottom = 111, kShelfTop = 112, kSkateboard = 113,
              kSkateboardWheel = 114, kSlingshotElasticBack = 115, kSlingshotElasticFront = 116,
              kSlingshotFrameBack = 117, kSlingshotFrameFront = 118, kSlingshotPocketBack = 119,
              kSlingshotPocketFront = 120, kSoccerBall = 121, kSpring = 123, kSpringSeat = 124, kStar01 = 125,
              kStarGlow = 137, kTennisBall = 138, kTrapdoorLeft = 140, kTrapdoorLever = 141,
              kTrapdoorLeverBase = 142, kTrapdoorRight = 143, kTrapdoorShelfLeft = 144, kTrapdoorShelfRight = 145,
              kZipLineAttachment = 147, kZipLineSegment = 148, kZipLineTrolley = 149, kBillboardBook = 6,
              kBalloonPop1 = 2, kPow1 = 69, kMagnetPulse1 = 63, kScissorsCut1 = 101, kSparkle03 = 122, kWave = 146;
}  // namespace fr

constexpr float kPopTime = 0.15f;
constexpr float kPowTime = 0.2f;
constexpr int kRotorFrames = 10;

constexpr float kPi = 0.017453289f * 180.0f;   // the game's Pi (docs/04)

Vec2 rotateVec(float angle, Vec2 v) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return Vec2(c * v.x - s * v.y, c * v.y + s * v.x);
}

// b2MulT(R0, p_k - p_0): body k's position in body 0's frame (the item frame).
Vec2 bodyOffset(const RenderItem& item, int k) {
    const aa::sim::RenderBody& b0 = item.bodies[0];
    const aa::sim::RenderBody& bk = item.bodies[static_cast<std::size_t>(k)];
    const float dx = bk.position.x - b0.position.x;
    const float dy = bk.position.y - b0.position.y;
    const float c = std::cos(b0.angle);
    const float s = std::sin(b0.angle);
    return Vec2(c * dx + s * dy, c * dy - s * dx);
}

// (a_k - a_item) · s: the rotation the centred helper FUN_000bc080 receives for a body part.
float bodyRotation(const RenderItem& item, int k) {
    return (item.bodies[static_cast<std::size_t>(k)].angle - item.angle) * item.flipSign();
}

float rawBodyRotation(const RenderItem& item, int k) { return item.bodies[static_cast<std::size_t>(k)].angle - item.angle; }

bool hasBody(const RenderItem& item, int k) { return k < item.bodyCount; }

const Atlas& items(ItemContext& ctx) { return ctx.atlases.gameItems; }

void single(ItemContext& ctx, int frame, Vec2 pos = Vec2(0.0f, 0.0f)) { ctx.batch.centered(items(ctx), frame, pos); }

// --- compound items -----------------------------------------------------------------------------------

void scissors(const RenderItem& item, int layer, ItemContext& ctx) {
    // FUN_000bcbc8: each half at its body position brought into the item frame by Rotate(-angle, p - pos).
    const int body = layer == 1 ? 1 : 0;
    if (!hasBody(item, body)) return;
    const Vec2 p = item.bodies[static_cast<std::size_t>(body)].position;
    const Vec2 local = rotateVec(-item.angle, Vec2(p.x - item.position.x, p.y - item.position.y));
    single(ctx, layer == 1 ? fr::kScissorsBottom : fr::kScissorsTop, local);
    if (layer != 0 || item.snipStep < 0) return;
    // The idle snip (docs/11 §5, FUN_000bcbc8): Scissors01..05 anchored at (23, 13) px on the top half,
    // rotated by the cut angle; while the snip runs, Sparkle03 at the tip scaled by the snip phase.
    if (!item.snipping) {
        const Vec2 off = rotateVec(item.cutAngle, Vec2(0.016f, 0.06f));
        QuadParams q;
        q.pos = Vec2(local.x + off.x, local.y + off.y);
        q.anchorPx = Vec2(23.0f, 13.0f);
        q.rotation = item.cutAngle;
        ctx.batch.anchored(items(ctx), fr::kScissorsCut1 + item.snipStep, q);
    } else {
        const Vec2 tip = rotateVec(item.cutAngle, Vec2(item.halfSize - 0.005f, -0.01f));
        ctx.batch.centered(items(ctx), fr::kSparkle03, tip, Vec2(item.snipPhase, item.snipPhase));
    }
}

void piggyBank(const RenderItem& item, ItemContext& ctx) {
    if ((item.state & aa::sim::object_state::kActivated) == 0 || item.bodyCount < 5) {
        single(ctx, fr::kPiggyBank);
        return;
    }
    // Broken: coins and the three pieces at their raw world offsets from body 0, pieces rotated by their
    // raw body angle (M4 adds the POW frames of the 0.2 s timer).
    const Vec2 p0 = item.bodies[0].position;
    const Vec2 p1 = item.bodies[1].position;
    single(ctx, fr::kCoins, Vec2(p1.x - p0.x, p1.y - p0.y));
    for (int k = 2; k <= 4; ++k) {
        const Vec2 pk = item.bodies[static_cast<std::size_t>(k)].position;
        ctx.batch.centeredRotated(items(ctx), fr::kPiggyBankPiece1 + (k - 2), Vec2(pk.x - p0.x, pk.y - p0.y),
                                  item.bodies[static_cast<std::size_t>(k)].angle);
    }
    // POW1..4 for the first 0.2 s after the break (PiggyBank+8).
    if (item.piggyTimer < kPowTime) {
        int pow = static_cast<int>(item.piggyTimer / kPowTime * 4.0f);
        if (pow < 0) pow = 0;
        if (pow > 3) pow = 3;
        single(ctx, fr::kPow1 + pow);
    }
}

void boxingGlove(const RenderItem& item, ItemContext& ctx) {
    if (item.bodyCount < 2) return;
    const Atlas& a = items(ctx);
    constexpr float kLattice = 0.2973f;
    const Vec2 b1 = bodyOffset(item, 1);
    const Vec2 arm(b1.x * item.scale.x + 0.2f, b1.y * item.scale.y + 0.0f);
    single(ctx, fr::kGloveAttachment, Vec2(arm.x - 0.16f, arm.y));
    single(ctx, fr::kBoxingGlove, Vec2(arm.x - 0.03f, arm.y));
    const float theta = item.latticeAngle;
    const float lx = std::sin(theta) * kLattice * 0.5f;
    const float c = 0.01f + std::cos(theta) * kLattice;
    const float ly = (0.03f + c * 0.5f) - 0.2f;
    const float hy = static_cast<float>(static_cast<double>(ly) + 0.03);
    // Scissor lattice: PlateNE at ±θ from each hinge, PlateNW mirrored (the Pi ∓ θ copies point down).
    for (int n = 1; n <= 3; n += 2) {
        const Vec2 p(lx * static_cast<float>(n), hy);
        ctx.batch.bottomAnchored(a, fr::kGlovePlateNE, p, -theta);
        ctx.batch.bottomAnchored(a, fr::kGlovePlateNE, p, theta - kPi);
    }
    for (int n = 1; n <= 5; n += 2) {
        const Vec2 p(lx * static_cast<float>(n), hy);
        ctx.batch.bottomAnchored(a, fr::kGlovePlateNW, p, theta);
        ctx.batch.bottomAnchored(a, fr::kGlovePlateNW, p, kPi - theta);
    }
    ctx.batch.bottomAnchored(a, fr::kGloveStand, Vec2(0.0f, -0.2f));
    ctx.batch.bottomAnchoredStretched(a, fr::kGloveSpring, Vec2(0.0f, -0.12f), c - 0.04f);
    ctx.batch.bottomAnchoredStretched(a, fr::kGlovePlunger, Vec2(0.0f, ((c + 0.03f) + 0.045f) - 0.2f), 0.35f - c);
    ctx.batch.bottomAnchoredStretched(a, fr::kGloveButton, Vec2(0.0f, 0.2f), ctx.batch.k() * item.buttonHeightPx);
    ctx.batch.bottomAnchored(a, fr::kGloveButtonSupport, Vec2(0.0f, 0.19f));
    const float hingeTop = (c + 0.03f) - 0.2f;
    for (int n = 0; n <= 4; n += 2) {
        const float x = lx * static_cast<float>(n);
        ctx.batch.bottomAnchored(a, fr::kGloveHingeSmall, Vec2(x, -0.17f));
        ctx.batch.bottomAnchored(a, fr::kGloveHingeSmall, Vec2(x, hingeTop));
    }
    for (int n = 1; n <= 5; n += 2) ctx.batch.bottomAnchored(a, fr::kGloveHingeBig, Vec2(lx * static_cast<float>(n), ly));
}

void doll(const RenderItem& item, ItemContext& ctx) {
    if (item.bodyCount < 6) return;
    auto limb = [&](int frame, int k) {
        const Vec2 b = bodyOffset(item, k);
        ctx.batch.centeredRotated(items(ctx), frame, Vec2(b.x * item.scale.x, b.y * item.scale.y), bodyRotation(item, k));
    };
    limb(fr::kDollBackArm, 3);
    limb(fr::kDollBackLeg, 5);
    single(ctx, fr::kDollBody);
    limb(fr::kDollFrontLeg, 4);
    single(ctx, fr::kDollDress);
    if (item.attachmentCount > 0 && item.attachments[0].state != aa::sim::attachment_state::kFree) {
        ctx.batch.centeredRotated(items(ctx), fr::kRopeDollFront, Vec2(0.01f, -0.04f), 0.0f);
    }
    limb(fr::kDollFrontArm, 2);
    limb(fr::kDollHead, 1);
}

void skateboard(const RenderItem& item, ItemContext& ctx) {
    single(ctx, fr::kSkateboard);
    for (int k = 1; k <= 2; ++k) {
        if (!hasBody(item, k)) continue;
        ctx.batch.centeredRotated(items(ctx), fr::kSkateboardWheel, bodyOffset(item, k), bodyRotation(item, k));
    }
}

void pulley(int layer, ItemContext& ctx) {
    if (layer == 1) {
        single(ctx, fr::kPulleyScrew, Vec2(0.0f, 0.152f));
        return;
    }
    single(ctx, fr::kPulleyWheel);
    ctx.batch.bottomAnchored(items(ctx), fr::kPulleyClevis, Vec2(0.0f, -0.036f));
}

void seesaw(const RenderItem& item, ItemContext& ctx) {
    if (item.bodyCount < 2) return;
    // The arm frame is the right half: drawn from the pivot rightwards and mirrored leftwards. The
    // original passes body 1's metre offset as the pixel anchor (≈ (0, 0) px) — reproduced as is.
    QuadParams q;
    q.anchorPx = bodyOffset(item, 1);
    q.pos = Vec2(0.0f, -0.02f);
    q.rotation = rawBodyRotation(item, 1);
    ctx.batch.anchored(items(ctx), fr::kSeesawArm, q);
    q.scale = Vec2(-1.0f, 1.0f);
    ctx.batch.anchored(items(ctx), fr::kSeesawArm, q);
    single(ctx, fr::kSeesawFulcrum);
}

void spring(const RenderItem& item, ItemContext& ctx) {
    if (item.bodyCount < 3) return;
    const Atlas& a = items(ctx);
    const aa::sim::RenderBody& base = item.bodies[1];
    const aa::sim::RenderBody& seat = item.bodies[2];
    const aa::sim::RenderBody& b0 = item.bodies[0];
    auto endPoint = [&](const aa::sim::RenderBody& b, float along) {
        // p + R_b · (0, along), into body 0's frame
        const float dx = (b.position.x - std::sin(b.angle) * along) - b0.position.x;
        const float dy = (b.position.y + std::cos(b.angle) * along) - b0.position.y;
        const float c = std::cos(b0.angle);
        const float s = std::sin(b0.angle);
        return Vec2(c * dx + s * dy, c * dy - s * dx);
    };
    const Vec2 seatPt = endPoint(seat, 0.015f);
    const Vec2 basePt = endPoint(base, -0.015f);
    const float dx = seatPt.x - basePt.x;
    const float dy = seatPt.y - basePt.y;
    const float lengthPx = std::sqrt(dy * dy + dx * dx) / ctx.batch.k();
    const aa::sim::Frame& f = a.frame(fr::kSpring);
    const float frameH = std::fabs(f.y1 - f.y0);
    if (lengthPx <= frameH) {
        // Compressed: the visible band of the sprite around its centre.
        const float cy = (f.y0 + f.y1) * 0.5f;
        ctx.batch.centeredSrcRect(a, f.x0, f.x1, cy - lengthPx * 0.5f, cy + lengthPx * 0.5f, Vec2(0.0f, 0.0f));
    } else {
        ctx.batch.centered(a, fr::kSpring, Vec2(0.0f, 0.0f), Vec2(1.0f, lengthPx / frameH));
    }
    ctx.batch.centered(a, fr::kSpringSeat, seatPt);
    ctx.batch.centered(a, fr::kSpringSeat, basePt, Vec2(1.0f, -1.0f));
}

void slingshot(const RenderItem& item, int layer, ItemContext& ctx) {
    // FUN_000bc904: the pouch (Slingshot+0xC) is item-local; the elastic runs from the pouch to fixed
    // frame points, the pockets rotate towards (0, 0.076).
    const Atlas& a = items(ctx);
    const Vec2 pouch = item.endVector;
    const float dir = static_cast<float>(std::atan2(static_cast<double>(0.076f - pouch.y), static_cast<double>(0.0f - pouch.x)));
    if (layer == 1) {
        const Vec2 off = rotateVec(dir, Vec2(0.02f, 0.03f));
        ctx.batch.stretchedBetween(a, fr::kSlingshotElasticBack, Vec2(pouch.x + off.x, pouch.y + off.y), Vec2(0.04f, 0.091f));
        single(ctx, fr::kSlingshotFrameBack, Vec2(0.04f, 0.08f));
        const Vec2 pocket = rotateVec(dir, Vec2(0.02f, 0.02f));
        ctx.batch.centeredRotated(a, fr::kSlingshotPocketBack, Vec2(pouch.x + pocket.x, pouch.y + pocket.y), dir);
        return;
    }
    ctx.batch.stretchedBetween(a, fr::kSlingshotElasticFront, pouch, Vec2(-0.02f, 0.056f));
    ctx.batch.centeredRotated(a, fr::kSlingshotPocketFront, pouch, dir);
    single(ctx, fr::kSlingshotFrameFront);
}

// The RC signal waves (docs/11 §3 step 7, §5): frame 146 twice, mirrored, at the antenna ± x, scaled
// (scale, 0.34) for the controller / (scale, 0.29) for the truck, colour = alpha.
void waves(const RenderItem& item, Vec2 antenna, float heightScale, ItemContext& ctx) {
    for (const aa::sim::RenderWave& w : item.waves) {
        SpriteBatch::setColor(1.0f, 1.0f, 1.0f, w.alpha);
        ctx.batch.centered(items(ctx), fr::kWave, Vec2(antenna.x + w.x, antenna.y), Vec2(w.scale, heightScale));
        ctx.batch.centered(items(ctx), fr::kWave, Vec2(antenna.x - w.x, antenna.y), Vec2(-w.scale, heightScale));
    }
    SpriteBatch::setColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void rcTruck(const RenderItem& item, int layer, ItemContext& ctx) {
    const Atlas& a = items(ctx);
    if (layer == 1) {
        single(ctx, fr::kRCHitch, Vec2(-0.354f, -0.005f));
        return;
    }
    if (item.builtInController && hasBody(item, 3)) {   // legacy built-in controller (Truck+8)
        const Vec2 b3 = bodyOffset(item, 3);
        single(ctx, fr::kRCControllerButton, Vec2(b3.x + 0.0f, b3.y + 0.072f));
        single(ctx, fr::kRCController, Vec2(b3.x + 0.0f, b3.y + 0.15f));
    }
    ctx.batch.bottomAnchored(a, fr::kRCAntenna, Vec2(-0.26f, 0.1f));
    single(ctx, fr::kRCTruck, Vec2(0.0f, 0.04f));
    waves(item, Vec2(-0.26f, 0.32f), 0.29f, ctx);
    for (int k = 1; k <= 2; ++k) {
        if (!hasBody(item, k)) continue;
        ctx.batch.centeredRotated(a, fr::kRCTruckWheel, bodyOffset(item, k), bodyRotation(item, k));
    }
}

void rcController(const RenderItem& item, ItemContext& ctx) {
    if (hasBody(item, 1)) single(ctx, fr::kRCControllerButton, bodyOffset(item, 1));
    const int pairedType = static_cast<int>(static_cast<std::uint32_t>(item.stateWord) >> aa::sim::Handle::kTypeShift);
    // Any other paired type selects frame 150, which the atlas does not have: nothing is drawn then.
    if (pairedType == static_cast<int>(ItemType::RCTruck)) single(ctx, fr::kRCController, Vec2(0.0f, 0.15f));
    else if (pairedType == static_cast<int>(ItemType::Helicopter)) single(ctx, fr::kRCHelicopterController, Vec2(0.0f, 0.15f));
    waves(item, Vec2(0.0f, 0.3f), 0.34f, ctx);
}

void trapdoor(const RenderItem& item, ItemContext& ctx) {
    const Atlas& a = items(ctx);
    if (item.builtInController && hasBody(item, 3)) {   // legacy built-in lever (Trapdoor+8)
        const Vec2 b3 = bodyOffset(item, 3);
        ctx.batch.bottomAnchored(a, fr::kTrapdoorLever, Vec2(b3.x - 0.01f, b3.y));
        single(ctx, fr::kTrapdoorLeverBase, b3);
    }
    if (item.bodyCount >= 3) {
        const aa::sim::Frame& left = a.frame(fr::kTrapdoorLeft);
        const aa::sim::Frame& right = a.frame(fr::kTrapdoorRight);
        QuadParams q;
        q.anchorPx = Vec2(3.0f, std::fabs(left.y1 - left.y0) * 0.6f);
        q.pos = bodyOffset(item, 1);
        q.rotation = rawBodyRotation(item, 1);
        ctx.batch.anchored(a, fr::kTrapdoorLeft, q);
        q.anchorPx = Vec2(std::fabs(right.x1 - right.x0) - 4.0f, std::fabs(right.y1 - right.y0) * 0.6f);
        q.pos = bodyOffset(item, 2);
        q.rotation = rawBodyRotation(item, 2);
        ctx.batch.anchored(a, fr::kTrapdoorRight, q);
    }
    single(ctx, fr::kTrapdoorShelfLeft, Vec2(item.halfSize * -0.8f, 0.0f));
    single(ctx, fr::kTrapdoorShelfRight, Vec2(item.halfSize * 0.8f, 0.0f));
}

void trapdoorLever(const RenderItem& item, ItemContext& ctx) {
    const float rot = hasBody(item, 1) ? rawBodyRotation(item, 1) : 0.0f;
    ctx.batch.bottomAnchored(items(ctx), fr::kTrapdoorLever, Vec2(-0.01f, 0.0f), rot);
    single(ctx, fr::kTrapdoorLeverBase);
}

void helicopter(const RenderItem& item, ItemContext& ctx) {
    const float r = item.halfSize;
    if (item.builtInController && hasBody(item, 1)) {   // legacy built-in controller (Helicopter+8)
        const Vec2 b1 = bodyOffset(item, 1);
        single(ctx, fr::kRCControllerButton, Vec2(b1.x + 0.0f, b1.y + 0.072f));
        single(ctx, fr::kRCHelicopterController, Vec2(b1.x + 0.0f, b1.y + 0.15f));
    }
    // Rotor phases (Helicopter+0x20/+0x28): the tail rotor rotated by the tail phase, the rotor frame
    // 46 + (int(phase·10) mod 10).
    ctx.batch.centeredRotated(items(ctx), fr::kHelicopterTailRotor, Vec2(r * 0.9f, (r * 0.25f) * 0.6f), item.tailPhase);
    single(ctx, fr::kHelicopter);
    int rotor = static_cast<int>(item.rotorPhase * 10.0f) % kRotorFrames;
    if (rotor < 0) rotor += kRotorFrames;
    single(ctx, fr::kHelicopterRotor01 + rotor, Vec2(r * -0.34f, r * 0.51f));
}

void zipLineLocal(const RenderItem& item, ItemContext& ctx) {
    // FUN_000bc478 layer 0: raw body angles (the item angle of a zip line is 0).
    if (item.bodyCount < 3) return;
    const Atlas& a = items(ctx);
    const float lineAngle = item.bodies[1].angle;
    const float trolleyAngle = item.bodies[2].angle;
    ctx.batch.centeredRotated(a, fr::kZipLineAttachment, rotateVec(lineAngle, Vec2(-0.06f, 0.0f)), lineAngle);
    const Vec2 farOff = rotateVec(lineAngle, Vec2(0.06f, 0.0f));
    ctx.batch.centeredRotated(a, fr::kZipLineAttachment, Vec2(item.endVector.x + farOff.x, item.endVector.y + farOff.y),
                              lineAngle, Vec2(-1.0f, 1.0f));
    const Vec2 b2 = bodyOffset(item, 2);
    const Vec2 b2r = rotateVec(lineAngle, rotateVec(-lineAngle, b2));   // the original's round trip
    const Vec2 hang = rotateVec(trolleyAngle, Vec2(0.0f, -0.044f));
    ctx.batch.centeredRotated(a, fr::kZipLineTrolley, Vec2(b2r.x + hang.x, b2r.y + hang.y), trolleyAngle);
    // ZipLine+0x4c (something hangs on the trolley) is M3: frame 99 RopeZipLine at b2 + rot(trolley, (0, -0.13)).
}

}  // namespace

Vec2 partOffset(const RenderItem& item) {
    return item.type == ItemType::BoxingGlove ? Vec2(-0.2f, 0.0f) : Vec2(0.0f, 0.0f);
}

void drawWorldParts(const RenderItem& item, int layer, ItemContext& ctx) {
    const Atlas& a = items(ctx);
    if (item.type == ItemType::Rope && layer == 0 && item.bodyCount >= 3) {
        // FUN_000bd4f0 case 9: the strip along the chain bodies, one RopeSegment per link pair; a cut rope
        // skips the segments whose distance joint is gone (RopeRenderUtils::AddIndices, `ropeSegments`).
        std::vector<int> order(static_cast<std::size_t>(item.bodyCount - 1));
        aa::sim::items::ropeBodyIndices(item.bodyCount, order.data());
        std::vector<Vec2> points(order.size());
        for (std::size_t i = 0; i < order.size(); ++i) points[i] = item.bodies[static_cast<std::size_t>(order[i])].position;
        const aa::sim::Frame& f = a.frame(fr::kRopeSegment);
        ctx.batch.strip(a, fr::kRopeSegment, points.data(), static_cast<int>(points.size()),
                        std::fabs(f.x1 - f.x0) * 0.5f * ctx.batch.k(), item.ropeSegments.data());
    } else if (item.type == ItemType::ZipLine && layer == 1) {
        // FUN_000bc478 layer 1: the line from the item position along the end vector, segment count
        // length / (k · frame height) + 1, one segment texture per piece.
        const Vec2 end = item.endVector;
        const float len = std::sqrt(end.y * end.y + end.x * end.x);
        constexpr float kMinLength = 0.0001f;
        if (len < kMinLength) return;
        const aa::sim::Frame& f = a.frame(fr::kZipLineSegment);
        const int n = static_cast<int>(len / (ctx.batch.k() * std::fabs(f.y1 - f.y0)) + 1.0f);
        const float step = len / static_cast<float>(n);
        const Vec2 dir(end.x / len, end.y / len);
        std::vector<Vec2> points(static_cast<std::size_t>(n + 1));
        for (int i = 0; i < n; ++i) {
            const float t = step * static_cast<float>(i);
            points[static_cast<std::size_t>(i)] = Vec2(item.position.x + t * dir.x, item.position.y + t * dir.y);
        }
        points[static_cast<std::size_t>(n)] = Vec2(item.position.x + end.x, item.position.y + end.y);
        ctx.batch.strip(a, fr::kZipLineSegment, points.data(), n + 1, std::fabs(f.x1 - f.x0) * 0.5f * ctx.batch.k());
    }
}

void drawLocalParts(const RenderItem& item, int layer, ItemContext& ctx) {
    const float r = item.halfSize;
    if (layer == 1) {
        switch (item.type) {
        case ItemType::Shelf: single(ctx, fr::kShelfBottom, Vec2(0.0f, -(r * 0.2f))); break;
        case ItemType::Scissors: scissors(item, 1, ctx); break;
        case ItemType::Bucket: single(ctx, fr::kBucketHandle, Vec2(-0.01f, r)); break;
        case ItemType::Pipe:
            single(ctx, fr::kPipeBracketLeft, Vec2(-0.16f, 0.0f));
            single(ctx, fr::kPipeBracketRight, Vec2(0.17f, 0.0f));
            single(ctx, fr::kPipeBack);
            break;
        case ItemType::Pipe90:
            single(ctx, fr::kPipe90Back);
            single(ctx, fr::kPipe90Bracket);
            break;
        case ItemType::Pulley: pulley(1, ctx); break;
        case ItemType::Billboard: {
            const int hint = item.stateWord & 0xF;
            if (hint == 2) single(ctx, fr::kBillboardShelf);
            else if (hint == 3) single(ctx, fr::kBillboardBook);
            break;
        }
        case ItemType::LaundryBasket: single(ctx, fr::kLaundryBasketBack); break;
        case ItemType::Slingshot: slingshot(item, 1, ctx); break;
        case ItemType::RCTruck: rcTruck(item, 1, ctx); break;
        default: break;   // ZipLine layer 1 is the world-space strip
        }
        return;
    }
    switch (item.type) {
    case ItemType::Shelf: single(ctx, fr::kShelfTop); break;
    case ItemType::TennisBall: single(ctx, fr::kTennisBall); break;
    case ItemType::BowlingBall: single(ctx, fr::kBowlingBall); break;
    case ItemType::SoccerBall: single(ctx, fr::kSoccerBall); break;
    case ItemType::Balloon: {
        // Popping: frames 2..5 by (0.15 − t) / 0.15 · 4 (docs/11 §5).
        int frame = fr::kBalloon;
        if (item.popped) {
            int pop = static_cast<int>((kPopTime - item.popTimer) / kPopTime * 4.0f);
            if (pop < 0) pop = 0;
            if (pop > 3) pop = 3;
            frame = fr::kBalloonPop1 + pop;
        }
        single(ctx, frame, Vec2(0.0f, -0.07f));
        break;
    }
    case ItemType::Scissors: scissors(item, 0, ctx); break;
    case ItemType::Bucket: single(ctx, fr::kBucket); break;
    case ItemType::Hook: single(ctx, fr::kHook); break;
    case ItemType::CardboardBoxMedium: single(ctx, fr::kCardboardBoxMedium); break;
    case ItemType::CardboardBoxSmall: single(ctx, fr::kCardboardBoxSmall); break;
    case ItemType::FishBowl:
        single(ctx, fr::kFishBowl);
        single(ctx, fr::kFish, Vec2(0.0f, -0.02f));
        single(ctx, fr::kFishBowlHighlight, Vec2(0.09f, 0.01f));
        break;
    case ItemType::PiggyBank: piggyBank(item, ctx); break;
    case ItemType::BoxingGlove: boxingGlove(item, ctx); break;
    case ItemType::Book: single(ctx, fr::kBook + item.stateWord); break;
    case ItemType::EightBall: single(ctx, fr::kEightBall); break;
    case ItemType::Pipe: single(ctx, fr::kPipe); break;
    case ItemType::Pipe90: single(ctx, fr::kPipe90); break;
    case ItemType::Doll: doll(item, ctx); break;
    case ItemType::Skateboard: skateboard(item, ctx); break;
    case ItemType::Pulley: pulley(0, ctx); break;
    case ItemType::Seesaw: seesaw(item, ctx); break;
    case ItemType::GoalStar:
        single(ctx, fr::kStarGlow);
        single(ctx, fr::kStar01 + item.starFrame);   // the spin (VisualWorldState stars, docs/11 §5)
        break;
    case ItemType::Magnet:
        single(ctx, fr::kMagnet);
        if (item.magnetPulling) single(ctx, fr::kMagnetPulse1 + item.magnetFrame, Vec2(0.0f, 0.19f));
        break;
    case ItemType::Pinball: single(ctx, fr::kPinball); break;
    case ItemType::PaperPlane: single(ctx, fr::kPaperPlane); break;
    case ItemType::Spring: spring(item, ctx); break;
    case ItemType::Dart: single(ctx, fr::kDart, Vec2(0.0f, 0.007f)); break;   // stuck sparkle: M4
    case ItemType::HangingLamp: single(ctx, fr::kLampShade); break;
    case ItemType::LaundryBasket: single(ctx, fr::kLaundryBasketFront); break;
    case ItemType::Bumper: single(ctx, item.bumperOn == 0 ? fr::kBumperOff : fr::kBumperOn); break;
    case ItemType::Slingshot: slingshot(item, 0, ctx); break;
    case ItemType::RCTruck: rcTruck(item, 0, ctx); break;
    case ItemType::RCController: rcController(item, ctx); break;
    case ItemType::Trapdoor: trapdoor(item, ctx); break;
    case ItemType::TrapdoorLever: trapdoorLever(item, ctx); break;
    case ItemType::Helicopter: helicopter(item, ctx); break;
    case ItemType::BouncyBall: single(ctx, fr::kBouncyBall); break;
    case ItemType::ZipLine: zipLineLocal(item, ctx); break;
    default: break;   // WorldBound, Billboard, SelectionArea: nothing in layer 0
    }
}

void drawKnots(const RenderItem& item, ItemContext& ctx) {
    if (item.attachmentCount <= 0) return;
    if (item.type == ItemType::Rope || item.type == ItemType::Doll || item.type == ItemType::ZipLine) return;
    for (int k = 0; k < item.attachmentCount; ++k) {
        const aa::sim::AttachmentRecord& rec = item.attachments[static_cast<std::size_t>(k)];
        if (rec.state == aa::sim::attachment_state::kFree || rec.point.kind == aa::sim::attachment_kind::kPipeEnd) continue;
        Vec2 pos(rec.point.pos.x + 0.0f, rec.point.pos.y - 0.03f);
        if (rec.point.body > 0 && hasBody(item, rec.point.body)) {
            const Vec2 off = bodyOffset(item, rec.point.body);
            pos = Vec2(pos.x + off.x, pos.y + off.y);
        }
        ctx.batch.centeredRotated(items(ctx), fr::kRopeKnot, pos, 0.0f);
    }
}

}  // namespace aa::platform::render
