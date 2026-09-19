// st::GameItemUtils::HandleCollisionSounds — the per-type impact sound of WorldContactListener::PreSolve
// [verified: decompile + disassembly (jump table at 0xa909c)].
#include "aa/sim/animations.h"
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {
constexpr float kEpsilon = 0.0001f;   // st::Epsilon (the volume floor)
constexpr int kMaxSameSound = 9;      // more than nine of one sound already queued → dropped
constexpr int kBouncyBallSounds = 0x39;

// Sound id and volume factor per type; id 0 = no impact sound.
struct ImpactSound {
    int id;
    float factor;
};

ImpactSound impactSound(ItemType type) {
    switch (type) {
    case ItemType::TennisBall: return {0x10, 1.0f};
    case ItemType::BowlingBall: return {0x11, 1.0f};
    case ItemType::SoccerBall: return {0x0f, 1.0f};
    case ItemType::Bucket: return {0x15, 0.5f};
    case ItemType::CardboardBoxMedium: return {0x18, 0.6f};
    case ItemType::CardboardBoxSmall: return {0x18, 0.4f};
    case ItemType::Book: return {0x14, 0.7f};
    case ItemType::EightBall: return {0x12, 1.0f};
    case ItemType::Skateboard: return {0x32, 0.2f};
    case ItemType::Pinball: return {0x13, 1.0f};
    case ItemType::PaperPlane: return {0x1a, 1.0f};
    case ItemType::HangingLamp: return {0x1c, 0.4f};
    case ItemType::LaundryBasket: return {0x19, 0.1f};
    case ItemType::RCController: return {0x2d, 0.4f};
    default: return {0, 1.0f};
    }
}
}  // namespace

void handleCollisionSounds(const GameItem& item, const PhysicsObject& obj, int bodyIndex, float impact, ActionQueue& queue,
                           const PhysicsWorld& world, int timeSeed) {
    if (obj.type == ItemType::Doll) {
        dollHandleCollisionSounds(obj, bodyIndex, impact, queue);
        return;
    }
    if (obj.type == ItemType::BoxingGlove) {
        gloveHandleCollisionSounds(item, obj, bodyIndex, impact, queue, world);
        return;
    }
    // Volume: impact / 5 clamped to [0.1, 1], then the type's factor.
    float v = impact / 5.0f;
    if (1.0f - v < 0.0f) v = 1.0f;
    if (v - 0.1f < 0.0f) v = 0.1f;
    int id;
    switch (obj.type) {
    case ItemType::RCTruck:
    case ItemType::Helicopter: {
        // Truck 0x1b, helicopter 0x37: volume (impact − 1) / 3 clamped to [0, 1].
        id = obj.type == ItemType::RCTruck ? 0x1b : 0x37;
        const float t = (impact - 1.0f) / 3.0f;
        v = 1.0f;
        if (0.0f <= 1.0f - t) v = t;
        if (t < 0.0f) v = 0.0f;
        break;
    }
    case ItemType::BouncyBall: {
        // One of three sounds chosen by a Random seeded with the wall clock (TimeUtils::GetAbsoluteTime).
        Random rnd;
        rnd.setSeed(timeSeed);
        id = kBouncyBallSounds + rnd.getInt(0, 2);
        break;
    }
    default: {
        const ImpactSound s = impactSound(obj.type);
        if (s.id == 0) return;
        id = s.id;
        if (s.factor != 1.0f) v = v * s.factor;
        break;
    }
    }
    if (!(kEpsilon <= v)) return;
    // Already a sound for this object this pass, or ten of the same sound: dropped.
    int same = 0;
    for (const Action& a : queue.actions) {
        if (a.id == action::kPlaySound && a.handle == obj.handle) return;
        if (a.payload == id) {
            ++same;
            if (same > kMaxSameSound) return;
        }
    }
    queue.add(Action::soundOf(obj.handle, id, obj.position, v));
}

}  // namespace aa::sim::items
