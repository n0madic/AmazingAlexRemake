// Collision filters of docs/04-physics.md §5.
#include "aa/sim/filters.h"

#include <doctest.h>

using namespace aa::sim::filters;

TEST_CASE("filters: the nine filters of CollisionFiltersUtils::Create") {
    struct Row {
        const b2Filter* f;
        uint16 category, mask;
        int16 group;
    };
    const Row rows[] = {
        {&kStatic, 0x001, 0xFFFF, 0},   {&kDynamic, 0x002, 0x0103, 0},   {&kRope, 0x004, 0x0101, -1},
        {&kDebris, 0x008, 0x0001, -6},  {&kSelection, 0x010, 0x0000, 0}, {&kPipeFilling, 0x040, 0x0141, 0},
        {&kTopping, 0x080, 0x0080, 0},  {&kReturnAreaBound, 0x100, 0x0100, 0}, {&kNonCollidable, 0, 0, 0},
    };
    for (const Row& r : rows) {
        CHECK(r.f->categoryBits == r.category);
        CHECK(r.f->maskBits == r.mask);
        CHECK(r.f->groupIndex == r.group);
    }
}

TEST_CASE("filters: Dynamic+ and group helpers") {
    const b2Filter plus = dynamicPlus();
    CHECK(plus.categoryBits == 0x12);
    CHECK(plus.maskBits == 0x103);
    CHECK(plus.groupIndex == 0);
    const b2Filter sensor = withGroup(kDynamic, kGroupGoalSensor);
    CHECK(sensor.categoryBits == 0x2);
    CHECK(sensor.groupIndex == -7);
    CHECK(kGroupSharp == -2);
    CHECK(kGroupDartTip == -8);
}
