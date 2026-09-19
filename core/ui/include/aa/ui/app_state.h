// What the scenes share (the original's GameApp + st::GameState slice the UI reads): the asset root, the
// save store with the settings / progress / location states, the location infos, the current location and
// level, the audio hooks and the scene-flow flags.
#pragma once

#include "aa/data/asset_root.h"
#include "aa/data/ui_loaders.h"
#include "aa/sim/level.h"
#include "aa/sim/screen_layout.h"
#include "aa/game/localization.h"
#include "aa/game/progress.h"
#include "aa/game/save_store.h"
#include "aa/sim/sound_sink.h"
#include "aa/sim/toolbox.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aa::ui {

// The audio the scenes drive (GameApp::playMusic / stopMusic, the mute toggle).
class AppAudio {
public:
    virtual ~AppAudio() = default;
    virtual void playMusic(int audioId) = 0;
    virtual void stopMusic() = 0;
    virtual void setMuted(bool muted) = 0;
    virtual void setMusicEnabled(bool on) = 0;   // remake: the note button (docs/06 §4)
};

// The per-level header the menus need (SerializationUtils::PreCacheLevelNames): the title / tip ids.
struct LevelMeta {
    std::string name;          // file name
    std::string titleId;       // TEXT_LEVEL_NAME_cc_nn
    std::string tipId;         // TEXT_LEVEL_TIP_cc_nn
    int backgroundIndex = 0;
};

struct AppState {
    // Index of the My Contraptions entry in `locations` (the fifth, after the four chapters).
    static constexpr int kSandboxLocation = aa::game::kLocationCount;

    const aa::data::AssetRoot* assets = nullptr;
    aa::game::SaveStore* saves = nullptr;
    aa::game::Localization* localization = nullptr;
    AppAudio* audio = nullptr;
    aa::sim::SoundSink* soundSink = nullptr;          // the session's clips (the platform's AudioSystem)
    aa::sim::ToolboxFrameSizes toolboxSizes;          // UIElements strip sizes (ToolboxFrameSizes::fromFrames)
    // The imported UI profile's PixelScale (its width / 1024: 2 for 2048X1536, 1 for 1024X768), set by
    // loadCatalogue from the manifest — ScreenLayout::compute's third argument everywhere the app lays out.
    float profilePixelScale = aa::sim::ScreenLayout::kProfilePixelScale;
    static float profilePixelScaleOf(const aa::data::AssetRoot& root);

    aa::game::Settings settings;
    aa::game::GameProgress progress;
    std::vector<aa::game::LocationInfo> locations;   // the four chapters, then the My Contraptions location
    std::vector<std::vector<LevelMeta>> levelMeta;   // per location, per level
    std::vector<aa::data::Tip> tips;
    bool quitRequested = false;                      // the main menu's exit dialog confirmed
    bool unlockAllLevels = false;                     // --unlock-all (remake-only debug aid, docs/13)

    // st::GameState+0x834 LocationInfo (the loaded location) and +0x209c LocationState.
    int locationIndex = -1;
    aa::game::LocationState locationState;
    int currentLevel = 0;              // LocationState+0 (the level being played)
    int pendingLevel = -1;             // LevelLoadingScene's target level
    bool returningFromGame = false;    // ChapterSelection / LevelSelection: SetReturningFromGame
    bool desktop = true;               // the tip filter: Platform = 1 tips are iOS-only

    std::unique_ptr<aa::data::SceneTree> dialogsTree;
    const aa::game::LocationInfo* location() const {
        return locationIndex >= 0 && locationIndex < static_cast<int>(locations.size()) ? &locations[static_cast<std::size_t>(locationIndex)] : nullptr;
    }
    // LocationInfoUtils::Load + LocationStateUtils::Load for `index`.
    void loadLocation(int index);
    void saveLocation();
    void saveProgress();
    void saveSettings();
    // Reads the chapter indices and the level headers of every location (once at start-up).
    void loadCatalogue();
    // LocationInfoUtils::LoadFromDocs for the My Contraptions location: the sandbox index re-read, the
    // level titles from the files (a level whose file does not load keeps an empty title — the list drops
    // it, as the original's Refresh does).
    void loadSandboxLocation();
    bool saveSandboxLocation();   // false when the index could not be written
    // LocationInfoUtils::RemoveLevel plus the level's files, with the list's titles kept in step — a
    // refresh that could not write the index back goes on with the in-memory list and these metas.
    void removeSandboxLevel(int level);
    bool isSandboxLocation(int loc) const { return loc == kSandboxLocation; }
    // Location `loc`'s level `level`: from the asset tree, or the sandbox directory for the fifth location.
    aa::sim::Level loadLevel(int loc, int level) const;
    // ui/scenes/Dialogs.json's "Dialogs" node (loaded once).
    const aa::data::JsonNode dialogs();
    const LevelMeta& meta(int loc, int level) const { return levelMeta[static_cast<std::size_t>(loc)][static_cast<std::size_t>(level)]; }
};

}  // namespace aa::ui
