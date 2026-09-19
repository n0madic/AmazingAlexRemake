#include "aa/data/asset_root.h"


namespace aa::data {

namespace {
constexpr const char* kManifest = "manifest.json";
}

AssetRoot::AssetRoot(std::string dir, FileReader reader) : dir_(std::move(dir)), reader_(std::move(reader)) {
    while (dir_.size() > 1 && (dir_.back() == '/' || dir_.back() == '\\')) dir_.pop_back();
    const JsonDoc doc = json(kManifest);
    const JsonNode root = doc.root();
    manifest_.format = root.getInt("format");
    manifest_.sourceKind = root.child("source").getString("kind");
    manifest_.profile = root.getString("profile");
    for (const JsonNode& c : root.child("chapters").array()) chapters_.push_back(c.getString());
    for (const auto& [key, node] : root.child("counts").members()) manifest_.counts[key] = node.getInt();
    for (const auto& [key, node] : root.child("files").members()) manifest_.sha1[key] = node.getString();
}

std::string AssetRoot::defaultFileReader(const std::string& path) { return readTextFile(path); }

std::string AssetRoot::path(const std::string& relative) const { return dir_ + "/" + relative; }

std::string AssetRoot::read(const std::string& relative) const { return reader_(path(relative)); }

JsonDoc AssetRoot::json(const std::string& relative) const {
    const std::string full = path(relative);
    return JsonDoc(reader_(full), full);
}

bool AssetRoot::exists(const std::string& relative) const {
    return relative == kManifest || manifest_.sha1.count(relative) != 0;
}

std::vector<std::string> AssetRoot::list(const std::string& prefix) const {
    std::vector<std::string> out;   // std::map iterates in key order, so the result is sorted
    for (auto it = manifest_.sha1.lower_bound(prefix); it != manifest_.sha1.end(); ++it) {
        const std::string& key = it->first;
        if (key.compare(0, prefix.size(), prefix) != 0) break;
        if (key.find('/', prefix.size()) != std::string::npos) continue;
        out.push_back(key);
    }
    return out;
}

std::string AssetRoot::levelIndexPath(const std::string& chapter) const {
    return path("levels/" + chapter + "/index.json");
}
std::string AssetRoot::levelPath(const std::string& chapter, const std::string& name) const {
    return path("levels/" + chapter + "/" + name + ".json");
}
std::string AssetRoot::atlasJsonPath(const std::string& atlas) const { return path("atlases/" + atlas + ".json"); }
std::string AssetRoot::atlasPngPath(const std::string& atlas) const { return path("atlases/" + atlas + ".png"); }
std::string AssetRoot::textsPath(const std::string& locale) const { return path("texts/" + locale + ".json"); }
std::string AssetRoot::tipsPath() const { return path("tips.json"); }

}  // namespace aa::data
