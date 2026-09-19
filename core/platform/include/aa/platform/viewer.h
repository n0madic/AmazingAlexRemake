// The level viewer (docs/10 §10 M2 / M3 exit checks): every shipped level rendered in set-up mode through
// the core's renderState(), played with the mouse as the one finger (drag, strip, gizmos, ghost), keyboard
// navigation, undo / redo / play / stop / restart, camera pan and zoom, a screenshot mode that writes one
// PNG per level, and a scripted mode that replays pointer events.
#pragma once

#include <string>

namespace aa::platform {

struct ViewerOptions {
    std::string assets;             // imported asset tree (tools/import_assets.py)
    std::string level;              // "<chapter>/<name>" to start at; empty = the first level
    int width = 1024;
    int height = 768;
    int maxFrames = 0;              // > 0: close after this many frames (automated smoke)
    bool headless = false;          // load + report only, no window
    std::string screenshotDir;      // non-empty: render every level once, write <chapter>_<name>.png, exit
    bool markers = true;            // draw the goal markers (final animation frames) interactively
    bool screenshotMarkers = false; // and in the screenshot run (the original thumbnails have none)
    std::string script;             // non-empty: replay a pointer / key script at 60 Hz instead of reading input
    bool audio = true;              // play the session's sounds (clips, loops)
};

// Returns the process exit code.
int runViewer(const ViewerOptions& options);

}  // namespace aa::platform
