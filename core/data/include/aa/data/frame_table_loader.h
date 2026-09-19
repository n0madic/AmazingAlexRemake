// atlases/<name>.json → aa::sim::FrameTable (docs/12-asset-tree.md §3): frames in file order, so the
// index of "TennisBall.png" in GameItems is 138 as the physics templates expect (docs/03 §4).
#pragma once

#include "aa/data/json.h"
#include "aa/sim/frame_table.h"

#include <string>

namespace aa::data {

struct AtlasInfo {
    std::string texture;   // PNG file name next to the JSON
    int width = 0;
    int height = 0;
};

aa::sim::FrameTable loadFrameTable(const JsonNode& root, AtlasInfo* info = nullptr);
aa::sim::FrameTable loadFrameTableFile(const std::string& path, AtlasInfo* info = nullptr);

}  // namespace aa::data
