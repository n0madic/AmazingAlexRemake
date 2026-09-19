// The desktop answers (macOS / Linux / Windows): the tree on disk, no lifecycle, the environment's locale.
// A macOS .app bundle (tools/build_macos_app.sh) is the one desktop case with an asset tree of its own —
// Contents/Resources/assets — and no LANG from Finder, so the system language list stands in.
#include "aa/platform/platform.h"

#include <raylib.h>

#include "aa/game/localization.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <sys/stat.h>
#elif defined(_WIN32)
// NOGDI / NOUSER keep windows.h's Rectangle / CloseWindow / ShowCursor out of raylib's way; the locale
// query is kernel32's (winnls.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
#endif

namespace aa::platform {

void setWindowIconFromAssets(const std::string& assetDir) {
#if defined(__APPLE__)
    (void)assetDir;   // Finder/Dock branding comes from the app bundle's AppIcon.icns.
#else
    if (assetDir.empty()) return;
    const std::string path = assetDir + "/branding/icon.png";
    Image icon = LoadImage(path.c_str());
    if (!IsImageValid(icon)) return;
    SetWindowIcon(icon);
    UnloadImage(icon);
#endif
}

namespace {

#if defined(__APPLE__)
// Contents/Resources/assets next to the executable's Contents/MacOS, when a manifest lives there; empty
// for the bare binary.
std::string bundleAssetDir() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string exe(size, '\0');
    if (_NSGetExecutablePath(exe.data(), &size) != 0) return {};
    char resolved[PATH_MAX];
    if (!realpath(exe.c_str(), resolved)) return {};
    std::string dir = resolved;
    const std::size_t slash = dir.rfind('/');
    if (slash == std::string::npos) return {};
    dir = dir.substr(0, slash) + "/../Resources/assets";
    struct stat st{};
    if (stat((dir + "/manifest.json").c_str(), &st) != 0 || !realpath(dir.c_str(), resolved)) return {};
    return resolved;
}

// CFLocaleCopyPreferredLanguages: "de-DE", "ru-RU", ... in the user's order.
void appendPreferredLanguages(std::vector<std::string>& out) {
    CFArrayRef list = CFLocaleCopyPreferredLanguages();
    if (!list) return;
    const CFIndex count = CFArrayGetCount(list);
    for (CFIndex i = 0; i < count; ++i) {
        auto* s = static_cast<CFStringRef>(CFArrayGetValueAtIndex(list, i));
        char buf[64];
        if (s && CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) out.emplace_back(buf);
    }
    CFRelease(list);
}
#elif defined(_WIN32)
// The user default locale's ISO 639 language name ("de", "ru", ...): the Windows source the
// localization.h contract names, since no LANG is set by Explorer.
void appendPreferredLanguages(std::vector<std::string>& out) {
    char buf[16];
    if (GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, buf, sizeof buf) > 0 && buf[0]) out.emplace_back(buf);
}
#endif

}  // namespace

PlatformInfo queryPlatform() {
    PlatformInfo info;
    info.languages = aa::game::systemPreferredLanguages();
#if defined(__APPLE__)
    appendPreferredLanguages(info.languages);   // after the environment: LANG=de_DE still wins
    info.assetDir = bundleAssetDir();
#elif defined(_WIN32)
    appendPreferredLanguages(info.languages);   // after the environment, as on macOS
#endif
    return info;
}

void installLogSink() {}

void installLifecycleHooks(LifecycleHooks) {}

bool takeTouchCancel() { return false; }

aa::data::FileReader assetFileReader() { return &aa::data::AssetRoot::defaultFileReader; }

}  // namespace aa::platform
