#include "animation/SkinLibrary.hpp"

#include "assets/MeshCache.hpp"
#include "assets/MeshLoader.hpp"
#include "ecs/ComponentQuery.hpp"
#include "math/Quat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace Caffeine::Animation {

void skinVertices(const Assets::Mesh3D& bind, const std::vector<Mat4>& bones,
                  std::vector<Assets::Vertex3D>& out);

namespace {

std::unordered_map<std::string, ImportedSkin> g_skins;
std::unordered_map<u32, std::vector<Assets::Vertex3D>> g_posed;
std::unordered_map<u32, std::vector<Vec3>> g_joints;
std::unordered_map<u32, std::vector<i32>> g_jointParents;
std::string g_projectRoot;

std::unordered_map<u32, u64> g_stamp;
std::unordered_map<u32, u64> g_generation;
u64 g_skinRevision = 1;
/// Animated bone locals before authored offsets. The gizmo builds bone frames from these.
std::unordered_map<u32, std::vector<Mat4>> g_animLocal;
u64 g_frame = 1;

void dropEntityPose(u32 id) {
    g_posed.erase(id);
    g_joints.erase(id);
    g_jointParents.erase(id);
    g_stamp.erase(id);
    g_generation.erase(id);
    g_animLocal.erase(id);
}

u32 floatBits(f32 value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct PoseHasher {
    u64 hash = 1469598103934665603ull;
    void mix(u32 bits) {
        hash ^= bits;
        hash *= 1099511628211ull;
    }
    void mixF(f32 value) { mix(floatBits(value)); }
};

u64 hashPoseInputs(const SkinnedPose& pose, const std::vector<PoseLayerMix>& mix, u8 mode) {
    PoseHasher h;
    h.mix(mode);
    h.mix(static_cast<u32>(pose.offsets.size()));
    for (const BonePose& offset : pose.offsets) {
        h.mixF(offset.rotation.x);
        h.mixF(offset.rotation.y);
        h.mixF(offset.rotation.z);
        h.mixF(offset.translation.x);
        h.mixF(offset.translation.y);
        h.mixF(offset.translation.z);
    }
    h.mix(static_cast<u32>(mix.size()));
    for (const PoseLayerMix& layer : mix) {
        h.mixF(layer.weight);
        h.mix(layer.mask | (layer.additive ? 0x80000000u : 0u));
        h.mix(static_cast<u32>(layer.samples.size()));
        for (const PoseSample& sample : layer.samples) {
            h.mix(static_cast<u32>(sample.clip));
            h.mixF(sample.time);
            h.mixF(sample.weight);
            h.mixF(sample.loopBlend);
            h.mix(sample.loop ? 1u : 0u);
        }
    }
    for (i32 bone : pose.removedBones) h.mix(static_cast<u32>(bone));
    for (int slot = 0; slot < static_cast<int>(HumanoidBone::Count); ++slot) h.mix(static_cast<u32>(pose.humanoid.bones[slot]));
    return h.hash;
}

bool poseCacheHit(u32 id, u64 hash) {
    const auto it = g_stamp.find(id);
    return it != g_stamp.end() && it->second == hash;
}

void storePoseCache(u32 id, u64 hash, bool hasPosedVertices) {
    g_stamp[id] = hash;
    if (hasPosedVertices) {
        u64& generation = g_generation[id];
        generation = generation == 0 ? 1 : generation + 1;
        ++g_skinRevision;
    } else {
        g_generation[id] = 0;
        ++g_skinRevision;
    }
}

std::string normalizedBone(const std::string& name) {
    const auto cut = name.find_last_of(":|");
    const std::string tail = cut == std::string::npos ? name : name.substr(cut + 1);
    std::string out;
    out.reserve(tail.size());
    for (unsigned char c : tail) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

struct Alias {
    HumanoidBone bone;
    const char* token;
};

constexpr Alias kAliases[] = {
    {HumanoidBone::LeftUpperArm, "leftupperarm"}, {HumanoidBone::RightUpperArm, "rightupperarm"},
    {HumanoidBone::LeftLowerArm, "leftlowerarm"}, {HumanoidBone::RightLowerArm, "rightlowerarm"},
    {HumanoidBone::LeftUpperArm, "lupperarm"},    {HumanoidBone::RightUpperArm, "rupperarm"},
    {HumanoidBone::LeftLowerArm, "lforearm"},     {HumanoidBone::RightLowerArm, "rforearm"},
    {HumanoidBone::LeftUpperArm, "leftarm"},      {HumanoidBone::RightUpperArm, "rightarm"},
    {HumanoidBone::LeftHand, "lefthand"},         {HumanoidBone::RightHand, "righthand"},
    {HumanoidBone::LeftHand, "lhand"},            {HumanoidBone::RightHand, "rhand"},
    {HumanoidBone::LeftUpperLeg, "leftupleg"},    {HumanoidBone::RightUpperLeg, "rightupleg"},
    {HumanoidBone::LeftUpperLeg, "leftthigh"},    {HumanoidBone::RightUpperLeg, "rightthigh"},
    {HumanoidBone::LeftUpperLeg, "lthigh"},       {HumanoidBone::RightUpperLeg, "rthigh"},
    {HumanoidBone::LeftLowerLeg, "leftleg"},      {HumanoidBone::RightLowerLeg, "rightleg"},
    {HumanoidBone::LeftLowerLeg, "lcalf"},        {HumanoidBone::RightLowerLeg, "rcalf"},
    {HumanoidBone::LeftFoot, "leftfoot"},         {HumanoidBone::RightFoot, "rightfoot"},
    {HumanoidBone::LeftFoot, "lfoot"},            {HumanoidBone::RightFoot, "rfoot"},
    {HumanoidBone::Chest, "spine2"},              {HumanoidBone::Chest, "spine1"},
    {HumanoidBone::Chest, "chest"},               {HumanoidBone::Spine, "spine"},
    {HumanoidBone::Neck, "neck"},                 {HumanoidBone::Head, "head"},
    {HumanoidBone::Hips, "hips"},                 {HumanoidBone::Hips, "pelvis"},
};

bool tokenMatches(const std::string& bone, const char* token) {
    const std::string needle(token);
    if (bone == needle) return true;
    return bone.size() > needle.size() && bone.compare(bone.size() - needle.size(), needle.size(), needle) == 0;
}

Vec3 lerpPose(const Vec3& a, const Vec3& b, f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

void samplePoseKeys(SkinnedPose& pose, u32 boneCount, f32 time) {
    std::vector<BonePose> sampled(boneCount);
    for (u32 bone = 0; bone < boneCount; ++bone) {
        const BoneKeyframe* before = nullptr;
        const BoneKeyframe* after = nullptr;
        for (const BoneKeyframe& key : pose.keys) {
            if (key.bone != static_cast<i32>(bone)) continue;
            if (key.time <= time + 0.0001f) {
                if (!before || key.time >= before->time) before = &key;
            } else if (!after || key.time < after->time) {
                after = &key;
            }
        }
        if (before && after && after->time > before->time) {
            const f32 alpha = evaluateKeyCurve(
                before->curve, std::clamp((time - before->time) / (after->time - before->time), 0.0f, 1.0f));
            sampled[bone].rotation = lerpPose(before->rotation, after->rotation, alpha);
            sampled[bone].translation = lerpPose(before->translation, after->translation, alpha);
        } else if (before) {
            sampled[bone].rotation = before->rotation;
            sampled[bone].translation = before->translation;
        } else if (after) {
            sampled[bone].rotation = after->rotation;
            sampled[bone].translation = after->translation;
        }
    }
    pose.offsets = std::move(sampled);
}

struct LocalTRS {
    Vec3 t{};
    Quat r = Quat::identity();
    Vec3 s{1.0f, 1.0f, 1.0f};
};

Mat4 trsMatrix(const LocalTRS& trs) {
    return Mat4::translation(trs.t) * trs.r.toMatrix() * Mat4::scale(trs.s.x, trs.s.y, trs.s.z);
}

const std::vector<LocalTRS>& restPoseFor(const Skeleton& skeleton) {
    static std::unordered_map<const Skeleton*, std::vector<LocalTRS>> cache;
    std::vector<LocalTRS>& rest = cache[&skeleton];
    if (rest.size() == skeleton.boneCount()) return rest;
    rest.resize(skeleton.boneCount());
    for (u32 bone = 0; bone < skeleton.boneCount(); ++bone) {
        const Mat4& m = skeleton.bones[bone].localTransform;
        LocalTRS& out = rest[bone];
        out.t = Vec3(m(0, 3), m(1, 3), m(2, 3));
        out.s = Vec3(Vec3(m(0, 0), m(1, 0), m(2, 0)).length(), Vec3(m(0, 1), m(1, 1), m(2, 1)).length(),
                     Vec3(m(0, 2), m(1, 2), m(2, 2)).length());
        out.r = Quat::fromMatrix(m).normalized();
    }
    return rest;
}

void blendTRS(LocalTRS& into, const LocalTRS& sample, f32 weight) {
    if (weight <= 0.0f) return;
    if (weight >= 1.0f) {
        into = sample;
        return;
    }
    into.t = into.t + (sample.t - into.t) * weight;
    into.s = into.s + (sample.s - into.s) * weight;
    into.r = Quat::slerp(into.r, sample.r, weight).normalized();
}

f32 wrapClipTime(f32 time, f32 duration, bool loop) {
    if (duration <= 0.0f) return 0.0f;
    if (loop) {
        f32 t = std::fmod(time, duration);
        if (t < 0.0f) t += duration;
        return t;
    }
    return std::clamp(time, 0.0f, duration);
}

void sampleClipLocal(const SkeletalClip& clip, f32 time, const Skeleton& skeleton,
                     const std::vector<LocalTRS>& rest, std::vector<LocalTRS>& out) {
    const u32 boneCount = skeleton.boneCount();
    out.resize(boneCount);
    for (u32 bone = 0; bone < boneCount; ++bone) {
        const std::vector<SkeletalKeyframe>* keys = clip.channels.get(bone);
        if (!keys || keys->empty()) {
            out[bone] = rest[bone];
            continue;
        }
        const std::vector<SkeletalKeyframe>& kf = *keys;
        LocalTRS& trs = out[bone];
        if (time <= kf.front().time || kf.size() == 1) {
            trs = {kf.front().position, kf.front().rotation, kf.front().scale};
            continue;
        }
        if (time >= kf.back().time) {
            trs = {kf.back().position, kf.back().rotation, kf.back().scale};
            continue;
        }
        const auto upper = std::upper_bound(kf.begin(), kf.end(), time,
                                            [](f32 t, const SkeletalKeyframe& key) { return t < key.time; });
        const SkeletalKeyframe& b = *upper;
        const SkeletalKeyframe& a = *(upper - 1);
        const f32 span = b.time - a.time;
        const f32 alpha = span > 0.0f ? (time - a.time) / span : 0.0f;
        trs.t = a.position + (b.position - a.position) * alpha;
        trs.s = a.scale + (b.scale - a.scale) * alpha;
        trs.r = Quat::slerp(a.rotation, b.rotation, alpha).normalized();
    }
}

void samplePoseSample(const ImportedSkin& skin, const PoseSample& sample, const std::vector<LocalTRS>& rest,
                      std::vector<LocalTRS>& out, std::vector<LocalTRS>& scratch) {
    const SkeletalClip& clip = skin.clips[static_cast<size_t>(sample.clip)];
    const f32 duration = std::max(clip.duration, 0.0f);
    const f32 t = wrapClipTime(sample.time, duration, sample.loop);
    sampleClipLocal(clip, t, skin.skeleton, rest, out);
    const f32 window = std::min(sample.loopBlend, duration * 0.5f);
    if (sample.loop && window > 0.0f && t > duration - window) {
        sampleClipLocal(clip, 0.0f, skin.skeleton, rest, scratch);
        const f32 w = (t - (duration - window)) / window;
        for (size_t bone = 0; bone < out.size(); ++bone) blendTRS(out[bone], scratch[bone], w);
    }
}

/// Body region of each bone: the nearest mapped humanoid bone above it decides.
void bodyPartsFor(const ImportedSkin& skin, const HumanoidRig& rig, std::vector<u32>& parts) {
    const u32 boneCount = skin.skeleton.boneCount();
    parts.assign(boneCount, static_cast<u32>(BodyMask::All));
    if (!rig.matched) return;
    std::vector<u32> direct(boneCount, 0u);
    auto setPart = [&](HumanoidBone slot, BodyMask part) {
        const i32 bone = rig.bones[static_cast<int>(slot)];
        if (bone >= 0 && bone < static_cast<i32>(boneCount)) direct[static_cast<size_t>(bone)] = static_cast<u32>(part);
    };
    setPart(HumanoidBone::Hips, BodyMask::Root);
    setPart(HumanoidBone::Spine, BodyMask::Body);
    setPart(HumanoidBone::Chest, BodyMask::Body);
    setPart(HumanoidBone::Neck, BodyMask::Head);
    setPart(HumanoidBone::Head, BodyMask::Head);
    setPart(HumanoidBone::LeftUpperArm, BodyMask::LeftArm);
    setPart(HumanoidBone::LeftLowerArm, BodyMask::LeftArm);
    setPart(HumanoidBone::LeftHand, BodyMask::LeftArm);
    setPart(HumanoidBone::RightUpperArm, BodyMask::RightArm);
    setPart(HumanoidBone::RightLowerArm, BodyMask::RightArm);
    setPart(HumanoidBone::RightHand, BodyMask::RightArm);
    setPart(HumanoidBone::LeftUpperLeg, BodyMask::LeftLeg);
    setPart(HumanoidBone::LeftLowerLeg, BodyMask::LeftLeg);
    setPart(HumanoidBone::LeftFoot, BodyMask::LeftLeg);
    setPart(HumanoidBone::RightUpperLeg, BodyMask::RightLeg);
    setPart(HumanoidBone::RightLowerLeg, BodyMask::RightLeg);
    setPart(HumanoidBone::RightFoot, BodyMask::RightLeg);
    for (u32 bone = 0; bone < boneCount; ++bone) {
        u32 part = static_cast<u32>(BodyMask::Root);
        i32 walk = static_cast<i32>(bone);
        for (u32 guard = 0; walk >= 0 && walk < static_cast<i32>(boneCount) && guard < boneCount; ++guard) {
            if (direct[static_cast<size_t>(walk)] != 0u) {
                part = direct[static_cast<size_t>(walk)];
                break;
            }
            walk = skin.skeleton.bones[static_cast<size_t>(walk)].parentIndex;
        }
        parts[bone] = part;
    }
}

void evaluateMix(const ImportedSkin& skin, const SkinnedPose& pose, const std::vector<PoseLayerMix>& mix,
                 std::vector<LocalTRS>& result) {
    const std::vector<LocalTRS>& rest = restPoseFor(skin.skeleton);
    result = rest;
    std::vector<LocalTRS> layerPose;
    std::vector<LocalTRS> sampled;
    std::vector<LocalTRS> scratch;
    std::vector<u32> parts;
    const i32 clipCount = static_cast<i32>(skin.clips.size());
    for (size_t layerIndex = 0; layerIndex < mix.size(); ++layerIndex) {
        const PoseLayerMix& layer = mix[layerIndex];
        if (layer.weight <= 0.0f) continue;
        bool any = false;
        layerPose = rest;
        for (const PoseSample& sample : layer.samples) {
            if (sample.clip < 0 || sample.clip >= clipCount || sample.weight <= 0.0f) continue;
            samplePoseSample(skin, sample, rest, sampled, scratch);
            for (size_t bone = 0; bone < layerPose.size(); ++bone) blendTRS(layerPose[bone], sampled[bone], sample.weight);
            any = true;
        }
        if (!any) continue;
        const bool masked = (layer.mask & static_cast<u32>(BodyMask::All)) != static_cast<u32>(BodyMask::All);
        if (masked && parts.empty()) bodyPartsFor(skin, pose.humanoid, parts);
        for (size_t bone = 0; bone < result.size(); ++bone) {
            const f32 w = (masked && (parts[bone] & layer.mask) == 0u) ? 0.0f : layer.weight;
            if (w <= 0.0f) continue;
            if (layer.additive) {
                const Quat delta = (rest[bone].r.conjugate() * layerPose[bone].r).normalized();
                result[bone].r = (result[bone].r * Quat::slerp(Quat::identity(), delta, std::min(w, 1.0f))).normalized();
                result[bone].t = result[bone].t + (layerPose[bone].t - rest[bone].t) * w;
            } else {
                blendTRS(result[bone], layerPose[bone], w);
            }
        }
    }
}

template <typename Fn>
void parallelChunks(size_t count, Fn&& fn) {
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const size_t workers = count < 24000 ? 1 : std::min<size_t>(std::min(hardware, 12u), count / 12000);
    if (workers <= 1) {
        fn(size_t(0), count);
        return;
    }
    const size_t chunk = (count + workers - 1) / workers;
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (size_t w = 1; w < workers; ++w) {
        const size_t begin = w * chunk;
        const size_t end = std::min(count, begin + chunk);
        if (begin >= end) break;
        threads.emplace_back([&fn, begin, end]() { fn(begin, end); });
    }
    fn(size_t(0), std::min(count, chunk));
    for (std::thread& thread : threads) thread.join();
}

void skinFromLocals(const Skeleton& skeleton, const std::vector<Mat4>& local, const Assets::Mesh3D& mesh,
                    std::vector<Assets::Vertex3D>& posed, std::vector<Vec3>& joints) {
    const u32 boneCount = skeleton.boneCount();
    std::vector<Mat4> world(boneCount, Mat4::identity());
    std::vector<u8> resolved(boneCount, 0);
    for (u32 pass = 0; pass < boneCount; ++pass) {
        bool progressed = false;
        for (u32 bone = 0; bone < boneCount; ++bone) {
            if (resolved[bone]) continue;
            const i32 parent = skeleton.bones[bone].parentIndex;
            if (parent >= 0 && (parent >= static_cast<i32>(boneCount) || !resolved[static_cast<u32>(parent)])) continue;
            world[bone] = parent >= 0 ? world[static_cast<u32>(parent)] * local[bone] : local[bone];
            resolved[bone] = 1;
            progressed = true;
        }
        if (!progressed) break;
    }
    std::vector<Mat4> skin(boneCount, Mat4::identity());
    for (u32 bone = 0; bone < boneCount; ++bone) {
        skin[bone] = skeleton.skinSpace * world[bone] * skeleton.bones[bone].bindPoseInverse;
    }
    skinVertices(mesh, skin, posed);
    joints.resize(boneCount);
    for (u32 bone = 0; bone < boneCount; ++bone) {
        joints[bone] = skeleton.skinSpace.transformPoint(
            Vec3(world[bone](0, 3), world[bone](1, 3), world[bone](2, 3)));
    }
}

void fillBindJoints(const Skeleton& skeleton, std::vector<Vec3>& joints) {
    const u32 boneCount = skeleton.boneCount();
    std::vector<Mat4> world(boneCount, Mat4::identity());
    std::vector<u8> resolved(boneCount, 0);
    for (u32 pass = 0; pass < boneCount; ++pass) {
        bool progressed = false;
        for (u32 bone = 0; bone < boneCount; ++bone) {
            if (resolved[bone]) continue;
            const i32 parent = skeleton.bones[bone].parentIndex;
            if (parent >= 0 && (parent >= static_cast<i32>(boneCount) || !resolved[static_cast<u32>(parent)])) continue;
            world[bone] = parent >= 0
                ? world[static_cast<u32>(parent)] * skeleton.bones[bone].localTransform
                : skeleton.bones[bone].localTransform;
            resolved[bone] = 1;
            progressed = true;
        }
        if (!progressed) break;
    }
    joints.resize(boneCount);
    for (u32 bone = 0; bone < boneCount; ++bone) {
        joints[bone] = skeleton.skinSpace.transformPoint(
            Vec3(world[bone](0, 3), world[bone](1, 3), world[bone](2, 3)));
    }
}

bool nameHasToken(const std::string& bone, const char* token) {
    const std::string needle(token);
    return bone.find(needle) != std::string::npos;
}

}  // namespace

bool isFingerBoneName(const std::string& name) {
    const std::string bone = normalizedBone(name);
    if (bone.empty()) return false;
    const char* tokens[] = {"thumb", "index", "middle", "ring", "pinky", "pinkie", "little",
                            "finger", "digit", "pollex", "forefinger"};
    for (const char* token : tokens) {
        if (nameHasToken(bone, token)) return true;
    }
    return false;
}

bool poseOmitsBone(const SkinnedPose& pose, i32 bone) {
    return std::find(pose.removedBones.begin(), pose.removedBones.end(), bone) != pose.removedBones.end();
}

bool isFingerBone(const SkinnedPose& pose, const ImportedSkin& skin, i32 bone) {
    if (bone < 0) return false;
    for (int slot = kHumanoidBodyCount; slot < static_cast<int>(HumanoidBone::Count); ++slot) {
        if (pose.humanoid.bones[slot] == bone) return true;
    }
    const std::string name = bone < static_cast<i32>(skin.boneNames.size())
                                 ? skin.boneNames[static_cast<size_t>(bone)]
                                 : std::string();
    if (isFingerBoneName(name)) return true;
    const i32 leftHand = pose.humanoid.bones[static_cast<int>(HumanoidBone::LeftHand)];
    const i32 rightHand = pose.humanoid.bones[static_cast<int>(HumanoidBone::RightHand)];
    if (leftHand < 0 && rightHand < 0) return false;
    for (int slot = 0; slot < kHumanoidBodyCount; ++slot) {
        if (pose.humanoid.bones[slot] == bone) return false;
    }
    i32 walk = bone < static_cast<i32>(skin.skeleton.bones.size())
                   ? skin.skeleton.bones[static_cast<size_t>(bone)].parentIndex
                   : -1;
    for (int guard = 0; walk >= 0 && guard < 32; ++guard) {
        if (walk == leftHand || walk == rightHand) return true;
        if (walk >= static_cast<i32>(skin.skeleton.bones.size())) break;
        walk = skin.skeleton.bones[static_cast<size_t>(walk)].parentIndex;
    }
    return false;
}

namespace {

bool poseIsEdited(const SkinnedPose& pose) {
    if (!pose.keys.empty() || pose.sampleKeys) return true;
    for (const BonePose& offset : pose.offsets) {
        if (offset.rotation.lengthSquared() > 1.0e-8f || offset.translation.lengthSquared() > 1.0e-8f) return true;
    }
    return false;
}

}  // namespace

namespace {

std::string skinFileKey(const std::string& path) {
    std::string name = std::filesystem::path(path).filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

}  // namespace

void registerImportedSkin(const std::string& path, ImportedSkin skin) {
    if (path.empty() || skin.skeleton.bones.empty()) return;
    g_skins[path] = std::move(skin);
    const std::string file = skinFileKey(path);
    if (!file.empty() && file != path) g_skins[file] = g_skins[path];
    ++g_skinRevision;
}

bool synthesizeHumanoidSkin(const std::string& path, Assets::Mesh3D& mesh, bool replace) {
    if (path.empty() || mesh.vertices.empty()) return false;
    if (replace) {
        const std::string file = skinFileKey(path);
        g_skins.erase(path);
        if (!file.empty()) {
            for (auto it = g_skins.begin(); it != g_skins.end();) {
                if (it->first == path || skinFileKey(it->first) == file) it = g_skins.erase(it);
                else ++it;
            }
        }
    } else if (findImportedSkin(path)) {
        return true;
    }

    Vec3 minP = mesh.vertices[0].position;
    Vec3 maxP = minP;
    for (const auto& vertex : mesh.vertices) {
        minP.x = std::min(minP.x, vertex.position.x);
        minP.y = std::min(minP.y, vertex.position.y);
        minP.z = std::min(minP.z, vertex.position.z);
        maxP.x = std::max(maxP.x, vertex.position.x);
        maxP.y = std::max(maxP.y, vertex.position.y);
        maxP.z = std::max(maxP.z, vertex.position.z);
    }
    const f32 height = std::max(maxP.y - minP.y, 0.1f);
    const f32 halfW = std::max(0.5f * (maxP.x - minP.x), 0.12f * height);
    const Vec3 origin((minP.x + maxP.x) * 0.5f, minP.y, (minP.z + maxP.z) * 0.5f);

    struct Def {
        const char* name;
        i32 parent;
        f32 x;
        f32 y;
        f32 z;
    };
    // T-pose, Y-up. x is ±1 at the hands so arm span follows the mesh width.
    const Def defs[] = {
        {"Hips", -1, 0.00f, 0.52f, 0.00f},
        {"Spine", 0, 0.00f, 0.62f, 0.00f},
        {"Chest", 1, 0.00f, 0.72f, 0.00f},
        {"Neck", 2, 0.00f, 0.84f, 0.00f},
        {"Head", 3, 0.00f, 0.94f, 0.00f},
        {"LeftUpperArm", 2, -0.28f, 0.78f, 0.00f},
        {"LeftLowerArm", 5, -0.68f, 0.78f, 0.00f},
        {"LeftHand", 6, -1.00f, 0.78f, 0.00f},
        {"RightUpperArm", 2, 0.28f, 0.78f, 0.00f},
        {"RightLowerArm", 8, 0.68f, 0.78f, 0.00f},
        {"RightHand", 9, 1.00f, 0.78f, 0.00f},
        {"LeftUpperLeg", 0, -0.16f, 0.50f, 0.00f},
        {"LeftLowerLeg", 11, -0.16f, 0.26f, 0.00f},
        {"LeftFoot", 12, -0.16f, 0.03f, 0.06f},
        {"RightUpperLeg", 0, 0.16f, 0.50f, 0.00f},
        {"RightLowerLeg", 14, 0.16f, 0.26f, 0.00f},
        {"RightFoot", 15, 0.16f, 0.03f, 0.06f},
    };
    constexpr int kCount = static_cast<int>(sizeof(defs) / sizeof(defs[0]));

    ImportedSkin imported;
    imported.synthesized = true;
    imported.mesh = &mesh;
    imported.skeleton.skinSpace = Mat4::identity();
    imported.skeleton.bones.resize(static_cast<size_t>(kCount));
    imported.boneNames.resize(static_cast<size_t>(kCount));
    std::vector<Vec3> worldPos(static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        worldPos[static_cast<size_t>(i)] = Vec3(origin.x + defs[i].x * halfW,
                                                origin.y + defs[i].y * height,
                                                origin.z + defs[i].z * height);
        imported.boneNames[static_cast<size_t>(i)] = defs[i].name;
        imported.skeleton.bones[static_cast<size_t>(i)].name = defs[i].name;
        imported.skeleton.bones[static_cast<size_t>(i)].parentIndex = defs[i].parent;
        const Vec3 local = defs[i].parent < 0
                               ? worldPos[static_cast<size_t>(i)]
                               : worldPos[static_cast<size_t>(i)] - worldPos[static_cast<size_t>(defs[i].parent)];
        imported.skeleton.bones[static_cast<size_t>(i)].localTransform = Mat4::translation(local);
        imported.skeleton.bones[static_cast<size_t>(i)].bindPoseInverse =
            Mat4::translation(-worldPos[static_cast<size_t>(i)].x, -worldPos[static_cast<size_t>(i)].y,
                              -worldPos[static_cast<size_t>(i)].z);
    }

    mesh.skin.assign(mesh.vertices.size(), {});
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        const Vec3 p = mesh.vertices[v].position;
        f32 bestDist[4] = {1.0e12f, 1.0e12f, 1.0e12f, 1.0e12f};
        int bestBone[4] = {0, 0, 0, 0};
        for (int b = 0; b < kCount; ++b) {
            Vec3 a = worldPos[static_cast<size_t>(b)];
            Vec3 d = p - a;
            if (defs[b].parent >= 0) {
                const Vec3 parent = worldPos[static_cast<size_t>(defs[b].parent)];
                const Vec3 seg = a - parent;
                const f32 seg2 = seg.lengthSquared();
                if (seg2 > 1.0e-8f) {
                    const f32 t = std::clamp((p - parent).dot(seg) / seg2, 0.0f, 1.0f);
                    d = p - (parent + seg * t);
                }
            }
            const f32 dist2 = d.lengthSquared();
            for (int slot = 0; slot < 4; ++slot) {
                if (dist2 < bestDist[slot]) {
                    for (int shift = 3; shift > slot; --shift) {
                        bestDist[shift] = bestDist[shift - 1];
                        bestBone[shift] = bestBone[shift - 1];
                    }
                    bestDist[slot] = dist2;
                    bestBone[slot] = b;
                    break;
                }
            }
        }
        f32 sum = 0.0f;
        f32 weights[4] = {};
        for (int slot = 0; slot < 4; ++slot) {
            weights[slot] = 1.0f / (bestDist[slot] + 1.0e-4f);
            sum += weights[slot];
        }
        Assets::VertexSkin influence;
        if (sum <= 0.0f) {
            influence.joints[0] = 0;
            influence.weights[0] = 1.0f;
        } else {
            for (int slot = 0; slot < 4; ++slot) {
                influence.joints[slot] = static_cast<u16>(bestBone[slot]);
                influence.weights[slot] = weights[slot] / sum;
            }
        }
        mesh.skin[v] = influence;
    }

    registerImportedSkin(path, std::move(imported));
    return findImportedSkin(path) != nullptr;
}

const ImportedSkin* findImportedSkin(const std::string& path) {
    if (path.empty()) return nullptr;
    const auto exact = g_skins.find(path);
    if (exact != g_skins.end()) return &exact->second;
    const std::string file = skinFileKey(path);
    if (!file.empty()) {
        const auto byFile = g_skins.find(file);
        if (byFile != g_skins.end()) return &byFile->second;
    }
    const ImportedSkin* match = nullptr;
    for (const auto& entry : g_skins) {
        if (!file.empty() && skinFileKey(entry.first) == file) {
            match = &entry.second;
            continue;
        }
        if (entry.first.size() >= path.size() &&
            entry.first.compare(entry.first.size() - path.size(), path.size(), path) == 0) {
            match = &entry.second;
        }
    }
    return match;
}

const char* humanoidBoneName(HumanoidBone bone) {
    switch (bone) {
        case HumanoidBone::Hips: return "Hips";
        case HumanoidBone::Spine: return "Spine";
        case HumanoidBone::Chest: return "Chest";
        case HumanoidBone::Neck: return "Neck";
        case HumanoidBone::Head: return "Head";
        case HumanoidBone::LeftUpperArm: return "Left Upper Arm";
        case HumanoidBone::LeftLowerArm: return "Left Lower Arm";
        case HumanoidBone::LeftHand: return "Left Hand";
        case HumanoidBone::RightUpperArm: return "Right Upper Arm";
        case HumanoidBone::RightLowerArm: return "Right Lower Arm";
        case HumanoidBone::RightHand: return "Right Hand";
        case HumanoidBone::LeftUpperLeg: return "Left Upper Leg";
        case HumanoidBone::LeftLowerLeg: return "Left Lower Leg";
        case HumanoidBone::LeftFoot: return "Left Foot";
        case HumanoidBone::RightUpperLeg: return "Right Upper Leg";
        case HumanoidBone::RightLowerLeg: return "Right Lower Leg";
        case HumanoidBone::RightFoot: return "Right Foot";
        case HumanoidBone::LeftThumbProximal: return "Left Thumb Proximal";
        case HumanoidBone::LeftThumbIntermediate: return "Left Thumb Intermediate";
        case HumanoidBone::LeftThumbDistal: return "Left Thumb Distal";
        case HumanoidBone::LeftIndexProximal: return "Left Index Proximal";
        case HumanoidBone::LeftIndexIntermediate: return "Left Index Intermediate";
        case HumanoidBone::LeftIndexDistal: return "Left Index Distal";
        case HumanoidBone::LeftMiddleProximal: return "Left Middle Proximal";
        case HumanoidBone::LeftMiddleIntermediate: return "Left Middle Intermediate";
        case HumanoidBone::LeftMiddleDistal: return "Left Middle Distal";
        case HumanoidBone::LeftRingProximal: return "Left Ring Proximal";
        case HumanoidBone::LeftRingIntermediate: return "Left Ring Intermediate";
        case HumanoidBone::LeftRingDistal: return "Left Ring Distal";
        case HumanoidBone::LeftLittleProximal: return "Left Little Proximal";
        case HumanoidBone::LeftLittleIntermediate: return "Left Little Intermediate";
        case HumanoidBone::LeftLittleDistal: return "Left Little Distal";
        case HumanoidBone::RightThumbProximal: return "Right Thumb Proximal";
        case HumanoidBone::RightThumbIntermediate: return "Right Thumb Intermediate";
        case HumanoidBone::RightThumbDistal: return "Right Thumb Distal";
        case HumanoidBone::RightIndexProximal: return "Right Index Proximal";
        case HumanoidBone::RightIndexIntermediate: return "Right Index Intermediate";
        case HumanoidBone::RightIndexDistal: return "Right Index Distal";
        case HumanoidBone::RightMiddleProximal: return "Right Middle Proximal";
        case HumanoidBone::RightMiddleIntermediate: return "Right Middle Intermediate";
        case HumanoidBone::RightMiddleDistal: return "Right Middle Distal";
        case HumanoidBone::RightRingProximal: return "Right Ring Proximal";
        case HumanoidBone::RightRingIntermediate: return "Right Ring Intermediate";
        case HumanoidBone::RightRingDistal: return "Right Ring Distal";
        case HumanoidBone::RightLittleProximal: return "Right Little Proximal";
        case HumanoidBone::RightLittleIntermediate: return "Right Little Intermediate";
        case HumanoidBone::RightLittleDistal: return "Right Little Distal";
        case HumanoidBone::Count: break;
    }
    return "";
}

bool isHumanoidFingerSlot(HumanoidBone bone) {
    const int slot = static_cast<int>(bone);
    return slot >= kHumanoidBodyCount && slot < static_cast<int>(HumanoidBone::Count);
}

void assignHumanoidBone(HumanoidRig& rig, HumanoidBone slot, i32 bone) {
    const int index = static_cast<int>(slot);
    if (index < 0 || index >= static_cast<int>(HumanoidBone::Count)) return;
    for (int other = kHumanoidBodyCount; other < static_cast<int>(HumanoidBone::Count); ++other) {
        if (other != index && rig.bones[other] == bone) rig.bones[other] = -1;
    }
    rig.bones[index] = bone;
    rig.manual = true;
    refreshHumanoidMatched(rig);
}

namespace {

struct FingerAlias {
    HumanoidBone bone;
    const char* token;
};

constexpr FingerAlias kFingerAliases[] = {
    {HumanoidBone::LeftThumbProximal, "lefthandthumb1"}, {HumanoidBone::LeftThumbIntermediate, "lefthandthumb2"},
    {HumanoidBone::LeftThumbDistal, "lefthandthumb3"}, {HumanoidBone::RightThumbProximal, "righthandthumb1"},
    {HumanoidBone::RightThumbIntermediate, "righthandthumb2"}, {HumanoidBone::RightThumbDistal, "righthandthumb3"},
    {HumanoidBone::LeftIndexProximal, "lefthandindex1"}, {HumanoidBone::LeftIndexIntermediate, "lefthandindex2"},
    {HumanoidBone::LeftIndexDistal, "lefthandindex3"}, {HumanoidBone::RightIndexProximal, "righthandindex1"},
    {HumanoidBone::RightIndexIntermediate, "righthandindex2"}, {HumanoidBone::RightIndexDistal, "righthandindex3"},
    {HumanoidBone::LeftMiddleProximal, "lefthandmiddle1"}, {HumanoidBone::LeftMiddleIntermediate, "lefthandmiddle2"},
    {HumanoidBone::LeftMiddleDistal, "lefthandmiddle3"}, {HumanoidBone::RightMiddleProximal, "righthandmiddle1"},
    {HumanoidBone::RightMiddleIntermediate, "righthandmiddle2"}, {HumanoidBone::RightMiddleDistal, "righthandmiddle3"},
    {HumanoidBone::LeftRingProximal, "lefthandring1"}, {HumanoidBone::LeftRingIntermediate, "lefthandring2"},
    {HumanoidBone::LeftRingDistal, "lefthandring3"}, {HumanoidBone::RightRingProximal, "righthandring1"},
    {HumanoidBone::RightRingIntermediate, "righthandring2"}, {HumanoidBone::RightRingDistal, "righthandring3"},
    {HumanoidBone::LeftLittleProximal, "lefthandpinky1"}, {HumanoidBone::LeftLittleIntermediate, "lefthandpinky2"},
    {HumanoidBone::LeftLittleDistal, "lefthandpinky3"}, {HumanoidBone::RightLittleProximal, "righthandpinky1"},
    {HumanoidBone::RightLittleIntermediate, "righthandpinky2"}, {HumanoidBone::RightLittleDistal, "righthandpinky3"},
    {HumanoidBone::LeftThumbProximal, "lthumb1"}, {HumanoidBone::LeftThumbIntermediate, "lthumb2"},
    {HumanoidBone::LeftThumbDistal, "lthumb3"}, {HumanoidBone::RightThumbProximal, "rthumb1"},
    {HumanoidBone::RightThumbIntermediate, "rthumb2"}, {HumanoidBone::RightThumbDistal, "rthumb3"},
    {HumanoidBone::LeftThumbProximal, "finger0"}, {HumanoidBone::LeftThumbIntermediate, "finger01"},
    {HumanoidBone::LeftThumbDistal, "finger02"},
};

bool boneUsed(const HumanoidRig& rig, i32 bone) {
    if (bone < 0) return false;
    for (int slot = 0; slot < static_cast<int>(HumanoidBone::Count); ++slot) {
        if (rig.bones[slot] == bone) return true;
    }
    return false;
}

std::string boneLabel(const ImportedSkin& skin, i32 bone) {
    if (bone < 0) return {};
    if (bone < static_cast<i32>(skin.boneNames.size())) return normalizedBone(skin.boneNames[static_cast<size_t>(bone)]);
    if (bone < static_cast<i32>(skin.skeleton.bones.size())) {
        return normalizedBone(skin.skeleton.bones[static_cast<size_t>(bone)].name.cStr());
    }
    return {};
}

i32 firstChildOf(const ImportedSkin& skin, i32 parent, const HumanoidRig& rig) {
    i32 found = -1;
    for (i32 bone = 0; bone < static_cast<i32>(skin.skeleton.bones.size()); ++bone) {
        if (skin.skeleton.bones[static_cast<size_t>(bone)].parentIndex != parent) continue;
        if (boneUsed(rig, bone)) continue;
        if (found < 0) found = bone;
        else return -1;
    }
    return found;
}

void fillFingerChain(HumanoidRig& rig, const ImportedSkin& skin, i32 proximal, HumanoidBone firstSlot) {
    i32 bone = proximal;
    for (int joint = 0; joint < 3 && bone >= 0; ++joint) {
        const int slot = static_cast<int>(firstSlot) + joint;
        if (slot >= static_cast<int>(HumanoidBone::Count) || rig.bones[slot] >= 0) break;
        rig.bones[slot] = bone;
        bone = firstChildOf(skin, bone, rig);
    }
}

int fingerFamily(const std::string& name, bool left) {
    const char* families[] = {"thumb", "index", "middle", "ring", "pinky", "pinkie", "little"};
    const int map[] = {0, 1, 2, 3, 4, 4, 4};
    int found = -1;
    for (int i = 0; i < 7; ++i) {
        if (!nameHasToken(name, families[i])) continue;
        found = map[i];
        break;
    }
    if (found < 0 && nameHasToken(name, "finger")) {
        for (char digit = '0'; digit <= '4'; ++digit) {
            if (name.find(std::string("finger") + digit) != std::string::npos) {
                found = digit - '0';
                break;
            }
        }
    }
    if (found < 0) return -1;
    const bool nameLeft = nameHasToken(name, "left") || nameHasToken(name, "lhand") ||
                          (name.size() > 1 && name[0] == 'l' && (name[1] == 't' || name[1] == 'i' || name[1] == 'm' ||
                                                                 name[1] == 'r' || name[1] == 'p' || name[1] == 'f'));
    const bool nameRight = nameHasToken(name, "right") || nameHasToken(name, "rhand");
    if (left && nameRight) return -1;
    if (!left && nameLeft && !nameRight) return -1;
    return found;
}

void matchHandFingers(HumanoidRig& rig, const ImportedSkin& skin, HumanoidBone handSlot, HumanoidBone thumbSlot) {
    const i32 hand = rig.bones[static_cast<int>(handSlot)];
    if (hand < 0) return;
    const bool left = handSlot == HumanoidBone::LeftHand;
    struct Chain {
        i32 root = -1;
        int family = -1;
        f32 sort = 0.0f;
    };
    std::vector<Chain> chains;
    for (i32 bone = 0; bone < static_cast<i32>(skin.skeleton.bones.size()); ++bone) {
        if (skin.skeleton.bones[static_cast<size_t>(bone)].parentIndex != hand) continue;
        if (boneUsed(rig, bone)) continue;
        Chain chain;
        chain.root = bone;
        chain.family = fingerFamily(boneLabel(skin, bone), left);
        const Mat4& local = skin.skeleton.bones[static_cast<size_t>(bone)].localTransform;
        chain.sort = left ? -local(0, 3) : local(0, 3);
        chains.push_back(chain);
    }
    std::stable_sort(chains.begin(), chains.end(), [](const Chain& a, const Chain& b) {
        if (a.family >= 0 && b.family >= 0 && a.family != b.family) return a.family < b.family;
        if (a.family >= 0 && b.family < 0) return true;
        if (a.family < 0 && b.family >= 0) return false;
        return a.sort < b.sort;
    });
    bool usedFamily[5] = {};
    int nextOpen = 0;
    for (const Chain& chain : chains) {
        int family = chain.family;
        if (family < 0 || usedFamily[family]) {
            while (nextOpen < 5 && usedFamily[nextOpen]) ++nextOpen;
            if (nextOpen >= 5) break;
            family = nextOpen;
        }
        usedFamily[family] = true;
        fillFingerChain(rig, skin, chain.root,
                        static_cast<HumanoidBone>(static_cast<int>(thumbSlot) + family * 3));
    }
}

void matchNamedFingers(HumanoidRig& rig, const ImportedSkin& skin) {
    const u32 count = std::max(skin.skeleton.boneCount(), static_cast<u32>(skin.boneNames.size()));
    for (u32 i = 0; i < count; ++i) {
        const std::string bone = boneLabel(skin, static_cast<i32>(i));
        for (const FingerAlias& alias : kFingerAliases) {
            const int slot = static_cast<int>(alias.bone);
            if (rig.bones[slot] >= 0) continue;
            if (tokenMatches(bone, alias.token) || bone == alias.token) {
                rig.bones[slot] = static_cast<i32>(i);
                break;
            }
        }
    }
}

}  // namespace

HumanoidRig matchHumanoid(const ImportedSkin& skin) {
    HumanoidRig rig;
    const u32 count = std::max(skin.skeleton.boneCount(), static_cast<u32>(skin.boneNames.size()));
    for (u32 i = 0; i < count; ++i) {
        const std::string source = i < skin.boneNames.size() ? skin.boneNames[i] : skin.skeleton.bones[i].name.cStr();
        const std::string bone = normalizedBone(source);
        if (isFingerBoneName(source)) continue;
        for (const Alias& alias : kAliases) {
            const int slot = static_cast<int>(alias.bone);
            if (rig.bones[slot] >= 0) continue;
            if (tokenMatches(bone, alias.token)) {
                rig.bones[slot] = static_cast<i32>(i);
                break;
            }
        }
    }
    matchNamedFingers(rig, skin);
    matchHandFingers(rig, skin, HumanoidBone::LeftHand, HumanoidBone::LeftThumbProximal);
    matchHandFingers(rig, skin, HumanoidBone::RightHand, HumanoidBone::RightThumbProximal);
    refreshHumanoidMatched(rig);
    return rig;
}

void refreshHumanoidMatched(HumanoidRig& rig) {
    const bool arm = rig.bones[static_cast<int>(HumanoidBone::LeftUpperArm)] >= 0 ||
                     rig.bones[static_cast<int>(HumanoidBone::RightUpperArm)] >= 0;
    const bool leg = rig.bones[static_cast<int>(HumanoidBone::LeftUpperLeg)] >= 0 ||
                     rig.bones[static_cast<int>(HumanoidBone::RightUpperLeg)] >= 0;
    rig.matched = rig.bones[static_cast<int>(HumanoidBone::Hips)] >= 0 &&
                  rig.bones[static_cast<int>(HumanoidBone::Head)] >= 0 && arm && leg;
}

void skinVertices(const Assets::Mesh3D& bind, const std::vector<Mat4>& bones,
                  std::vector<Assets::Vertex3D>& out) {
    const size_t count = bind.vertices.size();
    if (out.size() != count) {
        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            out[i].texcoord = bind.vertices[i].texcoord;
            out[i].tangent = bind.vertices[i].tangent;
        }
    }
    if (bind.skin.size() != count || bones.empty()) {
        for (size_t i = 0; i < count; ++i) {
            out[i].position = bind.vertices[i].position;
            out[i].normal = bind.vertices[i].normal;
        }
        return;
    }

    struct Palette {
        f32 x0, y0, z0, x1, y1, z1, x2, y2, z2, tx, ty, tz;
    };
    std::vector<Palette> palette(bones.size());
    for (size_t bone = 0; bone < bones.size(); ++bone) {
        const Mat4& m = bones[bone];
        palette[bone] = {m(0, 0), m(1, 0), m(2, 0), m(0, 1), m(1, 1), m(2, 1),
                         m(0, 2), m(1, 2), m(2, 2), m(0, 3), m(1, 3), m(2, 3)};
    }

    const Assets::Vertex3D* source = bind.vertices.data();
    const Assets::VertexSkin* influences = bind.skin.data();
    Assets::Vertex3D* destination = out.data();
    const Palette* joints = palette.data();
    const u32 jointCount = static_cast<u32>(palette.size());

    parallelChunks(count, [&](size_t first, size_t last) {
    for (size_t i = first; i < last; ++i) {
        const Assets::VertexSkin& skin = influences[i];
        const Vec3 position = source[i].position;
        const Vec3 normal = source[i].normal;
        f32 px = 0.0f, py = 0.0f, pz = 0.0f;
        f32 nx = 0.0f, ny = 0.0f, nz = 0.0f;
        bool used = false;
        for (int influence = 0; influence < 4; ++influence) {
            const f32 weight = skin.weights[influence];
            if (weight <= 0.0f) continue;
            const u32 joint = skin.joints[influence];
            if (joint >= jointCount) continue;
            const Palette& m = joints[joint];
            px += (m.x0 * position.x + m.x1 * position.y + m.x2 * position.z + m.tx) * weight;
            py += (m.y0 * position.x + m.y1 * position.y + m.y2 * position.z + m.ty) * weight;
            pz += (m.z0 * position.x + m.z1 * position.y + m.z2 * position.z + m.tz) * weight;
            nx += (m.x0 * normal.x + m.x1 * normal.y + m.x2 * normal.z) * weight;
            ny += (m.y0 * normal.x + m.y1 * normal.y + m.y2 * normal.z) * weight;
            nz += (m.z0 * normal.x + m.z1 * normal.y + m.z2 * normal.z) * weight;
            used = true;
        }
        if (!used) {
            destination[i].position = position;
            destination[i].normal = normal;
            continue;
        }
        destination[i].position = Vec3(px, py, pz);
        const f32 lengthSquared = nx * nx + ny * ny + nz * nz;
        if (lengthSquared > 0.0001f) {
            const f32 inverse = 1.0f / std::sqrt(lengthSquared);
            destination[i].normal = Vec3(nx * inverse, ny * inverse, nz * inverse);
        }
    }
    });
}

SpriteSheetRect spriteSheetFrame(const SpriteSheet& sheet, u32 frame, f32 textureWidth, f32 textureHeight) {
    const u32 columns = std::max(1u, sheet.columns);
    const u32 rows = std::max(1u, sheet.rows);
    const u32 count = std::max(1u, sheet.frameCount == 0 ? columns * rows : sheet.frameCount);
    const u32 index = frame % count;
    const u32 column = index % columns;
    const u32 row = std::min(index / columns, rows - 1);
    SpriteSheetRect rect;
    rect.w = textureWidth / static_cast<f32>(columns);
    rect.h = textureHeight / static_cast<f32>(rows);
    rect.x = static_cast<f32>(column) * rect.w;
    rect.y = static_cast<f32>(row) * rect.h;
    return rect;
}

const std::vector<Assets::Vertex3D>* skinnedVerticesFor(u32 entityId) {
    const auto it = g_posed.find(entityId);
    if (it == g_posed.end() || it->second.empty()) return nullptr;
    return &it->second;
}

u64 skinnedGenerationFor(u32 entityId) {
    const auto it = g_generation.find(entityId);
    return it == g_generation.end() ? 0 : it->second;
}

u64 skinRevision() {
    return g_skinRevision;
}

const std::vector<Vec3>* jointPositionsFor(u32 entityId) {
    const auto it = g_joints.find(entityId);
    if (it == g_joints.end() || it->second.empty()) return nullptr;
    return &it->second;
}

const std::vector<i32>* jointParentsFor(u32 entityId) {
    const auto it = g_jointParents.find(entityId);
    if (it == g_jointParents.end() || it->second.empty()) return nullptr;
    return &it->second;
}

const char* keyInterpName(KeyInterp mode) {
    switch (mode) {
        case KeyInterp::Linear: return "Linear";
        case KeyInterp::Constant: return "Constant";
        case KeyInterp::EaseIn: return "Ease In";
        case KeyInterp::EaseOut: return "Ease Out";
        case KeyInterp::EaseInOut: return "Ease In Out";
        case KeyInterp::Custom: return "Custom Curve";
    }
    return "Linear";
}

KeyCurve keyCurvePreset(KeyInterp mode) {
    KeyCurve curve;
    curve.mode = mode;
    switch (mode) {
        case KeyInterp::EaseIn: curve.x1 = 0.42f; curve.y1 = 0.0f; curve.x2 = 1.0f; curve.y2 = 1.0f; break;
        case KeyInterp::EaseOut: curve.x1 = 0.0f; curve.y1 = 0.0f; curve.x2 = 0.58f; curve.y2 = 1.0f; break;
        case KeyInterp::EaseInOut: curve.x1 = 0.42f; curve.y1 = 0.0f; curve.x2 = 0.58f; curve.y2 = 1.0f; break;
        default: break;
    }
    return curve;
}

f32 evaluateKeyCurve(const KeyCurve& curve, f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (curve.mode) {
        case KeyInterp::Linear: return t;
        case KeyInterp::Constant: return t >= 1.0f ? 1.0f : 0.0f;
        default: break;
    }
    const KeyCurve shape = curve.mode == KeyInterp::Custom ? curve : keyCurvePreset(curve.mode);
    const f32 x1 = std::clamp(shape.x1, 0.0f, 1.0f);
    const f32 x2 = std::clamp(shape.x2, 0.0f, 1.0f);
    auto bezier = [](f32 a, f32 b, f32 s) {
        const f32 inv = 1.0f - s;
        return 3.0f * inv * inv * s * a + 3.0f * inv * s * s * b + s * s * s;
    };
    f32 lo = 0.0f;
    f32 hi = 1.0f;
    f32 s = t;
    for (int i = 0; i < 24; ++i) {
        s = 0.5f * (lo + hi);
        if (bezier(x1, x2, s) < t) lo = s;
        else hi = s;
    }
    return bezier(shape.y1, shape.y2, s);
}

bool boneEditFrame(const ImportedSkin& skin, const SkinnedPose& pose, i32 bone, Mat4& out, u32 entityId) {
    const Skeleton& skeleton = skin.skeleton;
    const u32 boneCount = skeleton.boneCount();
    if (bone < 0 || bone >= static_cast<i32>(boneCount)) return false;
    const auto animated = g_animLocal.find(entityId);
    const std::vector<Mat4>* animLocal =
        (animated != g_animLocal.end() && animated->second.size() == boneCount) ? &animated->second : nullptr;
    auto baseLocal = [&](size_t index) -> const Mat4& {
        return animLocal ? (*animLocal)[index] : skeleton.bones[index].localTransform;
    };
    auto withOffset = [&](size_t index) {
        const BonePose* offset = index < pose.offsets.size() ? &pose.offsets[index] : nullptr;
        if (!offset) return baseLocal(index);
        const Quat rotation = Quat::fromEuler(offset->rotation.x, offset->rotation.y, offset->rotation.z);
        return baseLocal(index) * Mat4::translation(offset->translation) * rotation.toMatrix();
    };
    std::vector<i32> chain;
    for (i32 walk = skeleton.bones[static_cast<size_t>(bone)].parentIndex;
         walk >= 0 && walk < static_cast<i32>(boneCount) && chain.size() < boneCount;
         walk = skeleton.bones[static_cast<size_t>(walk)].parentIndex) {
        chain.push_back(walk);
    }
    Mat4 parent = Mat4::identity();
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) parent = parent * withOffset(static_cast<size_t>(*it));
    Vec3 translation{};
    if (static_cast<size_t>(bone) < pose.offsets.size()) translation = pose.offsets[static_cast<size_t>(bone)].translation;
    out = skeleton.skinSpace * parent * baseLocal(static_cast<size_t>(bone)) * Mat4::translation(translation);
    return true;
}

i32 clipForAnimatorState(const ImportedSkin& skin, const char* stateName, const char* motionName) {
    const char* wanted = (motionName && motionName[0] != '\0') ? motionName : stateName;
    if (!wanted || wanted[0] == '\0') return -1;
    for (int clip = 0; clip < static_cast<int>(skin.clipNames.size()); ++clip) {
        if (skin.clipNames[static_cast<size_t>(clip)] == wanted) return clip;
    }
    return -1;
}

void setSkinProjectRoot(const std::string& projectRoot) {
    g_projectRoot = projectRoot;
}

SkinnedPose* ensurePoseForMesh(ECS::World& world, ECS::Entity entity, const std::string& projectRoot) {
    if (!entity.isValid()) return nullptr;
    setSkinProjectRoot(projectRoot);
    ECS::MeshFilterComponent* filter = world.get<ECS::MeshFilterComponent>(entity);
    SkinnedPose* pose = world.get<SkinnedPose>(entity);
    const char* path = nullptr;
    if (pose && pose->meshPath[0] != '\0') path = pose->meshPath;
    else if (filter && !filter->customMeshPath.empty()) path = filter->customMeshPath.c_str();
    if (!path || path[0] == '\0') return pose;

    Assets::Mesh3D* mesh = Assets::MeshCache::getInstance().getMesh(path, projectRoot);
    const std::string resolved = Assets::MeshCache::getInstance().getResolvedPath();
    if (!findImportedSkin(path) && mesh) {
        Assets::MeshLoader::ensureGltfSkin(resolved.empty() ? std::string(path) : resolved, mesh);
    }
    if (!findImportedSkin(path)) return pose;

    if (!pose) {
        SkinnedPose created;
        std::strncpy(created.meshPath, path, sizeof(created.meshPath) - 1);
        created.playing = false;
        created.time = 0.0f;
        created.editorHeld = true;
        world.add<SkinnedPose>(entity, std::move(created));
        pose = world.get<SkinnedPose>(entity);
    } else if (pose->meshPath[0] == '\0') {
        std::strncpy(pose->meshPath, path, sizeof(pose->meshPath) - 1);
        pose->loaded = false;
    }
    if (filter && filter->customMeshPath.empty() && pose && pose->meshPath[0] != '\0') {
        filter->primitive = ECS::MeshPrimitive::Custom;
        filter->customMeshPath = pose->meshPath;
    }
    return pose;
}

void tickSkinnedPoses(ECS::World& world, f32 dt) {
    ECS::ComponentQuery query;
    query.with<SkinnedPose>();
    std::vector<u32> live;
    world.forEach<SkinnedPose>(query, [&](ECS::Entity entity, SkinnedPose& pose) {
        live.push_back(entity.id());
        if (pose.meshPath[0] == '\0') {
            dropEntityPose(entity.id());
            return;
        }
        const ImportedSkin* skin = findImportedSkin(pose.meshPath);
        const Assets::Mesh3D* mesh = skin ? skin->mesh : nullptr;
        if (!mesh) mesh = Assets::MeshCache::getInstance().getMesh(pose.meshPath, g_projectRoot);
        if (!skin || !mesh) {
            dropEntityPose(entity.id());
            return;
        }
        if (!pose.editorHeld) {
            pose.playing = false;
            pose.time = 0.0f;
            pose.editorHeld = true;
        }
        if (!pose.loaded) {
            if (!pose.humanoid.manual) pose.humanoid = matchHumanoid(*skin);
            else refreshHumanoidMatched(pose.humanoid);
            pose.loaded = true;
            if (pose.clipIndex < 0) pose.clipIndex = 0;
        }
        auto rememberParents = [&]() {
            if (g_jointParents.find(entity.id()) != g_jointParents.end()) return;
            std::vector<i32> parents;
            parents.reserve(skin->skeleton.boneCount());
            for (const Bone& bone : skin->skeleton.bones) parents.push_back(bone.parentIndex);
            g_jointParents[entity.id()] = std::move(parents);
        };

        // Priority: the timeline's override, then the animator, then the inspector's single clip.
        std::vector<PoseLayerMix> mix;
        u8 mode = 0;
        f32 keyTime = pose.time;
        auto mixHasSamples = [](const std::vector<PoseLayerMix>& layers) {
            for (const PoseLayerMix& layer : layers) {
                if (!layer.samples.empty()) return true;
            }
            return false;
        };
        const bool timelineOwns = pose.timelineHold && mixHasSamples(pose.overrideMix);
        if (timelineOwns) {
            mix = pose.overrideMix;
            mode = 1;
            keyTime = pose.time;
        } else {
            const Animator* animator = world.get<Animator>(entity);
            if (animator) buildAnimatorMix(*animator, *skin, mix);
            if (!mix.empty()) {
                mode = 2;
                if (!mix.front().samples.empty()) {
                    pose.clipIndex = mix.front().samples.back().clip;
                    pose.time = mix.front().samples.back().time;
                }
            } else if (!skin->clips.empty()) {
                if (pose.clipIndex < 0 || pose.clipIndex >= static_cast<i32>(skin->clips.size())) pose.clipIndex = 0;
                const SkeletalClip& clip = skin->clips[static_cast<size_t>(pose.clipIndex)];
                if (pose.playing) {
                    pose.time += dt * pose.speed;
                    const f32 duration = std::max(clip.duration, 0.0f);
                    if (duration > 0.0f && pose.time >= duration) {
                        if (pose.loop && clip.loop) {
                            pose.time = std::fmod(pose.time, duration);
                        } else {
                            pose.time = duration;
                            pose.playing = false;
                        }
                    }
                }
                PoseLayerMix single;
                single.samples.push_back({pose.clipIndex, pose.time, 1.0f, pose.loop && clip.loop, 0.0f});
                mix.push_back(std::move(single));
                mode = 3;
            }
        }
        if (pose.sampleKeys && !pose.keys.empty()) samplePoseKeys(pose, skin->skeleton.boneCount(), keyTime);

        if (mix.empty() && !poseIsEdited(pose)) {
            const u64 hash = hashPoseInputs(pose, mix, 0);
            if (poseCacheHit(entity.id(), hash)) return;
            g_posed.erase(entity.id());
            g_animLocal.erase(entity.id());
            std::vector<Vec3> joints;
            fillBindJoints(skin->skeleton, joints);
            g_joints[entity.id()] = std::move(joints);
            rememberParents();
            storePoseCache(entity.id(), hash, false);
            return;
        }
        const u64 hash = hashPoseInputs(pose, mix, mode);
        if (poseCacheHit(entity.id(), hash)) return;

        std::vector<LocalTRS> locals;
        evaluateMix(*skin, pose, mix, locals);
        const u32 boneCount = skin->skeleton.boneCount();
        std::vector<Mat4>& animLocal = g_animLocal[entity.id()];
        animLocal.resize(boneCount);
        std::vector<Mat4> local(boneCount);
        for (u32 bone = 0; bone < boneCount; ++bone) {
            animLocal[bone] = trsMatrix(locals[bone]);
            const BonePose* offset = bone < pose.offsets.size() ? &pose.offsets[bone] : nullptr;
            if (offset && (offset->rotation.lengthSquared() > 1.0e-10f || offset->translation.lengthSquared() > 1.0e-10f)) {
                const Quat rotation = Quat::fromEuler(offset->rotation.x, offset->rotation.y, offset->rotation.z);
                local[bone] = animLocal[bone] * Mat4::translation(offset->translation) * rotation.toMatrix();
            } else {
                local[bone] = animLocal[bone];
            }
        }
        std::vector<Vec3> joints;
        skinFromLocals(skin->skeleton, local, *mesh, g_posed[entity.id()], joints);
        g_joints[entity.id()] = std::move(joints);
        rememberParents();
        storePoseCache(entity.id(), hash, true);
    });
    ++g_frame;

    for (auto it = g_posed.begin(); it != g_posed.end();) {
        const bool stillLive = std::find(live.begin(), live.end(), it->first) != live.end();
        if (!stillLive) it = g_posed.erase(it);
        else ++it;
    }
    for (auto it = g_joints.begin(); it != g_joints.end();) {
        if (std::find(live.begin(), live.end(), it->first) == live.end()) it = g_joints.erase(it);
        else ++it;
    }
    for (auto it = g_jointParents.begin(); it != g_jointParents.end();) {
        if (std::find(live.begin(), live.end(), it->first) == live.end()) it = g_jointParents.erase(it);
        else ++it;
    }
    for (auto it = g_stamp.begin(); it != g_stamp.end();) {
        if (std::find(live.begin(), live.end(), it->first) == live.end()) it = g_stamp.erase(it);
        else ++it;
    }
    for (auto it = g_generation.begin(); it != g_generation.end();) {
        if (std::find(live.begin(), live.end(), it->first) == live.end()) it = g_generation.erase(it);
        else ++it;
    }
    for (auto it = g_animLocal.begin(); it != g_animLocal.end();) {
        if (std::find(live.begin(), live.end(), it->first) == live.end()) it = g_animLocal.erase(it);
        else ++it;
    }
}

u64 skinFrameCounter() {
    return g_frame;
}

f32 effectiveLayerWeight(const Animator& animator, const AnimatorLayer& layer) {
    if (layer.muted) return 0.0f;
    f32 weight = std::clamp(layer.weight, 0.0f, 1.0f);
    FixedString<32> parent = layer.parentGroup;
    for (int depth = 0; depth < 8 && !parent.empty(); ++depth) {
        const AnimatorLayer* group = nullptr;
        for (const AnimatorLayer& candidate : animator.layers) {
            if (candidate.isGroup && candidate.name == parent) {
                group = &candidate;
                break;
            }
        }
        if (!group) break;
        if (group->muted) return 0.0f;
        weight *= std::clamp(group->weight, 0.0f, 1.0f);
        parent = group->parentGroup;
    }
    return weight;
}

namespace {

void appendMachineSamples(const AnimatorStateMachine& sm, const ImportedSkin& skin, PoseLayerMix& layer) {
    auto clipOf = [&](const FixedString<32>& name) -> i32 {
        if (name.empty()) return -1;
        const AnimationState* state = sm.states.get(name);
        return clipForAnimatorState(skin, name.cStr(), state ? state->motion.cStr() : nullptr);
    };
    auto loops = [&](i32 clip) { return clip >= 0 && skin.clips[static_cast<size_t>(clip)].loop; };
    const i32 current = clipOf(sm.currentState);
    if (current < 0) return;
    if (sm.fading()) {
        const i32 from = clipOf(sm.fadeFromState);
        if (from >= 0) {
            layer.samples.push_back({from, sm.fadeFromTime, 1.0f, loops(from), 0.0f});
            const f32 alpha = evaluateKeyCurve(sm.fadeCurve, sm.fadeElapsed / sm.fadeDuration);
            layer.samples.push_back({current, sm.timeInState, std::clamp(alpha, 0.0f, 1.0f), loops(current), 0.0f});
            return;
        }
    }
    layer.samples.push_back({current, sm.timeInState, 1.0f, loops(current), 0.0f});
}

}  // namespace

void buildAnimatorMix(const Animator& animator, const ImportedSkin& skin, std::vector<PoseLayerMix>& out) {
    out.clear();
    bool active = !animator.currentState.empty();
    for (const AnimatorLayer& layer : animator.layers) active = active || (!layer.isGroup && !layer.currentState.empty());
    if (!active || skin.clips.empty()) return;
    PoseLayerMix base;
    appendMachineSamples(animator, skin, base);
    out.push_back(std::move(base));
    for (const AnimatorLayer& layer : animator.layers) {
        if (layer.isGroup) continue;
        PoseLayerMix mix;
        mix.weight = effectiveLayerWeight(animator, layer);
        mix.additive = layer.blend == LayerBlendMode::Additive;
        mix.mask = layer.mask;
        if (mix.weight <= 0.0f) continue;
        appendMachineSamples(layer, skin, mix);
        if (!mix.samples.empty()) out.push_back(std::move(mix));
    }
    if (out.size() == 1 && out.front().samples.empty()) out.clear();
}

namespace {

f32 stripClipDuration(const ClipStrip& strip, const ImportedSkin* skin) {
    if (!skin || strip.clip < 0 || strip.clip >= static_cast<i32>(skin->clips.size())) return 0.0f;
    return std::max(skin->clips[static_cast<size_t>(strip.clip)].duration, 0.0f);
}

f32 stripRange(const ClipStrip& strip, const ImportedSkin* skin) {
    const f32 duration = stripClipDuration(strip, skin);
    const f32 out = strip.clipOut < 0.0f ? duration : std::min(strip.clipOut, duration);
    return std::max(out - std::clamp(strip.clipIn, 0.0f, duration), 0.001f);
}

}  // namespace

f32 clipStripLength(const ClipStrip& strip, const ImportedSkin* skin) {
    return stripRange(strip, skin) / std::max(strip.speed, 0.01f) * static_cast<f32>(std::max(strip.repeat, 1));
}

bool clipStripSourceTime(const ClipStrip& strip, const ImportedSkin* skin, f32 time, f32& outSource) {
    const f32 length = clipStripLength(strip, skin);
    if (time < strip.start || time > strip.start + length) return false;
    const f32 range = stripRange(strip, skin);
    const f32 local = (time - strip.start) * std::max(strip.speed, 0.01f);
    f32 within = local >= range * static_cast<f32>(std::max(strip.repeat, 1)) - 1.0e-5f ? range : std::fmod(local, range);
    if (strip.reverse) within = range - within;
    outSource = std::clamp(strip.clipIn, 0.0f, stripClipDuration(strip, skin)) + within;
    return true;
}

f32 clipStripFade(const ClipStrip& strip, const ImportedSkin* skin, f32 time, f32 autoFadeIn) {
    const f32 length = clipStripLength(strip, skin);
    if (strip.muted || time < strip.start || time > strip.start + length) return 0.0f;
    f32 weight = std::clamp(strip.weight, 0.0f, 1.0f);
    const f32 fadeIn = std::min(std::max(strip.fadeIn, autoFadeIn), length);
    if (fadeIn > 0.0f && time - strip.start < fadeIn) {
        weight *= evaluateKeyCurve(strip.fadeInCurve, (time - strip.start) / fadeIn);
    }
    const f32 fadeOut = std::min(strip.fadeOut, length);
    const f32 end = strip.start + length;
    if (fadeOut > 0.0f && end - time < fadeOut) {
        weight *= 1.0f - evaluateKeyCurve(strip.fadeOutCurve, 1.0f - (end - time) / fadeOut);
    }
    return std::clamp(weight, 0.0f, 1.0f);
}

f32 clipStripOverlapIn(const SkinnedPose& pose, size_t index, const ImportedSkin* skin) {
    if (index >= pose.strips.size()) return 0.0f;
    const ClipStrip& strip = pose.strips[index];
    f32 overlap = 0.0f;
    for (size_t i = 0; i < pose.strips.size(); ++i) {
        const ClipStrip& other = pose.strips[i];
        if (i == index || other.track != strip.track || other.muted) continue;
        if (other.start > strip.start || (other.start == strip.start && i > index)) continue;
        overlap = std::max(overlap, other.start + clipStripLength(other, skin) - strip.start);
    }
    return std::clamp(overlap, 0.0f, clipStripLength(strip, skin));
}

void buildStripMix(const SkinnedPose& pose, const ImportedSkin* skin, f32 time, std::vector<PoseLayerMix>& out) {
    out.clear();
    if (!skin || pose.strips.empty()) return;
    std::vector<size_t> order(pose.strips.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const ClipStrip& sa = pose.strips[a];
        const ClipStrip& sb = pose.strips[b];
        return sa.track != sb.track ? sa.track < sb.track : sa.start < sb.start;
    });
    PoseLayerMix layer;
    for (size_t index : order) {
        const ClipStrip& strip = pose.strips[index];
        f32 source = 0.0f;
        if (!clipStripSourceTime(strip, skin, time, source)) continue;
        const f32 weight = clipStripFade(strip, skin, time, clipStripOverlapIn(pose, index, skin));
        if (weight <= 0.0f) continue;
        layer.samples.push_back({strip.clip, source, weight, false, strip.loopBlend});
        const f32 range = stripRange(strip, skin);
        const f32 window = std::min(strip.loopBlend, range * 0.5f);
        const f32 within = source - std::clamp(strip.clipIn, 0.0f, stripClipDuration(strip, skin));
        if (window > 0.0f && within > range - window) {
            const f32 blend = (within - (range - window)) / window;
            layer.samples.push_back({strip.clip, strip.clipIn, weight * blend, false, 0.0f});
        }
    }
    if (layer.samples.empty()) {
        i32 hold = -1;
        f32 holdEnd = -1.0f;
        for (size_t index : order) {
            const ClipStrip& strip = pose.strips[index];
            if (strip.muted) continue;
            const f32 end = strip.start + clipStripLength(strip, skin);
            if (time >= strip.start && end >= holdEnd) {
                hold = static_cast<i32>(index);
                holdEnd = end;
            } else if (hold < 0 && time < strip.start && (holdEnd < 0.0f || strip.start < holdEnd)) {
                hold = static_cast<i32>(index);
                holdEnd = strip.start;
            }
        }
        if (hold >= 0) {
            const ClipStrip& strip = pose.strips[static_cast<size_t>(hold)];
            f32 source = 0.0f;
            if (!clipStripSourceTime(strip, skin, std::clamp(time, strip.start, strip.start + clipStripLength(strip, skin)),
                                     source)) {
                source = time < strip.start ? strip.clipIn : strip.clipOut < 0.0f
                             ? stripClipDuration(strip, skin)
                             : strip.clipOut;
            }
            layer.samples.push_back({strip.clip, source, std::clamp(strip.weight, 0.0f, 1.0f), false, 0.0f});
        }
    }
    if (!layer.samples.empty()) out.push_back(std::move(layer));
}

f32 stripsDuration(const SkinnedPose& pose, const ImportedSkin* skin) {
    f32 end = 0.0f;
    for (const ClipStrip& strip : pose.strips) end = std::max(end, strip.start + clipStripLength(strip, skin));
    return end;
}

void ensureDefaultStrips(SkinnedPose& pose, const ImportedSkin* skin) {
    if (pose.stripsInitialized || !skin) return;
    pose.stripsInitialized = true;
    if (!pose.strips.empty() || skin->clips.empty()) return;
    i32 clip = pose.clipIndex;
    if (clip < 0 || clip >= static_cast<i32>(skin->clips.size())) clip = 0;
    ClipStrip strip;
    strip.clip = clip;
    const f32 duration = stripClipDuration(strip, skin);
    strip.loopBlend = std::min(0.12f, duration * 0.12f);
    pose.strips.push_back(strip);
}

void bakeClipToKeys(SkinnedPose& pose, const ImportedSkin& skin, i32 clip, i32 bone) {
    if (clip < 0 || clip >= static_cast<i32>(skin.clips.size())) return;
    const SkeletalClip& source = skin.clips[static_cast<size_t>(clip)];
    pose.keys.erase(std::remove_if(pose.keys.begin(), pose.keys.end(),
                                   [&](const BoneKeyframe& key) {
                                       return bone < 0 ? !isFingerBone(pose, skin, key.bone) : key.bone == bone;
                                   }),
                    pose.keys.end());
    for (const auto& pair : source.channels) {
        const i32 channel = static_cast<i32>(pair.key);
        if (bone >= 0 && channel != bone) continue;
        if (poseOmitsBone(pose, channel)) continue;
        if (bone < 0 && isFingerBone(pose, skin, channel)) continue;
        for (const SkeletalKeyframe& frame : pair.value) {
            BoneKeyframe key;
            key.time = frame.time;
            key.bone = channel;
            key.rotation = frame.rotation.toEuler();
            key.translation = frame.position;
            pose.keys.push_back(key);
        }
    }
    pose.sampleKeys = !pose.keys.empty();
}

bool deleteKeysNearTime(SkinnedPose& pose, i32 bone, f32 time, f32 window) {
    bool removed = false;
    auto near = [&](f32 keyTime) { return std::fabs(keyTime - time) <= window; };
    if (bone < 0) {
        const auto size = pose.objectKeys.size();
        pose.objectKeys.erase(std::remove_if(pose.objectKeys.begin(), pose.objectKeys.end(),
                                             [&](const ObjectKeyframe& key) { return near(key.time); }),
                              pose.objectKeys.end());
        removed = pose.objectKeys.size() != size;
    } else {
        const auto size = pose.keys.size();
        pose.keys.erase(std::remove_if(pose.keys.begin(), pose.keys.end(),
                                       [&](const BoneKeyframe& key) { return key.bone == bone && near(key.time); }),
                        pose.keys.end());
        removed = pose.keys.size() != size;
    }
    return removed;
}

namespace {

void writeCurve(std::ostream& out, const KeyCurve& curve) {
    out << static_cast<int>(curve.mode) << '\t' << curve.x1 << '\t' << curve.y1 << '\t' << curve.x2 << '\t' << curve.y2;
}

KeyCurve readCurve(const std::vector<std::string>& fields, size_t at) {
    KeyCurve curve;
    if (at + 4 >= fields.size()) return curve;
    const int mode = std::atoi(fields[at].c_str());
    curve.mode = static_cast<KeyInterp>(std::clamp(mode, 0, static_cast<int>(KeyInterp::Custom)));
    curve.x1 = std::strtof(fields[at + 1].c_str(), nullptr);
    curve.y1 = std::strtof(fields[at + 2].c_str(), nullptr);
    curve.x2 = std::strtof(fields[at + 3].c_str(), nullptr);
    curve.y2 = std::strtof(fields[at + 4].c_str(), nullptr);
    return curve;
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream stream(line);
    while (std::getline(stream, field, '\t')) fields.push_back(field);
    return fields;
}

}  // namespace

bool savePoseAnimation(const std::string& path, const std::string& name, const SkinnedPose& pose,
                       const ImportedSkin* skin) {
    std::error_code ec;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    if (!out) return false;
    f32 duration = std::max(stripsDuration(pose, skin), 0.0f);
    for (const BoneKeyframe& key : pose.keys) duration = std::max(duration, key.time);
    for (const ObjectKeyframe& key : pose.objectKeys) duration = std::max(duration, key.time);
    out << "CAFANIM1\n";
    out << "name " << name << '\n';
    out << "loop 1\n";
    out << "fps 30\n";
    out << "duration " << duration << '\n';
    out << "kind skeletal\n";
    out << "mesh " << pose.meshPath << '\n';
    auto clipName = [&](i32 clip) -> std::string {
        if (skin && clip >= 0 && clip < static_cast<i32>(skin->clipNames.size())) return skin->clipNames[static_cast<size_t>(clip)];
        return std::to_string(clip);
    };
    auto boneName = [&](i32 bone) -> std::string {
        if (skin && bone >= 0 && bone < static_cast<i32>(skin->boneNames.size())) return skin->boneNames[static_cast<size_t>(bone)];
        return std::string();
    };
    for (const ClipStrip& strip : pose.strips) {
        out << "strip\t" << strip.clip << '\t' << clipName(strip.clip) << '\t' << strip.track << '\t' << strip.start << '\t'
            << strip.clipIn << '\t' << strip.clipOut << '\t' << strip.speed << '\t' << strip.repeat << '\t' << strip.fadeIn
            << '\t' << strip.fadeOut << '\t';
        writeCurve(out, strip.fadeInCurve);
        out << '\t';
        writeCurve(out, strip.fadeOutCurve);
        out << '\t' << strip.weight << '\t' << (strip.muted ? 1 : 0) << '\t' << (strip.reverse ? 1 : 0) << '\t'
            << strip.loopBlend << '\t' << strip.state << '\n';
    }
    for (const BoneKeyframe& key : pose.keys) {
        out << "bonekey\t" << key.time << '\t' << key.bone << '\t' << boneName(key.bone) << '\t' << key.rotation.x << '\t'
            << key.rotation.y << '\t' << key.rotation.z << '\t' << key.translation.x << '\t' << key.translation.y << '\t'
            << key.translation.z << '\t';
        writeCurve(out, key.curve);
        out << '\n';
    }
    for (const ObjectKeyframe& key : pose.objectKeys) {
        out << "objkey\t" << key.time << '\t' << key.position.x << '\t' << key.position.y << '\t' << key.position.z << '\t';
        writeCurve(out, key.curve);
        out << '\n';
    }
    return static_cast<bool>(out);
}

bool isPoseAnimationFile(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    for (int i = 0; i < 12 && std::getline(in, line); ++i) {
        if (line == "kind skeletal") return true;
    }
    return false;
}

bool loadPoseAnimation(const std::string& path, SkinnedPose& pose, const ImportedSkin* skin) {
    std::ifstream in(path);
    std::string line;
    if (!std::getline(in, line) || line != "CAFANIM1") return false;
    auto findClip = [&](const std::string& name, i32 fallback) -> i32 {
        if (skin) {
            for (size_t i = 0; i < skin->clipNames.size(); ++i) {
                if (skin->clipNames[i] == name) return static_cast<i32>(i);
            }
            if (fallback >= 0 && fallback < static_cast<i32>(skin->clips.size())) return fallback;
            return -1;
        }
        return fallback;
    };
    auto findBone = [&](const std::string& name, i32 fallback) -> i32 {
        if (skin && !name.empty()) {
            for (size_t i = 0; i < skin->boneNames.size(); ++i) {
                if (skin->boneNames[i] == name) return static_cast<i32>(i);
            }
        }
        if (skin && fallback >= static_cast<i32>(skin->skeleton.boneCount())) return -1;
        return fallback;
    };
    std::vector<ClipStrip> strips;
    std::vector<BoneKeyframe> keys;
    std::vector<ObjectKeyframe> objectKeys;
    bool skeletal = false;
    while (std::getline(in, line)) {
        if (line == "kind skeletal") {
            skeletal = true;
            continue;
        }
        const std::vector<std::string> f = splitTabs(line);
        if (f.empty()) continue;
        auto num = [&](size_t i) { return i < f.size() ? std::strtof(f[i].c_str(), nullptr) : 0.0f; };
        if (f[0] == "strip" && f.size() >= 23) {
            ClipStrip strip;
            strip.clip = findClip(f[2], std::atoi(f[1].c_str()));
            if (strip.clip < 0) continue;
            strip.track = std::atoi(f[3].c_str());
            strip.start = num(4);
            strip.clipIn = num(5);
            strip.clipOut = num(6);
            strip.speed = num(7);
            strip.repeat = std::max(1, std::atoi(f[8].c_str()));
            strip.fadeIn = num(9);
            strip.fadeOut = num(10);
            strip.fadeInCurve = readCurve(f, 11);
            strip.fadeOutCurve = readCurve(f, 16);
            strip.weight = num(21);
            strip.muted = std::atoi(f[22].c_str()) != 0;
            strip.reverse = f.size() > 23 && std::atoi(f[23].c_str()) != 0;
            strip.loopBlend = num(24);
            if (f.size() > 25) std::snprintf(strip.state, sizeof(strip.state), "%s", f[25].c_str());
            strips.push_back(strip);
        } else if (f[0] == "bonekey" && f.size() >= 10) {
            BoneKeyframe key;
            key.time = num(1);
            key.bone = findBone(f[3], std::atoi(f[2].c_str()));
            if (key.bone < 0) continue;
            key.rotation = Vec3(num(4), num(5), num(6));
            key.translation = Vec3(num(7), num(8), num(9));
            key.curve = readCurve(f, 10);
            keys.push_back(key);
        } else if (f[0] == "objkey" && f.size() >= 5) {
            ObjectKeyframe key;
            key.time = num(1);
            key.position = Vec3(num(2), num(3), num(4));
            key.curve = readCurve(f, 5);
            objectKeys.push_back(key);
        }
    }
    if (!skeletal) return false;
    pose.strips = std::move(strips);
    pose.stripsInitialized = true;
    pose.keys = std::move(keys);
    pose.objectKeys = std::move(objectKeys);
    pose.sampleKeys = !pose.keys.empty();
    return true;
}

}  // namespace Caffeine::Animation
