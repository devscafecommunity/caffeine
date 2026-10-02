#pragma once

#include "animation/SkeletalAnimation.hpp"
#include "assets/MeshTypes.hpp"
#include "ecs/Entity.hpp"
#include "ecs/World.hpp"

#include <string>
#include <vector>

namespace Caffeine::Animation {

struct ImportedSkin {
    Skeleton skeleton;
    std::vector<std::string> boneNames;
    std::vector<SkeletalClip> clips;
    std::vector<std::string> clipNames;
    const Assets::Mesh3D* mesh = nullptr;
};

void registerImportedSkin(const std::string& path, ImportedSkin skin);
const ImportedSkin* findImportedSkin(const std::string& path);

enum class HumanoidBone : u8 {
    Hips,
    Spine,
    Chest,
    Neck,
    Head,
    LeftUpperArm,
    LeftLowerArm,
    LeftHand,
    RightUpperArm,
    RightLowerArm,
    RightHand,
    LeftUpperLeg,
    LeftLowerLeg,
    LeftFoot,
    RightUpperLeg,
    RightLowerLeg,
    RightFoot,
    Count
};

struct HumanoidRig {
    i32 bones[static_cast<int>(HumanoidBone::Count)];
    bool matched = false;

    HumanoidRig() {
        for (i32& bone : bones) bone = -1;
    }
};

const char* humanoidBoneName(HumanoidBone bone);
HumanoidRig matchHumanoid(const ImportedSkin& skin);

/// Writes skinned positions into `out`. Unskinned meshes are copied through.
void skinVertices(const Assets::Mesh3D& bind, const std::vector<Mat4>& bones,
                  std::vector<Assets::Vertex3D>& out);

/// Owned playback for a skinned or general 3D mesh. The skeleton stays in the import library.
struct SkinnedPose {
    char meshPath[260] = {};
    i32 clipIndex = 0;
    f32 time = 0.0f;
    f32 speed = 1.0f;
    bool playing = true;
    bool loop = true;
    bool loaded = false;
    HumanoidRig humanoid;
};

/// Grid of frames on one texture. frameIndex selects the cell.
struct SpriteSheet {
    u32 columns = 1;
    u32 rows = 1;
    u32 frameCount = 1;
};

struct SpriteSheetRect {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 w = 0.0f;
    f32 h = 0.0f;
};

SpriteSheetRect spriteSheetFrame(const SpriteSheet& sheet, u32 frame, f32 textureWidth, f32 textureHeight);

const std::vector<Assets::Vertex3D>* skinnedVerticesFor(u32 entityId);
const std::vector<Vec3>* jointPositionsFor(u32 entityId);
const std::vector<i32>* jointParentsFor(u32 entityId);
/// Editor and runtime pass the project root so `assets/...` paths resolve.
void setSkinProjectRoot(const std::string& projectRoot);
void tickSkinnedPoses(ECS::World& world, f32 dt);

}  // namespace Caffeine::Animation
