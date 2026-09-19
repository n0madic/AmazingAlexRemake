// Book (type 15): st::BookUtils::CreatePhysics. The body box comes from a hard-coded (w, h) pixel table
// indexed by the book colour (item state word), not from the sprite frame (docs/03 §15): the table is the
// static initialiser's data at 0x272ef4 of the Android build. Colours 1 and 3 use each other's height —
// reproduced as is. A selection box is added in set-up mode when either half-size is below 0.12 m.
#include "aa/sim/items/items.h"
#include "item_common.h"

namespace aa::sim::items {

namespace {

constexpr float kPixelToMetersLiteral = 0.0033300782f;   // the literal the book code uses (3.41/1024)
constexpr float kWidthFactor = 0.6f;
constexpr float kHeightFactor = 0.95f;
constexpr float kDensity = 25.0f;
constexpr float kFriction = 0.6f;

struct BookSize {
    float w;
    float h;
};
constexpr int kColourCount = 4;
constexpr BookSize kSizeTable[kColourCount] = {{26.0f, 108.0f}, {20.0f, 102.0f}, {18.0f, 88.0f}, {20.0f, 97.0f}};

}  // namespace

void createBook(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    const int colour = item.stateWord;
    if (colour < 0 || colour >= kColourCount) {
        // Index 4 would read the neighbouring constants in the original (docs/03 §15); levels use 0..3.
        throw std::out_of_range("book colour out of range");
    }
    const BookSize size = kSizeTable[colour];
    const float w = size.w * kPixelToMetersLiteral * kWidthFactor;
    const float h = size.h * kPixelToMetersLiteral * kHeightFactor;
    b2BodyDef def;
    def.type = b2_dynamicBody;   // dynamic in both modes
    def.position = obj.position;
    def.angle = obj.angle;
    const int body = addBody(obj, world, def);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    world.addBox(body, hw, hh, fixtureDef(kDensity, kFriction, 0.0f, filters::dynamicPlus()));
    if (mode == PhysicsMode::SetUp && (hw < kMinSelectionRadius || hh < kMinSelectionRadius)) {
        world.addBox(body, kMinSelectionRadius, kMinSelectionRadius, selectionDef());
    }
}

}  // namespace aa::sim::items
