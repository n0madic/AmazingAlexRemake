#include "aa/sim/types.h"

namespace aa::sim {

const char* itemTypeName(ItemType type) {
    static const char* const kNames[kItemTypeCount] = {
        "?",            "Shelf",         "TennisBall",  "BowlingBall",    "SoccerBall",         "Balloon",
        "Scissors",     "Bucket",        "Hook",        "Rope",           "CardboardBoxMedium", "CardboardBoxSmall",
        "FishBowl",     "PiggyBank",     "BoxingGlove", "Book",           "EightBall",          "Pipe",
        "Pipe90",       "Doll",          "Skateboard",  "Pulley",         "Seesaw",             "GoalStar",
        "Billboard",    "Magnet",        "Pinball",     "PaperPlane",     "Spring",             "Dart",
        "HangingLamp",  "WorldBound",    "LaundryBasket", "Bumper",       "Slingshot",          "RCTruck",
        "RCController", "Trapdoor",      "TrapdoorLever", "Helicopter",   "SelectionArea",      "BouncyBall",
        "ZipLine",
    };
    const int i = static_cast<int>(type);
    return (i >= 0 && i < kItemTypeCount) ? kNames[i] : "?";
}

}  // namespace aa::sim
