#include "aa/data/level_loader.h"

namespace aa::data {

using aa::sim::Goal;
using aa::sim::ItemType;
using aa::sim::Level;
using aa::sim::LevelAttachment;
using aa::sim::LevelItem;
using aa::sim::ToolboxSlot;
using aa::sim::Vec2;

namespace {

Vec2 vec2(const JsonNode& n) {
    if (n.size() != 2) throw JsonError(n.path() + ": expected [x, y]");
    return Vec2(n.at(0).getFloat(), n.at(1).getFloat());
}

ItemType itemType(const JsonNode& n) {
    const int t = n.getInt();
    if (!aa::sim::isValidItemType(t)) throw JsonError(n.path() + ": invalid item type " + std::to_string(t));
    return static_cast<ItemType>(t);
}

LevelItem loadItem(const JsonNode& n) {
    LevelItem item;
    item.type = itemType(n.child("type"));
    item.handle = n.getInt("handle");
    item.center = vec2(n.child("center"));
    item.angle = n.getFloat("angle");
    item.flags = n.getInt("flags");
    item.ropeEnd = vec2(n.child("ropeEnd"));
    item.itemData = n.getInt("itemData");
    const std::vector<JsonNode> atts = n.child("attachments").array();
    if (atts.size() > static_cast<std::size_t>(LevelItem::kMaxAttachments)) {
        throw JsonError(n.path() + ": more than " + std::to_string(LevelItem::kMaxAttachments) + " attachments");
    }
    item.attachmentCount = static_cast<int>(atts.size());
    for (std::size_t i = 0; i < atts.size(); ++i) {
        LevelAttachment& a = item.attachments[i];
        a.state = atts[i].getInt("state");
        a.objectIndex = atts[i].getInt("objectIndex");
        a.index = atts[i].getInt("index");
    }
    if (aa::sim::Handle::typeOf(item.handle) != item.type) {
        throw JsonError(n.path() + ": handle type bits do not match the item type");
    }
    return item;
}

Goal loadGoal(const JsonNode& n) {
    Goal g;
    g.type = n.getInt("type");
    g.itemCount = n.getInt("itemCount");
    auto handles = [&](const char* key, std::array<int, Goal::kMaxTargets>& out) {
        const std::vector<JsonNode> arr = n.child(key).array();
        if (arr.size() > static_cast<std::size_t>(Goal::kMaxTargets)) throw JsonError(n.path() + ": too many targets");
        for (std::size_t i = 0; i < arr.size(); ++i) out[i] = arr[i].getInt();
    };
    handles("itemHandles", g.itemHandles);
    handles("itemHandles2", g.itemHandles2);
    g.timeLimit = n.getInt("timeLimit", 0);
    g.height = n.getFloat("height");
    g.width = n.getFloat("width");
    g.angle = n.getFloat("angle");
    g.negated = n.getBool("negated", false);
    return g;
}

}  // namespace

Level loadLevel(const JsonNode& root) {
    Level level;
    level.version = root.getInt("version");
    if (level.version != 7) throw JsonError(root.path() + ": level version " + std::to_string(level.version) +
                                            " (the importer normalises every level to 7)");
    level.title = root.getString("title");
    level.description = root.getString("description");
    level.authorName = root.getString("authorName", "");
    level.backgroundIndex = root.getInt("backgroundIndex");
    for (const JsonNode& s : root.child("toolbox").array()) {
        ToolboxSlot slot;
        slot.type = itemType(s.child("type"));
        slot.amount = s.getInt("amount");
        level.toolbox.push_back(slot);
    }
    for (const JsonNode& i : root.child("items").array()) level.items.push_back(loadItem(i));
    level.goal = loadGoal(root.child("goal"));
    level.rewardId = root.getInt("rewardId", 0);
    level.tested = root.getBool("tested", false);
    return level;
}

Level loadLevelFile(const std::string& path) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadLevel(doc.root());
}

LevelIndex loadLevelIndex(const JsonNode& root) {
    LevelIndex index;
    index.name = root.getString("name");
    for (const JsonNode& l : root.child("levels").array()) index.levels.push_back(l.getString());
    if (root.has("unlisted")) {
        for (const JsonNode& l : root.child("unlisted").array()) index.unlisted.push_back(l.getString());
    }
    return index;
}

LevelIndex loadLevelIndexFile(const std::string& path) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadLevelIndex(doc.root());
}

}  // namespace aa::data
