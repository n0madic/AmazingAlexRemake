#include "aa/ui/app_state.h"

#include "aa/data/level_loader.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace aa::ui {

namespace {

// A save that cannot be written (a read-only directory, a full disk) is reported and the game goes on with
// the in-memory state; the UI callbacks that save must never unwind the frame loop.
template <typename Fn>
bool trySave(const char* what, Fn&& fn) {
    try {
        fn();
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "amazing_alex: cannot save %s: %s\n", what, e.what());
        return false;
    }
}

}  // namespace

float AppState::profilePixelScaleOf(const aa::data::AssetRoot& root) {
    // "<W>X<H>" → W / 1024: the profile's own PixelScale (docs/11 §1).
    const int profileWidth = std::atoi(root.manifest().profile.c_str());
    return profileWidth > 0 ? static_cast<float>(profileWidth) / aa::sim::ScreenLayout::kPlayFieldWidth : aa::sim::ScreenLayout::kProfilePixelScale;
}

void AppState::loadCatalogue() {
    locations.clear();
    levelMeta.clear();
    profilePixelScale = profilePixelScaleOf(*assets);
    const std::vector<std::string>& chapters = assets->chapters();
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        aa::game::LocationInfo info;
        info.index = static_cast<int>(i);
        info.chapter = chapters[i];
        const aa::data::LevelIndex index = aa::data::loadLevelIndex(assets->json("levels/" + chapters[i] + "/index.json").root());
        info.nameId = index.name;
        info.levels = index.levels;
        // The four Treehouse files the original's chapter index never listed (docs/12 §1) are appended after
        // the shipped play order, so existing saves' level indices (0-31) stay stable and only new slots
        // (32-35) are added.
        info.levels.insert(info.levels.end(), index.unlisted.begin(), index.unlisted.end());
        std::vector<LevelMeta> metas;
        for (const std::string& name : info.levels) {
            const aa::sim::Level level = aa::data::loadLevel(assets->json("levels/" + chapters[i] + "/" + name + ".json").root());
            LevelMeta m;
            m.name = name;
            m.titleId = level.title;
            m.tipId = level.description;
            m.backgroundIndex = level.backgroundIndex;
            metas.push_back(m);
        }
        locations.push_back(info);
        levelMeta.push_back(metas);
    }
    // --unlock-all: GameProgress::locationUnlocked() reports every chapter open regardless of the
    // collected-star thresholds (05 §4) without touching the stored per-location flag; the levels
    // themselves are unlocked the same way as each location loads (loadLocation).
    progress.unlockAll = unlockAllLevels;
    tips = aa::data::loadTips(assets->json("tips.json").root());
    // The fifth slot: the My Contraptions location (LocationInfo index −3), filled by loadSandboxLocation.
    aa::game::LocationInfo sandbox;
    sandbox.index = aa::game::SaveStore::kSandboxLocationIndex;
    sandbox.nameId = "CHAPTER_NAME_MYC";
    locations.push_back(sandbox);
    levelMeta.emplace_back();
    loadSandboxLocation();
}

void AppState::loadSandboxLocation() {
    aa::game::LocationInfo& info = locations[static_cast<std::size_t>(kSandboxLocation)];
    info = saves->loadSandboxLocation();
    std::vector<LevelMeta>& metas = levelMeta[static_cast<std::size_t>(kSandboxLocation)];
    metas.clear();
    for (const std::string& name : info.levels) {
        // LevelInfoUtils::LoadLevelTitle: the file's "title" (a literal for user levels); "" when the file
        // does not load, which LevelSelectorButton::Setup(3) treats as a broken level.
        LevelMeta m;
        m.name = name;
        try {
            const aa::sim::Level level = aa::data::loadLevelFile(saves->sandboxLevelPath(name));
            m.titleId = level.title;
            m.tipId = level.description;
            m.backgroundIndex = level.backgroundIndex;
        } catch (const std::exception&) {
            m.titleId.clear();
        }
        metas.push_back(m);
    }
}

bool AppState::saveSandboxLocation() {
    return trySave("the sandbox index", [&] { saves->saveSandboxLocation(locations[static_cast<std::size_t>(kSandboxLocation)]); });
}

void AppState::removeSandboxLevel(int level) {
    aa::game::LocationInfo& info = locations[static_cast<std::size_t>(kSandboxLocation)];
    if (level < 0 || level >= info.levelCount()) return;
    saves->removeSandboxLevel(info, level);
    std::vector<LevelMeta>& metas = levelMeta[static_cast<std::size_t>(kSandboxLocation)];
    if (level < static_cast<int>(metas.size())) metas.erase(metas.begin() + level);
}

void AppState::loadLocation(int index) {
    locationIndex = index;
    if (isSandboxLocation(index)) {
        // The original keeps no LocationState for the sandbox location (LocationStateUtils::Load with
        // index −3 finds no file): a fresh one, never saved.
        locationState = aa::game::LocationState::fresh(locations[static_cast<std::size_t>(index)]);
        currentLevel = 0;
        return;
    }
    locationState = saves->loadLocation(locations[static_cast<std::size_t>(index)]);
    currentLevel = locationState.currentLevel;
    // --unlock-all: set after SaveStore::loadLocation (and its repairPages pass) has already run on the
    // real stored statuses, so LocationState::status() reporting every level unlocked from here on never
    // feeds back into a stored value (progress.h).
    locationState.unlockAll = unlockAllLevels;
}


void AppState::saveLocation() {
    if (locationIndex < 0 || isSandboxLocation(locationIndex)) return;
    locationState.currentLevel = currentLevel;
    // --unlock-all is a cheat: the session is read-only for progress (docs/13) — nothing it plays, opens
    // or completes reaches the location or progress file (the in-memory state still advances).
    if (unlockAllLevels) return;
    trySave("the location state", [&] { saves->saveLocation(locationState, locations[static_cast<std::size_t>(locationIndex)]); });
}

void AppState::saveProgress() {
    if (unlockAllLevels) return;   // see saveLocation
    trySave("the progress", [&] { saves->saveProgress(progress); });
}

void AppState::saveSettings() {
    trySave("the settings", [&] { saves->saveSettings(settings); });
}

aa::sim::Level AppState::loadLevel(int loc, int level) const {
    const aa::game::LocationInfo& info = locations[static_cast<std::size_t>(loc)];
    const std::string& name = info.levels[static_cast<std::size_t>(level)];
    if (isSandboxLocation(loc)) return aa::data::loadLevelFile(saves->sandboxLevelPath(name));
    return aa::data::loadLevel(assets->json("levels/" + info.chapter + "/" + name + ".json").root());
}

const aa::data::JsonNode AppState::dialogs() {
    if (!dialogsTree) dialogsTree = std::make_unique<aa::data::SceneTree>(assets->json("ui/scenes/Dialogs.json"));
    return dialogsTree->view("Dialogs");
}

}  // namespace aa::ui
