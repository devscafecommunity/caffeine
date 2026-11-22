#pragma once

#include "animation/Skeleton2D.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace Caffeine::Animation {

// ============================================================================
// Text formats (one record per line, same style as CAFANIM1):
//
//   CAFRIG2D1
//   bone  <name> <parent> <px> <py> <rotation> <sx> <sy>
//   vert  <px> <py> <u> <v> <n> [<bone> <weight>] x n
//   tri   <a> <b> <c>
//   param <name> <min> <default> <max>
//   bind  <parameter index> <bone> <channel> <n> [<x> <y>] x n
//
//   CAFPCLIP1
//   name <text>
//   loop <0|1>
//   duration <seconds>
//   track <parameter name> <n> [<time> <value>] x n
//
// Names contain no spaces. Unknown parameter names in a clip are skipped on load.
// ============================================================================

inline bool saveRig2D(const std::filesystem::path& path, const Rig2D& rig) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (!out) return false;
    out << std::setprecision(9);
    out << "CAFRIG2D1\n";
    for (const Bone2D& bone : rig.skeleton.bones) {
        out << "bone " << bone.name.cStr() << ' ' << bone.parentIndex << ' '
            << bone.restPosition.x << ' ' << bone.restPosition.y << ' ' << bone.restRotation << ' '
            << bone.restScale.x << ' ' << bone.restScale.y << '\n';
    }
    for (const Vertex2D& v : rig.vertices) {
        out << "vert " << v.position.x << ' ' << v.position.y << ' ' << v.uv.x << ' ' << v.uv.y << ' '
            << v.influences.size();
        for (const BoneInfluence2D& inf : v.influences) {
            out << ' ' << inf.bone << ' ' << inf.weight;
        }
        out << '\n';
    }
    for (usize i = 0; i + 2 < rig.indices.size(); i += 3) {
        out << "tri " << rig.indices[i] << ' ' << rig.indices[i + 1] << ' ' << rig.indices[i + 2] << '\n';
    }
    for (const Parameter2D& p : rig.parameters) {
        out << "param " << p.name.cStr() << ' ' << p.minValue << ' ' << p.defaultValue << ' '
            << p.maxValue << '\n';
    }
    for (const ParamBinding2D& b : rig.bindings) {
        out << "bind " << b.parameter << ' ' << b.bone << ' ' << static_cast<u32>(b.channel) << ' '
            << b.keys.size();
        for (const CurveKey2D& k : b.keys) out << ' ' << k.x << ' ' << k.y;
        out << '\n';
    }
    return static_cast<bool>(out);
}

inline bool loadRig2D(const std::filesystem::path& path, Rig2D& outRig) {
    std::ifstream in(path);
    if (!in) return false;
    std::string header;
    if (!std::getline(in, header) || header != "CAFRIG2D1") return false;

    Rig2D rig;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "bone") {
            Bone2D bone;
            std::string name;
            ss >> name >> bone.parentIndex >> bone.restPosition.x >> bone.restPosition.y
               >> bone.restRotation >> bone.restScale.x >> bone.restScale.y;
            bone.name = name.c_str();
            rig.skeleton.bones.push_back(bone);
        } else if (tag == "vert") {
            Vertex2D v;
            u32 count = 0;
            ss >> v.position.x >> v.position.y >> v.uv.x >> v.uv.y >> count;
            for (u32 i = 0; i < count; ++i) {
                BoneInfluence2D inf;
                ss >> inf.bone >> inf.weight;
                v.influences.push_back(inf);
            }
            rig.vertices.push_back(v);
        } else if (tag == "tri") {
            u32 a = 0, b = 0, c = 0;
            ss >> a >> b >> c;
            rig.indices.insert(rig.indices.end(), {a, b, c});
        } else if (tag == "param") {
            Parameter2D p;
            std::string name;
            ss >> name >> p.minValue >> p.defaultValue >> p.maxValue;
            p.name = name.c_str();
            rig.parameters.push_back(p);
        } else if (tag == "bind") {
            ParamBinding2D b;
            u32 channel = 0;
            u32 count = 0;
            ss >> b.parameter >> b.bone >> channel >> count;
            b.channel = static_cast<BoneChannel2D>(channel);
            for (u32 i = 0; i < count; ++i) {
                CurveKey2D k;
                ss >> k.x >> k.y;
                b.keys.push_back(k);
            }
            rig.bindings.push_back(std::move(b));
        }
    }

    // Reject files that reference bones, vertices or parameters outside the rig.
    for (const Vertex2D& v : rig.vertices) {
        for (const BoneInfluence2D& inf : v.influences) {
            if (inf.bone >= rig.skeleton.boneCount()) return false;
        }
    }
    for (u32 index : rig.indices) {
        if (index >= rig.vertices.size()) return false;
    }
    for (const ParamBinding2D& b : rig.bindings) {
        if (b.parameter >= rig.parameters.size() || b.bone >= rig.skeleton.boneCount()) return false;
    }
    outRig = std::move(rig);
    return true;
}

inline bool saveParamClip2D(const std::filesystem::path& path, const ParamClip2D& clip, const Rig2D& rig) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    if (!out) return false;
    out << std::setprecision(9);
    out << "CAFPCLIP1\n";
    out << "name " << clip.name.cStr() << '\n';
    out << "loop " << (clip.loop ? 1 : 0) << '\n';
    out << "duration " << clip.duration << '\n';
    for (const ParamTrack2D& track : clip.tracks) {
        if (track.parameter >= rig.parameters.size()) continue;
        out << "track " << rig.parameters[track.parameter].name.cStr() << ' ' << track.keys.size();
        for (const CurveKey2D& k : track.keys) out << ' ' << k.x << ' ' << k.y;
        out << '\n';
    }
    return static_cast<bool>(out);
}

/// Resolves track names against `rig`. Tracks for unknown parameters are dropped.
inline bool loadParamClip2D(const std::filesystem::path& path, ParamClip2D& outClip, const Rig2D& rig) {
    std::ifstream in(path);
    if (!in) return false;
    std::string header;
    if (!std::getline(in, header) || header != "CAFPCLIP1") return false;

    ParamClip2D clip;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "name") {
            std::getline(ss, line);
            if (!line.empty() && line[0] == ' ') line.erase(0, 1);
            clip.name = line.c_str();
        } else if (tag == "loop") {
            int loop = 1;
            ss >> loop;
            clip.loop = loop != 0;
        } else if (tag == "duration") {
            ss >> clip.duration;
        } else if (tag == "track") {
            std::string name;
            u32 count = 0;
            ss >> name >> count;
            const i32 index = rig.findParameter(name.c_str());
            ParamTrack2D track;
            track.parameter = index >= 0 ? static_cast<u32>(index) : 0u;
            for (u32 i = 0; i < count; ++i) {
                CurveKey2D k;
                ss >> k.x >> k.y;
                track.keys.push_back(k);
            }
            if (index >= 0) clip.tracks.push_back(std::move(track));
        }
    }
    outClip = std::move(clip);
    return true;
}

}  // namespace Caffeine::Animation
