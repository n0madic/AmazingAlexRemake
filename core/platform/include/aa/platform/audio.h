// st::AudioSystem + AudioSystemUtils / SoundSystemUtils / BackgroundMusicUtils on raylib (docs/06 §4–§5):
// the clip table by AudioId, the playing list and its 20-clip limit, the play-field position filter, the
// master volume (mute), looping clips by handle and the two music tracks with the original's fade-out.
#pragma once

#include "aa/data/asset_root.h"
#include "aa/sim/sound_sink.h"

#include <raylib.h>

#include <string>
#include <vector>

namespace aa::platform {

// st::AudioId (docs/06 §5) → clip file; index 0 = none, 1–2 music, 3–12 UI, the rest gameplay.
const char* audioClipName(int audioId);
constexpr int kAudioIdCount = 71;
constexpr int kMusicTheme = 1;
constexpr int kMusicGame = 2;
constexpr int kUiButtonPush = 3;
constexpr int kUiButtonRelease = 4;

class AudioSystem : public aa::sim::SoundSink {
public:
    AudioSystem() = default;
    ~AudioSystem() override;
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // InitAudioDevice + the clips of sounds/ and music/. Without a device (`--headless`, CI) every play is
    // refused and the state machines still run.
    void load(const aa::data::AssetRoot& root, bool device = true);
    void unload();
    bool ready() const { return device_; }

    // --- SoundSink (SoundSystemUtils) --------------------------------------------------------
    int play(int audioId, float volume, aa::sim::Vec2 position) override;
    int playLooping(int audioId, float volume, aa::sim::Vec2 position) override;
    void stop(int handle) override;
    void setClipVolume(int handle, float volume) override;
    bool isClipPlaying(int handle) override;
    float masterVolume() override { return master_; }

    // A UI sound: Play(id, volume, (0, 0)) — the origin passes the position filter.
    int playUi(int audioId, float volume = 0.2f) { return play(audioId, volume, aa::sim::Vec2(0.0f, 0.0f)); }

    // AudioSystemUtils::Mute / Unmute: the master volume 0 / 1. Running one-shots keep their level (the
    // original multiplies at play time); loops and music follow on the next update.
    void mute() { master_ = 0.0f; }
    void unmute() { master_ = 1.0f; }
    bool muted() const { return master_ == 0.0f; }
    // The remake's music switch (the note button): the music streams keep running at gain 0 while off, the
    // one-shots and loops are untouched. Independent of the master mute.
    void setMusicEnabled(bool on) { musicEnabled_ = on; }
    bool musicEnabled() const { return musicEnabled_; }
    // GameApp::activateAudio(false / true) on nativePause / nativeResume: the output stopped and restarted
    // — here the device muted and the streams paused, independent of the user's mute setting.
    void setSuspended(bool suspended);
    bool suspended() const { return suspended_; }

    // --- BackgroundMusicUtils --------------------------------------------------------------
    // Play(music, id): starts the track when idle, else fades the current one out and queues `id`;
    // Stop: stops at once. Theme.mp3 plays at 0.3, Music.mp3 at 0.2; the fade-out rate is 0.6 / s for the
    // theme and 0.4 / s for the game music [verified].
    void playMusic(int audioId);
    void stopMusic();
    int currentMusic() const { return music_.current; }

    // Once per frame: BackgroundMusicUtils::Update(dt) (the track volume follows the master volume; the
    // fade-out), the loop streams, SoundSystemUtils::Update (the playing list pruned).
    void update(float dt);

    // Loaded state for the tests / status line.
    int playingCount() const { return static_cast<int>(playing_.size()); }
    int loopCount() const;

private:
    struct Voice {
        Sound sound{};
        int audioId = 0;
        int handle = aa::sim::kNoClip;
        bool alias = false;
    };
    struct Loop {
        Music music{};
        int audioId = 0;
        int handle = aa::sim::kNoClip;
        float volume = 1.0f;
    };
    struct MusicState {
        bool enabled = false;
        int state = 0;      // 0 stopped, 1 playing, 2 fading out
        int current = 0;    // AudioId of the playing track
        int next = 0;       // AudioId queued behind the fade
        Music stream{};
        float trackVolume = 1.0f;   // the audio::AudioOutput track gain (setTrackVolume)
    };

    bool positionAllowed(aa::sim::Vec2 position) const;
    Voice* voiceFor(int audioId);
    Voice* findVoice(int handle);
    Loop* findLoop(int handle);
    void startMusicTrack(int audioId);
    void applyMusicVolume(int audioId);

    bool device_ = false;
    bool suspended_ = false;
    float master_ = 1.0f;
    bool musicEnabled_ = true;
    const aa::data::AssetRoot* root_ = nullptr;
    std::vector<Sound> base_;          // by AudioId; id == 0 for missing files
    std::vector<bool> baseLoaded_;
    std::vector<Voice> voices_;        // one-shot instances (aliases of the base sounds)
    std::vector<Loop> loops_;          // looping clips as music streams
    std::vector<int> playing_;         // SoundSystemUtils' playing list (one-shot handles)
    MusicState music_;
    int nextHandle_ = 1;
};

}  // namespace aa::platform
