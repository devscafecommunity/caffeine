#include "animation/SkinLibrary.hpp"

#include "assets/MeshCache.hpp"
#include "ecs/ComponentQuery.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace Caffeine::Animation {
namespace {

std::unordered_map<std::string, ImportedSkin> g_skins;
std::unordered_map<u32, std::vector<Assets::Vertex3D>> g_posed;
std::unordered_map<u32, std::vector<Vec3>> g_joints;
std::unordered_map<u32, std::vector<i32>> g_jointParents;
std::string g_projectRoot;

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

}  // namespace

void registerImportedSkin(const std::string& path, ImportedSkin skin) {
    if (path.empty() || skin.skeleton.bones.empty()) return;
    g_skins[path] = std::move(skin);
}

const ImportedSkin* findImportedSkin(const std::string& path) {
    if (path.empty()) return nullptr;
    const auto exact = g_skins.find(path);
    if (exact != g_skins.end()) return &exact->second;
    const ImportedSkin* match = nullptr;
    for (const auto& entry : g_skins) {
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
        case HumanoidBone::Count: break;
    }
    return "";
}

HumanoidRig matchHumanoid(const ImportedSkin& skin) {
    HumanoidRig rig;
    const u32 count = std::max(skin.skeleton.boneCount(), static_cast<u32>(skin.boneNames.size()));
    for (u32 i = 0; i < count; ++i) {
        const std::string source = i < skin.boneNames.size() ? skin.boneNames[i] : skin.skeleton.bones[i].name.cStr();
        const std::string bone = normalizedBone(source);
        for (const Alias& alias : kAliases) {
            const int slot = static_cast<int>(alias.bone);
            if (rig.bones[slot] >= 0) continue;
            if (tokenMatches(bone, alias.token)) {
                rig.bones[slot] = static_cast<i32>(i);
                break;
            }
        }
    }
    const bool arm = rig.bones[static_cast<int>(HumanoidBone::LeftUpperArm)] >= 0 ||
                     rig.bones[static_cast<int>(HumanoidBone::RightUpperArm)] >= 0;
    const bool leg = rig.bones[static_cast<int>(HumanoidBone::LeftUpperLeg)] >= 0 ||
                     rig.bones[static_cast<int>(HumanoidBone::RightUpperLeg)] >= 0;
    rig.matched = rig.bones[static_cast<int>(HumanoidBone::Hips)] >= 0 &&
                  rig.bones[static_cast<int>(HumanoidBone::Head)] >= 0 && arm && leg;
    return rig;
}

void skinVertices(const Assets::Mesh3D& bind, const std::vector<Mat4>& bones,
                  std::vector<Assets::Vertex3D>& out) {
    out = bind.vertices;
    if (bind.skin.size() != bind.vertices.size() || bones.empty()) return;
    for (size_t i = 0; i < bind.vertices.size(); ++i) {
        const Assets::VertexSkin& skin = bind.skin[i];
        f32 weightSum = 0.0f;
        for (f32 weight : skin.weights) weightSum += weight;
        if (weightSum <= 0.0001f) continue;
        Vec3 position{};
        Vec3 normal{};
        for (int influence = 0; influence < 4; ++influence) {
            const f32 weight = skin.weights[influence];
            if (weight <= 0.0f) continue;
            const u32 joint = skin.joints[influence];
            if (joint >= bones.size()) continue;
            position = position + bones[joint].transformPoint(bind.vertices[i].position) * weight;
            normal = normal + bones[joint].transformVector(bind.vertices[i].normal) * weight;
        }
        out[i].position = position;
        if (normal.lengthSquared() > 0.0001f) out[i].normal = normal.normalized();
    }
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

void setSkinProjectRoot(const std::string& projectRoot) {
    g_projectRoot = projectRoot;
}

void tickSkinnedPoses(ECS::World& world, f32 dt) {
    ECS::ComponentQuery query;
    query.with<SkinnedPose>();
    std::vector<u32> live;
    world.forEach<SkinnedPose>(query, [&](ECS::Entity entity, SkinnedPose& pose) {
        live.push_back(entity.id());
        if (pose.meshPath[0] == '\0') {
            g_posed.erase(entity.id());
            g_joints.erase(entity.id());
            g_jointParents.erase(entity.id());
            return;
        }
        Assets::MeshCache::getInstance().getMesh(pose.meshPath, g_projectRoot);
        const ImportedSkin* skin = findImportedSkin(pose.meshPath);
        const Assets::Mesh3D* mesh = skin ? skin->mesh : nullptr;
        if (!mesh) mesh = Assets::MeshCache::getInstance().getMesh(pose.meshPath, g_projectRoot);
        if (!skin || !mesh) {
            g_posed.erase(entity.id());
            g_joints.erase(entity.id());
            g_jointParents.erase(entity.id());
            return;
        }
        if (const Animator* animator = world.get<Animator>(entity)) {
            const char* stateName = animator->currentState.cStr();
            if (stateName && stateName[0] != '\0') {
                for (int clip = 0; clip < static_cast<int>(skin->clipNames.size()); ++clip) {
                    if (skin->clipNames[static_cast<size_t>(clip)] == stateName && pose.clipIndex != clip) {
                        pose.clipIndex = clip;
                        pose.time = 0.0f;
                        pose.playing = true;
                        break;
                    }
                }
            }
        }
        if (!pose.loaded) {
            pose.humanoid = matchHumanoid(*skin);
            pose.loaded = true;
            if (pose.clipIndex < 0) pose.clipIndex = 0;
        }
        if (!skin->clips.empty()) {
            if (pose.clipIndex >= static_cast<i32>(skin->clips.size())) pose.clipIndex = 0;
            const SkeletalClip& clip = skin->clips[static_cast<size_t>(pose.clipIndex)];
            if (pose.playing) {
                pose.time += dt * pose.speed;
                const f32 duration = std::max(clip.duration, 0.0f);
                if (duration > 0.0f && pose.time >= duration) {
                    if (pose.loop && clip.loop) pose.time = std::fmod(pose.time, duration);
                    else {
                        pose.time = duration;
                        pose.playing = false;
                    }
                }
            }
            std::vector<Mat4> bones;
            std::vector<Vec3> joints;
            clip.sampleAt(pose.time, skin->skeleton, bones, &joints);
            skinVertices(*mesh, bones, g_posed[entity.id()]);
            g_joints[entity.id()] = std::move(joints);
        } else {
            g_posed[entity.id()] = mesh->vertices;
            g_joints.erase(entity.id());
        }
        std::vector<i32> parents;
        parents.reserve(skin->skeleton.boneCount());
        for (const Bone& bone : skin->skeleton.bones) parents.push_back(bone.parentIndex);
        g_jointParents[entity.id()] = std::move(parents);
    });

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
}

}  // namespace Caffeine::Animation
