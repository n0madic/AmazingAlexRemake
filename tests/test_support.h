// Shared test helpers: the imported asset tree from AA_ASSETS (environment) or -DAA_ASSETS (configure), the
// source tree from AA_SOURCE_DIR (environment; the configure-time path otherwise — a device or a
// cross-built binary sees the tree elsewhere).
#pragma once

#include <cstdlib>
#include <string>

inline std::string sourceDir() {
    if (const char* env = std::getenv("AA_SOURCE_DIR")) {
        if (*env) return env;
    }
    return AA_SOURCE_DIR;
}

inline std::string assetsDir() {
    if (const char* env = std::getenv("AA_ASSETS")) {
        if (*env) return env;
    }
#ifdef AA_ASSETS_CONFIGURED
    return AA_ASSETS_CONFIGURED;
#else
    return "";
#endif
}

// Use at the top of a test case that needs the assets; it returns from the test with a message when absent.
#define AA_REQUIRE_ASSETS()                                                    \
    if (assetsDir().empty()) {                                                 \
        MESSAGE("skipped: no imported asset tree (set AA_ASSETS or -DAA_ASSETS)"); \
        return;                                                                \
    }
