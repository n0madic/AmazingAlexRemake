// core/game: the LocationStateUtils / GameProgressUtils / SettingsUtils semantics (docs/05 §4, §8) and the
// JSON save store (docs/10 §7).
#include "aa/game/localization.h"
#include "aa/game/progress.h"
#include "aa/game/save_store.h"

#include <doctest.h>

#include <filesystem>
#include <fstream>

using namespace aa::game;

namespace {

LocationInfo classroomInfo(int count = 28) {
    LocationInfo info;
    info.index = 0;
    info.chapter = "00_Classroom";
    for (int i = 0; i < count; ++i) info.levels.push_back("Level" + std::to_string(i));
    return info;
}

std::string tempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("aa_test_" + std::string(name));
    std::filesystem::remove_all(dir);
    return dir.string();
}

}  // namespace

TEST_CASE("progression: fresh location state") {
    const LocationInfo info = classroomInfo();
    const LocationState s = LocationState::fresh(info);
    for (int i = 0; i < 4; ++i) CHECK(s.status(i) == 2);
    for (int i = 4; i < 28; ++i) CHECK(s.status(i) == 1);
    CHECK(s.status(28) == 0);
    CHECK(s.completedLevelsCount(info) == 0);
    CHECK(s.starCount(info) == 0);
    CHECK(s.firstUnplayedLevel(info) == 0);
    CHECK_FALSE(s.isLevelPlayed(0));
}

TEST_CASE("progression: MarkLevelAsDone keeps the maximum and unlocks pages of four") {
    const LocationInfo info = classroomInfo();
    LocationState s = LocationState::fresh(info);
    s.markLevelAsDone(2, 0, info);
    CHECK(s.status(0) == 5);
    CHECK(s.levelStarCount(0) == 2);
    s.markLevelAsDone(1, 0, info);
    CHECK(s.status(0) == 5);   // a worse run does not lower it
    CHECK(s.status(4) == 1);   // one completed level of the page: nothing unlocked
    s.markLevelAsDone(0, 1, info);
    CHECK(s.status(4) == 1);
    s.markLevelAsDone(3, 2, info);
    // three of four completed: the next page opens
    for (int i = 4; i < 8; ++i) CHECK(s.status(i) == 2);
    CHECK(s.status(8) == 1);
    CHECK(s.completedLevelsCount(info) == 3);
    CHECK(s.starCount(info) == 5);
    CHECK(s.wasLevelImproved(3, 0));
    CHECK_FALSE(s.wasLevelImproved(2, 0));
    CHECK_FALSE(s.wasLevelImproved(0, 1));
    CHECK(s.wasLevelImproved(0, 3));   // unlocked, never completed: any completion improves it
    // the last page never unlocks past the level count
    LocationState last = LocationState::fresh(info);
    for (int i = 24; i < 28; ++i) last.levels[static_cast<std::size_t>(i)].status = 2;
    last.markLevelAsDone(3, 24, info);
    last.markLevelAsDone(3, 25, info);
    last.markLevelAsDone(3, 26, info);
    CHECK(last.status(28) == 0);
}

TEST_CASE("progression: CanPlayNextLevel and the level list page") {
    const LocationInfo info = classroomInfo();
    LocationState s = LocationState::fresh(info);
    s.currentLevel = 0;
    CHECK(s.canPlayNextLevel(info));
    s.currentLevel = 3;
    CHECK_FALSE(s.canPlayNextLevel(info));   // level 4 is locked
    s.currentLevel = 27;
    CHECK_FALSE(s.canPlayNextLevel(info));   // no next level
    // first unplayed level, backed up to a playable one
    s.setLevelPlayed(0);
    s.setLevelPlayed(1);
    CHECK(s.firstUnplayedLevel(info) == 2);
    for (int i = 0; i < 4; ++i) s.setLevelPlayed(i);
    CHECK(s.firstUnplayedLevel(info) == 3);   // level 4 is locked: back to 3
}

TEST_CASE("progression: repairPages unlocks the rest of a page after an unlocked level") {
    const LocationInfo info = classroomInfo();
    LocationState s;
    for (int i = 0; i < 28; ++i) s.levels[static_cast<std::size_t>(i)].status = 1;
    s.levels[5].status = 2;
    s.repairPages(info);
    CHECK(s.status(0) == 2);
    CHECK(s.status(1) == 2);   // page 0: level 0 was forced to 2, the rest follows
    CHECK(s.status(4) == 1);
    CHECK(s.status(5) == 2);
    CHECK(s.status(6) == 2);
    CHECK(s.status(7) == 2);
    CHECK(s.status(8) == 1);
}

TEST_CASE("progression: location unlock thresholds and the extra books") {
    GameProgress p;
    CHECK(p.checkForNewLocationUnlocks());   // the Classroom at 0 stars
    CHECK(p.locations[0].unlocked);
    CHECK_FALSE(p.locations[1].unlocked);
    CHECK(p.worldOfContraptions);
    CHECK(p.levelOfTheWeek);
    CHECK_FALSE(p.myContraptions);
    CHECK_FALSE(p.checkForNewLocationUnlocks());
    p.addEarnedStars(29, 0);
    CHECK_FALSE(p.locations[1].unlocked);
    p.addEarnedStars(1, 0);
    CHECK(p.locations[1].unlocked);
    CHECK(p.unlockedLocationCount() == 2);
    p.addEarnedStars(45, 1);
    CHECK(p.locations[2].unlocked);
    CHECK_FALSE(p.locations[3].unlocked);
    p.addEarnedStars(60, 2);
    CHECK(p.locations[3].unlocked);
    CHECK(p.collectedStarCount() == 135);
    p.locations[0].chapterCompleteShown = true;
    p.checkForNewLocationUnlocks();
    CHECK(p.myContraptions);
}

TEST_CASE("progression: UnlockItems sets") {
    GameProgress p;
    p.unlockItems(0, false);
    CHECK(p.itemUnlocked[1]);    // Shelf
    CHECK(p.itemUnlocked[23]);   // GoalStar
    CHECK(p.itemUnlocked[32]);   // LaundryBasket
    CHECK_FALSE(p.itemUnlocked[15]);   // Book needs every star
    CHECK_FALSE(p.itemUnlocked[12]);   // FishBowl never
    p.unlockItems(0, true);
    CHECK(p.itemUnlocked[15]);
    p.unlockItems(1, true);
    CHECK(p.itemUnlocked[34]);   // Slingshot
    CHECK(p.itemUnlocked[14]);   // BoxingGlove bonus
    p.unlockItems(2, false);
    CHECK(p.itemUnlocked[36]);
    CHECK_FALSE(p.itemUnlocked[13]);
    p.unlockItems(3, true);
    CHECK(p.itemUnlocked[42]);
    CHECK(p.itemUnlocked[39]);
    CHECK_FALSE(p.itemUnlocked[21]);   // Pulley never
}

TEST_CASE("progression: settings audio state") {
    // The remake's two switches are independent: the speaker (soundEffectsOn, the master mute) and the note
    // (musicOn); the original's SetAudioState forced musicOn = true.
    Settings s;
    CHECK(s.soundEffectsOn);
    CHECK(s.musicOn);
    s.setAudioState(false);
    CHECK_FALSE(s.soundEffectsOn);
    CHECK(s.musicOn);
    s.setMusicState(false);
    CHECK_FALSE(s.musicOn);
    s.setAudioState(true);
    CHECK(s.soundEffectsOn);
    CHECK_FALSE(s.musicOn);
    s.setMusicState(true);
    CHECK(s.musicOn);
}

TEST_CASE("progression: save store round trip") {
    const std::string dir = tempDir("saves");
    const LocationInfo info = classroomInfo();
    {
        SaveStore store(dir);
        Settings s;
        s.setAudioState(false);
        s.setMusicState(false);
        s.playerName = "Alex";
        s.locale = "de_DE";
        store.saveSettings(s);
        GameProgress p;
        p.checkForNewLocationUnlocks();
        p.addEarnedStars(31, 0);
        p.locations[0].chapterCompleteShown = true;
        p.unlockItems(0, true);
        store.saveProgress(p);
        LocationState l = LocationState::fresh(info);
        l.markLevelAsDone(3, 0, info);
        l.markLevelAsDone(2, 1, info);
        l.markLevelAsDone(1, 2, info);
        l.setLevelPlayed(0);
        l.currentLevel = 2;
        l.visited = true;
        store.saveLocation(l, info);
    }
    {
        SaveStore store(dir);
        const Settings s = store.loadSettings();
        CHECK_FALSE(s.soundEffectsOn);
        CHECK_FALSE(s.musicOn);
        CHECK(s.playerName == "Alex");
        CHECK(s.locale == "de_DE");
        const GameProgress p = store.loadProgress();
        CHECK(p.locations[0].unlocked);
        CHECK(p.locations[1].unlocked);
        CHECK(p.locations[0].stars == 31);
        CHECK(p.locations[0].chapterCompleteShown);
        CHECK(p.itemUnlocked[15]);
        CHECK_FALSE(p.itemUnlocked[17]);
        const LocationState l = store.loadLocation(info);
        CHECK(l.currentLevel == 2);
        CHECK(l.visited);
        CHECK(l.status(0) == 6);
        CHECK(l.status(1) == 5);
        CHECK(l.status(2) == 4);
        CHECK(l.status(3) == 2);
        CHECK(l.status(4) == 2);   // the page unlock survived
        CHECK(l.status(8) == 1);
        CHECK(l.isLevelPlayed(0));
        CHECK_FALSE(l.isLevelPlayed(1));
        CHECK(store.asideFiles().empty());
    }
    // a level added to the location later is locked; a fresh store has defaults
    {
        SaveStore store(dir);
        const LocationState l = store.loadLocation(classroomInfo(29));
        CHECK(l.status(28) == 1);
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("progression: repairPages stays inside the slots of a full location") {
    const LocationInfo info = classroomInfo(kMaxLevelsPerLocation);   // My Contraptions can hold all 96
    const LocationState s = LocationState::fresh(info);
    for (int i = 0; i < 4; ++i) CHECK(s.status(i) == 2);
    for (int i = 4; i < kMaxLevelsPerLocation; ++i) CHECK(s.status(i) == 1);
}

TEST_CASE("progression: a save with wrong-typed members is set aside like a parse error") {
    const std::string dir = tempDir("wrongtype");
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir + "/settings.json");
        out << R"({"musicOn": 1, "playerName": "x"})";
    }
    {
        std::ofstream out(dir + "/progress.json");
        out << R"({"myContraptions": true, "locations": [{"stars": "12"}]})";
    }
    {
        std::ofstream out(dir + "/location_0.json");
        out << R"({"currentLevel": 1.5})";
    }
    SaveStore store(dir);
    const Settings s = store.loadSettings();
    CHECK(s.musicOn);
    CHECK(s.playerName.empty());   // the whole file falls back, not just the bad member
    const GameProgress p = store.loadProgress();
    CHECK_FALSE(p.myContraptions);
    const LocationInfo info = classroomInfo();
    const LocationState l = store.loadLocation(info);
    CHECK(l.status(0) == 2);
    CHECK(l.status(4) == 1);
    REQUIRE(store.asideFiles().size() == 3);
    CHECK_FALSE(std::filesystem::exists(dir + "/location_0.json"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("progression: a corrupt save is set aside and replaced by defaults") {
    const std::string dir = tempDir("corrupt");
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir + "/settings.json");
        out << "{ this is not json";
    }
    {
        std::ofstream out(dir + "/progress.json");
        out << "[1, 2, 3]";   // valid JSON, wrong shape
    }
    SaveStore store(dir);
    const Settings s = store.loadSettings();
    CHECK(s.soundEffectsOn);
    CHECK(s.musicOn);
    const GameProgress p = store.loadProgress();
    CHECK_FALSE(p.locations[0].unlocked);
    REQUIRE(store.asideFiles().size() == 2);
    CHECK(std::filesystem::exists(store.asideFiles()[0]));
    CHECK_FALSE(std::filesystem::exists(dir + "/settings.json"));
    store.saveSettings(s);
    CHECK(std::filesystem::exists(dir + "/settings.json"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("localization: locale choice") {
    CHECK(chooseLocale({}) == "en_EN");
    CHECK(chooseLocale({"de_DE"}) == "de_DE");
    CHECK(chooseLocale({"ru_RU", "fr_FR"}) == "fr_FR");
    CHECK(chooseLocale({"es_ES.UTF-8"}) == "es_ES");
    CHECK(chooseLocale({"it"}) == "it_IT");
    CHECK(chooseLocale({"pt_BR"}) == "en_EN");
    Localization l;
    l.setLocale("fr_FR", {{"HELLO", "Bonjour"}});
    CHECK(l.text("HELLO") == "Bonjour");
    CHECK(l.text("MISSING") == "MISSING");
    CHECK(l.imageName("BEST_RESULT") == "BEST_RESULT_FR");
    CHECK(l.languageSuffix() == "FR");
}
