#include "aa/platform/audio.h"

#include <algorithm>

namespace aa::platform {

namespace {

// st::AudioFilenames (docs/06 §5), indexed by AudioId.
const char* const kClipNames[kAudioIdCount] = {
    "",
    "Theme.mp3", "Music.mp3", "UIButtonPush.mp3", "UIButtonRelease.mp3", "UIItemAdded.mp3", "UIItemRemoved.mp3",
    "UISelectBuzz.mp3", "UIItemSelected.mp3", "UIItemDeselected.mp3", "UIStickerThump.mp3", "UIHitMetal.mp3",
    "UIMarkerStroke.mp3", "CaseyGiggle4.mp3", "CrowdCheer.mp3", "SoccerBallImpact.mp3", "TennisBallImpact.mp3",
    "BowlingBallImpact.mp3", "BilliardBallImpact.mp3", "PinballImpact.mp3", "BookImpact.mp3", "BucketImpact.mp3",
    "DartImpact.mp3", "BumperImpact.mp3", "CardboardBoxImpact.mp3", "LaundryBasketImpact.mp3",
    "PaperPlaneImpact.mp3", "TruckImpact.mp3", "LampImpact.mp3", "DollImpact1.mp3", "DollSqueak.mp3",
    "BalloonPop.mp3", "PiggyBankBreak1.mp3", "Spring.mp3", "ScissorsClose.mp3", "ScissorsCut.mp3",
    "BoxingGloveTriggered.mp3", "BoxingGloveHit.mp3", "SlingshotFire.mp3", "SlingshotStretch.mp3",
    "SlingshotUnstretch.mp3", "RCTruckLoop.mp3", "RCTruckStart.mp3", "RCTruckEnd.mp3", "RCButtonClick.mp3",
    "RCRemoteImpact.mp3", "MagnetActive.mp3", "TrapdoorOpen.mp3", "TrapdoorLever.mp3", "SeesawMove.mp3",
    "SkateboardWheelImpact.mp3", "SkateboardRoll.mp3", "HelicopterStart.mp3", "HelicopterLoop.mp3",
    "HelicopterEnd.mp3", "HelicopterImpact.mp3", "HelicopterBladesImpact.mp3", "BouncyBallImpact1.mp3",
    "BouncyBallImpact2.mp3", "BouncyBallImpact3.mp3", "ZipLineLoop.mp3", "GoalStarPickup1.mp3",
    "GoalStarPickup2.mp3", "GoalStarPickup3.mp3", "RopeTie1.mp3", "RopeUntie1.mp3", "PipeSnap.mp3",
    "PipeUnsnap.mp3", "ResultScreenStar1.mp3", "ResultScreenStar2.mp3", "ResultScreenStar3.mp3",
};

// SoundSystemUtils::Play: refused outside 1 m around the play field and when 20 clips are listed.
constexpr float kMinX = -1.0f;
constexpr float kMaxX = 4.41f;
constexpr float kMinY = -1.0f;
constexpr float kMaxY = 3.12459f;
constexpr int kMaxPlaying = 20;   // `0x13 < count` refuses
constexpr int kMaxVoices = 48;    // remake cap on alias instances
constexpr float kThemeVolume = 0.3f;
constexpr float kGameMusicVolume = 0.2f;
constexpr float kThemeFadeRate = 0.3f;   // × 2·dt per Update
constexpr float kGameMusicFadeRate = 0.2f;

}  // namespace

const char* audioClipName(int audioId) {
    if (audioId < 0 || audioId >= kAudioIdCount) return "";
    return kClipNames[audioId];
}

AudioSystem::~AudioSystem() { unload(); }

void AudioSystem::load(const aa::data::AssetRoot& root, bool device) {
    root_ = &root;
    base_.assign(kAudioIdCount, Sound{});
    baseLoaded_.assign(kAudioIdCount, false);
    if (!device) return;
    InitAudioDevice();
    device_ = IsAudioDeviceReady();
    if (!device_) return;
    for (int id = kUiButtonPush; id < kAudioIdCount; ++id) {
        const std::string file = std::string("sounds/") + kClipNames[id];
        if (!root.exists(file)) continue;
        const Sound s = LoadSound(root.path(file).c_str());
        if (s.frameCount == 0) continue;
        base_[static_cast<std::size_t>(id)] = s;
        baseLoaded_[static_cast<std::size_t>(id)] = true;
    }
    music_.enabled = true;
}

void AudioSystem::unload() {
    if (!device_) return;
    for (Voice& v : voices_) {
        if (v.alias) UnloadSoundAlias(v.sound);
    }
    voices_.clear();
    for (Loop& l : loops_) UnloadMusicStream(l.music);
    loops_.clear();
    if (music_.state != 0) UnloadMusicStream(music_.stream);
    music_ = MusicState{};
    for (std::size_t i = 0; i < base_.size(); ++i) {
        if (baseLoaded_[i]) UnloadSound(base_[i]);
    }
    base_.clear();
    baseLoaded_.clear();
    CloseAudioDevice();
    device_ = false;
}

bool AudioSystem::positionAllowed(aa::sim::Vec2 p) const {
    return !(p.x < kMinX || kMaxX < p.x || p.y < kMinY || kMaxY < p.y);
}

AudioSystem::Voice* AudioSystem::voiceFor(int audioId) {
    if (audioId <= 0 || audioId >= kAudioIdCount || !baseLoaded_[static_cast<std::size_t>(audioId)]) return nullptr;
    // A finished instance of the same clip is reused; otherwise a new alias, up to the cap.
    for (Voice& v : voices_) {
        if (v.audioId == audioId && !IsSoundPlaying(v.sound)) return &v;
    }
    if (static_cast<int>(voices_.size()) >= kMaxVoices) {
        for (Voice& v : voices_) {
            if (!IsSoundPlaying(v.sound)) {
                UnloadSoundAlias(v.sound);
                v.sound = LoadSoundAlias(base_[static_cast<std::size_t>(audioId)]);
                v.audioId = audioId;
                return &v;
            }
        }
        return nullptr;
    }
    Voice v;
    v.sound = LoadSoundAlias(base_[static_cast<std::size_t>(audioId)]);
    v.audioId = audioId;
    v.alias = true;
    voices_.push_back(v);
    return &voices_.back();
}

AudioSystem::Voice* AudioSystem::findVoice(int handle) {
    if (handle == aa::sim::kNoClip) return nullptr;
    for (Voice& v : voices_) {
        if (v.handle == handle) return &v;
    }
    return nullptr;
}

AudioSystem::Loop* AudioSystem::findLoop(int handle) {
    if (handle == aa::sim::kNoClip) return nullptr;
    for (Loop& l : loops_) {
        if (l.handle == handle) return &l;
    }
    return nullptr;
}

int AudioSystem::play(int audioId, float volume, aa::sim::Vec2 position) {
    if (!device_ || !positionAllowed(position) || static_cast<int>(playing_.size()) >= kMaxPlaying) return aa::sim::kNoClip;
    Voice* v = voiceFor(audioId);
    if (!v) return aa::sim::kNoClip;
    v->handle = nextHandle_++;
    SetSoundVolume(v->sound, volume * master_);
    PlaySound(v->sound);
    playing_.push_back(v->handle);
    return v->handle;
}

int AudioSystem::playLooping(int audioId, float volume, aa::sim::Vec2 position) {
    if (!device_ || !positionAllowed(position)) return aa::sim::kNoClip;
    if (audioId <= 0 || audioId >= kAudioIdCount) return aa::sim::kNoClip;
    const std::string file = std::string("sounds/") + kClipNames[audioId];
    if (!root_->exists(file)) return aa::sim::kNoClip;
    Loop* slot = nullptr;
    for (Loop& l : loops_) {
        if (l.handle == aa::sim::kNoClip && l.audioId == audioId) {
            slot = &l;
            break;
        }
    }
    if (!slot) {
        Loop l;
        l.music = LoadMusicStream(root_->path(file).c_str());
        if (l.music.frameCount == 0) return aa::sim::kNoClip;
        l.music.looping = true;
        l.audioId = audioId;
        loops_.push_back(l);
        slot = &loops_.back();
    }
    slot->handle = nextHandle_++;
    slot->volume = volume * master_;
    SetMusicVolume(slot->music, slot->volume);
    PlayMusicStream(slot->music);
    return slot->handle;
}

void AudioSystem::stop(int handle) {
    if (Voice* v = findVoice(handle)) {
        StopSound(v->sound);
        v->handle = aa::sim::kNoClip;
        playing_.erase(std::remove(playing_.begin(), playing_.end(), handle), playing_.end());
        return;
    }
    if (Loop* l = findLoop(handle)) {
        StopMusicStream(l->music);
        l->handle = aa::sim::kNoClip;
    }
}

void AudioSystem::setClipVolume(int handle, float volume) {
    if (Voice* v = findVoice(handle)) {
        SetSoundVolume(v->sound, volume);
        return;
    }
    if (Loop* l = findLoop(handle)) {
        l->volume = volume;
        SetMusicVolume(l->music, volume);
    }
}

bool AudioSystem::isClipPlaying(int handle) {
    if (Voice* v = findVoice(handle)) return IsSoundPlaying(v->sound);
    if (Loop* l = findLoop(handle)) return IsMusicStreamPlaying(l->music);
    return false;
}

int AudioSystem::loopCount() const {
    int n = 0;
    for (const Loop& l : loops_) n += l.handle != aa::sim::kNoClip ? 1 : 0;
    return n;
}

void AudioSystem::applyMusicVolume(int audioId) {
    // The track gain (Theme 0.3 / Music 0.2) × the fade level × the remake's music switch: with the music off
    // the stream keeps running silently, so switching it back on resumes mid-track without a restart.
    const float gain = (audioId == kMusicTheme ? kThemeVolume : kGameMusicVolume) * music_.trackVolume;
    SetMusicVolume(music_.stream, musicEnabled_ ? gain : 0.0f);
}

void AudioSystem::startMusicTrack(int audioId) {
    const std::string file = std::string("music/") + audioClipName(audioId);
    if (!root_->exists(file)) return;
    music_.stream = LoadMusicStream(root_->path(file).c_str());
    if (music_.stream.frameCount == 0) return;
    music_.stream.looping = true;
    music_.trackVolume = master_;
    applyMusicVolume(audioId);
    PlayMusicStream(music_.stream);
    music_.current = audioId;
    music_.state = 1;
}

void AudioSystem::playMusic(int audioId) {
    // BackgroundMusicUtils::Play [verified].
    if (!music_.enabled || music_.current == audioId) return;
    if (music_.state == 1) {
        music_.next = audioId;
        music_.state = 2;
    } else if (music_.state == 2) {
        music_.next = audioId;
    } else if (music_.state == 0) {
        startMusicTrack(audioId);
    }
}

void AudioSystem::stopMusic() {
    // BackgroundMusicUtils::Stop.
    if (!music_.enabled) return;
    if (music_.state != 0) {
        StopMusicStream(music_.stream);
        UnloadMusicStream(music_.stream);
    }
    music_.state = 0;
    music_.current = 0;
    music_.next = 0;
}

void AudioSystem::setSuspended(bool suspended) {
    if (suspended == suspended_) return;
    suspended_ = suspended;
    if (!device_) return;
    SetMasterVolume(suspended ? 0.0f : 1.0f);
    if (music_.enabled && music_.state != 0) {
        if (suspended) PauseMusicStream(music_.stream);
        else ResumeMusicStream(music_.stream);
    }
    for (Loop& l : loops_) {
        if (l.handle == aa::sim::kNoClip) continue;
        if (suspended) PauseMusicStream(l.music);
        else ResumeMusicStream(l.music);
    }
}

void AudioSystem::update(float dt) {
    if (!device_ || suspended_) return;
    // BackgroundMusicUtils::Update [verified].
    if (music_.enabled) {
        if (music_.state == 1) {
            music_.trackVolume = master_;
            applyMusicVolume(music_.current);
        } else if (music_.state == 2) {
            if (music_.trackVolume <= 0.0f) {
                StopMusicStream(music_.stream);
                UnloadMusicStream(music_.stream);
                music_.state = 0;
                const int next = music_.next;
                music_.current = 0;
                if (next != 0) startMusicTrack(next);
                music_.next = 0;
            } else {
                const float rate = music_.current == kMusicTheme ? kThemeFadeRate : kGameMusicFadeRate;
                float v = music_.trackVolume - rate * (dt + dt);
                if (v < 0.0f) v = 0.0f;
                music_.trackVolume = v * master_;
                applyMusicVolume(music_.current);
            }
        }
        if (music_.state != 0) UpdateMusicStream(music_.stream);
    }
    for (Loop& l : loops_) {
        if (l.handle != aa::sim::kNoClip) UpdateMusicStream(l.music);
    }
    // SoundSystemUtils::Update: drop the finished one-shots from the playing list.
    for (std::size_t i = playing_.size(); i-- > 0;) {
        Voice* v = findVoice(playing_[i]);
        if (!v || !IsSoundPlaying(v->sound)) {
            if (v) v->handle = aa::sim::kNoClip;
            playing_.erase(playing_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

}  // namespace aa::platform
