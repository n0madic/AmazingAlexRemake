// The imported asset tree (tools/import_assets.py output, docs/12-asset-tree.md): the one access
// interface to it. Files are read through a FileReader the platform supplies (the desktop and Web read
// the directory; Android reads the APK's `assets/aa/` in place through raylib's file reader), and
// `exists` / `list` are answered from manifest.json's "files" map, so no consumer touches the filesystem.
// `path()` remains for raylib's own loaders (LoadTexture / LoadSound / LoadMusicStream), which open the
// same tree through fopen.
#pragma once

#include "aa/data/json.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aa::data {

struct Manifest {
    int format = 0;             // manifest format version
    std::string sourceKind;     // "ipa" | "apk" | "bundle" | "decrypted"
    std::string profile;        // UI profile imported, e.g. "2048X1536"
    std::map<std::string, int> counts;          // "levels", "chapters", "frames.GameItems", ...
    std::map<std::string, std::string> sha1;    // relative path → sha1 of the emitted file
};

// Reads a whole file by its full path (`dir/relative`); throws when it cannot be read.
using FileReader = std::function<std::string(const std::string& path)>;

class AssetRoot {
public:
    // Throws when manifest.json is missing or unreadable.
    explicit AssetRoot(std::string dir, FileReader reader = defaultFileReader);
    // The std::ifstream reader (readTextFile): the desktop / Web tree on disk.
    static std::string defaultFileReader(const std::string& path);

    const std::string& dir() const { return dir_; }
    const Manifest& manifest() const { return manifest_; }
    std::string path(const std::string& relative) const;   // dir/relative

    // The whole file `relative`; throws JsonError("<dir>/<relative>: cannot open ...") when unreadable.
    std::string read(const std::string& relative) const;
    // read() parsed as JSON (the document is named by its path for error messages).
    JsonDoc json(const std::string& relative) const;
    // Whether the manifest lists `relative` (manifest.json itself included) — no filesystem access.
    bool exists(const std::string& relative) const;
    // The manifest entries directly under `prefix` (a directory with its trailing '/'), as full relative
    // paths in sorted order; entries in subdirectories are not listed.
    std::vector<std::string> list(const std::string& prefix) const;

    std::string levelIndexPath(const std::string& chapter) const;   // levels/<chapter>/index.json
    std::string levelPath(const std::string& chapter, const std::string& name) const;
    std::string atlasJsonPath(const std::string& atlas) const;     // atlases/<atlas>.json
    std::string atlasPngPath(const std::string& atlas) const;
    std::string textsPath(const std::string& locale) const;        // texts/<locale>.json
    std::string tipsPath() const;

    // Chapter directories in play order (from manifest "chapters" list).
    const std::vector<std::string>& chapters() const { return chapters_; }

private:
    std::string dir_;
    FileReader reader_;
    Manifest manifest_;
    std::vector<std::string> chapters_;
};

}  // namespace aa::data
