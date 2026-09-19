// st::PhysicsObjectUtils::CreatePhysics — the per-type dispatch (docs/10-architecture.md §5.3).
#include "aa/sim/items/items.h"

#include <string>

namespace aa::sim {

NotImplemented::NotImplemented(ItemType type)
    : std::runtime_error(std::string("createPhysics: item type ") + std::to_string(static_cast<int>(type)) + " (" +
                         itemTypeName(type) + ") is not implemented yet"),
      type_(type) {}

bool isItemImplemented(ItemType type) {
    // Every type has its port; the switch keeps the list explicit (and NotImplemented for anything else).
    switch (type) {
    case ItemType::Shelf:
    case ItemType::TennisBall:
    case ItemType::BowlingBall:
    case ItemType::SoccerBall:
    case ItemType::Bucket:
    case ItemType::Hook:
    case ItemType::CardboardBoxMedium:
    case ItemType::CardboardBoxSmall:
    case ItemType::FishBowl:
    case ItemType::Book:
    case ItemType::EightBall:
    case ItemType::GoalStar:
    case ItemType::Billboard:
    case ItemType::Pinball:
    case ItemType::HangingLamp:
    case ItemType::WorldBound:
    case ItemType::LaundryBasket:
    case ItemType::BouncyBall:
    case ItemType::Bumper:
    case ItemType::PaperPlane:
    case ItemType::Pulley:
    case ItemType::Magnet:
    case ItemType::Helicopter:
    case ItemType::Pipe:
    case ItemType::Pipe90:
    case ItemType::Balloon:
    case ItemType::PiggyBank:
    case ItemType::Dart:
    case ItemType::Scissors:
    case ItemType::Seesaw:
    case ItemType::TrapdoorLever:
    case ItemType::RCController:
    case ItemType::BoxingGlove:
    case ItemType::Skateboard:
    case ItemType::RCTruck:
    case ItemType::Spring:
    case ItemType::Trapdoor:
    case ItemType::Slingshot:
    case ItemType::ZipLine:
    case ItemType::Rope:
    case ItemType::Doll:
    case ItemType::SelectionArea:
        return true;
    default:
        return false;
    }
}

void createPhysics(PhysicsObject& obj, const GameItem& item, PhysicsWorld& world, PhysicsMode mode) {
    switch (obj.type) {
    case ItemType::Shelf: items::createShelf(obj, world, mode); break;
    case ItemType::TennisBall:
    case ItemType::BowlingBall:
    case ItemType::SoccerBall:
    case ItemType::EightBall:
    case ItemType::Pinball:
    case ItemType::BouncyBall: items::createBall(obj, world, mode); break;
    case ItemType::Bucket: items::createBucket(obj, world, mode); break;
    case ItemType::Hook: items::createHook(obj, world, mode); break;
    case ItemType::CardboardBoxMedium:
    case ItemType::CardboardBoxSmall:
    case ItemType::FishBowl: items::createBox(obj, world, mode); break;
    case ItemType::Book: items::createBook(obj, item, world, mode); break;
    case ItemType::GoalStar: items::createGoalStar(obj, world, mode); break;
    case ItemType::Billboard: items::createBillboard(obj, world, mode); break;
    case ItemType::HangingLamp: items::createHangingLamp(obj, world, mode); break;
    case ItemType::WorldBound: items::createWorldBound(obj, item, world, mode); break;
    case ItemType::LaundryBasket: items::createLaundryBasket(obj, world, mode); break;
    case ItemType::Bumper: items::createBumper(obj, world, mode); break;
    case ItemType::PaperPlane: items::createPaperPlane(obj, world, mode); break;
    case ItemType::Pulley: items::createPulley(obj, world, mode); break;
    case ItemType::Magnet: items::createMagnet(obj, world, mode); break;
    case ItemType::Helicopter: items::createHelicopter(obj, item, world, mode); break;
    case ItemType::Pipe: items::createPipe(obj, world, mode); break;
    case ItemType::Pipe90: items::createPipe90(obj, world, mode); break;
    case ItemType::Balloon: items::createBalloon(obj, world); break;
    case ItemType::PiggyBank: items::createPiggyBank(obj, world); break;
    case ItemType::Dart: items::createDart(obj, world, mode); break;
    case ItemType::Scissors: items::createScissors(obj, item, world, mode); break;
    case ItemType::Seesaw: items::createSeesaw(obj, world, mode); break;
    case ItemType::TrapdoorLever: items::createTrapdoorLever(obj, world, mode); break;
    case ItemType::RCController: items::createRCController(obj, world, mode); break;
    case ItemType::BoxingGlove: items::createBoxingGlove(obj, world, mode); break;
    case ItemType::Skateboard: items::createSkateboard(obj, world, mode); break;
    case ItemType::RCTruck: items::createRCTruck(obj, item, world, mode); break;
    case ItemType::Spring: items::createSpring(obj, world, mode); break;
    case ItemType::Trapdoor: items::createTrapdoor(obj, item, world, mode); break;
    case ItemType::Slingshot: items::createSlingshot(obj, item, world, mode); break;
    case ItemType::ZipLine: items::createZipLine(obj, item, world, mode); break;
    case ItemType::Rope: items::createRope(obj, item, world, mode); break;
    case ItemType::Doll: items::createDoll(obj, world, mode); break;
    case ItemType::SelectionArea: items::createSelectionArea(obj, world, mode); break;
    default:
        throw NotImplemented(obj.type);
    }
}

}  // namespace aa::sim
