#include "catch.hpp"
#include "../src/Caffeine.hpp"
#include "../src/animation/AnimationComponents.hpp"
#include "../src/animation/AnimationSystem.hpp"

#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;
using namespace Caffeine::Animation;

static constexpr f32 kEps = 0.001f;
static bool approxEq(f32 a, f32 b) { return std::fabs(a - b) < kEps; }

// ============================================================================
// FrameRect
// ============================================================================

TEST_CASE("FrameRect - default values", "[animation]") {
    FrameRect f;
    REQUIRE(approxEq(f.x, 0.0f));
    REQUIRE(approxEq(f.y, 0.0f));
    REQUIRE(approxEq(f.w, 0.0f));
    REQUIRE(approxEq(f.h, 0.0f));
}

// ============================================================================
// AnimationClip
// ============================================================================

TEST_CASE("AnimationClip - default values", "[animation]") {
    AnimationClip clip;
    REQUIRE(clip.fps  == 12u);
    REQUIRE(clip.loop == true);
    REQUIRE(clip.frames.empty());
}

TEST_CASE("AnimationClip - duration is zero when no frames", "[animation]") {
    AnimationClip clip;
    REQUIRE(approxEq(clip.duration(), 0.0f));
}

TEST_CASE("AnimationClip - duration is zero when fps is zero", "[animation]") {
    AnimationClip clip;
    clip.fps = 0;
    clip.frames.push_back({});
    REQUIRE(approxEq(clip.duration(), 0.0f));
}

TEST_CASE("AnimationClip - duration computed correctly", "[animation]") {
    AnimationClip clip;
    clip.fps = 4;
    clip.frames = {{}, {}, {}, {}};
    REQUIRE(approxEq(clip.duration(), 1.0f));
}

TEST_CASE("AnimationClip - duration with 3 frames at 12fps", "[animation]") {
    AnimationClip clip;
    clip.fps = 12;
    clip.frames = {{}, {}, {}};
    REQUIRE(approxEq(clip.duration(), 0.25f));
}

// ============================================================================
// AnimationTransition
// ============================================================================

TEST_CASE("AnimationTransition - defaults", "[animation]") {
    AnimationTransition t;
    REQUIRE(approxEq(t.blendTime, 0.1f));
    REQUIRE(t.hasExitTime == false);
    REQUIRE(!t.condition);
}

// ============================================================================
// AnimationState
// ============================================================================

TEST_CASE("AnimationState - defaults", "[animation]") {
    AnimationState s;
    REQUIRE(s.clip == nullptr);
    REQUIRE(approxEq(s.speed, 1.0f));
    REQUIRE(s.transitions.empty());
}

// ============================================================================
// Animator
// ============================================================================

TEST_CASE("Animator - defaults", "[animation]") {
    Animator anim;
    REQUIRE(approxEq(anim.timeInState,   0.0f));
    REQUIRE(approxEq(anim.blendWeight,   1.0f));
    REQUIRE(approxEq(anim.playbackScale, 1.0f));
    REQUIRE(anim.paused == false);
    REQUIRE(anim.frameEvents.empty());
    REQUIRE(!anim.onFrameEvent);
}

// ============================================================================
// AnimationSystem — logic tests via ECS World
// ============================================================================

static AnimationClip makeClip(u32 frameCount, u32 fps, bool loop = true) {
    AnimationClip clip;
    clip.fps  = fps;
    clip.loop = loop;
    for (u32 i = 0; i < frameCount; ++i) {
        clip.frames.push_back({static_cast<f32>(i) * 16.0f, 0.0f, 16.0f, 16.0f});
    }
    return clip;
}

TEST_CASE("AnimationSystem - entity with no Sprite is skipped safely", "[animation]") {
    World world;
    AnimationSystem sys;
    Entity e = world.create();
    Animator anim;
    world.add<Animator>(e, anim);
    sys.onUpdate(world, 0.016f);
    REQUIRE(true);
}

TEST_CASE("AnimationSystem - entity with no Animator is skipped safely", "[animation]") {
    World world;
    AnimationSystem sys;
    Entity e = world.create();
    world.add<Sprite>(e, Sprite{});
    sys.onUpdate(world, 0.016f);
    REQUIRE(true);
}

TEST_CASE("AnimationSystem - empty Animator does not crash on update", "[animation]") {
    World world;
    AnimationSystem sys;
    Entity e = world.create();
    world.add<Animator>(e, Animator{});
    world.add<Sprite>(e, Sprite{});
    sys.onUpdate(world, 0.016f);
    REQUIRE(true);
}

TEST_CASE("AnimationSystem - paused Animator does not advance time", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip clip = makeClip(4, 4);
    AnimationState state;
    state.name = "idle";
    state.clip = &clip;

    Animator anim;
    anim.states.set(FixedString<32>("idle"), state);
    anim.currentState = "idle";
    anim.paused       = true;

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 0.5f);

    const Animator* a = world.get<Animator>(e);
    REQUIRE(approxEq(a->timeInState, 0.0f));
}

TEST_CASE("AnimationSystem - advances frameIndex with time", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip clip = makeClip(4, 4, true);
    AnimationState state;
    state.name = "idle";
    state.clip = &clip;

    Animator anim;
    anim.states.set(FixedString<32>("idle"), state);
    anim.currentState = "idle";

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 0.5f);

    const Sprite* sp = world.get<Sprite>(e);
    REQUIRE(sp->frameIndex < 4u);
}

TEST_CASE("AnimationSystem - non-looping clip clamps at last frame", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip clip = makeClip(4, 4, false);
    AnimationState state;
    state.name = "attack";
    state.clip = &clip;

    Animator anim;
    anim.states.set(FixedString<32>("attack"), state);
    anim.currentState = "attack";

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 10.0f);

    const Sprite* sp = world.get<Sprite>(e);
    REQUIRE(sp->frameIndex == 3u);
}

TEST_CASE("AnimationSystem - looping clip wraps frame index", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip clip = makeClip(4, 4, true);
    AnimationState state;
    state.name = "walk";
    state.clip = &clip;

    Animator anim;
    anim.states.set(FixedString<32>("walk"), state);
    anim.currentState = "walk";

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 1.125f);

    const Sprite* sp = world.get<Sprite>(e);
    REQUIRE(sp->frameIndex < 4u);
}

TEST_CASE("AnimationSystem - transition fires when condition is true", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip idleClip = makeClip(2, 4, true);
    AnimationClip walkClip = makeClip(4, 8, true);

    AnimationState idle;
    idle.name = "idle";
    idle.clip = &idleClip;
    AnimationTransition toWalk;
    toWalk.toState   = "walk";
    toWalk.condition = []() { return true; };
    idle.transitions.push_back(toWalk);

    AnimationState walk;
    walk.name = "walk";
    walk.clip = &walkClip;

    Animator anim;
    anim.states.set(FixedString<32>("idle"), idle);
    anim.states.set(FixedString<32>("walk"), walk);
    anim.currentState = "idle";

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 0.016f);

    const Animator* a = world.get<Animator>(e);
    REQUIRE(a->currentState == FixedString<32>("walk"));
}

TEST_CASE("AnimationSystem - transition does not fire when condition is false", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip idleClip = makeClip(2, 4, true);

    AnimationState idle;
    idle.name = "idle";
    idle.clip = &idleClip;
    AnimationTransition t;
    t.toState   = "walk";
    t.condition = []() { return false; };
    idle.transitions.push_back(t);

    Animator anim;
    anim.states.set(FixedString<32>("idle"), idle);
    anim.currentState = "idle";

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 0.016f);

    const Animator* a = world.get<Animator>(e);
    REQUIRE(a->currentState == FixedString<32>("idle"));
}

TEST_CASE("AnimationSystem - frame event fires at correct frame", "[animation]") {
    World world;
    AnimationSystem sys;

    AnimationClip clip = makeClip(4, 1000, true);

    AnimationState state;
    state.name = "action";
    state.clip = &clip;

    Animator anim;
    anim.states.set(FixedString<32>("action"), state);
    anim.currentState = "action";
    anim.frameEvents.push_back({0u, FixedString<32>("hit")});

    FixedString<32> fired;
    anim.onFrameEvent = [&](const FixedString<32>& evt) { fired = evt; };

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.onUpdate(world, 0.0f);

    const Animator* a = world.get<Animator>(e);
    REQUIRE(a->currentState == FixedString<32>("action"));
}

TEST_CASE("AnimationSystem - play() resets state", "[animation]") {
    World world;
    AnimationSystem sys;

    Entity e = world.create();
    world.add<Animator>(e, Animator{});
    world.add<Sprite>(e, Sprite{});

    sys.play(world, e, "walk");

    const Animator* a = world.get<Animator>(e);
    REQUIRE(a->currentState == FixedString<32>("walk"));
    REQUIRE(approxEq(a->timeInState, 0.0f));
    REQUIRE(a->paused == false);
}

TEST_CASE("AnimationSystem - pause and resume", "[animation]") {
    World world;
    AnimationSystem sys;

    Entity e = world.create();
    world.add<Animator>(e, Animator{});
    world.add<Sprite>(e, Sprite{});

    sys.pause(world, e);
    REQUIRE(world.get<Animator>(e)->paused == true);

    sys.resume(world, e);
    REQUIRE(world.get<Animator>(e)->paused == false);
}

TEST_CASE("AnimationSystem - setSpeed updates playbackScale", "[animation]") {
    World world;
    AnimationSystem sys;

    Entity e = world.create();
    world.add<Animator>(e, Animator{});
    world.add<Sprite>(e, Sprite{});

    sys.setSpeed(world, e, 2.0f);
    REQUIRE(approxEq(world.get<Animator>(e)->playbackScale, 2.0f));
}

TEST_CASE("AnimationSystem - isPlaying returns true for active state", "[animation]") {
    World world;
    AnimationSystem sys;

    Animator anim;
    anim.currentState = "idle";
    anim.paused       = false;

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    REQUIRE(sys.isPlaying(world, e, "idle") == true);
    REQUIRE(sys.isPlaying(world, e, "walk") == false);
}

TEST_CASE("AnimationSystem - isPlaying returns false when paused", "[animation]") {
    World world;
    AnimationSystem sys;

    Animator anim;
    anim.currentState = "idle";
    anim.paused       = true;

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    REQUIRE(sys.isPlaying(world, e, "idle") == false);
}

TEST_CASE("AnimationSystem - play() stores previousState", "[animation]") {
    World world;
    AnimationSystem sys;

    Animator anim;
    anim.currentState = "idle";

    Entity e = world.create();
    world.add<Animator>(e, anim);
    world.add<Sprite>(e, Sprite{});

    sys.play(world, e, "walk");

    const Animator* a = world.get<Animator>(e);
    REQUIRE(a->previousState == FixedString<32>("idle"));
    REQUIRE(a->currentState  == FixedString<32>("walk"));
}
