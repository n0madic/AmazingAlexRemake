// Browser platform answers: imported assets are preloaded at /assets and web_pre.js mounts the
// persistent save tree before main. The rest of the game deliberately keeps its ordinary file APIs.
#include "aa/platform/platform.h"

#include "aa/game/save_store.h"

#include <emscripten.h>

#include <cstring>

namespace aa::platform {

void setWindowIconFromAssets(const std::string&) {}

namespace {

EM_JS(int, browserLanguages, (char* out, int capacity), {
    const languages = (navigator.languages && navigator.languages.length ? navigator.languages : [navigator.language || "en"]);
    const text = languages.join(",");
    stringToUTF8(text, out, capacity);
    return lengthBytesUTF8(text);
});

EM_JS(int, browserHasTouch, (), {
    return navigator.maxTouchPoints > 0 ? 1 : 0;
});

EM_JS(int, browserViewportWidth, (), { return window.innerWidth; });
EM_JS(int, browserViewportHeight, (), { return window.innerHeight; });

}  // namespace

PlatformInfo queryPlatform() {
    PlatformInfo info;
    info.dataDir = aa::game::SaveStore::kWebPersistentDir;
    info.assetDir = "/assets";
    // Keep the browser canvas at the requested landscape size even on a touchscreen. app.cpp polls
    // touch points in addition to mouse input for Web; using mobile=true here would pass 0x0 to InitWindow.
    info.mobile = false;
    info.tablet = browserHasTouch() != 0;
    // ScreenLayout::compute (core/sim) already lays out any width/height continuously — letterbox pillars
    // on a wide window, bands on a narrow one — so the real viewport size, not AppOptions' 1024x768
    // default, is what makes a widescreen browser window fill the screen instead of a fixed 4:3 box.
    info.width = browserViewportWidth();
    info.height = browserViewportHeight();
    char languages[256] = {};
    browserLanguages(languages, sizeof languages);
    for (char* language = std::strtok(languages, ","); language; language = std::strtok(nullptr, ",")) {
        info.languages.emplace_back(language);
    }
    return info;
}

void installLogSink() {}
void installLifecycleHooks(LifecycleHooks) {}
bool takeTouchCancel() { return false; }
aa::data::FileReader assetFileReader() { return &aa::data::AssetRoot::defaultFileReader; }

}  // namespace aa::platform
