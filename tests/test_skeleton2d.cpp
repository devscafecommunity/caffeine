#include "catch.hpp"
#include "../src/caffeine/animation/Skeleton2D.hpp"
#include "../src/caffeine/animation/Smoothing.hpp"
#include "../src/caffeine/animation/Rig2DAsset.hpp"
#include "../src/caffeine/animation/Rig2DSystem.hpp"
#include "../src/caffeine/animation/AnimationSystem.hpp"
#include "caffeine/animation/AnimationApi.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#ifdef CF_HAS_SDL3
#include "../src/caffeine/render/BatchRenderer.hpp"
#include "../src/caffeine/render/Scene2DOverlay.hpp"
#endif

using namespace Caffeine;
using namespace Caffeine::Animation;

static constexpr f32 kPi  = 3.14159265358979f;
static constexpr f32 kEps = 1e-3f;

static bool near(f32 a, f32 b, f32 eps = kEps) { return std::fabs(a - b) < eps; }
static bool near(Vec2 a, Vec2 b, f32 eps = kEps) { return near(a.x, b.x, eps) && near(a.y, b.y, eps); }

// ============================================================================
// Affine2
// ============================================================================

TEST_CASE("Affine2 - identity leaves points unchanged", "[animation][skeleton2d]") {
    REQUIRE(near(Affine2::identity().apply(Vec2(3.0f, -4.0f)), Vec2(3.0f, -4.0f)));
}

TEST_CASE("Affine2 - quarter turn maps +X to +Y", "[animation][skeleton2d]") {
    const Affine2 m = Affine2::fromTRS(Vec2(0.0f, 0.0f), kPi * 0.5f, Vec2(1.0f, 1.0f));
    REQUIRE(near(m.apply(Vec2(1.0f, 0.0f)), Vec2(0.0f, 1.0f)));
}

TEST_CASE("Affine2 - scale then rotate then translate", "[animation][skeleton2d]") {
    const Affine2 m = Affine2::fromTRS(Vec2(10.0f, 0.0f), kPi * 0.5f, Vec2(2.0f, 1.0f));
    // (1,0) scales to (2,0), rotates to (0,2), translates to (10,2).
    REQUIRE(near(m.apply(Vec2(1.0f, 0.0f)), Vec2(10.0f, 2.0f)));
}

TEST_CASE("Affine2 - inverse undoes the transform", "[animation][skeleton2d]") {
    const Affine2 t = Affine2::fromTRS(Vec2(2.0f, 3.0f), 0.7f, Vec2(1.5f, 0.5f));
    const Vec2 p(1.3f, -2.1f);
    REQUIRE(near((t * t.inverse()).apply(p), p));
    REQUIRE(near((t.inverse() * t).apply(p), p));
}

TEST_CASE("Affine2 - singular inverse falls back to identity", "[animation][skeleton2d]") {
    Affine2 degenerate;
    degenerate.a = 0.0f;
    degenerate.d = 0.0f;
    const Affine2 inv = degenerate.inverse();
    REQUIRE(near(inv.apply(Vec2(5.0f, 6.0f)), Vec2(5.0f, 6.0f)));
}

// ============================================================================
// Skeleton and bone hierarchy
// ============================================================================

TEST_CASE("Skeleton2D - findBone returns index or -1", "[animation][skeleton2d]") {
    Skeleton2D skel;
    Bone2D root;
    root.name = "root";
    Bone2D arm;
    arm.name = "arm";
    arm.parentIndex = 0;
    skel.bones = {root, arm};
    REQUIRE(skel.findBone("arm") == 1);
    REQUIRE(skel.findBone("missing") == -1);
}

TEST_CASE("computeWorld2D - child follows rotated parent", "[animation][skeleton2d]") {
    Skeleton2D skel;
    Bone2D parent;
    parent.restPosition = Vec2(10.0f, 0.0f);
    parent.restRotation = kPi * 0.5f;
    Bone2D child;
    child.parentIndex = 0;
    child.restPosition = Vec2(1.0f, 0.0f);
    skel.bones = {parent, child};

    const auto world = computeWorld2D(skel, restPose2D(skel));
    // The child's offset (1,0) is rotated by the parent's quarter turn.
    REQUIRE(near(world[1].apply(Vec2(0.0f, 0.0f)), Vec2(10.0f, 1.0f)));
}

TEST_CASE("computeWorld2D - parent listed after child still resolves", "[animation][skeleton2d]") {
    Skeleton2D skel;
    Bone2D child;
    child.parentIndex = 1;
    child.restPosition = Vec2(1.0f, 0.0f);
    Bone2D parent;
    parent.restPosition = Vec2(10.0f, 0.0f);
    skel.bones = {child, parent};

    const auto world = computeWorld2D(skel, restPose2D(skel));
    REQUIRE(near(world[0].apply(Vec2(0.0f, 0.0f)), Vec2(11.0f, 0.0f)));
}

TEST_CASE("computeWorld2D - cyclic parents fall back to local transforms", "[animation][skeleton2d]") {
    Skeleton2D skel;
    Bone2D a;
    a.parentIndex = 1;
    a.restPosition = Vec2(4.0f, 0.0f);
    Bone2D b;
    b.parentIndex = 0;
    b.restPosition = Vec2(0.0f, 5.0f);
    skel.bones = {a, b};

    const auto world = computeWorld2D(skel, restPose2D(skel));
    REQUIRE(near(world[0].apply(Vec2(0.0f, 0.0f)), Vec2(4.0f, 0.0f)));
    REQUIRE(near(world[1].apply(Vec2(0.0f, 0.0f)), Vec2(0.0f, 5.0f)));
}

// ============================================================================
// Curves and parameters
// ============================================================================

TEST_CASE("sampleCurve2D - linear between keys and clamped outside", "[animation][skeleton2d]") {
    const std::vector<CurveKey2D> keys = {{0.0f, 0.0f}, {1.0f, 10.0f}};
    REQUIRE(near(sampleCurve2D(keys, 0.25f), 2.5f));
    REQUIRE(near(sampleCurve2D(keys, -5.0f), 0.0f));
    REQUIRE(near(sampleCurve2D(keys, 5.0f), 10.0f));
}

TEST_CASE("sampleCurve2D - empty keys give zero", "[animation][skeleton2d]") {
    REQUIRE(near(sampleCurve2D({}, 0.5f), 0.0f));
}

TEST_CASE("clampParameters2D - clamps and fills new slots with defaults", "[animation][skeleton2d]") {
    Rig2D rig;
    Parameter2D angle;
    angle.name = "AngleX";
    angle.minValue = -1.0f;
    angle.defaultValue = 0.25f;
    angle.maxValue = 1.0f;
    Parameter2D blink;
    blink.name = "Blink";
    blink.minValue = 0.0f;
    blink.defaultValue = 1.0f;
    blink.maxValue = 1.0f;
    rig.parameters = {angle, blink};

    ParamValues2D values = {5.0f};
    clampParameters2D(rig, values);
    REQUIRE(values.size() == 2);
    REQUIRE(near(values[0], 1.0f));
    REQUIRE(near(values[1], 1.0f));

    REQUIRE(rig.findParameter("Blink") == 1);
    REQUIRE(rig.findParameter("Nope") == -1);
}

TEST_CASE("evaluateBonePose2D - bindings add curve offsets to rest values", "[animation][skeleton2d]") {
    Rig2D rig;
    Bone2D head;
    head.restPosition = Vec2(1.0f, 2.0f);
    rig.skeleton.bones = {head};
    Parameter2D p;
    p.minValue = -1.0f;
    p.maxValue = 1.0f;
    rig.parameters = {p};

    ParamBinding2D move;
    move.parameter = 0;
    move.bone = 0;
    move.channel = BoneChannel2D::TranslateX;
    move.keys = {{-1.0f, -0.5f}, {1.0f, 0.5f}};
    rig.bindings = {move};

    const auto atZero = evaluateBonePose2D(rig, {0.0f});
    REQUIRE(near(atZero[0].position, Vec2(1.0f, 2.0f)));

    const auto atOne = evaluateBonePose2D(rig, {1.0f});
    REQUIRE(near(atOne[0].position, Vec2(1.5f, 2.0f)));

    // Out-of-range input is clamped before sampling.
    const auto beyond = evaluateBonePose2D(rig, {9.0f});
    REQUIRE(near(beyond[0].position, Vec2(1.5f, 2.0f)));
}

// ============================================================================
// Skinning
// ============================================================================

static Rig2D makeTwoBoneRig() {
    Rig2D rig;
    Bone2D root;
    root.name = "root";
    Bone2D tip;
    tip.name = "tip";
    tip.parentIndex = 0;
    tip.restPosition = Vec2(2.0f, 0.0f);
    rig.skeleton.bones = {root, tip};

    Vertex2D a;
    a.position = Vec2(0.5f, 0.2f);
    a.influences = {{0, 1.0f}};
    Vertex2D b;
    b.position = Vec2(2.5f, 0.3f);
    b.influences = {{1, 1.0f}};
    Vertex2D mid;
    mid.position = Vec2(1.5f, 0.0f);
    mid.influences = {{0, 0.5f}, {1, 0.5f}};
    rig.vertices = {a, b, mid};
    return rig;
}

TEST_CASE("skinMesh2D - rest pose returns rest positions", "[animation][skeleton2d]") {
    Rig2D rig = makeTwoBoneRig();
    const auto out = skinMesh2D(rig, defaultParameters2D(rig));
    REQUIRE(out.size() == rig.vertices.size());
    for (usize i = 0; i < out.size(); ++i) {
        REQUIRE(near(out[i], rig.vertices[i].position));
    }
}

TEST_CASE("skinMesh2D - rotating a root moves its vertices around the pivot", "[animation][skeleton2d]") {
    Rig2D rig = makeTwoBoneRig();
    Parameter2D bend;
    bend.name = "Bend";
    bend.minValue = 0.0f;
    bend.maxValue = 1.0f;
    rig.parameters = {bend};

    ParamBinding2D turn;
    turn.parameter = 0;
    turn.bone = 0;
    turn.channel = BoneChannel2D::Rotation;
    turn.keys = {{0.0f, 0.0f}, {1.0f, kPi * 0.5f}};
    rig.bindings = {turn};

    // Only vertex 0 is weighted to the root. It sits at (0.5,0.2) and rotates a quarter turn.
    rig.vertices = {rig.vertices[0]};
    const auto out = skinMesh2D(rig, {1.0f});
    REQUIRE(near(out[0], Vec2(-0.2f, 0.5f)));

    // Out-of-range parameter values clamp to the maximum.
    const auto clamped = skinMesh2D(rig, {5.0f});
    REQUIRE(near(clamped[0], out[0]));
}

TEST_CASE("skinMesh2D - weights blend between bones", "[animation][skeleton2d]") {
    Rig2D rig;
    Bone2D turning;
    turning.name = "turning";
    Bone2D still;
    still.name = "still";
    rig.skeleton.bones = {turning, still};

    Parameter2D p;
    p.minValue = 0.0f;
    p.maxValue = 1.0f;
    rig.parameters = {p};
    ParamBinding2D turn;
    turn.parameter = 0;
    turn.bone = 0;
    turn.channel = BoneChannel2D::Rotation;
    turn.keys = {{0.0f, 0.0f}, {1.0f, kPi * 0.5f}};
    rig.bindings = {turn};

    Vertex2D v;
    v.position = Vec2(1.0f, 0.0f);
    v.influences = {{0, 0.5f}, {1, 0.5f}};
    rig.vertices = {v};

    // Half the vertex follows the quarter turn (0,1), half stays at (1,0).
    const auto out = skinMesh2D(rig, {1.0f});
    REQUIRE(near(out[0], Vec2(0.5f, 0.5f)));
}

TEST_CASE("skinMesh2D - vertex without influences keeps its rest position", "[animation][skeleton2d]") {
    Rig2D rig = makeTwoBoneRig();
    Vertex2D loose;
    loose.position = Vec2(9.0f, 9.0f);
    rig.vertices = {loose};
    const auto out = skinMesh2D(rig, defaultParameters2D(rig));
    REQUIRE(near(out[0], Vec2(9.0f, 9.0f)));
}

TEST_CASE("normalizeInfluences2D - keeps 4 largest and normalizes", "[animation][skeleton2d]") {
    Vertex2D v;
    v.influences = {{0, 0.5f}, {1, 0.0f}, {2, 0.3f}, {3, 0.1f}, {4, 0.05f}, {5, 0.2f}};
    normalizeInfluences2D(v);
    REQUIRE(v.influences.size() == 4);
    REQUIRE(v.influences[0].bone == 0);

    f32 sum = 0.0f;
    for (const BoneInfluence2D& inf : v.influences) {
        REQUIRE(inf.weight > 0.0f);
        sum += inf.weight;
    }
    REQUIRE(near(sum, 1.0f));
}

// ============================================================================
// Parameter clips
// ============================================================================

TEST_CASE("sampleParamClip2D - looping clip wraps time", "[animation][skeleton2d]") {
    ParamClip2D clip;
    clip.duration = 1.0f;
    clip.loop = true;
    ParamTrack2D track;
    track.parameter = 0;
    track.keys = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    clip.tracks = {track};

    ParamValues2D values = {0.0f};
    sampleParamClip2D(clip, 1.25f, values);
    REQUIRE(near(values[0], 0.25f));
}

TEST_CASE("sampleParamClip2D - non-looping clip clamps at the end", "[animation][skeleton2d]") {
    ParamClip2D clip;
    clip.duration = 1.0f;
    clip.loop = false;
    ParamTrack2D track;
    track.parameter = 0;
    track.keys = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    clip.tracks = {track};

    ParamValues2D values = {0.0f};
    sampleParamClip2D(clip, 3.0f, values);
    REQUIRE(near(values[0], 1.0f));
}

TEST_CASE("sampleParamClip2D - tracks outside the value array are skipped", "[animation][skeleton2d]") {
    ParamClip2D clip;
    clip.duration = 1.0f;
    ParamTrack2D track;
    track.parameter = 7;
    track.keys = {{0.0f, 3.0f}};
    clip.tracks = {track};

    ParamValues2D values = {0.5f};
    sampleParamClip2D(clip, 0.0f, values);
    REQUIRE(values.size() == 1);
    REQUIRE(near(values[0], 0.5f));
}

// ============================================================================
// Smoothing (PhaseShift parameter smoothing)
// ============================================================================

TEST_CASE("smoothDamp - converges to target without overshoot", "[animation][smoothing]") {
    f32 value = 0.0f;
    f32 velocity = 0.0f;
    f32 previous = value;
    const f32 dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 180; ++frame) {
        value = smoothDamp(value, 1.0f, velocity, 0.2f, dt);
        REQUIRE(value >= previous - 1e-6f);
        REQUIRE(value <= 1.0f + 1e-6f);
        previous = value;
    }
    REQUIRE(near(value, 1.0f));
}

TEST_CASE("smoothDamp - zero dt keeps the current value", "[animation][smoothing]") {
    f32 velocity = 2.0f;
    REQUIRE(near(smoothDamp(0.3f, 1.0f, velocity, 0.2f, 0.0f), 0.3f));
    REQUIRE(near(velocity, 2.0f));
}

TEST_CASE("smoothDamp - very large dt snaps to target without NaN", "[animation][smoothing]") {
    f32 velocity = 0.0f;
    const f32 out = smoothDamp(0.0f, 1.0f, velocity, 0.2f, 10.0f);
    REQUIRE(std::isfinite(out));
    REQUIRE(near(out, 1.0f));
}

TEST_CASE("exponentialApproach - half life matches the rate", "[animation][smoothing]") {
    const f32 halfway = exponentialApproach(0.0f, 1.0f, 0.6931472f, 1.0f);
    REQUIRE(near(halfway, 0.5f));
    REQUIRE(near(exponentialApproach(0.0f, 1.0f, 0.0f, 1.0f), 0.0f));
}

// ============================================================================
// Rig asset files and grid mesh
// ============================================================================

static Rig2D makeRootRig() {
    Rig2D rig;
    Bone2D root;
    root.name = "root";
    rig.skeleton.bones = {root};

    Parameter2D bend;
    bend.name = "Bend";
    bend.minValue = 0.0f;
    bend.maxValue = 1.0f;
    rig.parameters = {bend};

    ParamBinding2D turn;
    turn.parameter = 0;
    turn.bone = 0;
    turn.channel = BoneChannel2D::Rotation;
    turn.keys = {{0.0f, 0.0f}, {1.0f, kPi * 0.5f}};
    rig.bindings = {turn};

    Vertex2D v;
    v.position = Vec2(1.0f, 0.0f);
    v.uv = Vec2(0.0f, 0.5f);
    v.influences = {{0, 1.0f}};
    rig.vertices = {v};
    return rig;
}

TEST_CASE("Rig2D grid - 2x2 cells give 9 vertices and 8 triangles", "[animation][skeleton2d]") {
    Rig2D rig;
    buildGridMesh2D(rig, 2, 2);
    REQUIRE(rig.vertices.size() == 9);
    REQUIRE(rig.indices.size() == 24);
    REQUIRE(near(rig.vertices[0].position, Vec2(-0.5f, -0.5f)));
    REQUIRE(near(rig.vertices[8].position, Vec2(0.5f, 0.5f)));
    REQUIRE(near(rig.vertices[8].uv, Vec2(1.0f, 1.0f)));
    for (u32 index : rig.indices) REQUIRE(index < rig.vertices.size());
}

TEST_CASE("Rig2D grid - binding to a bone gives full weight to every vertex", "[animation][skeleton2d]") {
    Rig2D rig;
    buildGridMesh2D(rig, 1, 1);
    bindVerticesToBone2D(rig, 3);
    for (const Vertex2D& v : rig.vertices) {
        REQUIRE(v.influences.size() == 1);
        REQUIRE(v.influences[0].bone == 3);
        REQUIRE(near(v.influences[0].weight, 1.0f));
    }
}

TEST_CASE("Rig2D asset - save and load keep bones, mesh, parameters and bindings", "[animation][skeleton2d]") {
    Rig2D rig = makeRootRig();
    buildGridMesh2D(rig, 1, 1);
    bindVerticesToBone2D(rig, 0);
    const auto path = std::filesystem::temp_directory_path() / "caffeine_test_roundtrip.rig2d";
    REQUIRE(saveRig2D(path, rig));

    Rig2D loaded;
    REQUIRE(loadRig2D(path, loaded));
    REQUIRE(loaded.skeleton.boneCount() == 1);
    REQUIRE(loaded.skeleton.bones[0].name == "root");
    REQUIRE(loaded.vertices.size() == 4);
    REQUIRE(loaded.indices.size() == 6);
    REQUIRE(loaded.indices == rig.indices);
    REQUIRE(loaded.vertices[1].influences.size() == 1);
    REQUIRE(loaded.parameters.size() == 1);
    REQUIRE(loaded.parameters[0].name == "Bend");
    REQUIRE(loaded.bindings.size() == 1);
    REQUIRE(loaded.bindings[0].keys.size() == 2);
    REQUIRE(near(loaded.bindings[0].keys[1].y, kPi * 0.5f));
    std::filesystem::remove(path);
}

TEST_CASE("Rig2D asset - out-of-range indices are rejected", "[animation][skeleton2d]") {
    const auto path = std::filesystem::temp_directory_path() / "caffeine_test_bad.rig2d";
    {
        std::ofstream out(path);
        out << "CAFRIG2D1\nbone root -1 0 0 0 1 1\nvert 0 0 0 0 0\ntri 0 0 5\n";
    }
    Rig2D loaded;
    REQUIRE_FALSE(loadRig2D(path, loaded));
    std::filesystem::remove(path);
}

TEST_CASE("ParamClip2D asset - tracks resolve by parameter name", "[animation][skeleton2d]") {
    Rig2D rig = makeRootRig();
    ParamClip2D clip;
    clip.name = "wave";
    clip.duration = 1.0f;
    clip.loop = false;
    ParamTrack2D track;
    track.parameter = 0;
    track.keys = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    clip.tracks = {track};

    const auto path = std::filesystem::temp_directory_path() / "caffeine_test_roundtrip.pclip";
    REQUIRE(saveParamClip2D(path, clip, rig));

    ParamClip2D loaded;
    REQUIRE(loadParamClip2D(path, loaded, rig));
    REQUIRE(loaded.tracks.size() == 1);
    REQUIRE(loaded.loop == false);
    ParamValues2D values = defaultParameters2D(rig);
    sampleParamClip2D(loaded, 0.5f, values);
    REQUIRE(near(values[0], 0.5f));

    // A rig without the parameter drops the track instead of failing.
    Rig2D other;
    ParamClip2D unresolved;
    REQUIRE(loadParamClip2D(path, unresolved, other));
    REQUIRE(unresolved.tracks.empty());
    std::filesystem::remove(path);
}

// ============================================================================
// Runtime: PhaseShift parameters drive a rig
// ============================================================================

TEST_CASE("PhaseShift - smoothed float gates a transition until the spring catches up", "[animation][smoothing]") {
    ECS::World world;
    AnimationSystem sys;

    AnimationClip clip;
    clip.frames = {{0.0f, 0.0f, 16.0f, 16.0f}};
    clip.fps = 12;

    PhaseShift ps;
    ps.addParameter("speed", ParameterType::Float);
    ps.setSmoothing("speed", 0.5f);   // smoothed starts at 0
    ps.setFloat("speed", 1.0f);       // target is 1

    AnimationTransition toRun;
    toRun.toState = "run";
    toRun.blendTime = 0.0f;
    TransitionCondition cond;
    cond.parameterName = "speed";
    cond.op = ConditionOperator::Greater;
    cond.floatValue = 0.5f;
    toRun.conditions = {cond};

    AnimationState idle;
    idle.name = "idle";
    idle.clip = &clip;
    idle.transitions = {toRun};
    AnimationState run;
    run.name = "run";
    run.clip = &clip;

    ps.states.set(FixedString<32>("idle"), idle);
    ps.states.set(FixedString<32>("run"), run);
    ps.defaultState = "idle";
    ps.currentState = "idle";

    ECS::Entity e = world.create();
    world.add<PhaseShift>(e, ps);
    world.add<ECS::Sprite>(e, ECS::Sprite{});

    const f32 dt = 1.0f / 60.0f;
    sys.onUpdate(world, dt);
    REQUIRE(world.get<PhaseShift>(e)->currentState == "idle");

    for (int frame = 0; frame < 120; ++frame) {
        sys.onUpdate(world, dt);
    }
    REQUIRE(world.get<PhaseShift>(e)->currentState == "run");
}

TEST_CASE("Rig2DPlayer - PhaseShift float overrides the clip, the clip drives without it", "[animation][skeleton2d]") {
    const auto dir = std::filesystem::temp_directory_path();
    const auto rigPath = dir / "caffeine_test_runtime.rig2d";
    const auto clipPath = dir / "caffeine_test_runtime.pclip";

    Rig2D rig = makeRootRig();
    REQUIRE(saveRig2D(rigPath, rig));
    // The clip holds Bend at 0 the whole time.
    ParamClip2D clip;
    clip.duration = 1.0f;
    clip.loop = true;
    ParamTrack2D hold;
    hold.parameter = 0;
    hold.keys = {{0.0f, 0.0f}, {1.0f, 0.0f}};
    clip.tracks = {hold};
    REQUIRE(saveParamClip2D(clipPath, clip, rig));

    ECS::World world;
    AnimationSystem sys;

    // Entity A: PhaseShift sets Bend to 1, which overrides the clip's 0.
    ECS::Entity a = world.create();
    REQUIRE(attachRig2D(world, a, rigPath.string()));
    REQUIRE(playRig2DClip(world, a, clipPath.string()));
    PhaseShift ps;
    ps.addParameter("Bend", ParameterType::Float);
    ps.setFloat("Bend", 1.0f);
    world.add<PhaseShift>(a, ps);

    // Entity B: no PhaseShift, so the clip's 0 holds and the rig stays at rest.
    ECS::Entity b = world.create();
    REQUIRE(attachRig2D(world, b, rigPath.string()));
    REQUIRE(playRig2DClip(world, b, clipPath.string()));

    sys.onUpdate(world, 0.0f);

    const Rig2DPlayer* pa = world.get<Rig2DPlayer>(a);
    const Rig2DPlayer* pb = world.get<Rig2DPlayer>(b);
    REQUIRE(pa->skinned.size() == 1);
    REQUIRE(near(pa->skinned[0], Vec2(0.0f, 1.0f)));
    REQUIRE(near(pb->skinned[0], Vec2(1.0f, 0.0f)));

    std::filesystem::remove(rigPath);
    std::filesystem::remove(clipPath);
}

TEST_CASE("AnimationApi - attaching without a path or to nothing fails cleanly", "[animation][skeleton2d]") {
    ECS::World world;
    ECS::Entity e = world.create();
    REQUIRE_FALSE(attachRig2D(world, e, ""));
    REQUIRE_FALSE(playRig2DClip(world, e, "clip.pclip"));
    REQUIRE_FALSE(setPhaseShiftSmoothing(world, e, "speed", 0.2f));
}

// ============================================================================
// Renderer: mesh submission (needs the SDL3 render build)
// ============================================================================

#ifdef CF_HAS_SDL3
TEST_CASE("BatchRenderer - mesh vertices go through the transform and the uv rect", "[render][skeleton2d]") {
    Render::BatchRenderer renderer(nullptr);
    renderer.beginFrame();
    const std::vector<Vec2> positions = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    const std::vector<Vec2> uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    renderer.submitMesh2D(Mat4::translation(10.0f, 0.0f, 0.0f), positions, uvs, {0u, 1u, 2u},
                          Vec4(0.0f, 0.0f, 1.0f, 1.0f), 0xFF00FF00u);

    std::vector<Render::SpriteVertex> verts;
    std::vector<u32> indices;
    renderer.buildFrame(verts, indices);

    REQUIRE(verts.size() == 3);
    REQUIRE(indices.size() == 3);
    REQUIRE(near(verts[1].position.x, 11.0f));
    REQUIRE(near(verts[1].position.y, 0.0f));
    REQUIRE(verts[1].tint == 0xFF00FF00u);
    // uv (1,0) maps to the right, bottom edge of the rect; uv (0,1) to the top-left.
    REQUIRE(near(verts[1].texcoord.x, 1.0f));
    REQUIRE(near(verts[1].texcoord.y, 1.0f));
    REQUIRE(near(verts[2].texcoord.x, 0.0f));
    REQUIRE(near(verts[2].texcoord.y, 0.0f));
}

TEST_CASE("BatchRenderer - triangles with missing vertices are dropped, beginFrame clears meshes", "[render][skeleton2d]") {
    Render::BatchRenderer renderer(nullptr);
    renderer.beginFrame();
    const std::vector<Vec2> positions = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    renderer.submitMesh2D(Mat4::identity(), positions, {}, {0u, 1u, 2u, 0u, 1u, 9u}, Vec4(0.0f, 0.0f, 1.0f, 1.0f));

    std::vector<Render::SpriteVertex> verts;
    std::vector<u32> indices;
    renderer.buildFrame(verts, indices);
    REQUIRE(indices.size() == 3);

    renderer.beginFrame();
    renderer.buildFrame(verts, indices);
    REQUIRE(verts.empty());
    REQUIRE(indices.empty());
}
#endif

#ifdef CF_HAS_SDL3
TEST_CASE("Rig2DOverlay - an entity's placement becomes translation, rotation and scale", "[render][skeleton2d]") {
    ECS::World world;
    ECS::Entity e = world.create();
    world.add<ECS::Position3D>(e, ECS::Position3D{Vec3(1.0f, 2.0f, 3.0f)});
    world.add<ECS::Scale3D>(e, ECS::Scale3D{Vec3(2.0f, 2.0f, 1.0f)});
    const Mat4 m = Render::entityPlacementMatrix(world, e);
    // A point at the origin moves to the position.
    const Vec3 origin = m.transformPoint(Vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(near(origin.x, 1.0f));
    REQUIRE(near(origin.y, 2.0f));
    REQUIRE(near(origin.z, 3.0f));
    // A unit point along x is scaled by 2 before the translation.
    const Vec3 right = m.transformPoint(Vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(near(right.x, 3.0f));
}

TEST_CASE("Rig2DOverlay - without a GPU pipeline nothing is drawn and nothing is counted", "[render][skeleton2d]") {
    ECS::World world;
    ECS::Entity e = world.create();
    Animation::Rig2DPlayer player;
    player.skinned = {Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(0.0f, 1.0f)};
    player.rig.vertices.resize(3);
    player.rig.indices = {0u, 1u, 2u};
    world.add<Animation::Rig2DPlayer>(e, std::move(player));

    Render::Scene2DOverlay overlay;
    REQUIRE(overlay.draw(nullptr, nullptr, world, Mat4::identity(), "") == 0);
    REQUIRE_FALSE(overlay.attach(nullptr, RHI::TextureFormat::R8G8B8A8_UNORM));
    REQUIRE(overlay.draw(nullptr, nullptr, world, Mat4::identity(), "") == 0);
}
#endif
