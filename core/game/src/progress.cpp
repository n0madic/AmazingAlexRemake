#include "aa/game/progress.h"

#include <algorithm>

namespace aa::game {

namespace {

constexpr int kPageSize = 4;   // MarkLevelAsDone unlocks in pages of four (level >> 2)

// GameProgressUtils::UnlockItems item sets by location (docs/05 §4), ItemType values.
constexpr int kClassroomItems[] = {23, 1, 2, 4, 7, 10, 11, 32, 5, 6};
constexpr int kBackyardItems[] = {17, 18, 16, 8, 9, 3, 22, 34, 29, 28, 20};
constexpr int kBedroomItems[] = {26, 33, 19, 37, 38, 36, 35, 30, 27, 25};
constexpr int kTreehouseItems[] = {42, 41};
constexpr int kBonusItems[kLocationCount] = {15, 14, 13, 39};   // Book, BoxingGlove, PiggyBank, Helicopter

}  // namespace

int GameProgress::collectedStarCount() const {
    return locations[0].stars + locations[1].stars + locations[2].stars + locations[3].stars;
}

int GameProgress::unlockedLocationCount() const {
    int n = 0;
    for (const LocationProgress& l : locations) n += l.unlocked ? 1 : 0;
    return n;
}

bool GameProgress::checkForNewLocationUnlocks() {
    const int stars = collectedStarCount();
    int opened = 0;
    for (int i = 0; i < kLocationCount; ++i) {
        LocationProgress& l = locations[static_cast<std::size_t>(i)];
        if (!l.unlocked && kStarsToUnlockLocation[static_cast<std::size_t>(i)] <= stars) {
            l.unlocked = true;
            ++opened;
        }
        if (i == 0 && locations[0].chapterCompleteShown) myContraptions = true;
    }
    worldOfContraptions = true;
    levelOfTheWeek = true;
    return opened > 0;
}

void GameProgress::addEarnedStars(int stars, int location) {
    locations[static_cast<std::size_t>(location)].stars += stars;
    checkForNewLocationUnlocks();
}

void GameProgress::unlockItems(int location, bool allStars) {
    auto unlock = [&](const int* items, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) itemUnlocked[static_cast<std::size_t>(items[i])] = true;
    };
    switch (location) {
    case 0: unlock(kClassroomItems, std::size(kClassroomItems)); break;
    case 1: unlock(kBackyardItems, std::size(kBackyardItems)); break;
    case 2: unlock(kBedroomItems, std::size(kBedroomItems)); break;
    case 3: unlock(kTreehouseItems, std::size(kTreehouseItems)); break;
    default: return;
    }
    if (allStars) itemUnlocked[static_cast<std::size_t>(kBonusItems[location])] = true;
}

LocationState LocationState::fresh(const LocationInfo& info) {
    LocationState s;
    for (int i = 0; i < kMaxLevelsPerLocation; ++i) {
        LevelSlot& slot = s.levels[static_cast<std::size_t>(i)];
        slot.played = false;
        slot.status = i < info.levelCount() ? (i < kPageSize ? 2 : 1) : 0;
    }
    s.repairPages(info);
    return s;
}

void LocationState::repairPages(const LocationInfo& info) {
    // The tail of LocationStateUtils::Load: the first level is at least unlocked; then, page by page up to
    // and including page (levelCount >> 2), the first unlocked level of a page unlocks the rest of it.
    // A full location (levelCount == kMaxLevelsPerLocation) would name one page past the array: clamp.
    levels[0].status = std::max(levels[0].status, 2);
    const int lastPage = std::min(info.levelCount() >> 2, kMaxLevelsPerLocation / kPageSize - 1);
    for (int page = 0; page <= lastPage; ++page) {
        const int base = page * kPageSize;
        int first = 0;
        while (first < kPageSize && status(base + first) < 2) ++first;
        if (first == kPageSize) continue;
        for (int k = first; k < kPageSize; ++k) {
            LevelSlot& slot = levels[static_cast<std::size_t>(base + k)];
            if (slot.status < 2) slot.status = 2;
        }
    }
}

void LocationState::markLevelAsDone(int collectedStars, int level, const LocationInfo& info) {
    const int pageStart = (level >> 2) * kPageSize;
    LevelSlot& slot = levels[static_cast<std::size_t>(level)];
    slot.status = std::max(slot.status, collectedStars + 3);
    if (pageStart + kPageSize < info.levelCount()) {
        int completed = 0;
        for (int k = 0; k < kPageSize; ++k) completed += status(pageStart + k) > 2 ? 1 : 0;
        if (completed > 2) {
            for (int k = 0; k < kPageSize; ++k) {
                LevelSlot& next = levels[static_cast<std::size_t>(pageStart + kPageSize + k)];
                if (next.status == 1) next.status = 2;
            }
        }
    }
}

bool LocationState::wasLevelImproved(int collectedStars, int level) const { return status(level) < collectedStars + 3; }

bool LocationState::canPlayNextLevel(const LocationInfo& info) const {
    if (info.levelCount() - 1 <= currentLevel) return false;
    return status(currentLevel + 1) > 1;
}

int LocationState::levelStarCount(int level) const {
    // GetLevelStarCount: clamp(status - 3, 0, 3) computed in float (the int result is the same).
    const int n = status(level) - 3;
    return n < 0 ? 0 : (n > 3 ? 3 : n);
}

int LocationState::starCount(const LocationInfo& info) const {
    int total = 0;
    for (int i = 0; i < info.levelCount(); ++i) {
        if (status(i) > 3) total += status(i) - 3;
    }
    return total;
}

int LocationState::completedLevelsCount(const LocationInfo& info) const {
    int n = 0;
    for (int i = 0; i < info.levelCount(); ++i) n += status(i) > 2 ? 1 : 0;
    return n;
}

int LocationState::firstUnplayedLevel(const LocationInfo& info) const {
    // LevelSelectionView::Refresh [verified]: the first unplayed level, backed up while the level before
    // it is not playable; when every level was played, the original scans the completed levels with a
    // stride of two from level 2 (as compiled) and falls back to level 0 past the end.
    const int count = info.levelCount();
    int idx = 0;
    if (levels[0].played) {
        while (idx < kMaxLevelsPerLocation && levels[static_cast<std::size_t>(idx)].played) ++idx;
        if (idx < kMaxLevelsPerLocation && status(idx) < 2) {
            while (idx > 0 && status(idx) < 2) --idx;
        }
    }
    if (idx >= count) {
        if (status(0) < 3) idx = 1;
        else {
            idx = 3;
            for (int probe = 2; probe < kMaxLevelsPerLocation && status(probe) > 2; probe += 2) idx = probe + 3;
        }
        if (idx >= count) idx = 0;
    }
    return idx;
}

}  // namespace aa::game
