#include "audio/volumetric/VolumetricMixer.hpp"

#include "audio/Spatial.hpp"
#include "audio/volumetric/Dsp.hpp"

#include <array>
#include <cmath>

namespace Caffeine::Audio {

namespace {

constexpr f32 kMaxItdSeconds = 0.00066f;
constexpr f64 kVoiceQueryInterval = 0.04;
constexpr f64 kRoomQueryInterval = 0.10;

f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

Vec3 normalizedOr(const Vec3& v, const Vec3& fallback) {
    const f32 l = v.length();
    return l > 1e-5f ? v / l : fallback;
}

/// A clip read at a fractional frame (cubic interpolation); outside a non-looping clip it is silent.
f32 readClip(const VolumetricClip& clip, u32 channel, f64 position, bool loop) {
    const i64 frames = clip.frames();
    if (frames < 4) return 0.0f;
    if (!loop && (position < 0.0 || position >= static_cast<f64>(frames))) return 0.0f;
    f64 p = position;
    if (loop) {
        p = std::fmod(p, static_cast<f64>(frames));
        if (p < 0.0) p += static_cast<f64>(frames);
    }
    const i64 base = static_cast<i64>(std::floor(p));
    const f32 t = static_cast<f32>(p - static_cast<f64>(base));
    auto at = [&](i64 i) {
        if (loop) {
            i %= frames;
            if (i < 0) i += frames;
        } else {
            i = std::clamp<i64>(i, 0, frames - 1);
        }
        return clip.samples[static_cast<usize>(i) * clip.channels + std::min(channel, clip.channels - 1)];
    };
    const f32 y0 = at(base - 1), y1 = at(base), y2 = at(base + 1), y3 = at(base + 2);
    return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
}

/// How much the highs survive a wall -> the cutoff of the dull sound that gets through.
f32 cutoffForHighs(f32 transmissionHigh) { return 350.0f + 19650.0f * std::sqrt(std::clamp(transmissionHigh, 0.0f, 1.0f)); }

// ---- The ears: a spherical head (radius 8.75 cm).
constexpr f32 kHeadRadius = 0.0875f;

/// Brown & Duda's head shadow for one ear: a one-pole, one-zero filter whose zero moves with the angle between the
/// source and the ear's axis (the ear facing the source gets a boost in the highs, the far ear a deep cut).
struct HeadShadow {
    f32 x1 = 0.0f, y1 = 0.0f;
    f32 process(f32 x, f32 earAngleRad, f32 sampleRate, f32 speedOfSound) {
        const f32 alpha = 1.05f + 0.95f * std::cos(earAngleRad * (180.0f / 150.0f));
        const f32 a = 2.0f * speedOfSound / kHeadRadius;  // 2 w0, the filter's corner in rad/s (w0 = c / head radius)
        const f32 k = 2.0f * sampleRate;
        const f32 inv = 1.0f / (a + k);
        const f32 b0 = (a + alpha * k) * inv, b1 = (a - alpha * k) * inv, a1 = (a - k) * inv;
        const f32 y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

struct Smoothed {
    f32 value = 0.0f;
    f32 target = 0.0f;
    bool set = false;
    void aim(f32 t) {
        target = t;
        if (!set) {
            value = t;
            set = true;
        }
    }
    f32 step(f32 perSample) {
        const f32 v = value;
        value += (target - value) * perSample;
        return v;
    }
};

}  // namespace

struct VolumetricMixer::Impl {
    struct Voice {
        VoiceId id = 0;
        bool active = false;
        VoiceParams params;
        Vec3 velocity{0.0f, 0.0f, 0.0f};  // m/s, from the last moves
        f64 movedAt = 0.0;               // mixer clock of the last move
        i64 time = 0;  // samples played since the start, at the mixer's rate
        // Delay (samples) the sound takes to arrive: `rawPrev` is the true one at the last block, `effective` is what
        // the Doppler scale lets through.
        bool started = false;
        f32 rawPrev = 0.0f;
        f32 effective = 0.0f;
        // Per-block targets, followed per sample.
        f32 gainT0 = 0.0f, gainD0 = 0.0f, cutT0 = 20000.0f, cutD0 = 6000.0f, pan0 = 0.0f, rear0 = 0.0f;
        f32 wet0 = 0.0f, echo0 = 0.0f, ear0 = 0.0f, remote0 = 0.0f;
        RoomEstimate sourceRoom;  // the room the source stands in, read while its sound is behind a wall
        f64 nextSourceRoom = 0.0;
        bool inited = false;
        dsp::OnePole lpT, lpD, lpAmbient[2], rearLp;
        HeadShadow shadowLeft, shadowRight;
        dsp::DelayLine itdLeft{96}, itdRight{96};
        // Cached acoustic queries.
        f64 nextQuery = 0.0;
        Transmission transmission;
        bool diffracted = false;
        f32 extraPath = 0.0f;
        Vec3 detour;
        VoiceDebug debug;
        Voice() = default;
    };

    explicit Impl(u32 rate) : rate(rate), reverb(static_cast<f32>(rate)), remoteReverb(static_cast<f32>(rate)), echoLine(rate * 2) {
        layerVolume.fill(1.0f);
    }

    u32 rate;
    VolumetricSettings settings;
    const AcousticWorld* world = nullptr;
    Vec3 listenerPos{0.0f, 0.0f, 0.0f}, listenerForward{0.0f, 0.0f, 1.0f}, listenerUp{0.0f, 1.0f, 0.0f};
    Vec3 listenerVelocity{0.0f, 0.0f, 0.0f};
    f64 listenerMovedAt = 0.0;
    std::vector<std::unique_ptr<Voice>> voices;
    VoiceId nextId = 1;
    std::array<f32, 4> layerVolume;
    f64 clock = 0.0;
    f64 nextRoomQuery = 0.0;
    RoomEstimate room;
    Smoothed rt60, damping, predelay, wetLevel, sky;
    std::array<EchoTap, 4> echoNow{};
    std::array<f32, 4> echoGainNow{};
    bool echoStarted = false;
    dsp::Reverb reverb;
    dsp::DelayLine echoLine;
    dsp::OnePole echoLp;
    std::vector<f32> wetBus, echoBus, remoteBus;
    // The rooms sources stand in when they are not in the listener's: one shared reverb, fitted to their average.
    dsp::Reverb remoteReverb;
    Smoothed remoteRt60, remoteDamping;
    f32 remoteWeight = 0.0f, remoteRtSum = 0.0f, remoteDampSum = 0.0f;

    Voice* find(VoiceId id) const {
        for (const auto& v : voices)
            if (v->active && v->id == id) return v.get();
        return nullptr;
    }

    void updateRoom() {
        if (clock < nextRoomQuery) return;
        nextRoomQuery = clock + kRoomQueryInterval;
        room = world ? probeRoom(*world, listenerPos) : RoomEstimate{};
        if (!settings.reverb) room.wet = 0.0f;
        rt60.aim(room.rt60);
        damping.aim(room.damping);
        predelay.aim(std::min(0.04f, room.meanDistance * 2.0f / settings.speedOfSound * 0.3f));
        wetLevel.aim(room.wet);
        sky.aim(room.skyExposure);
        for (u32 i = 0; i < echoNow.size(); ++i) {
            echoNow[i].delay = i < room.echoCount ? room.echoes[i].delay : echoNow[i].delay;
        }
    }

    void queryVoice(Voice& v) {
        if (clock < v.nextQuery) return;
        v.nextQuery = clock + kVoiceQueryInterval;
        v.transmission = Transmission{};
        v.diffracted = false;
        v.extraPath = 0.0f;
        if (!world || !settings.occlusion) return;
        const Vec3& from = v.params.position;
        v.transmission = world->transmission(from, listenerPos);
        if (v.transmission.clear() || !settings.diffraction) return;
        // Can the sound bend round whatever is in the way? The best waypoint with a clear leg to each side.
        const f32 direct = (listenerPos - from).length();
        f32 bestExtra = 1e9f;
        for (const Vec3& waypoint : world->detourCandidates(from, listenerPos)) {
            if (!world->transmission(from, waypoint).clear() || !world->transmission(waypoint, listenerPos).clear()) continue;
            const f32 extra = (waypoint - from).length() + (listenerPos - waypoint).length() - direct;
            if (extra < bestExtra) {
                bestExtra = extra;
                v.detour = waypoint;
            }
        }
        if (bestExtra < 1e8f) {
            v.diffracted = true;
            v.extraPath = bestExtra;
        }
    }
};

VolumetricMixer::VolumetricMixer(u32 sampleRate) : m_impl(new Impl(std::max<u32>(sampleRate, 8000))) {}
VolumetricMixer::~VolumetricMixer() = default;

u32 VolumetricMixer::sampleRate() const { return m_impl->rate; }
VolumetricSettings& VolumetricMixer::settings() { return m_impl->settings; }
const VolumetricSettings& VolumetricMixer::settings() const { return m_impl->settings; }
void VolumetricMixer::setWorld(const AcousticWorld* world) {
    m_impl->world = world;
    m_impl->nextRoomQuery = 0.0;
}
void VolumetricMixer::setListener(const Vec3& position, const Vec3& forward, const Vec3& up) {
    m_impl->listenerVelocity = Vec3(0.0f, 0.0f, 0.0f);
    m_impl->listenerPos = position;
    m_impl->listenerForward = normalizedOr(forward, Vec3(0.0f, 0.0f, 1.0f));
    m_impl->listenerUp = normalizedOr(up, Vec3(0.0f, 1.0f, 0.0f));
}

VoiceId VolumetricMixer::play(const VoiceParams& params) {
    if (!params.clip || params.clip->frames() < 4) return 0;
    for (auto& slot : m_impl->voices) {
        if (!slot->active) {
            slot = std::make_unique<Impl::Voice>();
            slot->id = m_impl->nextId++;
            slot->active = true;
            slot->params = params;
            slot->time = static_cast<i64>(params.startOffsetSeconds * static_cast<f32>(m_impl->rate));
            return slot->id;
        }
    }
    auto voice = std::make_unique<Impl::Voice>();
    voice->id = m_impl->nextId++;
    voice->active = true;
    voice->params = params;
    voice->time = static_cast<i64>(params.startOffsetSeconds * static_cast<f32>(m_impl->rate));
    const VoiceId id = voice->id;
    m_impl->voices.push_back(std::move(voice));
    return id;
}

void VolumetricMixer::stop(VoiceId voice) {
    if (auto* v = m_impl->find(voice)) v->active = false;
}
void VolumetricMixer::stopAll() {
    for (auto& v : m_impl->voices) v->active = false;
}
bool VolumetricMixer::playing(VoiceId voice) const { return m_impl->find(voice) != nullptr; }
void VolumetricMixer::setPosition(VoiceId voice, const Vec3& position) {
    if (auto* v = m_impl->find(voice)) {
        v->params.position = position;
        v->velocity = Vec3(0.0f, 0.0f, 0.0f);
        v->movedAt = m_impl->clock;
    }
}

void VolumetricMixer::moveVoice(VoiceId voice, const Vec3& position, f32 interval) {
    auto* v = m_impl->find(voice);
    if (!v) return;
    if (interval > 1e-4f) {
        Vec3 velocity = (position - v->params.position) / interval;
        const f32 speed = velocity.length();
        if (speed > 3000.0f) velocity = velocity * (3000.0f / speed);
        v->velocity = velocity;
    }
    v->params.position = position;
    v->movedAt = m_impl->clock;
}

void VolumetricMixer::moveListener(const Vec3& position, const Vec3& forward, const Vec3& up, f32 interval) {
    Impl& m = *m_impl;
    Vec3 velocity = m.listenerVelocity;
    if (interval > 1e-4f) {
        velocity = (position - m.listenerPos) / interval;
        const f32 speed = velocity.length();
        if (speed > 3000.0f) velocity = velocity * (3000.0f / speed);
    }
    setListener(position, forward, up);
    m.listenerVelocity = velocity;
    m.listenerMovedAt = m.clock;
}
void VolumetricMixer::setVolume(VoiceId voice, f32 volume) {
    if (auto* v = m_impl->find(voice)) v->params.volume = std::max(volume, 0.0f);
}
u32 VolumetricMixer::activeVoices() const {
    u32 n = 0;
    for (const auto& v : m_impl->voices) n += v->active ? 1u : 0u;
    return n;
}
void VolumetricMixer::setLayerVolume(AudioLayer layer, f32 volume) {
    m_impl->layerVolume[static_cast<usize>(layer)] = std::max(volume, 0.0f);
}
f32 VolumetricMixer::layerVolume(AudioLayer layer) const { return m_impl->layerVolume[static_cast<usize>(layer)]; }
f64 VolumetricMixer::time() const { return m_impl->clock; }
const RoomEstimate& VolumetricMixer::room() const { return m_impl->room; }

VoiceDebug VolumetricMixer::debug(VoiceId voice) const {
    if (const auto* v = m_impl->find(voice)) return v->debug;
    return VoiceDebug{};
}

void VolumetricMixer::render(f32* out, u32 frames) {
    Impl& m = *m_impl;
    if (!out || frames == 0) return;
    const f32 fs = static_cast<f32>(m.rate);
    const f32 c = std::max(m.settings.speedOfSound, 50.0f);
    for (u32 i = 0; i < frames * 2; ++i) out[i] = 0.0f;
    if (m.wetBus.size() < frames) {
        m.wetBus.resize(frames);
        m.echoBus.resize(frames);
        m.remoteBus.resize(frames);
    }
    std::fill(m.wetBus.begin(), m.wetBus.begin() + frames, 0.0f);
    std::fill(m.echoBus.begin(), m.echoBus.begin() + frames, 0.0f);
    std::fill(m.remoteBus.begin(), m.remoteBus.begin() + frames, 0.0f);
    m.remoteWeight = m.remoteRtSum = m.remoteDampSum = 0.0f;
    m.updateRoom();

    // Where things will be when this block ends: the last move carried on along its velocity (for at most 0.1 s, the
    // time a caller can be late before the extrapolation would only be guessing).
    const f64 blockEnd = m.clock + static_cast<f64>(frames) / static_cast<f64>(m.rate);
    auto carried = [&](const Vec3& position, const Vec3& velocity, f64 movedAt) {
        return position + velocity * static_cast<f32>(std::clamp(blockEnd - movedAt, 0.0, 0.1));
    };
    const Vec3 listenerNow = carried(m.listenerPos, m.listenerVelocity, m.listenerMovedAt);
    const Vec3 forward = m.listenerForward;
    Vec3 right = forward.cross(m.listenerUp);
    right = normalizedOr(right, Vec3(1.0f, 0.0f, 0.0f));
    const f32 invFrames = 1.0f / static_cast<f32>(frames);
    const f32 smoothPerBlock = 1.0f - std::exp(-static_cast<f32>(frames) / (0.25f * fs));

    for (auto& voicePtr : m.voices) {
        Impl::Voice& v = *voicePtr;
        if (!v.active) continue;
        const VoiceParams& p = v.params;
        const f32 layerGain = m.layerVolume[static_cast<usize>(p.layer)] * p.volume * m.settings.masterVolume;
        const VolumetricClip& clip = *p.clip;
        const f32 rateRatio = static_cast<f32>(clip.sampleRate) / fs;

        // ---- Targets for this block.
        f32 gainT = layerGain, gainD = 0.0f, cutT = 20000.0f, cutD = 6000.0f, pan = 0.0f, rear = 0.0f;
        f32 wet = 0.0f, echo = 0.0f, remote = 0.0f;
        f32 delayBegin = 0.0f, delayEnd = 0.0f;
        const bool spatial = p.layer == AudioLayer::World;
        VoiceDebug dbg;
        dbg.active = true;
        if (spatial) {
            m.queryVoice(v);
            const Vec3 sourceNow = carried(p.position, v.velocity, v.movedAt);
            const Vec3 toSource = sourceNow - listenerNow;
            const f32 d = toSource.length();
            const f32 raw = (m.settings.propagationDelay && p.propagate) ? d / c * fs : 0.0f;
            if (!v.started) {
                v.started = true;
                v.rawPrev = raw;
                v.effective = raw;
            }
            const f32 scale = (p.doppler && p.propagate) ? m.settings.dopplerScale : 0.0f;
            const f32 previousEffective = v.effective;
            v.effective += scale * (raw - v.rawPrev);
            v.rawPrev = raw;
            delayBegin = previousEffective;
            delayEnd = v.effective;
            dbg.pitchRatio = 1.0f - (v.effective - previousEffective) * invFrames;
            dbg.distance = d;
            dbg.delaySeconds = v.effective / fs;

            const f32 distanceGainValue = distanceGain(d, p.refDistance, p.maxDistance, Rolloff::Inverse, p.rolloff);
            const f32 airCut = m.settings.airAbsorption ? std::max(20000.0f / (1.0f + d / 90.0f), 1800.0f) : 20000.0f;
            gainT = layerGain * distanceGainValue * v.transmission.low;
            cutT = std::min(airCut, cutoffForHighs(v.transmission.high));
            dbg.transmissionLow = v.transmission.low;
            dbg.transmissionHigh = v.transmission.high;
            dbg.blocked = !v.transmission.clear();
            Vec3 dir = normalizedOr(toSource, forward);
            if (v.diffracted) {
                gainD = layerGain * distanceGainValue * 0.8f / (1.0f + v.extraPath * 0.8f);
                cutD = std::min(airCut, std::clamp(6500.0f / (1.0f + v.extraPath * 0.5f), 700.0f, 6500.0f));
                const Vec3 viaDetour = normalizedOr(v.detour - listenerNow, dir);
                const f32 total = gainT + gainD + 1e-6f;
                dir = normalizedOr(dir * (gainT / total) + viaDetour * (gainD / total), dir);
                dbg.diffracted = true;
                dbg.diffractionGain = gainD;
            }
            dbg.gain = gainT + gainD;
            dbg.cutoffHz = cutT;
            dbg.apparentDirection = dir;
            pan = std::clamp(dir.dot(right), -1.0f, 1.0f);
            rear = std::max(0.0f, -dir.dot(forward));
            const f32 soft = 1.0f / (1.0f + d / (3.0f * std::max(p.refDistance, 0.1f)));
            // What reaches the listener's room from behind a wall is what the wall lets through: the reverb is fed
            // with that share (and with the dulled signal), not with the full sound.
            const f32 open = layerGain * distanceGainValue;
            const f32 pass = open > 1e-9f ? std::clamp((gainT + gainD) / open, 0.0f, 1.0f) : 1.0f;
            const f32 base = layerGain * soft * pass;
            wet = base * m.wetLevel.value;
            echo = m.room.echoCount > 0 ? base * 0.8f : 0.0f;
            if (m.settings.reverb && m.world && dbg.blocked) {
                // Behind a wall the source is in another room: its own reverb is what leaks through the wall.
                if (m.clock >= v.nextSourceRoom) {
                    v.sourceRoom = probeRoom(*m.world, p.position);
                    v.nextSourceRoom = m.clock + 0.25;
                }
                remote = base * v.sourceRoom.wet * 1.4f;
                m.remoteWeight += remote;
                m.remoteRtSum += remote * v.sourceRoom.rt60;
                m.remoteDampSum += remote * v.sourceRoom.damping;
            }
            dbg.reverbSend = wet;
        } else if (p.layer == AudioLayer::Ambient) {
            // Rain and wind outside: they cover the scene, but a roof and walls keep most of them out.
            const f32 exposure = m.sky.value;
            gainT = layerGain * lerp(0.16f, 1.0f, exposure);
            cutT = lerp(650.0f, 20000.0f, std::pow(exposure, 0.8f));
            dbg.gain = gainT;
            dbg.cutoffHz = cutT;
        } else {
            dbg.gain = gainT;
        }
        v.debug = dbg;

        if (!v.inited) {
            v.inited = true;
            v.gainT0 = gainT;
            v.gainD0 = gainD;
            v.cutT0 = cutT;
            v.cutD0 = cutD;
            v.pan0 = pan;
            v.rear0 = rear;
            v.wet0 = wet;
            v.echo0 = echo;
            v.remote0 = remote;
        }

        // ---- The block: every parameter glides from where it was to this block's target.
        const f32 stepGainT = (gainT - v.gainT0) * invFrames, stepGainD = (gainD - v.gainD0) * invFrames;
        const f32 stepPan = (pan - v.pan0) * invFrames, stepRear = (rear - v.rear0) * invFrames;
        const f32 stepWet = (wet - v.wet0) * invFrames, stepEcho = (echo - v.echo0) * invFrames;
        const f32 stepRemote = (remote - v.remote0) * invFrames;
        const f32 stepCutT = (cutT - v.cutT0) * invFrames, stepCutD = (cutD - v.cutD0) * invFrames;
        const f32 stepDelay = (delayEnd - delayBegin) * invFrames;
        f32 curGainT = v.gainT0, curGainD = v.gainD0, curPan = v.pan0, curRear = v.rear0, curWet = v.wet0;
        f32 curRemote = v.remote0;
        f32 curEcho = v.echo0, curCutT = v.cutT0, curCutD = v.cutD0, curDelay = delayBegin;
        bool finished = false;
        for (u32 i = 0; i < frames; ++i) {
            f32 left = 0.0f, rightOut = 0.0f;
            const f64 position = (static_cast<f64>(v.time) + i - curDelay) * rateRatio;
            if (!p.loop && position >= static_cast<f64>(clip.frames())) finished = true;
            if (spatial) {
                const f32 x = readClip(clip, 0, position, p.loop) * (clip.channels > 1 ? 0.5f : 1.0f) +
                              (clip.channels > 1 ? readClip(clip, 1, position, p.loop) * 0.5f : 0.0f);
                const f32 filtered = v.lpT.process(x, dsp::lowpassCoefficient(curCutT, fs));
                f32 mono = filtered * curGainT;
                if (curGainD > 1e-5f) mono += v.lpD.process(x, dsp::lowpassCoefficient(curCutD, fs)) * curGainD;
                m.wetBus[i] += filtered * curWet;
                m.echoBus[i] += filtered * curEcho;
                m.remoteBus[i] += filtered * curRemote;
                if (m.settings.ears) {
                    // A source behind is dulled a little (the outer ear); then the head: the sound reaches the far ear later
                    // (Woodworth's time difference) and shadowed (a filter, not just a level: the highs are the ones the head
                    // stops).
                    mono = v.rearLp.process(mono, dsp::lowpassCoefficient(lerp(20000.0f, 5500.0f, curRear), fs));
                    const f32 lateral = std::clamp(curPan, -1.0f, 1.0f);
                    const f32 phi = std::asin(std::fabs(lateral));
                    const f32 itdSamples = (kHeadRadius / c) * (phi + std::sin(phi)) * fs;
                    v.itdLeft.push(mono);
                    v.itdRight.push(mono);
                    const f32 dl = 1.0f + (lateral > 0.0f ? itdSamples : 0.0f);
                    const f32 dr = 1.0f + (lateral < 0.0f ? itdSamples : 0.0f);
                    // The right ear's axis points to the listener's right; the angle to the source is acos(lateral).
                    const f32 angleRight = std::acos(lateral);
                    const f32 angleLeft = std::acos(-lateral);
                    left = v.shadowLeft.process(v.itdLeft.read(dl), angleLeft, fs, c);
                    rightOut = v.shadowRight.process(v.itdRight.read(dr), angleRight, fs, c);
                } else {
                    left = rightOut = mono;
                }
            } else if (p.layer == AudioLayer::Ambient) {
                const f32 cof = dsp::lowpassCoefficient(curCutT, fs);
                const f32 a = v.lpAmbient[0].process(readClip(clip, 0, position, p.loop), cof) * curGainT;
                const f32 b = v.lpAmbient[1].process(readClip(clip, clip.channels > 1 ? 1 : 0, position, p.loop), cof) * curGainT;
                left = a;
                rightOut = b;
                m.wetBus[i] += (a + b) * 0.5f * m.wetLevel.value * 0.3f;
            } else {
                left = readClip(clip, 0, position, p.loop) * curGainT;
                rightOut = readClip(clip, clip.channels > 1 ? 1 : 0, position, p.loop) * curGainT;
            }
            out[i * 2] += left;
            out[i * 2 + 1] += rightOut;
            curGainT += stepGainT;
            curGainD += stepGainD;
            curPan += stepPan;
            curRear += stepRear;
            curWet += stepWet;
            curEcho += stepEcho;
            curRemote += stepRemote;
            curCutT += stepCutT;
            curCutD += stepCutD;
            curDelay += stepDelay;
        }
        v.gainT0 = gainT;
        v.gainD0 = gainD;
        v.cutT0 = cutT;
        v.cutD0 = cutD;
        v.pan0 = pan;
        v.rear0 = rear;
        v.wet0 = wet;
        v.echo0 = echo;
        v.remote0 = remote;
        v.time += frames;
        if (finished) v.active = false;
    }

    // ---- The room: the shared reverb and the echoes, fitted to where the listener stands.
    m.rt60.step(smoothPerBlock);
    m.damping.step(smoothPerBlock);
    m.predelay.step(smoothPerBlock);
    m.wetLevel.step(smoothPerBlock);
    m.sky.step(smoothPerBlock);
    if (m.settings.reverb) m.reverb.setDecay(m.rt60.value, m.damping.value, m.predelay.value);
    if (m.remoteWeight > 1e-6f) {
        m.remoteRt60.aim(m.remoteRtSum / m.remoteWeight);
        m.remoteDamping.aim(m.remoteDampSum / m.remoteWeight);
    }
    m.remoteRt60.step(smoothPerBlock);
    m.remoteDamping.step(smoothPerBlock);
    if (m.settings.reverb && m.remoteRt60.set) m.remoteReverb.setDecay(m.remoteRt60.value, m.remoteDamping.value, 0.02f);
    for (u32 i = 0; i < m.echoGainNow.size(); ++i) {
        const f32 target = i < m.room.echoCount ? m.room.echoes[i].gain : 0.0f;
        m.echoGainNow[i] = m.echoStarted ? m.echoGainNow[i] + (target - m.echoGainNow[i]) * smoothPerBlock : target;
    }
    m.echoStarted = true;
    for (u32 i = 0; i < frames; ++i) {
        f32 l = 0.0f, r = 0.0f;
        if (m.settings.reverb) {
            m.reverb.process(m.wetBus[i], l, r);
            l *= 0.9f;
            r *= 0.9f;
            f32 rl = 0.0f, rr = 0.0f;
            m.remoteReverb.process(m.remoteBus[i], rl, rr);
            l += rl * 0.9f;
            r += rr * 0.9f;
        }
        m.echoLine.push(m.echoBus[i]);
        f32 echoMono = 0.0f;
        if (m.settings.reverb) {
            for (u32 k = 0; k < m.echoNow.size(); ++k) {
                if (m.echoGainNow[k] > 1e-4f) echoMono += m.echoLine.read(std::max(m.echoNow[k].delay * fs, 2.0f)) * m.echoGainNow[k];
            }
        }
        echoMono = m.echoLp.process(echoMono, dsp::lowpassCoefficient(3500.0f, fs));
        out[i * 2] += l + echoMono * 0.8f;
        out[i * 2 + 1] += r + echoMono * 0.8f;
    }

    // ---- Out: a soft limiter, so a loud scene saturates gently instead of clipping.
    for (u32 i = 0; i < frames * 2; ++i) {
        const f32 x = out[i] * m.settings.masterVolume;
        out[i] = std::fabs(x) < 0.6f ? x : std::copysign(0.6f + 0.4f * std::tanh((std::fabs(x) - 0.6f) / 0.4f), x);
    }
    m.clock += static_cast<f64>(frames) / static_cast<f64>(m.rate);
}


}  // namespace Caffeine::Audio
