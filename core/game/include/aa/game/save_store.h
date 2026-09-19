// Save files (docs/10-architecture.md §7): own versioned JSON — settings.json, progress.json,
// location_<n>.json — in the OS user-data directory (or a --save-dir). Writes are atomic (temp file +
// rename); a corrupt file is renamed aside and replaced by defaults, never silently overwritten.
#pragma once

#include "aa/game/progress.h"

#include <functional>
#include <string>
#include <vector>

namespace aa::data {
class JsonNode;
}

namespace aa::game {

class SaveStore {
public:
    static constexpr int kFormat = 1;

    // The platform's user-data directory for the game: ~/Library/Application Support/AmazingAlex on macOS,
    // $XDG_DATA_HOME/AmazingAlex (or ~/.local/share/AmazingAlex) on Linux, %APPDATA%\AmazingAlex on Windows.
    static std::string defaultDir();

    // Web: the IDBFS mount point web/web_pre.js mounts before main; shared with platform_web.cpp's
    // PlatformInfo::dataDir so the two never diverge.
    static constexpr const char* kWebPersistentDir = "/persistent";

    // Creates `dir` when missing; throws when it cannot.
    explicit SaveStore(std::string dir);
    const std::string& dir() const { return dir_; }

    std::string settingsPath() const { return dir_ + "/settings.json"; }
    std::string progressPath() const { return dir_ + "/progress.json"; }
    std::string locationPath(int index) const { return dir_ + "/location_" + std::to_string(index) + ".json"; }

    // Loaders return defaults for a missing file; a file that cannot be parsed is moved aside
    // (`<name>.corrupt-<n>`) and defaults are returned. Savers throw on an I/O failure.
    Settings loadSettings();
    void saveSettings(const Settings& settings);
    GameProgress loadProgress();
    void saveProgress(const GameProgress& progress);
    // LocationStateUtils::Load: a level missing from the file is locked (status 1), then the page repair.
    LocationState loadLocation(const LocationInfo& info);
    void saveLocation(const LocationState& state, const LocationInfo& info);

    // --- the My Contraptions location (docs/05 §8): <save-dir>/sandbox/index.json lists the user levels
    // (LocationInfoUtils::LoadFromDocs / Save on 0_Location.plist of AppConfig::SandboxDir), each level in
    // <name>.json (LevelLayoutUtils::SavePlist wrote <name>.plist) with its thumbnail <name>.png next to it
    // (the original's <name>_<size>.jpg). The LocationInfo carries kSandboxLocationIndex; a missing index
    // is an empty location.
    static constexpr int kSandboxLocationIndex = -3;   // LocationInfo+0 of the My Contraptions location [verified]
    static constexpr int kMaxSandboxLevels = kMaxLevelsPerLocation;   // MyContraptionsView::ButtonPressed: < 0x60
    std::string sandboxDir() const { return dir_ + "/sandbox"; }
    std::string sandboxIndexPath() const { return sandboxDir() + "/index.json"; }
    std::string sandboxLevelPath(const std::string& name) const { return sandboxDir() + "/" + name + ".json"; }
    std::string sandboxThumbPath(const std::string& name) const { return sandboxDir() + "/" + name + ".png"; }
    LocationInfo loadSandboxLocation();
    void saveSandboxLocation(const LocationInfo& info);
    // LocationInfoUtils::GenerateUniqueFilename with an empty prefix: a random non-negative integer that no
    // listed level uses (case-insensitively). The original seeds its Random with the wall clock.
    std::string generateSandboxName(const LocationInfo& info) const;
    // LocationInfoUtils::RemoveLevel plus the level's files (MyContraptionsView's trash path removes the
    // level, its solution and its thumbnail).
    void removeSandboxLevel(LocationInfo& info, int level);

    // Files set aside by the loaders of this store (for the log / tests).
    const std::vector<std::string>& asideFiles() const { return aside_; }

private:
    std::string dir_;
    std::vector<std::string> aside_;

    // Runs `parse` over the root object of `path`; false when the file is absent, or when it cannot be
    // read, parsed or typed — then it is moved aside (`.corrupt-<n>`) and the caller keeps its defaults.
    bool parseOrAside(const std::string& path, const std::function<void(const aa::data::JsonNode&)>& parse);
    void writeAtomic(const std::string& path, const std::string& text);
};

}  // namespace aa::game
