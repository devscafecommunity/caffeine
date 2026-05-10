#include "catch.hpp"
#include "../src/animation/SkeletalAnimation.hpp"
#include "Caffeine.hpp"

using namespace Caffeine;
using namespace Caffeine::Animation;
using namespace Caffeine::Math;

TEST_CASE("Skeleton default state", "[skeletal]") {
    Skeleton skeleton;
    REQUIRE(skeleton.boneCount() == 0);
    REQUIRE(skeleton.findBone("nonexistent") == -1);
}

TEST_CASE("Skeleton findBone", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(3);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[1].name = "hip";
    skeleton.bones[1].parentIndex = 0;
    skeleton.bones[2].name = "knee";
    skeleton.bones[2].parentIndex = 1;

    REQUIRE(skeleton.boneCount() == 3);
    REQUIRE(skeleton.findBone("root") == 0);
    REQUIRE(skeleton.findBone("hip") == 1);
    REQUIRE(skeleton.findBone("knee") == 2);
    REQUIRE(skeleton.findBone("ankle") == -1);
}

TEST_CASE("SkeletalKeyframe defaults", "[skeletal]") {
    SkeletalKeyframe kf;
    REQUIRE(kf.time == Approx(0.0f));
    REQUIRE(kf.position.x == Approx(0.0f));
    REQUIRE(kf.rotation.x == Approx(0.0f));
    REQUIRE(kf.rotation.w == Approx(1.0f));
    REQUIRE(kf.scale.x == Approx(1.0f));
}

TEST_CASE("SkeletalClip sampleAt returns identity for empty clip", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(2);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[1].name = "child";
    skeleton.bones[1].parentIndex = 0;

    SkeletalClip clip;
    clip.duration = 1.0f;

    std::vector<Mat4> boneMatrices(2);
    clip.sampleAt(0.5f, skeleton, boneMatrices);

    // Without keyframes, bones stay at identity
    for (u32 i = 0; i < 2; ++i) {
        for (u32 k = 0; k < 4; ++k) {
            for (u32 j = 0; j < 4; ++j) {
                f32 expected = (k == j) ? 1.0f : 0.0f;
                REQUIRE(boneMatrices[i](k, j) == Approx(expected).margin(0.001f));
            }
        }
    }
}

TEST_CASE("SkeletalClip sampleAt keyframe interpolation", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;

    SkeletalClip clip;
    clip.duration = 2.0f;
    clip.loop = false;

    // Two keyframes: at t=0, position (0,0,0); at t=2, position (2,0,0)
    std::vector<SkeletalKeyframe> keys;
    SkeletalKeyframe k0;
    k0.time = 0.0f;
    k0.position = {0.0f, 0.0f, 0.0f};
    k0.rotation = Quat::identity();
    k0.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k0);

    SkeletalKeyframe k1;
    k1.time = 2.0f;
    k1.position = {2.0f, 0.0f, 0.0f};
    k1.rotation = Quat::identity();
    k1.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k1);

    clip.channels.set(0, keys);

    // Sample at t=0 → should get exact first keyframe
    std::vector<Mat4> boneMatrices(1);
    clip.sampleAt(0.0f, skeleton, boneMatrices);
    REQUIRE(boneMatrices[0](0, 3) == Approx(0.0f).margin(0.01f));

    // Sample at t=1 (midpoint) → translation should be (1, 0, 0)
    clip.sampleAt(1.0f, skeleton, boneMatrices);
    REQUIRE(boneMatrices[0](0, 3) == Approx(1.0f).margin(0.01f));
    REQUIRE(boneMatrices[0](1, 3) == Approx(0.0f).margin(0.01f));
    REQUIRE(boneMatrices[0](2, 3) == Approx(0.0f).margin(0.01f));

    // Sample at t=2 → should get exact second keyframe
    clip.sampleAt(2.0f, skeleton, boneMatrices);
    REQUIRE(boneMatrices[0](0, 3) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("SkeletalClip looping wraps time", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;

    SkeletalClip clip;
    clip.duration = 1.0f;
    clip.loop = true;

    std::vector<SkeletalKeyframe> keys;
    SkeletalKeyframe k0;
    k0.time = 0.0f;
    k0.position = {0.0f, 0.0f, 0.0f};
    k0.rotation = Quat::identity();
    k0.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k0);

    SkeletalKeyframe k1;
    k1.time = 1.0f;
    k1.position = {1.0f, 0.0f, 0.0f};
    k1.rotation = Quat::identity();
    k1.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k1);

    clip.channels.set(0, keys);

    // t=1.5 should wrap to t=0.5
    std::vector<Mat4> boneMatrices(1);
    clip.sampleAt(1.5f, skeleton, boneMatrices);
    REQUIRE(boneMatrices[0](0, 3) == Approx(0.5f).margin(0.01f));
}

TEST_CASE("SkeletalClip non-looping clamps at end", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;

    SkeletalClip clip;
    clip.duration = 1.0f;
    clip.loop = false;

    std::vector<SkeletalKeyframe> keys;
    SkeletalKeyframe k0;
    k0.time = 0.0f;
    k0.position = {0.0f, 0.0f, 0.0f};
    k0.rotation = Quat::identity();
    k0.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k0);

    SkeletalKeyframe k1;
    k1.time = 1.0f;
    k1.position = {10.0f, 0.0f, 0.0f};
    k1.rotation = Quat::identity();
    k1.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k1);

    clip.channels.set(0, keys);

    std::vector<Mat4> boneMatrices(1);
    clip.sampleAt(5.0f, skeleton, boneMatrices);
    REQUIRE(boneMatrices[0](0, 3) == Approx(10.0f).margin(0.01f));
}

TEST_CASE("SkeletalClip hierarchy propagates to children", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(2);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[1].name = "child";
    skeleton.bones[1].parentIndex = 0;

    SkeletalClip clip;
    clip.duration = 1.0f;

    // Root moves 5 units on X
    std::vector<SkeletalKeyframe> rootKeys;
    SkeletalKeyframe k0;
    k0.time = 0.0f;
    k0.position = {5.0f, 0.0f, 0.0f};
    k0.rotation = Quat::identity();
    k0.scale = {1.0f, 1.0f, 1.0f};
    rootKeys.push_back(k0);
    clip.channels.set(0, rootKeys);

    // Child at identity
    std::vector<SkeletalKeyframe> childKeys;
    SkeletalKeyframe c0;
    c0.time = 0.0f;
    c0.position = {0.0f, 0.0f, 0.0f};
    c0.rotation = Quat::identity();
    c0.scale = {1.0f, 1.0f, 1.0f};
    childKeys.push_back(c0);
    clip.channels.set(1, childKeys);

    std::vector<Mat4> boneMatrices(2);
    clip.sampleAt(0.0f, skeleton, boneMatrices);

    // Root has translation X=5
    REQUIRE(boneMatrices[0](0, 3) == Approx(5.0f).margin(0.01f));

    // Child has world translation X=5 (inherits from root)
    REQUIRE(boneMatrices[1](0, 3) == Approx(5.0f).margin(0.01f));
}

TEST_CASE("BlendNode defaults", "[skeletal]") {
    BlendNode node;
    REQUIRE(node.type == BlendNode::Type::Clip);
    REQUIRE(node.clip == nullptr);
    REQUIRE(node.children.empty());
    REQUIRE(node.parameter == Approx(0.0f));
}

TEST_CASE("SkeletalAnimationState defaults", "[skeletal]") {
    SkeletalAnimationState state;
    REQUIRE(state.name == "");
    REQUIRE(state.clip == nullptr);
    REQUIRE(state.speed == Approx(1.0f));
}

TEST_CASE("SkeletalAnimator defaults", "[skeletal]") {
    SkeletalAnimator anim;
    REQUIRE(anim.skeleton == nullptr);
    REQUIRE(anim.boneMatrices.empty());
    REQUIRE(anim.currentState == "");
    REQUIRE(anim.timeInState == Approx(0.0f));
    REQUIRE(anim.blendWeight == Approx(1.0f));
    REQUIRE(anim.paused == false);
    REQUIRE(anim.blendTree == nullptr);
}

TEST_CASE("SkeletalAnimator play and pause", "[skeletal]") {
    // Test logic without a full ECS world
    SkeletalAnimator anim;
    SkeletalAnimationSystem system;

    // Verify the system API compiles and works with basic operations
    // Full integration tests require a World with an entity
    REQUIRE(anim.paused == false);
    anim.paused = true;
    REQUIRE(anim.paused == true);
    anim.paused = false;
}

TEST_CASE("SkeletalAnimationState transitions", "[skeletal]") {
    SkeletalAnimationState idle;
    idle.name = "idle";
    idle.speed = 1.0f;

    SkeletalAnimationState walk;
    walk.name = "walk";
    walk.speed = 2.0f;

    SkeletalAnimator anim;
    anim.states.set(idle.name, idle);
    anim.states.set(walk.name, walk);
    anim.currentState = "idle";
    anim.timeInState = 0.0f;

    REQUIRE(anim.currentState == "idle");

    // Manually switch state (testing the state storage, not transitions)
    anim.currentState = walk.name;
    REQUIRE(anim.currentState == "walk");
}

TEST_CASE("Skeleton bones stored parent-first order", "[skeletal]") {
    // This test verifies the assumption that bones are stored in topological order
    // (parents before children), which sampleAt depends on.
    Skeleton skeleton;
    skeleton.bones.resize(3);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;
    skeleton.bones[1].name = "child";
    skeleton.bones[1].parentIndex = 0;
    skeleton.bones[2].name = "grandchild";
    skeleton.bones[2].parentIndex = 1;

    for (u32 i = 0; i < skeleton.boneCount(); ++i) {
        if (skeleton.bones[i].parentIndex >= 0) {
            REQUIRE(static_cast<u32>(skeleton.bones[i].parentIndex) < i);
        }
    }
}

TEST_CASE("SkeletalClip rotation interpolation via slerp", "[skeletal]") {
    Skeleton skeleton;
    skeleton.bones.resize(1);
    skeleton.bones[0].name = "root";
    skeleton.bones[0].parentIndex = -1;

    SkeletalClip clip;
    clip.duration = 1.0f;
    clip.loop = false;

    std::vector<SkeletalKeyframe> keys;
    SkeletalKeyframe k0;
    k0.time = 0.0f;
    k0.position = {0.0f, 0.0f, 0.0f};
    k0.rotation = Quat::identity();
    k0.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k0);

    // Rotate 90 degrees around Y at t=1
    SkeletalKeyframe k1;
    k1.time = 1.0f;
    k1.position = {0.0f, 0.0f, 0.0f};
    k1.rotation = Quat::fromAxisAngle({0, 1, 0}, degToRad(90.0f));
    k1.scale = {1.0f, 1.0f, 1.0f};
    keys.push_back(k1);

    clip.channels.set(0, keys);

    std::vector<Mat4> boneMatrices(1);

    // At t=0: identity → forward = (0,0,1)
    clip.sampleAt(0.0f, skeleton, boneMatrices);
    Vec3 fwd0 = Quat::fromMatrix(boneMatrices[0]).rotate({0, 0, 1});
    REQUIRE(fwd0.z == Approx(1.0f).margin(0.01f));

    // At t=1: 90° Y → forward = (1,0,0)
    clip.sampleAt(1.0f, skeleton, boneMatrices);
    Vec3 fwd1 = Quat::fromMatrix(boneMatrices[0]).rotate({0, 0, 1});
    REQUIRE(fwd1.x == Approx(1.0f).margin(0.1f));
}
