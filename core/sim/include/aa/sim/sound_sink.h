// The audio interface the core drives (st::SoundSystemUtils over st::AudioSystem, docs/06 §4–§5): the
// looping sounds of SoundRenderer::Render need synchronous answers (a clip handle, "is it still playing"),
// so they go through this interface instead of the SessionEvent queue. The platform implements it on
// raylib; headless runs leave it unset (every play is refused, as the original refuses off-field sounds).
#pragma once

#include "aa/sim/types.h"

namespace aa::sim {

constexpr int kNoClip = -1;

class SoundSink {
public:
    virtual ~SoundSink() = default;
    // SoundSystemUtils::Play: a one-shot clip at `volume` (before the master volume) near `position`
    // (world metres); returns the clip handle or kNoClip when refused (position outside the play field
    // margin, 20 clips already playing).
    virtual int play(int audioId, float volume, Vec2 position) = 0;
    // SoundSystemUtils::PlayLooping: as play() without the count limit; the loop runs until stop().
    virtual int playLooping(int audioId, float volume, Vec2 position) = 0;
    // SoundSystemUtils::Stop(handle, audio).
    virtual void stop(int handle) = 0;
    // SoundSystemUtils::SetClipVolume(handle, volume): the volume as computed, already × master.
    virtual void setClipVolume(int handle, float volume) = 0;
    // audio::AudioOutput::isClipPlaying.
    virtual bool isClipPlaying(int handle) = 0;
    // AudioSystem+4: 1.0, or 0.0 while muted.
    virtual float masterVolume() = 0;
};

}  // namespace aa::sim
