// The My Contraptions files (docs/05 §8): the sandbox index, the unique names, the level / thumbnail
// files, the cap, a corrupt user level (the parsing-error path) and the legal-prompt setting.
#include "aa/data/level_loader.h"
#include "aa/data/level_writer.h"
#include "aa/game/save_store.h"

#include <doctest.h>

#include <cctype>

#include <filesystem>
#include <fstream>
#include <string>

using namespace aa::game;

namespace {

std::string tempDir(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("aa_sandbox_") + name)).string();
}

aa::sim::Level userLevel(const std::string& title) {
    aa::sim::Level level;
    level.title = title;
    level.authorName = "Alex";
    aa::sim::LevelItem bound;
    bound.type = aa::sim::ItemType::WorldBound;
    bound.handle = aa::sim::Handle::make(aa::sim::ItemType::WorldBound, 0, 0);
    level.items.push_back(bound);
    return level;
}

}  // namespace

TEST_CASE("sandbox store: an empty location, names, save / list / load / remove") {
    const std::string dir = tempDir("store");
    std::filesystem::remove_all(dir);
    SaveStore store(dir);
    LocationInfo info = store.loadSandboxLocation();
    CHECK(info.index == SaveStore::kSandboxLocationIndex);
    CHECK(info.nameId == "CHAPTER_NAME_MYC");
    CHECK(info.levelCount() == 0);

    // LevelLoadingScene::ActivationComplete (location 3): a unique name, AddLevel, Save.
    const std::string a = store.generateSandboxName(info);
    CHECK_FALSE(a.empty());
    for (char c : a) CHECK(std::isdigit(static_cast<unsigned char>(c)));
    // GenerateUniqueFilename's value is st::Random's 15-bit CustomRand (GetInt(0, 0x7fffffff) wraps its modulus).
    CHECK(a.size() <= 5);
    CHECK(std::stoi(a) <= 32767);
    info.levels.push_back(a);
    store.saveSandboxLocation(info);
    aa::data::writeLevelFile(store.sandboxLevelPath(a), userLevel("first"));
    const std::string b = store.generateSandboxName(info);
    CHECK(b != a);
    info.levels.push_back(b);
    store.saveSandboxLocation(info);
    aa::data::writeLevelFile(store.sandboxLevelPath(b), userLevel("second"));
    {
        std::ofstream thumb(store.sandboxThumbPath(b), std::ios::binary);
        thumb << "png";
    }

    {
        SaveStore again(dir);
        const LocationInfo listed = again.loadSandboxLocation();
        REQUIRE(listed.levelCount() == 2);
        CHECK(listed.levels[0] == a);
        CHECK(listed.levels[1] == b);
        CHECK(aa::data::loadLevelFile(again.sandboxLevelPath(b)).title == "second");
        CHECK(std::filesystem::exists(again.sandboxThumbPath(b)));
    }
    // The trash path: the level leaves the index and its files go with it.
    store.removeSandboxLevel(info, 1);
    store.saveSandboxLocation(info);
    CHECK(info.levelCount() == 1);
    CHECK_FALSE(std::filesystem::exists(store.sandboxLevelPath(b)));
    CHECK_FALSE(std::filesystem::exists(store.sandboxThumbPath(b)));
    CHECK(std::filesystem::exists(store.sandboxLevelPath(a)));
    CHECK(store.loadSandboxLocation().levelCount() == 1);
    store.removeSandboxLevel(info, 5);   // out of range: nothing happens
    CHECK(info.levelCount() == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("sandbox store: the cap and a corrupt level file") {
    const std::string dir = tempDir("cap");
    std::filesystem::remove_all(dir);
    SaveStore store(dir);
    LocationInfo info = store.loadSandboxLocation();
    for (int i = 0; i < SaveStore::kMaxSandboxLevels + 4; ++i) info.levels.push_back("level" + std::to_string(i));
    store.saveSandboxLocation(info);
    // LoadFromDocs keeps at most 0x60 names (the LocationInfo table); ButtonAdd refuses at the cap.
    const LocationInfo capped = store.loadSandboxLocation();
    CHECK(capped.levelCount() == SaveStore::kMaxSandboxLevels);
    CHECK_FALSE(capped.levelCount() < SaveStore::kMaxSandboxLevels);

    // A level file that does not parse: the loader throws; MyContraptionsView::Refresh drops the entry.
    info = LocationInfo{};
    info.index = SaveStore::kSandboxLocationIndex;
    info.levels = {"broken"};
    std::filesystem::create_directories(store.sandboxDir());
    {
        std::ofstream out(store.sandboxLevelPath("broken"));
        out << "{ not a level";
    }
    CHECK_THROWS_AS(aa::data::loadLevelFile(store.sandboxLevelPath("broken")), aa::data::JsonError);
    store.removeSandboxLevel(info, 0);
    CHECK(info.levelCount() == 0);
    CHECK_FALSE(std::filesystem::exists(store.sandboxLevelPath("broken")));

    // A corrupt index is set aside like the other save files.
    {
        std::ofstream out(store.sandboxIndexPath());
        out << "nope";
    }
    CHECK(store.loadSandboxLocation().levelCount() == 0);
    CHECK(store.asideFiles().size() == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("sandbox store: the legal prompt setting round-trips") {
    const std::string dir = tempDir("legal");
    std::filesystem::remove_all(dir);
    SaveStore store(dir);
    Settings s = store.loadSettings();
    CHECK_FALSE(s.sandboxLegalAccepted);
    s.sandboxLegalAccepted = true;
    store.saveSettings(s);
    CHECK(SaveStore(dir).loadSettings().sandboxLegalAccepted);
    std::filesystem::remove_all(dir);
}
