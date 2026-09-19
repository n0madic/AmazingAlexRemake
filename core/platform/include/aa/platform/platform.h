// The per-OS answers the application needs (docs/10 §6, M7). Remake code with no original counterpart:
// the original's Java activity (AAActivity / MyRenderer JNI) and framework::OSInterface supplied the same
// facts — the screen, the data directory, the locale, the lifecycle calls — through nativeInit /
// nativePause / nativeResume / nativeInput.
#pragma once

#include "aa/data/asset_root.h"

#include <functional>
#include <string>
#include <vector>

namespace aa::platform {

// Applies branding/icon.png to an already-created native desktop window. Mobile launchers and Web
// supply their icons through their packages instead.
void setWindowIconFromAssets(const std::string& assetDir);

struct PlatformInfo {
    // Writable application data (Android: ANativeActivity::internalDataPath; Web: IDBFS /persistent).
    // Empty on the desktop, where SaveStore::defaultDir picks the OS user-data directory.
    std::string dataDir;
    // The imported asset tree (Android: `aa`, the APK's `assets/aa/` prefix, read in place through
    // assetFileReader and raylib's wrapped fopen; a macOS .app: `Contents/Resources/assets`). Empty for
    // a bare desktop binary (`--assets` is required).
    std::string assetDir;
    // The OS language preference list (docs/06 §3): LC_ALL / LC_MESSAGES / LANG on the desktop (macOS
    // adds the system's preferred languages after them), the AConfiguration language + country on Android.
    std::vector<std::string> languages;
    // DeviceParams::IsTablet: the Android original sets it unconditionally in GameApp::GameApp [verified:
    // 0xa7b08], so it never pinch-zooms nor edge-scrolls; a desktop window is a phone (10 §6).
    bool tablet = false;
    // A mobile build: the window takes the display, the input is touch, the launch arguments come from
    // the platform (Android: the `debug.amazingalex.args` system property and `<dataDir>/args.txt`).
    bool mobile = false;
    std::vector<std::string> launchArgs;
    // Web only: the browser viewport in CSS pixels (window.innerWidth/innerHeight), read before
    // InitWindow so ScreenLayout::compute lays the game out for the page's actual aspect ratio instead
    // of AppOptions' 1024x768 default. 0 elsewhere, where AppOptions' own width/height apply.
    int width = 0;
    int height = 0;
};

PlatformInfo queryPlatform();

// Android: stdout / stderr piped to logcat (tag "amazing_alex"); no-op elsewhere. Call once, first.
void installLogSink();

// The activity lifecycle the original answered with nativePause / nativeResume (GameApp::activate +
// activateAudio): Android wraps raylib's android_app command callback after InitWindow (the frame loop
// is blocked while the activity is paused, so both hooks run from within EndDrawing's event poll);
// no-op elsewhere.
struct LifecycleHooks {
    std::function<void()> onPause;
    std::function<void()> onResume;
};
void installLifecycleHooks(LifecycleHooks hooks);

// Android: true once after the system cancelled the touch stream (AMOTION_EVENT_ACTION_CANCEL — a
// gesture the OS took over, the notification shade); raylib only drops its touch points, so the frame
// loop asks here and cancels its fingers instead of ending them, as the original's nativeInput action 3
// queued Cancelled (a button under the finger does not fire). Elsewhere: false.
bool takeTouchCancel();

// The reader AssetRoot uses for the asset tree: the std::ifstream default on the desktop and Web (the
// tree is a directory — on Web, the preloaded MEMFS blob), raylib's LoadFileData on Android, whose
// linker-wrapped fopen serves a relative path (`aa/...`) from the APK's assets (tools/build_android.sh
// packs the imported tree under `assets/aa`). The reader throws when a file cannot be read.
aa::data::FileReader assetFileReader();

}  // namespace aa::platform
