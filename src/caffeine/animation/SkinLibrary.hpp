#pragma once

#include "animation/SkeletalAnimation.hpp"
#include "assets/MeshTypes.hpp"
#include "ecs/Entity.hpp"
#include "ecs/MeshComponents.hpp"
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
    bool synthesized = false;
};

void registerImportedSkin(const std::string& path, ImportedSkin skin);
const ImportedSkin* findImportedSkin(const std::string& path);
/// Build a T-pose humanoid from mesh bounds when the file has no glTF skin.
/// `replace` rebuilds an existing fitted skin after the mesh or bind changed.
bool synthesizeHumanoidSkin(const std::string& path, Assets::Mesh3D& mesh, bool replace = false);

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
    LeftThumbProximal,
    LeftThumbIntermediate,
    LeftThumbDistal,
    LeftIndexProximal,
    LeftIndexIntermediate,
    LeftIndexDistal,
    LeftMiddleProximal,
    LeftMiddleIntermediate,
    LeftMiddleDistal,
    LeftRingProximal,
    LeftRingIntermediate,
    LeftRingDistal,
    LeftLittleProximal,
    LeftLittleIntermediate,
    LeftLittleDistal,
    RightThumbProximal,
    RightThumbIntermediate,
    RightThumbDistal,
    RightIndexProximal,
    RightIndexIntermediate,
    RightIndexDistal,
    RightMiddleProximal,
    RightMiddleIntermediate,
    RightMiddleDistal,
    RightRingProximal,
    RightRingIntermediate,
    RightRingDistal,
    RightLittleProximal,
    RightLittleIntermediate,
    RightLittleDistal,
    Count
};

constexpr int kHumanoidBodyCount = static_cast<int>(HumanoidBone::LeftThumbProximal);
constexpr int kHumanoidLegacyCount = kHumanoidBodyCount;

struct HumanoidRig {
    i32 bones[static_cast<int>(HumanoidBone::Count)];
    bool matched = false;
    /// User mapping stays on the character. Automatic matching runs only while this is false.
    bool manual = false;

    HumanoidRig() {
        for (i32& bone : bones) bone = -1;
    }
};

const char* humanoidBoneName(HumanoidBone bone);
bool isHumanoidFingerSlot(HumanoidBone bone);
HumanoidRig matchHumanoid(const ImportedSkin& skin);
void refreshHumanoidMatched(HumanoidRig& rig);
void assignHumanoidBone(HumanoidRig& rig, HumanoidBone slot, i32 bone);

/// Writes skinned positions into `out`. Unskinned meshes are copied through.
void skinVertices(const Assets::Mesh3D& bind, const std::vector<Mat4>& bones,
                  std::vector<Assets::Vertex3D>& out);

/// Local edit on top of the bind pose. Euler angles are radians.
struct BonePose {
    Vec3 rotation{};
    Vec3 translation{};
};

const char* keyInterpName(KeyInterp mode);
KeyCurve keyCurvePreset(KeyInterp mode);
/// Maps segment progress t in [0, 1] through the curve.
f32 evaluateKeyCurve(const KeyCurve& curve, f32 t);

struct BoneKeyframe {
    f32 time = 0.0f;
    i32 bone = -1;
    Vec3 rotation{};
    Vec3 translation{};
    KeyCurve curve;
};

struct ObjectKeyframe {
    f32 time = 0.0f;
    Vec3 position{};
    KeyCurve curve;
};

/// One clip placed on the timeline, edited like a strip in a video editor.
struct ClipStrip {
    i32 clip = 0;
    /// Row on the timeline. Higher tracks blend over lower ones.
    i32 track = 0;
    f32 start = 0.0f;
    /// Source range in clip seconds. clipOut < 0 means the clip's end.
    f32 clipIn = 0.0f;
    f32 clipOut = -1.0f;
    f32 speed = 1.0f;
    /// Times the source range plays back to back.
    i32 repeat = 1;
    f32 fadeIn = 0.0f;
    f32 fadeOut = 0.0f;
    KeyCurve fadeInCurve;
    KeyCurve fadeOutCurve;
    f32 weight = 1.0f;
    bool muted = false;
    bool reverse = false;
    /// Seconds at the end of each pass that blend back into the first frame, so loops close.
    f32 loopBlend = 0.0f;
    /// Animator state this strip stands for. Empty when the strip is free.
    char state[32] = {};
};

/// Clip and its playback time, blended over what came before it in the layer.
struct PoseSample {
    i32 clip = -1;
    f32 time = 0.0f;
    f32 weight = 1.0f;
    bool loop = true;
    f32 loopBlend = 0.0f;
};

struct PoseLayerMix {
    std::vector<PoseSample> samples;
    f32 weight = 1.0f;
    bool additive = false;
    u32 mask = 0x7Fu;
};

/// Owned playback for a skinned or general 3D mesh. The skeleton stays in the import library.
struct SkinnedPose {
    char meshPath[260] = {};
    i32 clipIndex = 0;
    f32 time = 0.0f;
    f32 speed = 1.0f;
    bool playing = false;
    /// Cleared on load so an imported clip does not start until Play is pressed.
    bool editorHeld = false;
    bool loop = true;
    bool loaded = false;
    /// Timeline sets this while playing or scrubbing so keys replace the live pose.
    bool sampleKeys = false;
    HumanoidRig humanoid;
    std::vector<BonePose> offsets;
    std::vector<BoneKeyframe> keys;
    std::vector<ObjectKeyframe> objectKeys;
    /// Joints the user removed from the editable skeleton. Skinning keeps the bind pose.
    std::vector<i32> removedBones;
    /// Timeline clip strips. Filled from the imported clips the first time the timeline opens.
    std::vector<ClipStrip> strips;
    bool stripsInitialized = false;
    /// Editor override: the timeline writes the blend it wants shown.
    std::vector<PoseLayerMix> overrideMix;
    u64 overrideFrame = 0;
    /// Timeline is open on this entity. tickSkinnedPoses keeps overrideMix until this is cleared.
    bool timelineHold = false;
};

/// Seconds the strip occupies on the timeline.
f32 clipStripLength(const ClipStrip& strip, const ImportedSkin* skin);
/// Source clip time at timeline time `time`, or false outside the strip.
bool clipStripSourceTime(const ClipStrip& strip, const ImportedSkin* skin, f32 time, f32& outSource);
/// Fade weight at timeline time `time` (0 outside the strip). Overlap with the previous strip on the
/// same track counts as a crossfade even without an explicit fade in.
f32 clipStripFade(const ClipStrip& strip, const ImportedSkin* skin, f32 time, f32 autoFadeIn);
/// Seconds the strip overlaps the strip before it on the same track.
f32 clipStripOverlapIn(const SkinnedPose& pose, size_t index, const ImportedSkin* skin);
void buildStripMix(const SkinnedPose& pose, const ImportedSkin* skin, f32 time, std::vector<PoseLayerMix>& out);
f32 stripsDuration(const SkinnedPose& pose, const ImportedSkin* skin);
/// Places the active imported clip on track 0, once per pose, with a loop blend.
void ensureDefaultStrips(SkinnedPose& pose, const ImportedSkin* skin);
/// Copies the imported clip's channels onto editable pose keys. bone < 0 bakes every non-finger joint.
void bakeClipToKeys(SkinnedPose& pose, const ImportedSkin& skin, i32 clip, i32 bone);
/// Removes keys of `bone` (`-1` is object position) near `time`. Returns true when anything was removed.
bool deleteKeysNearTime(SkinnedPose& pose, i32 bone, f32 time, f32 window = 0.02f);
/// Monotonic tick counter. overrideMix is honoured while overrideFrame is this or the last tick.
u64 skinFrameCounter();

/// Skeletal .anim asset: strips, bone keys, object keys and their curves.
bool savePoseAnimation(const std::string& path, const std::string& name, const SkinnedPose& pose,
                       const ImportedSkin* skin);
bool loadPoseAnimation(const std::string& path, SkinnedPose& pose, const ImportedSkin* skin);
/// True when the file holds skeletal data rather than sprite frames.
bool isPoseAnimationFile(const std::string& path);

bool isFingerBoneName(const std::string& name);
bool poseOmitsBone(const SkinnedPose& pose, i32 bone);
/// Name tokens, hand children, or a slot the user assigned.
bool isFingerBone(const SkinnedPose& pose, const ImportedSkin& skin, i32 bone);

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
/// Bumps only when the posed vertex buffer changes. Zero means the bind mesh is in use.
u64 skinnedGenerationFor(u32 entityId);
/// Increments whenever any entity's posed vertices change. The editor viewport uses it to redraw.
u64 skinRevision();
const std::vector<Vec3>* jointPositionsFor(u32 entityId);
const std::vector<i32>* jointParentsFor(u32 entityId);
/// Skin-space frame of a joint before its own edit rotation: skinSpace * parent * bind local * translation.
/// The gizmo uses it to turn screen drags into bone-local rotation and translation.
bool boneEditFrame(const ImportedSkin& skin, const SkinnedPose& pose, i32 bone, Mat4& out,
                   u32 entityId = 0xFFFFFFFFu);
/// Layer weight after its groups' weights and mute flags.
f32 effectiveLayerWeight(const Animator& animator, const AnimatorLayer& layer);
/// Pose layers for the animator's current states and crossfades. Empty when nothing is playing.
void buildAnimatorMix(const Animator& animator, const ImportedSkin& skin, std::vector<PoseLayerMix>& out);
/// Clip index the animator state plays, or -1. A state's motion name wins over its own name.
i32 clipForAnimatorState(const ImportedSkin& skin, const char* stateName, const char* motionName);
/// Editor and runtime pass the project root so `assets/...` paths resolve.
void setSkinProjectRoot(const std::string& projectRoot);
/// If the entity's mesh has a skeleton, attach SkinnedPose so the timeline can drive it.
SkinnedPose* ensurePoseForMesh(ECS::World& world, ECS::Entity entity, const std::string& projectRoot);
void tickSkinnedPoses(ECS::World& world, f32 dt);

}  // namespace Caffeine::Animation
