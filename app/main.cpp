// amazing_alex — the game (docs/10 §6): the Rovio splash, the menus, the campaign with saves, audio and
// the five locales.
//   amazing_alex --assets <dir> [--save-dir <dir>] [--locale xx_XX] [--size WxH] [--fullscreen] [--no-audio]
//                [--nointro] [--frames N] [--headless] [--ui-screenshots <dir>] [--tablet | --phone]
// Keys in the game: mouse = the finger (drag items, the strip, the rotation ring), Esc = the back key
// (pause menu / previous screen / quit at the main menu), wheel = rotate the held item, F = flip it,
// Z / Y = undo / redo.
// On Android (a NativeActivity, docs/10 §6) there is no command line: the assets come from the APK, the
// saves live in the app's data directory, and the same options may be given through the system property
// `debug.amazingalex.args` (`adb shell setprop debug.amazingalex.args "--headless"`) or `files/args.txt`;
// relative --save-dir / --ui-screenshots paths are taken under the data directory.
//
// amazing_alex --viewer --assets <dir> [--level <chapter>/<name>] [--frames N | --headless]
//              [--screenshots <dir>] [--size WxH] [--no-markers] [--screenshot-markers] [--script <file>]
// The level viewer (docs/10 §10): the debug HUD, every level by index, the gate screenshots.
// Keys: ←/→ or PgUp/PgDn = previous / next level, Space = play / stop (rebuild only), Z / Y = undo / redo,
// R = restart, Delete = return the held item, T = strip in / out, F = flip the held item, P = pause-menu
// release, wheel = rotate the held item (zoom otherwise), S = set-up / simulation construction toggle,
// WASD + X = pan, +/− = zoom (0 resets), M = goal markers on/off, Esc = quit.
#include "aa/platform/app.h"
#include "aa/platform/platform.h"
#include "aa/platform/viewer.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: amazing_alex --assets <dir> [--save-dir <dir>] [--locale xx_XX] [--size WxH] [--fullscreen]\n"
                 "                    [--no-audio] [--nointro] [--frames N] [--headless] [--ui-screenshots <dir>] [--tablet | --phone]\n"
                 "       amazing_alex --viewer --assets <dir> [--level <chapter>/<name>] [--frames N | --headless]\n"
                 "                    [--screenshots <dir>] [--size WxH] [--no-markers] [--screenshot-markers]\n"
                 "                    [--script <file>] [--no-audio]\n");
    return 2;
}

bool parseSize(const char* text, int& width, int& height) {
    return std::sscanf(text, "%dx%d", &width, &height) == 2 && width > 0 && height > 0;
}

int runViewerMain(int argc, char** argv, const aa::platform::PlatformInfo& platform) {
    aa::platform::ViewerOptions options;
    options.assets = platform.assetDir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--viewer") continue;
        if (a == "--assets" && i + 1 < argc) options.assets = argv[++i];
        else if (a == "--level" && i + 1 < argc) options.level = argv[++i];
        else if (a == "--headless") options.headless = true;
        else if (a == "--frames" && i + 1 < argc) options.maxFrames = std::atoi(argv[++i]);
        else if (a == "--screenshots" && i + 1 < argc) options.screenshotDir = argv[++i];
        else if (a == "--size" && i + 1 < argc) {
            if (!parseSize(argv[++i], options.width, options.height)) {
                std::fprintf(stderr, "amazing_alex: --size expects WxH\n");
                return 2;
            }
        }
        else if (a == "--no-markers") options.markers = false;
        else if (a == "--screenshot-markers") options.screenshotMarkers = true;
        else if (a == "--script" && i + 1 < argc) options.script = argv[++i];
        else if (a == "--no-audio") options.audio = false;
        else return usage();
    }
    if (options.assets.empty()) {
        std::fprintf(stderr, "amazing_alex: --assets <dir> is required (run tools/import_assets.py first)\n");
        return 2;
    }
    return aa::platform::runViewer(options);
}

// A path given on a mobile build's argument line: relative ones go under the data directory.
std::string mobilePath(const aa::platform::PlatformInfo& platform, const std::string& path) {
    if (!platform.mobile || path.empty() || path[0] == '/') return path;
    return platform.dataDir + "/" + path;
}

int runGameMain(const std::vector<std::string>& args, const aa::platform::PlatformInfo& platform) {
    aa::platform::AppOptions options;
    options.assets = platform.assetDir;
    options.languages = platform.languages;
    options.mobile = platform.mobile;
    if (platform.tablet) options.tablet = 1;
    options.dataDir = platform.dataDir;
    // Web: lay the game out for the browser's actual viewport instead of the 1024x768 default.
    if (platform.width > 0 && platform.height > 0) {
        options.width = platform.width;
        options.height = platform.height;
    }
    const int argc = static_cast<int>(args.size());
    for (int i = 0; i < argc; ++i) {
        const std::string& a = args[static_cast<std::size_t>(i)];
        const auto next = [&]() -> const std::string& { return args[static_cast<std::size_t>(++i)]; };
        if (a == "--assets" && i + 1 < argc) options.assets = next();
        else if (a == "--save-dir" && i + 1 < argc) options.saveDir = mobilePath(platform, next());
        else if (a == "--locale" && i + 1 < argc) options.locale = next();
        else if (a == "--size" && i + 1 < argc) {
            if (!parseSize(next().c_str(), options.width, options.height)) {
                std::fprintf(stderr, "amazing_alex: --size expects WxH\n");
                return 2;
            }
        }
        else if (a == "--fullscreen") options.fullscreen = true;
        else if (a == "--no-audio") options.audio = false;
        else if (a == "--nointro") options.noIntro = true;
        else if (a == "--frames" && i + 1 < argc) options.maxFrames = std::atoi(next().c_str());
        else if (a == "--headless") options.headless = true;
        else if (a == "--ui-screenshots" && i + 1 < argc) options.screenshotDir = mobilePath(platform, next());
        else if (a == "--tablet") options.tablet = 1;
        else if (a == "--phone") options.tablet = 0;
        else if (a == "--unlock-all") options.unlockAll = true;   // undocumented (docs/13)
        else return usage();
    }
    if (options.assets.empty()) {
        std::fprintf(stderr, "amazing_alex: --assets <dir> is required (run tools/import_assets.py first)\n");
        return 2;
    }
    return aa::platform::runApp(options);
}

}  // namespace

int main(int argc, char** argv) {
    aa::platform::installLogSink();
    const aa::platform::PlatformInfo platform = aa::platform::queryPlatform();
    // The command line, or on a mobile build the platform's launch arguments (raylib's android_main
    // passes a single "raylib" argv).
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    if (platform.mobile) args = platform.launchArgs;
    bool viewer = false;
    for (const std::string& a : args) {
        if (a == "--viewer") viewer = true;
    }
    try {
        if (viewer) {
            std::vector<char*> argvCopy{argv[0]};
            for (std::string& a : args) argvCopy.push_back(a.data());
            return runViewerMain(static_cast<int>(argvCopy.size()), argvCopy.data(), platform);
        }
        return runGameMain(args, platform);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "amazing_alex: %s\n", ex.what());
        return 1;
    }
}
