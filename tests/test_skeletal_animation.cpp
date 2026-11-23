#include "catch.hpp"
#include "../src/caffeine/animation/SkeletalAnimation.hpp"
#include "../src/caffeine/animation/SkinLibrary.hpp"
#include "../src/caffeine/assets/MeshTypes.hpp"
#include "../src/caffeine/assets/MeshCache.hpp"
#include "../src/caffeine/assets/MeshLoader.hpp"
#include "../src/caffeine/ecs/MeshComponents.hpp"
#include "../src/caffeine/ecs/World.hpp"
#include <filesystem>
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

TEST_CASE("Humanoid names map mixamo bones", "[skeletal]") {
    ImportedSkin skin;
    skin.boneNames = {"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Head",
                      "mixamorig:LeftArm", "mixamorig:LeftUpLeg"};
    const HumanoidRig rig = matchHumanoid(skin);
    REQUIRE(rig.matched);
    REQUIRE(rig.bones[static_cast<int>(HumanoidBone::Hips)] == 0);
    REQUIRE(rig.bones[static_cast<int>(HumanoidBone::Head)] == 2);
    REQUIRE(rig.bones[static_cast<int>(HumanoidBone::LeftUpperArm)] == 3);
    REQUIRE(rig.bones[static_cast<int>(HumanoidBone::LeftUpperLeg)] == 4);
}

TEST_CASE("CPU skin moves a weighted vertex", "[skeletal]") {
    Assets::Mesh3D mesh;
    Assets::Vertex3D vertex{};
    vertex.position = {0.0f, 1.0f, 0.0f};
    mesh.vertices.push_back(vertex);
    Assets::VertexSkin weight{};
    weight.joints[0] = 0;
    weight.weights[0] = 1.0f;
    mesh.skin.push_back(weight);

    std::vector<Mat4> bones(1, Mat4::translation(2.0f, 0.0f, 0.0f));
    std::vector<Assets::Vertex3D> posed;
    skinVertices(mesh, bones, posed);
    REQUIRE(posed.size() == 1);
    REQUIRE(posed[0].position.x == Approx(2.0f).margin(0.001f));
    REQUIRE(posed[0].position.y == Approx(1.0f).margin(0.001f));
}

TEST_CASE("Sprite sheet frame picks a cell", "[animation]") {
    SpriteSheet sheet;
    sheet.columns = 4;
    sheet.rows = 1;
    sheet.frameCount = 4;
    const SpriteSheetRect cell = spriteSheetFrame(sheet, 1, 128.0f, 32.0f);
    REQUIRE(cell.x == Approx(32.0f).margin(0.001f));
    REQUIRE(cell.w == Approx(32.0f).margin(0.001f));
    REQUIRE(cell.h == Approx(32.0f).margin(0.001f));
}


TEST_CASE("Static glTF does not get an automatic humanoid skin", "[skeletal][skin]") {
    const std::filesystem::path repoRoot =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::string relative = "assets/vegetation-kit/models/moss_01/moss_01.gltf";
    const std::string absolute = (repoRoot / relative).string();

    Assets::Mesh3D* mesh = Assets::MeshCache::getInstance().getMesh(absolute, repoRoot.string());
    INFO(Assets::MeshCache::getInstance().getLastError());
    REQUIRE(mesh != nullptr);
    REQUIRE(Animation::findImportedSkin(absolute) == nullptr);

    Assets::MeshLoader::ensureGltfSkin(absolute, mesh);
    REQUIRE(Animation::findImportedSkin(absolute) == nullptr);
    REQUIRE(mesh->skin.empty());

    ECS::World world;
    ECS::Entity entity = world.create();
    auto& filter = world.add<ECS::MeshFilterComponent>(entity);
    filter.primitive = ECS::MeshPrimitive::Custom;
    filter.customMeshPath = relative;
    REQUIRE(Animation::ensurePoseForMesh(world, entity, repoRoot.string()) == nullptr);
    REQUIRE_FALSE(world.has<Animation::SkinnedPose>(entity));
}

TEST_CASE("Explicit humanoid synthesis still works", "[skeletal][skin]") {
    static Assets::Mesh3D mesh;
    mesh = Assets::Mesh3D{};
    mesh.vertices.resize(2);
    mesh.vertices[0].position = Vec3(0.0f, 0.0f, 0.0f);
    mesh.vertices[1].position = Vec3(0.0f, 2.0f, 0.0f);

    const std::string path =
        (std::filesystem::temp_directory_path() / "caffeine_explicit_humanoid_skin").string();
    REQUIRE(Animation::synthesizeHumanoidSkin(path, mesh, true));
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(path);
    REQUIRE(skin != nullptr);
    REQUIRE(skin->synthesized);
    REQUIRE(skin->boneNames.size() == 17);
    REQUIRE(!mesh.skin.empty());
    REQUIRE(mesh.skin[0].weights[0] > 0.0f);
}

// ============================================================================
// Blend-tree regression
// ============================================================================

TEST_CASE("BlendNode Blend1D - halfway between identity and a quarter turn is a 45 degree turn", "[skeletal]") {
    Skeleton skel;
    Bone bone;
    bone.name = "bone";
    bone.parentIndex = -1;
    bone.bindPoseInverse = Mat4::identity();
    bone.localTransform = Mat4::identity();
    skel.bones = {bone};
    skel.skinSpace = Mat4::identity();

    std::vector<SkeletalKeyframe> restKeys(1);
    restKeys[0].time = 0.0f;
    SkeletalClip still;
    still.duration = 1.0f;
    still.loop = false;
    still.channels.set(0, restKeys);

    const Quat quarter = Quat::fromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), 1.5707963f);
    std::vector<SkeletalKeyframe> turnKeys(2);
    turnKeys[0].time = 0.0f;
    turnKeys[0].rotation = quarter;
    turnKeys[1].time = 1.0f;
    turnKeys[1].rotation = quarter;
    SkeletalClip turned;
    turned.duration = 1.0f;
    turned.loop = false;
    turned.channels.set(0, turnKeys);

    BlendNode a;
    a.type = BlendNode::Type::Clip;
    a.clip = &still;
    BlendNode b;
    b.type = BlendNode::Type::Clip;
    b.clip = &turned;
    BlendNode root;
    root.type = BlendNode::Type::Blend1D;
    root.children = {{0.0f, &a}, {1.0f, &b}};
    root.parameter = 0.5f;

    std::vector<Mat4> out(1);
    for (u32 k = 0; k < 16; ++k) out[0].data()[k] = 0.0f;
    sampleBlendTree(&root, 0.0f, skel, out, 1.0f);
    const Mat4& m = out[0];

    // Slerp gives a true 45 degree rotation. Averaging matrices would give 0.5 here and a non-unit column.
    const f32 c45 = std::cos(0.7853982f);
    REQUIRE(m(0, 0) == Approx(c45).margin(1e-3));
    const f32 column = m(0, 0) * m(0, 0) + m(1, 0) * m(1, 0);
    REQUIRE(column == Approx(1.0f).margin(1e-3));
}
