// caffeine-audio-demo: renders scenarios of the volumetric audio to a WAV and a JSON trace, offline (no sound card).
// scripts/render-audio-demo.py turns each pair into a video (a top view of the scene with what the mixer is doing,
// and the audio), so the result can be listened to and checked by ear.
//
//   caffeine-audio-demo <doppler|shots|occlusion|layers> <clips-dir> <out-dir>
//
// The clips are mono or stereo 16-bit WAVs (the script decodes the moodist sounds into <clips-dir>).

#include "audio/volumetric/VolumetricMixer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace Caffeine;
using namespace Caffeine::Audio;

namespace {

constexpr u32 kRate = 44100;
constexpr u32 kBlock = 256;
constexpr f32 kFps = 24.0f;

VolumetricClipPtr loadWav(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return nullptr;
    }
    std::vector<unsigned char> bytes;
    unsigned char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) bytes.insert(bytes.end(), buffer, buffer + n);
    std::fclose(file);
    auto u32le = [&](size_t i) { return static_cast<u32>(bytes[i] | (bytes[i + 1] << 8) | (bytes[i + 2] << 16) | (bytes[i + 3] << 24)); };
    auto u16le = [&](size_t i) { return static_cast<u32>(bytes[i] | (bytes[i + 1] << 8)); };
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0) return nullptr;
    auto clip = std::make_shared<VolumetricClip>();
    size_t pos = 12;
    u32 bits = 16;
    while (pos + 8 <= bytes.size()) {
        const u32 size = u32le(pos + 4);
        if (std::memcmp(&bytes[pos], "fmt ", 4) == 0) {
            clip->channels = u16le(pos + 10);
            clip->sampleRate = u32le(pos + 12);
            bits = u16le(pos + 22);
        } else if (std::memcmp(&bytes[pos], "data", 4) == 0 && bits == 16) {
            const size_t count = std::min<size_t>(size, bytes.size() - pos - 8) / 2;
            clip->samples.resize(count);
            for (size_t i = 0; i < count; ++i)
                clip->samples[i] = static_cast<f32>(static_cast<int16_t>(u16le(pos + 8 + i * 2))) / 32768.0f;
            break;
        }
        pos += 8 + size + (size & 1);
    }
    return clip->samples.empty() ? nullptr : clip;
}

struct SourceDef {
    std::string name;
    VoiceParams params;
    std::function<Vec3(f32)> path;
    f32 startTime = 0.0f;
    f32 stopTime = 1e9f;
    VoiceId id = 0;
    bool started = false;
};

struct Pose {
    Vec3 position;
    f32 yaw = 0.0f;  // 0 faces +Z, positive towards +X
};

struct View {
    f32 cx = 0.0f, cz = 0.0f, scale = 6.0f;  // metres from the centre to the left/right edge of the picture
};

struct Scenario {
    AcousticBoxWorld world;
    std::vector<SourceDef> sources;
    f32 duration = 20.0f;
    std::function<Pose(f32)> listener;
    std::function<View(f32)> view;
    std::function<std::string(f32)> caption;
    std::function<void(VolumetricMixer&, f32)> controls;  // per-block settings changes (Doppler on/off, layer volumes)
};

const char* kindOf(const AcousticOrientedBox& box) {
    if (!box.solidForRooms) return "ground";
    return "wall";
}

void writeWav(const std::string& path, const std::vector<f32>& interleaved) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const u32 dataBytes = static_cast<u32>(interleaved.size() * 2);
    auto p32 = [&](u32 v) { std::fwrite(&v, 4, 1, f); };
    auto p16 = [&](u16 v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    p32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    p32(16);
    p16(1);
    p16(2);
    p32(kRate);
    p32(kRate * 4);
    p16(4);
    p16(16);
    std::fwrite("data", 1, 4, f);
    p32(dataBytes);
    for (f32 s : interleaved) {
        const int16_t v = static_cast<int16_t>(std::lround(std::max(-1.0f, std::min(1.0f, s)) * 32767.0f));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

int run(Scenario& sc, const std::string& outDir, const std::string& name) {
    VolumetricMixer mixer(kRate);
    mixer.setWorld(&sc.world);
    std::vector<f32> audio;
    audio.reserve(static_cast<size_t>(sc.duration * kRate * 2));
    std::string json = "{\"fps\":" + std::to_string(static_cast<int>(kFps)) + ",\"walls\":[";
    bool firstWall = true;
    for (const AcousticOrientedBox& b : sc.world.boxes()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s{\"x0\":%.2f,\"z0\":%.2f,\"x1\":%.2f,\"z1\":%.2f,\"y1\":%.2f,\"kind\":\"%s\"}", firstWall ? "" : ",",
                      b.center.x - b.half.x, b.center.z - b.half.z, b.center.x + b.half.x, b.center.z + b.half.z,
                      b.center.y + b.half.y, kindOf(b));
        json += buf;
        firstWall = false;
    }
    json += "],\"frames\":[";
    std::vector<f32> block(kBlock * 2);
    const u32 blocks = static_cast<u32>(sc.duration * kRate / kBlock);
    f32 nextFrame = 0.0f;
    bool firstFrame = true;
    for (u32 b = 0; b < blocks; ++b) {
        const f32 t = static_cast<f32>(b * kBlock) / kRate;
        const Pose pose = sc.listener(t);
        mixer.setListener(pose.position, Vec3(std::sin(pose.yaw), 0.0f, std::cos(pose.yaw)));
        if (sc.controls) sc.controls(mixer, t);
        for (SourceDef& s : sc.sources) {
            if (!s.started && t >= s.startTime) {
                s.params.position = s.path(t);
                s.id = mixer.play(s.params);
                s.started = true;
            }
            if (s.started && mixer.playing(s.id)) {
                if (t >= s.stopTime) mixer.stop(s.id);
                else mixer.setPosition(s.id, s.path(t));
            }
        }
        mixer.render(block.data(), kBlock);
        audio.insert(audio.end(), block.begin(), block.end());
        if (t >= nextFrame) {
            nextFrame += 1.0f / kFps;
            const View v = sc.view(t);
            const RoomEstimate& room = mixer.room();
            char head[512];
            std::snprintf(head, sizeof(head),
                          "%s{\"t\":%.3f,\"listener\":{\"x\":%.2f,\"z\":%.2f,\"yaw\":%.3f},\"view\":{\"cx\":%.2f,\"cz\":%.2f,\"scale\":%.2f},"
                          "\"room\":{\"open\":%.2f,\"sky\":%.2f,\"rt60\":%.2f,\"wet\":%.2f,\"echoes\":%u},\"layers\":{\"world\":%.2f,\"ambient\":%.2f},\"caption\":\"%s\",\"sources\":[",
                          firstFrame ? "" : ",", t, pose.position.x, pose.position.z, pose.yaw, v.cx, v.cz, v.scale, room.openness,
                          room.skyExposure, room.rt60, room.wet, room.echoCount, mixer.layerVolume(AudioLayer::World),
                          mixer.layerVolume(AudioLayer::Ambient), escape(sc.caption(t)).c_str());
            json += head;
            firstFrame = false;
            bool firstSource = true;
            for (const SourceDef& s : sc.sources) {
                if (!s.started || !mixer.playing(s.id)) continue;
                const VoiceDebug d = mixer.debug(s.id);
                const Vec3 pos = s.path(t);
                char buf[512];
                std::snprintf(buf, sizeof(buf),
                              "%s{\"name\":\"%s\",\"layer\":%d,\"x\":%.2f,\"z\":%.2f,\"dist\":%.1f,\"gain_db\":%.1f,\"cutoff\":%.0f,\"blocked\":%d,"
                              "\"diffracted\":%d,\"pitch\":%.4f,\"dx\":%.3f,\"dz\":%.3f}",
                              firstSource ? "" : ",", s.name.c_str(), static_cast<int>(s.params.layer), pos.x, pos.z, d.distance,
                              20.0f * std::log10(std::max(d.gain, 1e-5f)), d.cutoffHz, d.blocked ? 1 : 0, d.diffracted ? 1 : 0, d.pitchRatio,
                              d.apparentDirection.x, d.apparentDirection.z);
                json += buf;
                firstSource = false;
            }
            json += "]}";
        }
    }
    json += "]}";
    writeWav(outDir + "/" + name + ".wav", audio);
    if (std::FILE* f = std::fopen((outDir + "/" + name + ".json").c_str(), "wb")) {
        std::fwrite(json.data(), 1, json.size(), f);
        std::fclose(f);
    }
    std::printf("%s: %.1f s, %zu frames of audio\n", name.c_str(), sc.duration, audio.size() / 2);
    return 0;
}

Vec3 lerp3(const Vec3& a, const Vec3& b, f32 t) { return a + (b - a) * std::max(0.0f, std::min(1.0f, t)); }

SourceDef source(const std::string& name, const VolumetricClipPtr& clip, AudioLayer layer, bool loop, f32 volume,
                 std::function<Vec3(f32)> path, f32 start = 0.0f, f32 stop = 1e9f) {
    SourceDef s;
    s.name = name;
    s.params.clip = clip;
    s.params.layer = layer;
    s.params.loop = loop;
    s.params.volume = volume;
    s.params.maxDistance = 3000.0f;
    s.path = std::move(path);
    s.startTime = start;
    s.stopTime = stop;
    return s;
}

// ------------------------------------------------------------------------------------------ scenarios

/// An ambulance and an aeroplane go past: Doppler on, then the same pass with it switched off.
bool buildDoppler(Scenario& sc, const std::string& clips) {
    auto siren = loadWav(clips + "/ambulance-siren.wav");
    auto plane = loadWav(clips + "/airplane.wav");
    if (!siren || !plane) return false;
    sc.world.add({-2000, -1, -2000}, {2000, 0, 2000}, AcousticMaterial::ground(), false);
    sc.duration = 34.0f;
    sc.listener = [](f32) { return Pose{{0, 1.5f, 0}, 0.0f}; };
    sc.view = [](f32) { return View{0, 20, 200}; };
    const f32 speed = 30.0f;  // 108 km/h
    sc.sources.push_back(source("ambulance, Doppler on", siren, AudioLayer::World, true, 1.2f,
                                [=](f32 t) { return Vec3(-150.0f + speed * t, 1.0f, 10.0f); }, 0.0f, 10.0f));
    sc.sources.push_back(source("ambulance, Doppler off", siren, AudioLayer::World, true, 1.2f,
                                [=](f32 t) { return Vec3(-150.0f + speed * (t - 12.0f), 1.0f, 10.0f); }, 12.0f, 22.0f));
    sc.sources.push_back(source("airplane, 150 m/s", plane, AudioLayer::World, true, 1.5f,
                                [=](f32 t) { return Vec3(-420.0f + 150.0f * (t - 24.0f), 80.0f, 25.0f); }, 24.0f, 34.0f));
    sc.caption = [](f32 t) {
        if (t < 10.0f) return std::string("Ambulance at 108 km/h: the pitch falls as it passes (Doppler from the changing delay)");
        if (t < 12.0f) return std::string("...");
        if (t < 22.0f) return std::string("The same pass with Doppler off: the pitch never moves");
        if (t < 24.0f) return std::string("...");
        return std::string("Aeroplane at 150 m/s, 80 m up: a steep, fast shift");
    };
    sc.controls = [](VolumetricMixer& m, f32 t) { m.settings().dopplerScale = (t >= 12.0f && t < 22.0f) ? 0.0f : 1.0f; };
    return true;
}

/// Shots in a field with a far cliff, inside a hall, and from outside a wall.
bool buildShots(Scenario& sc, const std::string& clips) {
    auto bang = loadWav(clips + "/fireworks.wav");
    if (!bang) return false;
    // Stage A: the field and a cliff 120 m behind the shooter.
    sc.world.add({-400, -1, -400}, {400, 0, 400}, AcousticMaterial::ground(), false);
    sc.world.add({-120, 0, 160}, {120, 60, 175}, AcousticMaterial::concrete());
    // Stage B/C: a concrete hall at x = 600.
    sc.world.addRoom({590, 0, -8}, {614, 6, 8}, AcousticMaterial::concrete());
    sc.duration = 36.0f;
    sc.listener = [](f32 t) {
        if (t < 12.0f) return Pose{{0, 1.5f, 0}, 0.0f};
        if (t < 24.0f) return Pose{{596, 1.5f, 0}, 1.5708f};
        return Pose{{604, 1.5f, 0}, 1.5708f};  // inside, the shooter behind the wall
    };
    sc.view = [](f32 t) {
        if (t < 12.0f) return View{0, 80, 140};
        return View{602, 0, 40};
    };
    sc.sources.push_back(source("rifle, 150 m", bang, AudioLayer::World, true, 1.4f, [](f32) { return Vec3(0, 1.5f, 150); }, 0.0f, 12.0f));
    sc.sources.push_back(source("rifle in the hall", bang, AudioLayer::World, true, 1.4f, [](f32) { return Vec3(610, 1.5f, 0); }, 12.0f, 24.0f));
    sc.sources.push_back(source("rifle outside the wall", bang, AudioLayer::World, true, 1.4f, [](f32) { return Vec3(560, 1.5f, 0); }, 24.0f, 36.0f));
    sc.caption = [](f32 t) {
        if (t < 12.0f) return std::string("Open field: the shot arrives late (150 m = 0.44 s), dull, and the far cliff echoes it");
        if (t < 24.0f) return std::string("Concrete hall: the same shot rings in the room");
        return std::string("The shooter is outside the wall: muffled, the highs are gone");
    };
    return true;
}

/// Walking round the end of a wall, then along a house with a door.
bool buildOcclusion(Scenario& sc, const std::string& clips) {
    auto radio = loadWav(clips + "/tuning-radio.wav");
    auto fan = loadWav(clips + "/ceiling-fan.wav");
    if (!radio || !fan) return false;
    sc.world.add({-400, -1, -400}, {400, 0, 400}, AcousticMaterial::ground(), false);
    sc.world.add({-10, 0, 7.8f}, {8, 4, 8.2f}, AcousticMaterial::concrete());  // a free-standing wall
    // A house with a door in its +Z wall, further along.
    sc.world.addRoom({40, 0, -6}, {56, 3, 6}, AcousticMaterial::brick(), 0.3f, 2.0f, 48.0f);
    sc.duration = 28.0f;
    sc.listener = [](f32 t) {
        if (t < 14.0f) {
            const f32 u = t / 14.0f;  // from in front of the wall, round its right end (x = 8)
            const Vec3 a(0, 1.5f, 0), b(14, 1.5f, 0), c(14, 1.5f, 16);
            return u < 0.5f ? Pose{lerp3(a, b, u * 2.0f), 0.0f} : Pose{lerp3(b, c, (u - 0.5f) * 2.0f), 0.0f};
        }
        const f32 u = (t - 14.0f) / 14.0f;  // along the house's front, past its door
        return Pose{lerp3(Vec3(36, 1.5f, 10), Vec3(60, 1.5f, 10), u), 0.0f};
    };
    sc.view = [](f32 t) { return t < 14.0f ? View{4, 8, 22} : View{48, 0, 22}; };
    sc.sources.push_back(source("radio behind the wall", radio, AudioLayer::World, true, 1.3f, [](f32) { return Vec3(0, 1.5f, 15.0f); }, 0.0f, 14.0f));
    sc.sources.push_back(source("fan inside the house", fan, AudioLayer::World, true, 2.0f, [](f32) { return Vec3(48, 1.5f, -2.0f); }, 14.0f, 28.0f));
    sc.caption = [](f32 t) {
        if (t < 5.0f) return std::string("Behind a concrete wall: muffled and quiet (lows pass, highs do not)");
        if (t < 10.0f) return std::string("Past the end of the wall the sound bends round it, from the corner");
        if (t < 14.0f) return std::string("Clear line: full and bright");
        if (t < 20.0f) return std::string("Along a house: through the brick it is dull");
        return std::string("At the door the sound comes out of the doorway");
    };
    return true;
}

/// Rain and wind (ambient layer) outdoors and in a house with a fireplace (a localised sound), with thunder.
bool buildLayers(Scenario& sc, const std::string& clips) {
    auto rain = loadWav(clips + "/heavy-rain.wav");
    auto wind = loadWav(clips + "/wind.wav");
    auto fire = loadWav(clips + "/campfire.wav");
    auto thunder = loadWav(clips + "/thunder.wav");
    if (!rain || !wind || !fire || !thunder) return false;
    sc.world.add({-400, -1, -400}, {400, 0, 400}, AcousticMaterial::ground(), false);
    sc.world.addRoom({-8, 0, -6}, {8, 3, 6}, AcousticMaterial::brick(), 0.3f, 2.0f, 0.0f);
    sc.duration = 36.0f;
    sc.listener = [](f32 t) {
        // From the open (z = 24) to the door (z = 6), in, then to the fire at the back (z = -4), and back to the door.
        const Vec3 a(0, 1.5f, 24), door(0, 1.5f, 6.5f), inside(0, 1.5f, -1.0f), fireSpot(0, 1.5f, -3.0f);
        if (t < 8.0f) return Pose{lerp3(a, door, t / 8.0f), 3.14159f};
        if (t < 12.0f) return Pose{lerp3(door, inside, (t - 8.0f) / 4.0f), 3.14159f};
        if (t < 20.0f) return Pose{lerp3(inside, fireSpot, (t - 12.0f) / 8.0f), 3.14159f};
        if (t < 26.0f) return Pose{fireSpot, 3.14159f};
        return Pose{lerp3(fireSpot, door, (t - 26.0f) / 6.0f), 0.0f};
    };
    sc.view = [](f32) { return View{0, 8, 30}; };
    sc.sources.push_back(source("rain (ambient)", rain, AudioLayer::Ambient, true, 0.9f, [](f32) { return Vec3(0, 0, 0); }));
    sc.sources.push_back(source("wind (ambient)", wind, AudioLayer::Ambient, true, 0.5f, [](f32) { return Vec3(0, 0, 0); }));
    sc.sources.push_back(source("fireplace", fire, AudioLayer::World, true, 1.6f, [](f32) { return Vec3(0, 0.5f, -4.5f); }));
    sc.sources.push_back(source("thunder, 900 m", thunder, AudioLayer::World, false, 2.0f, [](f32) { return Vec3(-600, 60, 700); }, 1.0f));
    sc.sources.push_back(source("thunder, 450 m", thunder, AudioLayer::World, false, 2.0f, [](f32) { return Vec3(300, 60, 330); }, 17.0f));
    sc.caption = [](f32 t) {
        if (t < 8.0f) return std::string("Outside: the rain and wind (ambient layer) cover everything; the fire is far and faint");
        if (t < 12.0f) return std::string("Through the door: the ambient layer is muffled and quieter, the fire gets clearer");
        if (t < 20.0f) return std::string("Inside, walking to the fireplace: nearer is clearer");
        if (t < 26.0f) return std::string("By the fire; thunder rumbles through the walls");
        if (t < 32.0f) return std::string("Back out: the rain returns");
        return std::string("Ambient layer volume at 30%: layers are mixed on their own");
    };
    sc.controls = [](VolumetricMixer& m, f32 t) { m.setLayerVolume(AudioLayer::Ambient, t >= 32.0f ? 0.3f : 1.0f); };
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: caffeine-audio-demo <doppler|shots|occlusion|layers> <clips-dir> <out-dir>\n");
        return 2;
    }
    const std::string which = argv[1], clips = argv[2], out = argv[3];
    Scenario sc;
    bool ok = false;
    if (which == "doppler") ok = buildDoppler(sc, clips);
    else if (which == "shots") ok = buildShots(sc, clips);
    else if (which == "occlusion") ok = buildOcclusion(sc, clips);
    else if (which == "layers") ok = buildLayers(sc, clips);
    if (!ok) {
        std::fprintf(stderr, "could not build scenario '%s' (missing clips in %s?)\n", which.c_str(), clips.c_str());
        return 1;
    }
    return run(sc, out, which);
}
