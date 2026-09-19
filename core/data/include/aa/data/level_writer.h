// aa::sim::Level → the importer's level JSON (docs/12-asset-tree.md §2): the sandbox editor's save path
// (LevelLayoutUtils::SavePlist wrote an encrypted plist; the remake writes the same schema the loader reads).
#pragma once

#include "aa/sim/level.h"

#include <string>

namespace aa::data {

// The JSON text of `level` in the schema of levels/<chapter>/<name>.json (version 7; the float fields are
// printed by cJSON with up to 17 significant digits, so loadLevel() reproduces every float bit for bit).
std::string levelToJson(const aa::sim::Level& level);
// Writes `level` to `path` atomically (temp file + rename); throws std::runtime_error on an I/O failure.
void writeLevelFile(const std::string& path, const aa::sim::Level& level);

}  // namespace aa::data
