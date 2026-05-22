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

    void addParameter(ECS::World& world, ECS::Entity e, const char* name, ParameterType type) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->addParameter(name, type);
    }

    void setBool(ECS::World& world, ECS::Entity e, const char* name, bool value) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->setBool(name, value);
    }

    void setFloat(ECS::World& world, ECS::Entity e, const char* name, f32 value) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->setFloat(name, value);
    }

    void setInt(ECS::World& world, ECS::Entity e, const char* name, i32 value) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->setInt(name, value);
    }

    void setTrigger(ECS::World& world, ECS::Entity e, const char* name) {
        Animator* anim = world.get<Animator>(e);
        if (anim) anim->setTrigger(name);
    }

    bool getBool(ECS::World& world, ECS::Entity e, const char* name) const {
        const Animator* anim = world.get<Animator>(e);
        return anim ? anim->getBool(name) : false;
    }

    f32 getFloat(ECS::World& world, ECS::Entity e, const char* name) const {
        const Animator* anim = world.get<Animator>(e);
        return anim ? anim->getFloat(name) : 0.0f;
    }

    i32 getInt(ECS::World& world, ECS::Entity e, const char* name) const {
        const Animator* anim = world.get<Animator>(e);
        return anim ? anim->getInt(name) : 0;
    }

private:
    static bool evaluateCondition(const TransitionCondition& cond, const Animator& anim) {
        const AnimatorParameter* p = anim.findParameter(cond.parameterName);
        if (!p) return false;

        switch (p->type) {
            case ParameterType::Bool:
                return cond.op == ConditionOperator::Equals
                    ? (p->boolValue == cond.boolValue)
                    : (p->boolValue != cond.boolValue);

            case ParameterType::Trigger:
                return p->triggered;

            case ParameterType::Float:
                switch (cond.op) {
                    case ConditionOperator::Greater:        return p->floatValue >  cond.floatValue;
                    case ConditionOperator::Less:           return p->floatValue <  cond.floatValue;
                    case ConditionOperator::GreaterOrEqual: return p->floatValue >= cond.floatValue;
                    case ConditionOperator::LessOrEqual:    return p->floatValue <= cond.floatValue;
                    case ConditionOperator::Equals:         return p->floatValue == cond.floatValue;
                    case ConditionOperator::NotEquals:      return p->floatValue != cond.floatValue;
                }
                break;

            case ParameterType::Int:
                switch (cond.op) {
                    case ConditionOperator::Equals:         return p->intValue == cond.intValue;
                    case ConditionOperator::NotEquals:      return p->intValue != cond.intValue;
                    case ConditionOperator::Greater:        return p->intValue >  cond.intValue;
                    case ConditionOperator::Less:           return p->intValue <  cond.intValue;
                    case ConditionOperator::GreaterOrEqual: return p->intValue >= cond.intValue;
                    case ConditionOperator::LessOrEqual:    return p->intValue <= cond.intValue;
                }
                break;
        }
        return false;
    }

    static void consumeTriggers(Animator& anim) {
        for (auto& p : anim.parameters) {
            if (p.type == ParameterType::Trigger) {
                p.triggered = false;
            }
        }
    }

    static void evaluateTransitions(Animator& anim) {
        const AnimationState* state = anim.states.get(anim.currentState);
        if (!state) return;

        for (const auto& t : state->transitions) {
            if (t.hasExitTime && state->clip) {
                if (anim.timeInState < state->clip->duration()) continue;
            }

            bool conditionsMet = false;

            if (!t.conditions.empty()) {
                conditionsMet = true;
                for (const auto& cond : t.conditions) {
                    if (!evaluateCondition(cond, anim)) {
                        conditionsMet = false;
                        break;
                    }
                }
            } else if (t.legacyCondition) {
                conditionsMet = t.legacyCondition();
            }

            if (conditionsMet) {
                anim.previousState = anim.currentState;
                anim.currentState  = t.toState;
                anim.timeInState   = 0.0f;
                consumeTriggers(anim);
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