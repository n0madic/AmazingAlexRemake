// Collision filters of st::CollisionFiltersUtils::Create (docs/04-physics.md §5), as constants.
#pragma once

#include <Box2D/Dynamics/b2Fixture.h>

namespace aa::sim {

inline b2Filter makeFilter(uint16 category, uint16 mask, int16 group) {
    b2Filter f;
    f.categoryBits = category;
    f.maskBits = mask;
    f.groupIndex = group;
    return f;
}

namespace filters {
// Category bits.
constexpr uint16 kStaticBit = 0x001;
constexpr uint16 kDynamicBit = 0x002;
constexpr uint16 kRopeBit = 0x004;
constexpr uint16 kDebrisBit = 0x008;
constexpr uint16 kSelectionBit = 0x010;    // OR-ed into the category of most dynamic fixtures ("Dynamic+")
constexpr uint16 kPipeFillingBit = 0x040;
constexpr uint16 kToppingBit = 0x080;
constexpr uint16 kReturnAreaBit = 0x100;   // OR-ed into the mask of the box helper

// The nine filters, in the order the original creates them.
inline const b2Filter kStatic = makeFilter(kStaticBit, 0xFFFF, 0);
inline const b2Filter kDynamic = makeFilter(kDynamicBit, 0x0103, 0);
inline const b2Filter kTopping = makeFilter(kToppingBit, 0x0080, 0);
inline const b2Filter kReturnAreaBound = makeFilter(kReturnAreaBit, 0x0100, 0);
inline const b2Filter kRope = makeFilter(kRopeBit, 0x0101, -1);
inline const b2Filter kDebris = makeFilter(kDebrisBit, 0x0001, -6);
inline const b2Filter kSelection = makeFilter(kSelectionBit, 0x0000, 0);
inline const b2Filter kNonCollidable = makeFilter(0, 0, 0);
inline const b2Filter kPipeFilling = makeFilter(kPipeFillingBit, 0x0141, 0);

// Group indices with special meaning (docs/04 §5).
constexpr int16 kGroupSharp = -2;          // scissors blades, helicopter rotor: pops balloons
constexpr int16 kGroupGloveTrigger = -4;   // boxing-glove trigger button
constexpr int16 kGroupGloveStand = -5;     // glove plate / head box / wrist: mutually non-colliding
constexpr int16 kGroupDebris = -6;
constexpr int16 kGroupGoalSensor = -7;     // container interior used by goal type 7
constexpr int16 kGroupDartTip = -8;        // sharp and stabbing
constexpr int16 kGroupBucketInterior = 10; // the bucket's non-collidable interior polygon (BucketUtils::CreatePhysics)

// `Dynamic` with the selection category bit added, as most dynamic bodies use it.
inline b2Filter dynamicPlus() {
    b2Filter f = kDynamic;
    f.categoryBits = static_cast<uint16>(f.categoryBits | kSelectionBit);
    return f;
}
inline b2Filter withGroup(b2Filter f, int16 group) {
    f.groupIndex = group;
    return f;
}
}  // namespace filters

}  // namespace aa::sim
