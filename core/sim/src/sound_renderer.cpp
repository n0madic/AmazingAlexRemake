// st::SoundRenderer::Render / StopLoopingSounds [verified: decompile + disassembly, docs/06 §5]: the looping
// clips of the skateboard, magnet, RC truck / helicopter controller and zip line, driven from the live
// world state once per simulation frame; the clip handles live in the item blocks (GameItem::clipHandle,
// GameItem::clipId) like the original's, so they ride along in the state snapshots.
#include "aa/sim/session.h"
#include "aa/sim/sound_sink.h"

#include <Box2D/Dynamics/Joints/b2WheelJoint.h>

#include "aa/sim/math_utils.h"

namespace aa::sim {

namespace {

namespace audio_id {
constexpr int kRCTruckLoop = 0x29;
constexpr int kRCTruckStart = 0x2a;
constexpr int kRCTruckEnd = 0x2b;
constexpr int kMagnetActive = 0x2e;
constexpr int kSkateboardRoll = 0x33;
constexpr int kHelicopterStart = 0x34;
constexpr int kHelicopterLoop = 0x35;
constexpr int kHelicopterEnd = 0x36;
constexpr int kZipLineLoop = 0x3c;
}  // namespace audio_id

constexpr float kLoopStartVolume = 0.3f;        // 0x3e99999a: skateboard / zip line PlayLooping volume
constexpr float kControllerVolume = 0.2f;       // 0x3e4ccccd: the RC start / loop / end clips
constexpr float kSkateboardSpeedMin = 8.0f;     // wheel joint speed that starts the roll
constexpr float kSkateboardSpeedRange = 50.0f;
constexpr float kSkateboardVolumeMax = 0.5f;
constexpr float kZipLineSpeedMin = 0.1f;        // trolley speed that starts the loop
constexpr float kZipLineVolumeMax = 0.2f;
constexpr float kMagnetNearDist2 = 0.006f;      // distance² below which the volume ramps down to 0.1
constexpr float kMagnetVolumeMin = 0.1f;
constexpr float kMagnetNearSlope = 200.0f;

void renderSkateboard(GameItem& item, const PhysicsObject& obj, const PhysicsWorld& world, SoundSink& audio) {
    // The louder of the two wheel joints' speeds (compared in double, as compiled).
    const auto* back = static_cast<const b2WheelJoint*>(world.joint(obj.joints[0]));
    const auto* front = static_cast<const b2WheelJoint*>(world.joint(obj.joints[1]));
    const float s0 = std::fabs(back->GetJointSpeed());
    const float s1 = std::fabs(front->GetJointSpeed());
    const float speed = static_cast<double>(s0) > static_cast<double>(s1) ? s0 : s1;
    if (item.clipHandle == kNoClip) {
        if (!(speed > kSkateboardSpeedMin)) return;
        item.clipHandle = audio.playLooping(audio_id::kSkateboardRoll, kLoopStartVolume, obj.position);
        if (item.clipHandle == kNoClip) return;
    } else if (speed < kSkateboardSpeedMin) {
        audio.stop(item.clipHandle);
        item.clipHandle = kNoClip;
        return;
    }
    float v = (speed - kSkateboardSpeedMin) / kSkateboardSpeedRange;
    if (kSkateboardVolumeMax - v < 0.0f) v = kSkateboardVolumeMax;
    if (v < 0.0f) v = 0.0f;
    audio.setClipVolume(item.clipHandle, v * audio.masterVolume());
}

void renderMagnet(GameItem& item, const PhysicsObject& obj, SoundSink& audio) {
    if (!item.magnetPulling) {
        if (item.clipHandle != kNoClip) {
            audio.stop(item.clipHandle);
            item.clipHandle = kNoClip;
        }
        return;
    }
    if (item.clipHandle == kNoClip) {
        item.clipHandle = audio.playLooping(audio_id::kMagnetActive, 1.0f, obj.position);
        if (item.clipHandle == kNoClip) return;
    }
    const float d = item.magnetMinDist2 - kMagnetNearDist2;
    float v;
    if (d <= 0.0f) {
        // Close attraction: 1 + 200·d, clamped to [0.1, 1].
        v = d * kMagnetNearSlope + 1.0f;
        const float over = v - kMagnetVolumeMin;
        if (1.0f - v < 0.0f) v = 1.0f;
        if (over < 0.0f) v = kMagnetVolumeMin;
    } else {
        const float f = 1.0f - d;
        v = f;
        if (1.0f - f < 0.0f) v = 1.0f;
        if (f < 0.0f) v = 0.0f;
    }
    audio.setClipVolume(item.clipHandle, v * audio.masterVolume());
}

void renderController(GameItem& item, const PhysicsObject& obj, SoundSink& audio) {
    // The paired item's handle type (RadioController+8, top byte) selects the truck or helicopter clips.
    const bool truck = Handle::typeOf(item.stateWord) == ItemType::RCTruck;
    const int loopId = truck ? audio_id::kRCTruckLoop : audio_id::kHelicopterLoop;
    const int startId = truck ? audio_id::kRCTruckStart : audio_id::kHelicopterStart;
    const int endId = truck ? audio_id::kRCTruckEnd : audio_id::kHelicopterEnd;
    const bool startFinished = item.clipId == startId && !audio.isClipPlaying(item.clipHandle);
    if (item.buttonPressed) {
        if (item.clipId == endId) {
            audio.stop(item.clipHandle);
            item.clipHandle = kNoClip;
            item.clipId = 0;
            item.clipHandle = audio.play(startId, kControllerVolume, obj.position);
            item.clipId = startId;
        } else if (item.clipHandle == kNoClip) {
            item.clipHandle = audio.play(startId, kControllerVolume, obj.position);
            item.clipId = startId;
        }
    } else if (item.clipId == loopId || startFinished) {
        if (item.clipHandle != kNoClip) {
            audio.stop(item.clipHandle);
            item.clipHandle = audio.play(endId, kControllerVolume, obj.position);
            item.clipId = endId;
        }
    }
    if (startFinished && item.clipId == startId) {
        audio.stop(item.clipHandle);
        item.clipHandle = audio.playLooping(loopId, kControllerVolume, obj.position);
        item.clipId = loopId;
    }
}

void renderZipLine(GameItem& item, const PhysicsObject& obj, const PhysicsWorld& world, SoundSink& audio) {
    // The trolley's (body 2) speed; the sum is a chained multiply-add as compiled (vmla, not fused).
    const b2Vec2 v = world.body(obj.bodies[2])->GetLinearVelocity();
    const float speed = length(Vec2(v.x, v.y));
    if (item.clipHandle == kNoClip) {
        if (!(speed > kZipLineSpeedMin)) return;
        item.clipHandle = audio.playLooping(audio_id::kZipLineLoop, kLoopStartVolume, obj.position);
        if (item.clipHandle == kNoClip) return;
    } else if (speed < kZipLineSpeedMin) {
        audio.stop(item.clipHandle);
        item.clipHandle = kNoClip;
        return;
    }
    const float s = speed - kZipLineSpeedMin;
    float vol = s;
    if (kZipLineVolumeMax - s < 0.0f) vol = kZipLineVolumeMax;
    if (s < 0.0f) vol = 0.0f;
    audio.setClipVolume(item.clipHandle, vol * audio.masterVolume());
}

}  // namespace

void Session::renderSounds() {
    if (!soundSink_ || !world_) return;
    for (const PhysicsObject& obj : state_.objects) {
        if (!obj.valid()) continue;
        GameItem& item = state_.itemOf(obj);
        switch (obj.type) {
        case ItemType::Skateboard: renderSkateboard(item, obj, *world_, *soundSink_); break;
        case ItemType::Magnet: renderMagnet(item, obj, *soundSink_); break;
        case ItemType::RCController: renderController(item, obj, *soundSink_); break;
        case ItemType::ZipLine: renderZipLine(item, obj, *world_, *soundSink_); break;
        default: break;
        }
    }
}

void Session::stopLoopingSounds() {
    // SoundRenderer::StopLoopingSounds: the clip handle of every skateboard, magnet, controller and zip
    // line is stopped and cleared (the controller keeps its clipId).
    if (!soundSink_) return;
    for (const PhysicsObject& obj : state_.objects) {
        if (!obj.valid()) continue;
        switch (obj.type) {
        case ItemType::Skateboard:
        case ItemType::Magnet:
        case ItemType::RCController:
        case ItemType::ZipLine: {
            GameItem& item = state_.itemOf(obj);
            if (item.clipHandle != kNoClip) {
                soundSink_->stop(item.clipHandle);
                item.clipHandle = kNoClip;
            }
            break;
        }
        default: break;
        }
    }
}

}  // namespace aa::sim
