// Progression state (docs/05-gameplay.md §4, §8): the semantics of st::Settings, st::GameProgress and
// st::LocationState with their *Utils ports. Plain value types, no I/O here (persistence: save_store.h).
#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace aa::game {

constexpr int kLocationCount = 4;                     // the four chapter books
constexpr int kItemTypeCount = 43;                    // GameProgress item unlock bytes, indexed by ItemType
constexpr int kMaxLevelsPerLocation = 0x60;           // LocationState level slots (96)
// st::StarsToUnlockLocation: total stars needed to open each location [verified].
constexpr std::array<int, kLocationCount> kStarsToUnlockLocation = {0, 30, 75, 135};

// st::Settings (the fields the remake keeps): `soundEffectsOn` / `musicOn` default to true. The original's
// SetAudioState wrote `soundEffectsOn = on, musicOn = true` and AudioEnabled = soundEffectsOn && musicOn
// [verified: SettingsUtils::SetAudioState / AudioEnabled, Settings::Settings] — a single toggle. The remake
// splits them (docs/06 §4): `soundEffectsOn` is the speaker button (the master mute — everything off, as the
// original's toggle did), `musicOn` the note button (the background music alone). Old saves keep their meaning.
struct Settings {
    bool soundEffectsOn = true;
    bool musicOn = true;
    std::string playerName;   // SettingsParams::DefaultPlayerName when never set
    std::string locale;       // remake: "" = follow the OS, else one of en_EN/fr_FR/it_IT/de_DE/es_ES
    // Settings profile +3: the My Contraptions legal prompt was confirmed (MyContraptionsView::
    // MessageConfirmed sets it and saves; Show hides the prompt while it is set) [verified].
    bool sandboxLegalAccepted = false;

    void setAudioState(bool on) { soundEffectsOn = on; }
    void setMusicState(bool on) { musicOn = on; }
};

// st::LocationInfo (0_Location.plist of a chapter): index, the ordered level list. The original's four unlisted
// Treehouse files (docs/12 §1) are appended after the listed ones by AppState::loadCatalogue, so `levels` here
// is longer than the original's own play order (docs/02 §1).
struct LocationInfo {
    int index = 0;
    std::string chapter;                  // asset directory, e.g. "00_Classroom"
    std::string nameId;                   // CHAPTER_NAME_CHAPTER1..4
    std::vector<std::string> levels;      // level file names in play order

    int levelCount() const { return static_cast<int>(levels.size()); }
    int maxStarCount() const { return levelCount() * 3; }   // LocationInfoUtils::GetMaxStarCount
};

// st::GameProgress (0x800 bytes, docs/05 §4): per-location flags and star totals, the item unlock bytes,
// the three extra-book flags.
struct LocationProgress {
    bool unlocked = false;              // +0x20 + 0x10·loc
    bool chapterCompleteShown = false;  // +1
    bool threeStarsShown = false;       // +2
    int stars = 0;                      // +4
};

struct GameProgress {
    bool myContraptions = false;         // +0
    bool worldOfContraptions = false;    // +0x10
    bool levelOfTheWeek = false;         // +0x7A4
    std::array<LocationProgress, kLocationCount> locations{};
    std::array<bool, kItemTypeCount> itemUnlocked{};   // +0x160 + 0x10·type
    // Remake-only debug aid (--unlock-all, docs/13): not part of the original layout, never (de)serialised
    // (SaveStore reads/writes `locations[i].unlocked` alone) — locationUnlocked() reports every chapter open
    // without touching the stored flag, so a session run with it leaves the save file exactly as it was.
    bool unlockAll = false;

    // GameProgressUtils::GetCollectedStarCount: the four star totals.
    int collectedStarCount() const;
    // GameProgressUtils::GetUnlockedLocationCount.
    int unlockedLocationCount() const;
    // locations[i].unlocked, or true for every location while unlockAll is set (debug aid, not persisted).
    bool locationUnlocked(int i) const { return unlockAll || locations[static_cast<std::size_t>(i)].unlocked; }
    // GameProgressUtils::CheckForNewLocationUnlocks: opens every location whose threshold the total meets,
    // sets the My-Contraptions flag once the Classroom's chapter-complete panel was shown, and the WoC /
    // LotW flags unconditionally; true when a location was opened [verified].
    bool checkForNewLocationUnlocks();
    // GameProgressUtils::AddEarnedStars: adds to the location's total and re-checks the unlocks.
    void addEarnedStars(int stars, int location);
    // GameProgressUtils::UnlockItems: the fixed per-chapter item set (+ the bonus item with every star)
    // [verified; docs/05 §4]. The original saves the file inside; the caller saves here.
    void unlockItems(int location, bool allStars);
};

// st::LocationState (docs/05 §4): the current level index, the per-level {status, played} slots.
// Status: 0 no level, 1 locked, 2 unlocked, 3 + n completed with n stars.
struct LevelSlot {
    int status = 0;
    bool played = false;
};

struct LocationState {
    int currentLevel = 0;      // +0
    bool visited = false;      // +4
    bool finished = false;     // +5
    std::array<LevelSlot, kMaxLevelsPerLocation> levels{};
    // Remake-only debug aid (--unlock-all, docs/13): not part of the original layout, never (de)serialised
    // (SaveStore reads/writes each slot's `status` alone). Set by AppState::loadLocation *after* SaveStore::
    // loadLocation has already returned (repairPages has run on the real values by then), so status() below
    // reporting every level unlocked never feeds back into a stored status; AppState::saveLocation /
    // saveProgress write nothing while it is set (a cheat session is read-only for progress — a completion
    // stored on a still-locked page would otherwise unlock the rest of the page through repairPages), so
    // a session run with it leaves the save files exactly as they were.
    bool unlockAll = false;

    // LocationStateUtils::Load without a save file: levels 0–3 = 2, the rest of the location 1, beyond
    // it 0 — then the page repair of the load path (`repairPages`).
    static LocationState fresh(const LocationInfo& info);
    // The tail of LocationStateUtils::Load: the first level is at least unlocked, and inside every page of
    // four an unlocked level unlocks the ones after it [verified].
    void repairPages(const LocationInfo& info);

    // The slot's stored status, or at least 2 (unlocked) for every level while unlockAll is set — a
    // already-completed level (status 3+n) still reports its real, higher value.
    int status(int level) const {
        const int s = levels[static_cast<std::size_t>(level)].status;
        return unlockAll ? std::max(s, 2) : s;
    }
    // LocationStateUtils::MarkLevelAsDone: status = max(status, 3 + stars); when >= 3 levels of the page
    // are completed the next page's locked (1) levels become unlocked (2) [verified].
    void markLevelAsDone(int collectedStars, int level, const LocationInfo& info);
    // LocationStateUtils::WasLevelImproved: 3 + stars beats the stored status.
    bool wasLevelImproved(int collectedStars, int level) const;
    // LocationStateUtils::CanPlayNextLevel: a next level exists and its status is > 1.
    bool canPlayNextLevel(const LocationInfo& info) const;
    // LocationStateUtils::GetLevelStarCount: clamp(status - 3, 0, 3).
    int levelStarCount(int level) const;
    // LocationStateUtils::GetStarCount: the sum of (status - 3) over completed levels.
    int starCount(const LocationInfo& info) const;
    // LocationStateUtils::GetCompletedLevelsCount: levels with status > 2.
    int completedLevelsCount(const LocationInfo& info) const;
    void setLevelPlayed(int level) { levels[static_cast<std::size_t>(level)].played = true; }
    bool isLevelPlayed(int level) const { return levels[static_cast<std::size_t>(level)].played; }
    // LevelSelectionView::Init's initial page: the first unplayed level, backed up to a playable one.
    int firstUnplayedLevel(const LocationInfo& info) const;
};

}  // namespace aa::game
