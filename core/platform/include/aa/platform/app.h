// The game application (GameApp + framework::App of the original, docs/10 §6): the window, the asset
// tree, the saves, the audio, the UI scene stack and the level session; the input mapping (desktop:
// mouse = the one finger, Esc = the back key; mobile: the touch points by id, the BACK key); the
// headless campaign smoke and the scene screenshots.
#pragma once

#include <string>
#include <vector>

namespace aa::platform {

struct AppOptions {
    std::string assets;             // imported asset tree (tools/import_assets.py; Android: the APK prefix `aa`)
    std::string saveDir;            // empty = dataDir/saves on Android, dataDir on Web, else the OS user-data directory (SaveStore::defaultDir)
    std::string locale;             // empty = the settings file, else the OS language
    std::vector<std::string> languages;   // the OS language preferences (PlatformInfo); empty = the environment
    int width = 1024;
    int height = 768;
    bool fullscreen = false;
    bool mobile = false;            // the window is the display, the input is touch (PlatformInfo::mobile)
    std::string dataDir;            // the app's private data directory (PlatformInfo::dataDir; Android: internalDataPath)
    int tablet = -1;                // DeviceParams::IsTablet: 1 / 0, −1 = the platform's answer
    bool audio = true;
    bool noIntro = false;           // start at the main menu, bypassing the decorative Rovio/loading splash
    bool headless = false;          // no window: walk the campaign with scripted taps, exit 0 on success
    std::string screenshotDir;      // non-empty: write one PNG per scene of the scripted walk, then exit
    int maxFrames = 0;              // > 0: close after this many frames
    bool unlockAll = false;         // --unlock-all: every chapter and level open, undocumented (docs/13)
};

// Returns the process exit code.
int runApp(const AppOptions& options);

}  // namespace aa::platform
