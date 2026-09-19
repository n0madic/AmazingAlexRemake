#include "aa/sim/world_state.h"

#include "aa/sim/physics_world.h"

#include <stdexcept>

namespace aa::sim {

PhysicsObject PhysicsObject::fromTemplate(const PhysicsObjectTemplate& t) {
    PhysicsObject o;
    o.type = t.type;
    o.flags = t.flags;
    o.halfSize = t.halfSize;
    o.selectable = t.bodyFlags;
    o.attachmentCount = t.attachmentCount;
    for (int i = 0; i < t.attachmentCount; ++i) {
        o.attachments[static_cast<std::size_t>(i)].point = t.attachments[static_cast<std::size_t>(i)];
    }
    return o;
}

void PhysicsObject::addBody(int slot) {
    if (bodyCount >= kMaxBodies) throw std::length_error("PhysicsObject: too many bodies");
    bodies[static_cast<std::size_t>(bodyCount++)] = slot;
}

void PhysicsObject::addJoint(int slot) {
    if (jointCount >= kMaxJoints) throw std::length_error("PhysicsObject: too many joints");
    joints[static_cast<std::size_t>(jointCount++)] = slot;
}

HandleManager::HandleManager() : slots_(static_cast<std::size_t>(kSlotCount)) {
    // Reset: entry i links to i + 1, the last one ends the list; every generation starts at 1.
    for (int i = 0; i < kSlotCount; ++i) slots_[static_cast<std::size_t>(i)].next = i + 1 < kSlotCount ? i + 1 : kEndOfList;
}

void HandleManager::unlink(int slot) {
    if (freeHead_ == slot) {
        freeHead_ = slots_[static_cast<std::size_t>(slot)].next;
        return;
    }
    // GetEntryIndexPointingTo: walk the free list for the entry that links to `slot`.
    for (int i = freeHead_; i != kEndOfList; i = slots_[static_cast<std::size_t>(i)].next) {
        if (slots_[static_cast<std::size_t>(i)].next == slot) {
            slots_[static_cast<std::size_t>(i)].next = slots_[static_cast<std::size_t>(slot)].next;
            return;
        }
    }
    throw std::invalid_argument("HandleManager: slot is not free");
}

void HandleManager::addWithHandle(int handle, int itemIndex) {
    const int slot = Handle::slotOf(handle);
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    if (s.live) throw std::invalid_argument("HandleManager: duplicate handle slot");
    unlink(slot);
    s.generation = Handle::generationOf(handle);
    s.itemIndex = itemIndex;
    s.live = true;
}

int HandleManager::add(ItemType type, int itemIndex) {
    const int slot = freeSlot();
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    freeHead_ = s.next;
    s.generation = (s.generation + 1) & Handle::kGenerationMask;
    s.itemIndex = itemIndex;
    s.live = true;
    return Handle::make(type, s.generation, slot);
}

void HandleManager::remove(int handle) {
    // Only the live generation may unregister the slot: a stale handle must not evict its successor.
    if (lookup(handle) < 0) return;
    const int slot = Handle::slotOf(handle);
    Slot& s = slots_[static_cast<std::size_t>(slot)];
    s.live = false;
    s.next = freeHead_;
    freeHead_ = slot;
}

int HandleManager::freeSlot() const {
    if (freeHead_ == kEndOfList) throw std::length_error("HandleManager: table full");
    return freeHead_;
}

void HandleManager::setItemIndex(int handle, int itemIndex) {
    Slot& s = slots_[static_cast<std::size_t>(Handle::slotOf(handle))];
    if (s.live && s.generation == Handle::generationOf(handle)) s.itemIndex = itemIndex;
}

int HandleManager::lookup(int handle) const {
    const Slot& s = slots_[static_cast<std::size_t>(Handle::slotOf(handle))];
    if (!s.live || s.generation != Handle::generationOf(handle)) return -1;
    return s.itemIndex;
}

WorldState::WorldState() {
    items.reserve(static_cast<std::size_t>(kObjectCapacity));
    objects.reserve(static_cast<std::size_t>(kObjectCapacity));
}

int WorldState::insertItem(GameItem item) {
    // GameItemCollectionUtils::Insert: the collection is sorted by type; the new item goes behind the last
    // item of its type and every later item shifts by one (HandleManager::Update on each).
    auto pos = std::upper_bound(items.begin(), items.end(), item.type,
                                [](ItemType t, const GameItem& g) { return t < g.type; });
    const int itemIndex = static_cast<int>(pos - items.begin());
    if (items.capacity() < static_cast<std::size_t>(kObjectCapacity)) items.reserve(static_cast<std::size_t>(kObjectCapacity));
    items.insert(pos, item);
    reindexItems(itemIndex);
    return itemIndex;
}

void WorldState::reindexItems(int from) {
    for (std::size_t i = static_cast<std::size_t>(from); i < items.size(); ++i) {
        handles.setItemIndex(items[i].handle, static_cast<int>(i));
    }
}

int WorldState::addItemWithHandle(const TemplateTable& templates, int handle, Vec2 center, float angle) {
    const ItemType type = Handle::typeOf(handle);
    if (!isValidItemType(static_cast<int>(type))) throw std::invalid_argument("addItemWithHandle: bad type bits");
    // Register the handle first: it is the only step that can reject the item, and a rejected item must
    // leave no orphan object/item behind.
    handles.addWithHandle(handle, -1);
    if (objects.capacity() < static_cast<std::size_t>(kObjectCapacity)) objects.reserve(static_cast<std::size_t>(kObjectCapacity));
    PhysicsObject obj = PhysicsObject::fromTemplate(templates[static_cast<std::size_t>(type)]);
    obj.index = static_cast<int>(objects.size());
    obj.handle = handle;
    obj.position = center;
    obj.angle = angle;
    objects.push_back(obj);

    GameItem item = GameItem::defaults(type);
    item.handle = handle;
    item.objectIndex = obj.index;
    item.setInitialState(false);
    return insertItem(item);
}

void GameItem::setInitialState(bool fromToolbox) {
    switch (type) {
    case ItemType::Book:
        stateWord = handle & 3;
        break;
    case ItemType::RCTruck:
    case ItemType::Trapdoor:
    case ItemType::Helicopter:
        builtInController = fromToolbox;
        break;
    case ItemType::Scissors: {
        Random r;
        r.setSeed(handle);
        snipTimer = r.getFloat(kIdleTimerMin, kIdleTimerMax);
        break;
    }
    case ItemType::Dart: {
        Random r;
        r.setSeed(handle);
        wobbleTimer = r.getFloat(kIdleTimerMin, kIdleTimerMax);
        break;
    }
    default:
        break;
    }
}

int WorldState::addNewItem(const TemplateTable& templates, ItemType type, Vec2 center, float angle, bool fromToolbox) {
    if (!isValidItemType(static_cast<int>(type))) throw std::invalid_argument("addNewItem: bad item type");
    if (objects.capacity() < static_cast<std::size_t>(kObjectCapacity)) objects.reserve(static_cast<std::size_t>(kObjectCapacity));
    // PhysicsObjectsUtils::Add: the template copy at the end of the collection, the angle added to the
    // template's (always 0).
    PhysicsObject obj = PhysicsObject::fromTemplate(templates[static_cast<std::size_t>(type)]);
    obj.index = static_cast<int>(objects.size());
    obj.position = center;
    obj.angle = obj.angle + angle;
    // GameItemCollectionUtils::Insert allocates the handle (HandleManager::Add) for the default block.
    const int handle = handles.add(type, -1);
    obj.handle = handle;
    objects.push_back(obj);
    GameItem item = GameItem::defaults(type);
    item.handle = handle;
    item.objectIndex = obj.index;
    item.setInitialState(fromToolbox);
    return insertItem(item);
}

void WorldState::removeItemAt(int itemIndex) {
    // GameItemCollectionUtils::Remove: HandleManager::Remove, the later items shift down by one.
    handles.remove(items[static_cast<std::size_t>(itemIndex)].handle);
    items.erase(items.begin() + itemIndex);
    reindexItems(itemIndex);
}

void WorldState::removeObject(int objectIndex, PhysicsWorld* world) {
    // PhysicsObjectsUtils::Remove: the last object takes the freed slot.
    const int last = static_cast<int>(objects.size()) - 1;
    if (last != objectIndex) {
        PhysicsObject& moved = objects[static_cast<std::size_t>(objectIndex)];
        moved = objects[static_cast<std::size_t>(last)];
        moved.index = objectIndex;
        if (world) {
            for (int k = 0; k < moved.bodyCount; ++k) world->setBodyObject(moved.bodies[static_cast<std::size_t>(k)], objectIndex, moved.type);
        }
        if (moved.handle != 0) {
            const int i = handles.lookup(moved.handle);
            if (i >= 0) items[static_cast<std::size_t>(i)].objectIndex = objectIndex;
        }
        for (int k = 0; k < moved.attachmentCount; ++k) {
            const AttachmentRecord& rec = moved.attachments[static_cast<std::size_t>(k)];
            if (rec.state != attachment_state::kFree) {
                objects[static_cast<std::size_t>(rec.otherObject)].attachments[static_cast<std::size_t>(rec.otherPoint)].otherObject =
                    objectIndex;
            }
        }
    }
    objects.pop_back();
}

void WorldState::removeInvalidItems(PhysicsWorld* world) {
    // WorldStateUtils::RemoveInvalidItems: restart from the front after every removal (the swap moves the
    // last object into the freed slot).
    for (;;) {
        int victim = -1;
        for (const PhysicsObject& obj : objects) {
            if (!obj.valid()) {
                victim = obj.index;
                break;
            }
        }
        if (victim < 0) return;
        const int itemIndex = handles.lookup(objects[static_cast<std::size_t>(victim)].handle);
        if (itemIndex >= 0) removeItemAt(itemIndex);
        removeObject(victim, world);
    }
}

int WorldState::typeCount(ItemType type) const {
    int n = 0;
    for (const GameItem& item : items) n += item.type == type ? 1 : 0;
    return n;
}

GameItem GameItem::defaults(ItemType type) {
    GameItem item;
    item.type = type;
    switch (type) {
    case ItemType::Scissors:
        item.cutAngle = kDefaultCutAngle;
        item.snipStep = -1;
        item.snipTimer = 0.5f;
        item.snipDirection = 1.0f;
        break;
    case ItemType::Dart:
        item.wobbleDirection = 1.0f;
        item.wobbleTimer = 0.5f;
        break;
    case ItemType::Magnet:
        item.magnetMinDist2 = 10000.0f;
        break;
    case ItemType::Rope:
        item.endVector = Vec2(0.0f, -0.6f);
        break;
    case ItemType::BoxingGlove:
        // The default block holds an idle interpolator {to 36, duration 1, value 36}.
        item.gloveButton.to = kGloveButtonHeightPx;
        item.gloveButton.duration = 1.0f;
        item.gloveButton.value = kGloveButtonHeightPx;
        break;
    case ItemType::Billboard:
        item.stateWord = 1;
        break;
    case ItemType::Slingshot:
        item.endVector = Vec2(-0.11f, 0.03f);
        break;
    case ItemType::ZipLine:
        item.endVector = Vec2(1.0f, -0.2f);
        break;
    default:
        break;
    }
    return item;
}

GameItem* WorldState::findItem(int handle) {
    const int i = handles.lookup(handle);
    return i < 0 ? nullptr : &items[static_cast<std::size_t>(i)];
}

const GameItem* WorldState::findItem(int handle) const {
    const int i = handles.lookup(handle);
    return i < 0 ? nullptr : &items[static_cast<std::size_t>(i)];
}

}  // namespace aa::sim
