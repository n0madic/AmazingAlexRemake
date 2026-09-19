#include "aa/game/save_store.h"

#include "aa/data/json.h"
#include "aa/sim/animations.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace aa::game {

namespace fs = std::filesystem;

namespace {

#if defined(__EMSCRIPTEN__)
// web_pre.js serializes writes to IDBFS; SaveStore remains synchronous while a completed atomic
// rename is promptly queued for durable browser storage.
EM_JS(void, persistWebSaves, (), {
    if (Module.aaSyncPersistentStorage) Module.aaSyncPersistentStorage();
});
#endif

struct JsonDeleter {
    void operator()(cJSON* j) const { cJSON_Delete(j); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string print(const cJSON* root) {
    char* text = cJSON_Print(root);
    if (!text) throw std::runtime_error("cJSON_Print failed");
    std::string out(text);
    cJSON_free(text);
    out.push_back('\n');
    return out;
}

#if !defined(__EMSCRIPTEN__)
const char* envOrNull(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : nullptr;
}
#endif

}  // namespace

std::string SaveStore::defaultDir() {
    // The desktop OS user-data directories; a mobile build passes its data directory instead (app.cpp).
#if defined(__EMSCRIPTEN__)
    // Mounted by web/web_pre.js before main; post-js syncs this IDBFS tree back to IndexedDB.
    return kWebPersistentDir;
#elif defined(_WIN32)
    if (const char* appdata = envOrNull("APPDATA")) return std::string(appdata) + "\\AmazingAlex";
    return "AmazingAlex";
#elif defined(__APPLE__)
    if (const char* home = envOrNull("HOME")) return std::string(home) + "/Library/Application Support/AmazingAlex";
    return "AmazingAlex";
#else
    if (const char* xdg = envOrNull("XDG_DATA_HOME")) return std::string(xdg) + "/AmazingAlex";
    if (const char* home = envOrNull("HOME")) return std::string(home) + "/.local/share/AmazingAlex";
    return "AmazingAlex";
#endif
}

SaveStore::SaveStore(std::string dir) : dir_(std::move(dir)) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec && !fs::is_directory(dir_)) throw std::runtime_error("cannot create save directory " + dir_ + ": " + ec.message());
}

bool SaveStore::parseOrAside(const std::string& path, const std::function<void(const aa::data::JsonNode&)>& parse) {
    if (!fs::exists(path)) return false;
    try {
        const aa::data::JsonDoc doc(aa::data::readTextFile(path), path);
        if (!doc.root().isObject()) throw aa::data::JsonError(path + ": not an object");
        parse(doc.root());   // a wrong-typed member throws JsonError like a parse error does
        return true;
    } catch (const std::exception&) {
        // Set the file aside under a fresh name and fall back to defaults.
        std::string target;
        for (int n = 1;; ++n) {
            target = path + ".corrupt-" + std::to_string(n);
            if (!fs::exists(target)) break;
        }
        std::error_code ec;
        fs::rename(path, target, ec);
        if (!ec) aside_.push_back(target);
        return false;
    }
}

void SaveStore::writeAtomic(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + tmp);
    out << text;
    out.close();   // the stream buffers the whole (small) file: a full disk only shows here
    std::error_code ec;
    if (out.fail()) {
        fs::remove(tmp, ec);
        throw std::runtime_error("write failed: " + tmp);
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("cannot replace " + path);
    }
#if defined(__EMSCRIPTEN__)
    persistWebSaves();
#endif
}

Settings SaveStore::loadSettings() {
    Settings s;
    const bool ok = parseOrAside(settingsPath(), [&](const aa::data::JsonNode& root) {
        s.soundEffectsOn = root.getBool("soundEffectsOn", true);
        s.musicOn = root.getBool("musicOn", true);
        s.playerName = root.getString("playerName", "");
        s.locale = root.getString("locale", "");
        s.sandboxLegalAccepted = root.getBool("sandboxLegalAccepted", false);
    });
    return ok ? s : Settings{};
}

void SaveStore::saveSettings(const Settings& s) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddNumberToObject(root.get(), "format", kFormat);
    cJSON_AddBoolToObject(root.get(), "soundEffectsOn", s.soundEffectsOn);
    cJSON_AddBoolToObject(root.get(), "musicOn", s.musicOn);
    cJSON_AddStringToObject(root.get(), "playerName", s.playerName.c_str());
    cJSON_AddStringToObject(root.get(), "locale", s.locale.c_str());
    cJSON_AddBoolToObject(root.get(), "sandboxLegalAccepted", s.sandboxLegalAccepted);
    writeAtomic(settingsPath(), print(root.get()));
}

GameProgress SaveStore::loadProgress() {
    GameProgress p;
    const bool ok = parseOrAside(progressPath(), [&](const aa::data::JsonNode& root) {
        p.myContraptions = root.getBool("myContraptions", false);
        p.worldOfContraptions = root.getBool("worldOfContraptions", false);
        p.levelOfTheWeek = root.getBool("levelOfTheWeek", false);
        const aa::data::JsonNode locs = root.optional("locations");
        if (locs.isArray()) {
            for (int i = 0; i < locs.size() && i < kLocationCount; ++i) {
                const aa::data::JsonNode l = locs.at(i);
                LocationProgress& lp = p.locations[static_cast<std::size_t>(i)];
                lp.unlocked = l.getBool("unlocked", false);
                lp.chapterCompleteShown = l.getBool("chapterCompleteShown", false);
                lp.threeStarsShown = l.getBool("threeStarsShown", false);
                lp.stars = l.getInt("stars", 0);
            }
        }
        const aa::data::JsonNode items = root.optional("unlockedItems");
        if (items.isArray()) {
            for (const aa::data::JsonNode& n : items.array()) {
                const int type = n.getInt();
                if (type >= 0 && type < kItemTypeCount) p.itemUnlocked[static_cast<std::size_t>(type)] = true;
            }
        }
    });
    return ok ? p : GameProgress{};
}

void SaveStore::saveProgress(const GameProgress& p) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddNumberToObject(root.get(), "format", kFormat);
    cJSON_AddBoolToObject(root.get(), "myContraptions", p.myContraptions);
    cJSON_AddBoolToObject(root.get(), "worldOfContraptions", p.worldOfContraptions);
    cJSON_AddBoolToObject(root.get(), "levelOfTheWeek", p.levelOfTheWeek);
    cJSON* locs = cJSON_AddArrayToObject(root.get(), "locations");
    for (const LocationProgress& lp : p.locations) {
        cJSON* l = cJSON_CreateObject();
        cJSON_AddBoolToObject(l, "unlocked", lp.unlocked);
        cJSON_AddBoolToObject(l, "chapterCompleteShown", lp.chapterCompleteShown);
        cJSON_AddBoolToObject(l, "threeStarsShown", lp.threeStarsShown);
        cJSON_AddNumberToObject(l, "stars", lp.stars);
        cJSON_AddItemToArray(locs, l);
    }
    cJSON* items = cJSON_AddArrayToObject(root.get(), "unlockedItems");
    for (int type = 0; type < kItemTypeCount; ++type) {
        if (p.itemUnlocked[static_cast<std::size_t>(type)]) cJSON_AddItemToArray(items, cJSON_CreateNumber(type));
    }
    writeAtomic(progressPath(), print(root.get()));
}

LocationState SaveStore::loadLocation(const LocationInfo& info) {
    LocationState s;
    const bool ok = parseOrAside(locationPath(info.index), [&](const aa::data::JsonNode& root) {
        s.currentLevel = root.getInt("currentLevel", 0);
        s.visited = root.getBool("visited", false);
        s.finished = root.getBool("finished", false);
        const aa::data::JsonNode levelData = root.optional("levels");
        for (int i = 0; i < kMaxLevelsPerLocation; ++i) {
            LevelSlot& slot = s.levels[static_cast<std::size_t>(i)];
            if (i >= info.levelCount()) {
                slot = LevelSlot{};
                continue;
            }
            const aa::data::JsonNode l = levelData.isObject() ? levelData.optional(info.levels[static_cast<std::size_t>(i)].c_str())
                                                              : aa::data::JsonNode(nullptr, "");
            if (!l.isObject()) {
                slot.status = 1;   // "<file> doesn't contain <level>": locked
                slot.played = false;
                continue;
            }
            slot.status = l.getInt("status", 1);
            slot.played = l.getBool("played", false);
        }
    });
    if (!ok) return LocationState::fresh(info);
    if (s.currentLevel < 0 || s.currentLevel >= kMaxLevelsPerLocation) s.currentLevel = 0;
    s.repairPages(info);
    return s;
}

void SaveStore::saveLocation(const LocationState& s, const LocationInfo& info) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddNumberToObject(root.get(), "format", kFormat);
    cJSON_AddNumberToObject(root.get(), "currentLevel", s.currentLevel);
    cJSON_AddBoolToObject(root.get(), "visited", s.visited);
    cJSON_AddBoolToObject(root.get(), "finished", s.finished);
    cJSON* levels = cJSON_AddObjectToObject(root.get(), "levels");
    for (int i = 0; i < info.levelCount(); ++i) {
        const LevelSlot& slot = s.levels[static_cast<std::size_t>(i)];
        cJSON* l = cJSON_CreateObject();
        cJSON_AddNumberToObject(l, "status", slot.status);
        cJSON_AddBoolToObject(l, "played", slot.played);
        cJSON_AddItemToObject(levels, info.levels[static_cast<std::size_t>(i)].c_str(), l);
    }
    writeAtomic(locationPath(info.index), print(root.get()));
}

LocationInfo SaveStore::loadSandboxLocation() {
    LocationInfo info;
    info.index = kSandboxLocationIndex;
    info.nameId = "CHAPTER_NAME_MYC";
    parseOrAside(sandboxIndexPath(), [&](const aa::data::JsonNode& root) {
        const aa::data::JsonNode levels = root.optional("levels");
        if (!levels.isArray()) return;
        for (const aa::data::JsonNode& l : levels.array()) {
            if (l.isString() && info.levelCount() < kMaxSandboxLevels) info.levels.push_back(l.getString());
        }
    });
    return info;
}

void SaveStore::saveSandboxLocation(const LocationInfo& info) {
    std::error_code ec;
    fs::create_directories(sandboxDir(), ec);
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddNumberToObject(root.get(), "format", kFormat);
    cJSON_AddStringToObject(root.get(), "name", info.nameId.c_str());
    cJSON* levels = cJSON_AddArrayToObject(root.get(), "levels");
    for (const std::string& l : info.levels) cJSON_AddItemToArray(levels, cJSON_CreateString(l.c_str()));
    writeAtomic(sandboxIndexPath(), print(root.get()));
}

std::string SaveStore::generateSandboxName(const LocationInfo& info) const {
    // LocationInfoUtils::GenerateUniqueFilename [verified: 0xcc0ac]: a fresh st::Random seeded with
    // System::currentTimeMillis (the low 32 bits: SetSeed(int) takes r0 of the 64-bit result), one
    // GetInt(0, 0x7fffffff) — whose modulus wraps to INT_MIN, so the value is CustomRand's 15 bits, 0..32767 —
    // formatted by Format("{0}{1}", "", double(value)): the empty spec is sprintf "%g", exact for these
    // magnitudes; when a level of that name exists (case-insensitive) the whole thing repeats from the
    // re-seeded Random.
    static constexpr int kIntMax = 0x7fffffff;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    for (;;) {
        aa::sim::Random rng;
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        rng.setSeed(static_cast<int>(static_cast<std::uint32_t>(millis)));
        const std::string name = std::to_string(rng.getInt(0, kIntMax));
        const std::string key = lower(name);
        bool taken = false;
        for (const std::string& l : info.levels) taken = taken || lower(l) == key;
        if (!taken) return name;
    }
}

void SaveStore::removeSandboxLevel(LocationInfo& info, int level) {
    if (level < 0 || level >= info.levelCount()) return;
    const std::string name = info.levels[static_cast<std::size_t>(level)];
    info.levels.erase(info.levels.begin() + level);
    std::error_code ec;
    fs::remove(sandboxLevelPath(name), ec);
    fs::remove(sandboxThumbPath(name), ec);
#if defined(__EMSCRIPTEN__)
    persistWebSaves();
#endif
}

}  // namespace aa::game
