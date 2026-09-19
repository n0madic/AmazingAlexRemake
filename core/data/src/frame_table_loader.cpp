#include "aa/data/frame_table_loader.h"

namespace aa::data {

using aa::sim::Frame;
using aa::sim::FrameTable;

FrameTable loadFrameTable(const JsonNode& root, AtlasInfo* info) {
    if (info) {
        info->texture = root.getString("texture");
        const JsonNode size = root.child("size");
        info->width = size.at(0).getInt();
        info->height = size.at(1).getInt();
    }
    FrameTable table;
    for (const JsonNode& f : root.child("frames").array()) {
        // st::Frame: x0 = x, x1 = x + w, y0 = y, y1 = y + h (pixels, as floats) — docs/01 §3.
        const float x = static_cast<float>(f.getInt("x"));
        const float y = static_cast<float>(f.getInt("y"));
        const float w = static_cast<float>(f.getInt("w"));
        const float h = static_cast<float>(f.getInt("h"));
        Frame frame;
        frame.x0 = x;
        frame.y0 = y;
        frame.x1 = x + w;
        frame.y1 = y + h;
        const std::string name = f.getString("name");
        table.indexByName[name] = static_cast<int>(table.frames.size());
        table.frames.push_back(frame);
    }
    return table;
}

FrameTable loadFrameTableFile(const std::string& path, AtlasInfo* info) {
    const JsonDoc doc = JsonDoc::parseFile(path);
    return loadFrameTable(doc.root(), info);
}

}  // namespace aa::data
