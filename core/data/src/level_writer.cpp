#include "aa/data/level_writer.h"

#include <cJSON.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace aa::data {

using aa::sim::Goal;
using aa::sim::Level;
using aa::sim::LevelItem;

namespace {

struct JsonDeleter {
    void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

void addNumber(cJSON* obj, const char* key, float v) { cJSON_AddNumberToObject(obj, key, static_cast<double>(v)); }
void addInt(cJSON* obj, const char* key, int v) { cJSON_AddNumberToObject(obj, key, static_cast<double>(v)); }

cJSON* vec2(aa::sim::Vec2 v) {
    cJSON* arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(static_cast<double>(v.x)));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(static_cast<double>(v.y)));
    return arr;
}

cJSON* item(const LevelItem& it) {
    cJSON* o = cJSON_CreateObject();
    addInt(o, "type", static_cast<int>(it.type));
    addInt(o, "handle", it.handle);
    cJSON_AddItemToObject(o, "center", vec2(it.center));
    addNumber(o, "angle", it.angle);
    addInt(o, "flags", it.flags);
    cJSON_AddItemToObject(o, "ropeEnd", vec2(it.ropeEnd));
    addInt(o, "itemData", it.itemData);
    cJSON* atts = cJSON_CreateArray();
    for (int i = 0; i < it.attachmentCount && i < LevelItem::kMaxAttachments; ++i) {
        const aa::sim::LevelAttachment& a = it.attachments[static_cast<std::size_t>(i)];
        cJSON* ao = cJSON_CreateObject();
        addInt(ao, "state", a.state);
        addInt(ao, "objectIndex", a.objectIndex);
        addInt(ao, "index", a.index);
        cJSON_AddItemToArray(atts, ao);
    }
    cJSON_AddItemToObject(o, "attachments", atts);
    return o;
}

cJSON* goal(const Goal& g) {
    cJSON* o = cJSON_CreateObject();
    addInt(o, "type", g.type);
    addInt(o, "itemCount", g.itemCount);
    auto handles = [&](const char* key, const std::array<int, Goal::kMaxTargets>& hs) {
        cJSON* arr = cJSON_CreateArray();
        for (int h : hs) cJSON_AddItemToArray(arr, cJSON_CreateNumber(static_cast<double>(h)));
        cJSON_AddItemToObject(o, key, arr);
    };
    handles("itemHandles", g.itemHandles);
    handles("itemHandles2", g.itemHandles2);
    addInt(o, "timeLimit", g.timeLimit);
    addNumber(o, "height", g.height);
    addNumber(o, "width", g.width);
    addNumber(o, "angle", g.angle);
    cJSON_AddBoolToObject(o, "negated", g.negated);
    return o;
}

}  // namespace

std::string levelToJson(const Level& level) {
    JsonPtr root(cJSON_CreateObject());
    addInt(root.get(), "version", 7);
    addInt(root.get(), "sourceVersion", level.version);
    cJSON_AddStringToObject(root.get(), "title", level.title.c_str());
    cJSON_AddStringToObject(root.get(), "description", level.description.c_str());
    cJSON_AddStringToObject(root.get(), "authorName", level.authorName.c_str());
    addInt(root.get(), "backgroundIndex", level.backgroundIndex);
    cJSON* toolbox = cJSON_CreateArray();
    for (const aa::sim::ToolboxSlot& s : level.toolbox) {
        cJSON* so = cJSON_CreateObject();
        addInt(so, "type", static_cast<int>(s.type));
        addInt(so, "amount", s.amount);
        cJSON_AddItemToArray(toolbox, so);
    }
    cJSON_AddItemToObject(root.get(), "toolbox", toolbox);
    cJSON* items = cJSON_CreateArray();
    for (const LevelItem& it : level.items) cJSON_AddItemToArray(items, item(it));
    cJSON_AddItemToObject(root.get(), "items", items);
    cJSON_AddItemToObject(root.get(), "goal", goal(level.goal));
    addInt(root.get(), "rewardId", level.rewardId);
    cJSON_AddBoolToObject(root.get(), "tested", level.tested);
    char* text = cJSON_Print(root.get());
    if (!text) throw std::runtime_error("cJSON_Print failed");
    std::string out(text);
    cJSON_free(text);
    out.push_back('\n');
    return out;
}

void writeLevelFile(const std::string& path, const Level& level) {
    namespace fs = std::filesystem;
    const std::string text = levelToJson(level);
    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + tmp);
    out << text;
    out.close();   // the stream buffers the whole (small) file: a full disk only shows here
    if (out.fail()) {
        fs::remove(tmp, ec);
        throw std::runtime_error("write failed: " + tmp);
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        std::remove(tmp.c_str());
        throw std::runtime_error("cannot replace " + path + ": " + ec.message());
    }
}

}  // namespace aa::data
