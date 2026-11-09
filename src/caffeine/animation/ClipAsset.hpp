#pragma once

#include "core/Types.hpp"
#include "math/Quat.hpp"
#include "math/Vec3.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Caffeine::Animation {

struct SpriteFrameKey {
    f32 time = 0.0f;
    u32 frame = 0;
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 w = 0.0f;
    f32 h = 0.0f;
};

struct Vec3Key {
    f32 time = 0.0f;
    Vec3 value{};
};

struct QuatKey {
    f32 time = 0.0f;
    Quat value = Quat::identity();
};

/// Shared clip for 2D sprite frames and 3D local positions.
struct MotionClip {
    std::string name = "Clip";
    f32 duration = 1.0f;
    bool loop = true;
    u32 fps = 12;
    std::vector<SpriteFrameKey> spriteFrames;
    std::vector<Vec3Key> positions;
    std::vector<QuatKey> rotations;
};

inline f32 motionClipDuration(const MotionClip& clip) {
    f32 end = clip.duration;
    for (const SpriteFrameKey& key : clip.spriteFrames) end = std::max(end, key.time);
    for (const Vec3Key& key : clip.positions) end = std::max(end, key.time);
    for (const QuatKey& key : clip.rotations) end = std::max(end, key.time);
    return std::max(end, 0.0f);
}

inline u32 sampleSpriteFrame(const MotionClip& clip, f32 time) {
    if (clip.spriteFrames.empty()) return 0;
    u32 frame = clip.spriteFrames.front().frame;
    for (const SpriteFrameKey& key : clip.spriteFrames) {
        if (key.time > time) break;
        frame = key.frame;
    }
    return frame;
}

inline Vec3 samplePosition(const MotionClip& clip, f32 time) {
    if (clip.positions.empty()) return {};
    if (clip.positions.size() == 1 || time <= clip.positions.front().time) {
        return clip.positions.front().value;
    }
    for (size_t i = 1; i < clip.positions.size(); ++i) {
        const Vec3Key& b = clip.positions[i];
        if (time > b.time) continue;
        const Vec3Key& a = clip.positions[i - 1];
        const f32 span = b.time - a.time;
        const f32 t = span > 0.0f ? (time - a.time) / span : 1.0f;
        return a.value + (b.value - a.value) * t;
    }
    return clip.positions.back().value;
}

inline Quat sampleRotation(const MotionClip& clip, f32 time) {
    if (clip.rotations.empty()) return Quat::identity();
    if (clip.rotations.size() == 1 || time <= clip.rotations.front().time) {
        return clip.rotations.front().value;
    }
    for (size_t i = 1; i < clip.rotations.size(); ++i) {
        const QuatKey& b = clip.rotations[i];
        if (time > b.time) continue;
        const QuatKey& a = clip.rotations[i - 1];
        const f32 span = b.time - a.time;
        const f32 t = span > 0.0f ? (time - a.time) / span : 1.0f;
        return Quat::slerp(a.value, b.value, t);
    }
    return clip.rotations.back().value;
}

inline bool saveMotionClip(const std::filesystem::path& path, const MotionClip& clip) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (!out) return false;
    out << "CAFANIM1\n";
    out << "name " << clip.name << '\n';
    out << "loop " << (clip.loop ? 1 : 0) << '\n';
    out << "fps " << clip.fps << '\n';
    out << "duration " << clip.duration << '\n';
    for (const SpriteFrameKey& key : clip.spriteFrames) {
        out << "sprite " << key.time << ' ' << key.frame << ' ' << key.x << ' ' << key.y << ' '
            << key.w << ' ' << key.h << '\n';
    }
    for (const Vec3Key& key : clip.positions) {
        out << "pos " << key.time << ' ' << key.value.x << ' ' << key.value.y << ' ' << key.value.z
            << '\n';
    }
    for (const QuatKey& key : clip.rotations) {
        out << "rot " << key.time << ' ' << key.value.x << ' ' << key.value.y << ' ' << key.value.z
            << ' ' << key.value.w << '\n';
    }
    return static_cast<bool>(out);
}

inline bool loadMotionClip(const std::filesystem::path& path, MotionClip& outClip) {
    std::ifstream in(path);
    if (!in) return false;
    std::string header;
    if (!std::getline(in, header) || header != "CAFANIM1") return false;

    MotionClip clip;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "name") {
            std::getline(ss, clip.name);
            if (!clip.name.empty() && clip.name[0] == ' ') clip.name.erase(0, 1);
        } else if (tag == "loop") {
            int loop = 1;
            ss >> loop;
            clip.loop = loop != 0;
        } else if (tag == "fps") {
            ss >> clip.fps;
        } else if (tag == "duration") {
            ss >> clip.duration;
        } else if (tag == "sprite") {
            SpriteFrameKey key;
            ss >> key.time >> key.frame >> key.x >> key.y >> key.w >> key.h;
            clip.spriteFrames.push_back(key);
        } else if (tag == "pos") {
            Vec3Key key;
            ss >> key.time >> key.value.x >> key.value.y >> key.value.z;
            clip.positions.push_back(key);
        } else if (tag == "rot") {
            QuatKey key;
            ss >> key.time >> key.value.x >> key.value.y >> key.value.z >> key.value.w;
            clip.rotations.push_back(key);
        }
    }
    clip.duration = motionClipDuration(clip);
    outClip = std::move(clip);
    return true;
}

}  // namespace Caffeine::Animation
