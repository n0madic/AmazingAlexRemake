// The Android answers: the NativeActivity's data directory, the AConfiguration locale, the launch
// arguments from a system property / a file, stdout to logcat, the lifecycle hooks on raylib's
// android_app command callback, and the asset reader over raylib's LoadFileData (its linker-wrapped
// fopen opens a relative path from the APK's assets, so the imported tree is read in place).
#include "aa/platform/platform.h"

#include <android/configuration.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <raylib.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

extern "C" struct android_app* GetAndroidApp(void);   // raylib's rcore_android.c (not in raylib.h)

namespace aa::platform {

void setWindowIconFromAssets(const std::string&) {}

namespace {

constexpr const char* kLogTag = "amazing_alex";
constexpr const char* kArgsProperty = "debug.amazingalex.args";   // values are capped at PROP_VALUE_MAX (92)
constexpr const char* kArgsFile = "args.txt";                     // <dataDir>/args.txt: the long form
constexpr const char* kBundleRoot = "aa";                          // assets/aa/** in the APK
constexpr const char* kLegacyAssetDir = "assets";                  // <dataDir>/assets: the extracted copy of older builds
constexpr const char* kLegacyStamp = ".bundle";                    // its extraction stamp (a pushed tree has none)

std::vector<std::string> splitArgs(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// stdout / stderr → a pipe → a reader thread → logcat, line by line.
void logPump(int fd) {
    std::string line;
    char buf[512];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\n') {
                __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    if (!line.empty()) __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
}

LifecycleHooks g_hooks;
void (*g_previousCmd)(android_app*, int32_t) = nullptr;

void commandCallback(android_app* app, int32_t cmd) {
    if (g_previousCmd) g_previousCmd(app, cmd);
    // nativePause / nativeResume of the original's MyRenderer (Activity.onPause / onResume).
    if (cmd == APP_CMD_PAUSE && g_hooks.onPause) g_hooks.onPause();
    if (cmd == APP_CMD_RESUME && g_hooks.onResume) g_hooks.onResume();
}

int32_t (*g_previousInput)(android_app*, AInputEvent*) = nullptr;
std::atomic<bool> g_touchCancelled{false};

int32_t inputCallback(android_app* app, AInputEvent* event) {
    // Seen before raylib's callback folds ACTION_CANCEL into "no touch points" (rcore_android.c).
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION &&
        (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_CANCEL) {
        g_touchCancelled = true;
    }
    return g_previousInput ? g_previousInput(app, event) : 0;
}

}  // namespace

PlatformInfo queryPlatform() {
    PlatformInfo info;
    info.mobile = true;
    info.tablet = true;   // DeviceParams::IsTablet = 1 in the Android GameApp::GameApp [verified]
    android_app* app = GetAndroidApp();
    if (app && app->activity && app->activity->internalDataPath) info.dataDir = app->activity->internalDataPath;
    else info.dataDir = "/data/local/tmp/amazing_alex";
    info.assetDir = kBundleRoot;
    // Earlier builds extracted the tree into <dataDir>/assets; drop that copy once, it is read in place now.
    // Only a directory carrying the old extraction stamp goes: a tree pushed by hand for `--assets assets`
    // (args.txt, relative to the data directory) has no stamp and stays.
    std::error_code ec;
    const std::string legacy = info.dataDir + "/" + kLegacyAssetDir;
    if (std::filesystem::exists(legacy + "/" + kLegacyStamp, ec)) std::filesystem::remove_all(legacy, ec);
    if (app && app->config) {
        char lang[3] = {0, 0, 0};
        char country[3] = {0, 0, 0};
        AConfiguration_getLanguage(app->config, lang);
        AConfiguration_getCountry(app->config, country);
        if (lang[0] != 0) info.languages.push_back(country[0] != 0 ? std::string(lang) + "_" + country : std::string(lang));
    }
    char prop[PROP_VALUE_MAX + 1] = {0};
    if (__system_property_get(kArgsProperty, prop) > 0) info.launchArgs = splitArgs(prop);
    std::ifstream file(info.dataDir + "/" + kArgsFile);
    if (file) {
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        for (const std::string& a : splitArgs(text)) info.launchArgs.push_back(a);
    }
    // std::filesystem::temp_directory_path() wants TMPDIR (there is no /tmp): the walk modes' fresh save
    // directories and the thumbnail writer go under the app's own data.
    const std::string tmp = info.dataDir + "/tmp";
    std::filesystem::create_directories(tmp, ec);
    setenv("TMPDIR", tmp.c_str(), 1);
    return info;
}

void installLogSink() {
    static std::once_flag once;
    std::call_once(once, [] {
        int fds[2];
        if (pipe(fds) != 0) return;
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        std::thread(logPump, fds[0]).detach();
    });
}

void installLifecycleHooks(LifecycleHooks hooks) {
    g_hooks = std::move(hooks);
    android_app* app = GetAndroidApp();
    if (!app) return;
    if (app->onAppCmd != commandCallback) {
        g_previousCmd = app->onAppCmd;
        app->onAppCmd = commandCallback;
    }
    if (app->onInputEvent != inputCallback) {
        g_previousInput = app->onInputEvent;
        app->onInputEvent = inputCallback;
    }
}

bool takeTouchCancel() { return g_touchCancelled.exchange(false); }

aa::data::FileReader assetFileReader() {
    return [](const std::string& path) {
        int size = 0;
        unsigned char* data = LoadFileData(path.c_str(), &size);
        if (!data) {   // also a 0-byte file: the tree has none (tools/import_assets.py), so unreadable it is
            throw aa::data::JsonError(path + ": cannot open (tools/build_android.sh packs the imported tree under assets/aa)");
        }
        std::string text(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
        UnloadFileData(data);
        return text;
    };
}

}  // namespace aa::platform
