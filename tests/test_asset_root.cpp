// AssetRoot as the tree's reader: exists / list answered from the manifest, read / json through the
// FileReader (a fake one stands in for the Android APK reader), errors carry the path.
#include "aa/data/asset_root.h"
#include "test_support.h"

#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using aa::data::AssetRoot;
using aa::data::JsonError;

TEST_CASE("AssetRoot answers exists and list from the manifest") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    CHECK(root.exists("manifest.json"));
    CHECK(root.exists("tips.json"));
    CHECK(root.exists("atlases/GameItems.json"));
    CHECK(root.exists("sounds/UIButtonPush.mp3"));
    CHECK(root.exists("music/Theme.mp3"));
    CHECK_FALSE(root.exists("sounds/NoSuchClip.mp3"));
    CHECK_FALSE(root.exists("atlases"));

    // list("ui/<profile>/") is the on-disk *.json set of the profile directory, sorted.
    const std::string profile = "ui/" + root.manifest().profile + "/";
    std::vector<std::string> onDisk;
    for (const auto& entry : std::filesystem::directory_iterator(root.path(profile))) {
        if (entry.path().extension() == ".json") onDisk.push_back(profile + entry.path().filename().string());
    }
    std::sort(onDisk.begin(), onDisk.end());
    std::vector<std::string> listed;
    for (const std::string& file : root.list(profile)) {
        if (file.size() > 5 && file.compare(file.size() - 5, 5, ".json") == 0) listed.push_back(file);
    }
    CHECK(listed == onDisk);
    CHECK_FALSE(listed.empty());
    CHECK(std::is_sorted(listed.begin(), listed.end()));

    // Direct children only: "levels/" holds chapter directories, no files.
    CHECK(root.list("levels/").empty());
    CHECK_FALSE(root.list("levels/" + root.chapters().front() + "/").empty());
}

TEST_CASE("AssetRoot lists no empty file (the Android reader treats a null LoadFileData as an error)") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    for (const auto& entry : root.manifest().sha1) {
        CHECK_MESSAGE(std::filesystem::file_size(root.path(entry.first)) > 0, entry.first);
    }
}

TEST_CASE("AssetRoot::read reports the path of a missing file") {
    AA_REQUIRE_ASSETS();
    const AssetRoot root(assetsDir());
    CHECK(root.read("tips.json") == aa::data::readTextFile(root.path("tips.json")));
    CHECK_THROWS_WITH_AS(root.read("no/such.json"), (root.path("no/such.json") + ": cannot open").c_str(), JsonError);
    CHECK_THROWS_AS(root.json("no/such.json"), JsonError);
}

TEST_CASE("AssetRoot reads through a custom FileReader (the Android path without a device)") {
    std::map<std::string, std::string> files = {
        {"aa/manifest.json", R"({"format": 1, "source": {"kind": "apk"}, "profile": "P", "chapters": ["c1"],
                                 "counts": {"levels": 1}, "files": {"levels/c1/index.json": "x", "ui/P/A.json": "y"}})"},
        {"aa/levels/c1/index.json", R"({"name": "CHAPTER", "levels": ["L"]})"},
    };
    std::vector<std::string> asked;
    const AssetRoot root("aa/", [&](const std::string& path) {
        asked.push_back(path);
        auto it = files.find(path);
        if (it == files.end()) throw JsonError(path + ": cannot open");
        return it->second;
    });
    CHECK(root.dir() == "aa");
    CHECK(root.manifest().profile == "P");
    CHECK(root.chapters() == std::vector<std::string>{"c1"});
    CHECK(asked == std::vector<std::string>{"aa/manifest.json"});
    CHECK(root.exists("ui/P/A.json"));
    CHECK(root.exists("manifest.json"));
    CHECK_FALSE(root.exists("ui/P/B.json"));
    CHECK(root.list("ui/P/") == std::vector<std::string>{"ui/P/A.json"});
    CHECK(root.json("levels/c1/index.json").root().getString("name") == "CHAPTER");
    CHECK(asked.back() == "aa/levels/c1/index.json");
    CHECK_THROWS_WITH_AS(root.read("ui/P/A.json"), "aa/ui/P/A.json: cannot open", JsonError);
}
