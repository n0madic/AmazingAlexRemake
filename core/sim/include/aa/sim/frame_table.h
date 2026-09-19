// Atlas frame rectangles (st::Frame, docs/01-asset-formats.md §3): the physics templates derive their
// sizes from the GameItems.plist frames, in file order (docs/04-physics.md §4).
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace aa::sim {

// Pixel rectangle of one atlas frame (texture pixels, y down), as the original stores it: x0, y0, x1, y1.
struct Frame {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;   // x0 + w
    float y1 = 0.0f;   // y0 + h

    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
};

struct FrameTable {
    std::vector<Frame> frames;                        // index = position in the atlas file
    std::unordered_map<std::string, int> indexByName; // "TennisBall.png" -> 138

    const Frame& at(int index) const { return frames.at(static_cast<std::size_t>(index)); }
    int size() const { return static_cast<int>(frames.size()); }
    // Index of a frame by name, -1 when absent.
    int find(const std::string& name) const {
        auto it = indexByName.find(name);
        return it == indexByName.end() ? -1 : it->second;
    }
};

}  // namespace aa::sim
