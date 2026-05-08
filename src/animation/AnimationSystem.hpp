#pragma once

#include "animation/AnimationComponents.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"

#include <cmath>

namespace Caffeine::Animation {

using namespace Caffeine;

class AnimationSystem : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override {
        ECS::ComponentQuery q;
        q.with<Animator>();
        q.with<ECS::Sprite>();

        world.forEach<Animator, ECS::Sprite>(q,
            [dt](ECS::Entity, Animator& anim, ECS::Sprite& sprite) {
                if (anim.paused) return;

                evaluateTransitions(anim);

                const AnimationState* state = anim.states.get(anim.currentState);
                if (!state || !state->clip || state->clip->frames.empty()) return;

                const AnimationClip* clip = state->clip;
                f32 effectiveSpeed = state->speed * anim.playbackScale;
                anim.timeInState += dt * effectiveSpeed;

                f32 clipDur = clip->duration();
                if (clipDur > 0.0f) {
                    if (anim.timeInState >= clipDur) {
                        if (clip->loop) {
                            anim.timeInState = std::fmod(anim.timeInState, clipDur);
                        } else {
                            anim.timeInState = clipDur;
                        }
                    }
                }

                u32 frameCount = static_cast<u32>(clip->frames.size());
                u32 frame      = 0;
                if (clip->fps > 0 && frameCount > 0) {
                    frame = static_cast<u32>(anim.timeInState * static_cast<f32>(clip->fps));
                    if (clip->loop) {
                        frame = frame % frameCount;
                    } else {
                        frame = frame < frameCount ? frame : frameCount - 1u;
                    }
                }
                sprite.frameIndex = frame;

                if (anim.onFrameEvent) {
                    checkFrameEvents(anim, frame);
                }
            });
    }

    void play(ECS::World& world, ECS::Entity e, const char* stateName) {
        Animator* anim = world.get<Animator>(e);
        if (!anim) return;
        anim->previousState = anim->currentState;
        anim->currentState  = stateName;
        anim->timeInState   = 0.0f;
        anim->paused        = false;
    }

    void pause(ECS::World& world, ECS::Entity e) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->paused = true;
    }

    void resume(ECS::World& world, ECS::Entity e) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->paused = false;
    }

    void setSpeed(ECS::World& world, ECS::Entity e, f32 speed) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->playbackScale = speed;
    }

    bool isPlaying(ECS::World& world, ECS::Entity e, const char* stateName) const {
        const Animator* anim = world.get<Animator>(e);
        if (!anim || anim->paused) return false;
        return anim->currentState == FixedString<32>(stateName);
    }

private:
    static void evaluateTransitions(Animator& anim) {
        const AnimationState* state = anim.states.get(anim.currentState);
        if (!state) return;

        for (const auto& t : state->transitions) {
            if (!t.condition) continue;
            if (t.hasExitTime && state->clip) {
                if (anim.timeInState < state->clip->duration()) continue;
            }
            if (t.condition()) {
                anim.previousState = anim.currentState;
                anim.currentState  = t.toState;
                anim.timeInState   = 0.0f;
                return;
            }
        }
    }

    static void checkFrameEvents(Animator& anim, u32 currentFrame) {
        for (const auto& [frame, eventName] : anim.frameEvents) {
            if (frame == currentFrame) {
                anim.onFrameEvent(eventName);
            }
        }
    }
};

}  // namespace Caffeine::Animation
