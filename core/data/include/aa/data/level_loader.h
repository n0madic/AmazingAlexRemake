// levels/<chapter>/<name>.json → aa::sim::Level (schema: docs/12-asset-tree.md §2).
#pragma once

#include "aa/data/json.h"
#include "aa/sim/level.h"
#include "aa/sim/world_state.h"

#include <string>
#include <vector>

namespace aa::data {

aa::sim::Level loadLevel(const JsonNode& root);
aa::sim::Level loadLevelFile(const std::string& path);

// levels/<chapter>/index.json: play order and the chapter's localisation id.
struct LevelIndex {
    std::string name;                 // "CHAPTER_NAME_CHAPTER1"
    std::vector<std::string> levels;    // file stems in play order
    std::vector<std::string> unlisted;  // level files the chapter's plist does not list (docs/12 §2)
};
LevelIndex loadLevelIndex(const JsonNode& root);
LevelIndex loadLevelIndexFile(const std::string& path);

}  // namespace aa::data
