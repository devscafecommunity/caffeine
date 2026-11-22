#pragma once

#include "animation/AnimationComponents.hpp"
#include "animation/AnimationPlayer.hpp"
#include "animation/Rig2DSystem.hpp"
#include "animation/SkinLibrary.hpp"
#include "animation/Smoothing.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ISystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace Caffeine::Animation {

using namespace Caffeine;

class AnimationSystem : public ECS::ISystem {
public:
    void onUpdate(ECS::World& world, f32 dt) override {
        tickParameterSmoothing(world, dt);
        tickAnimationPlayers(world, dt);
        tickSkinnedPoses(world, dt);

        ECS::ComponentQuery q;
        q.with<PhaseShift>();
        q.with<ECS::Sprite>();

        world.forEach<PhaseShift, ECS::Sprite>(q,
            [dt](ECS::Entity, PhaseShift& anim, ECS::Sprite& sprite) {
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

        ECS::ComponentQuery skeletal;
        skeletal.with<PhaseShift>();
        world.forEach<PhaseShift>(skeletal, [&world, dt](ECS::Entity entity, PhaseShift& anim) {
            if (world.has<ECS::Sprite>(entity) || anim.paused) return;
            const SkinnedPose* pose = world.get<SkinnedPose>(entity);
            const ImportedSkin* skin = pose ? findImportedSkin(pose->meshPath) : nullptr;
            stepStateMachine(anim, dt, [skin](const PhaseShiftStateMachine& sm, const FixedString<32>& name) {
                return skeletalStateDuration(skin, sm, name);
            });
        });

        tickRig2DPlayers(world, dt);
    }

    using StateDurationFn = std::function<f32(const PhaseShiftStateMachine&, const FixedString<32>&)>;

    /// Advances the base graph and every layer. Skeletal poses read the result in tickSkinnedPoses.
    static void stepStateMachine(PhaseShift& anim, f32 dt, const StateDurationFn& durationOf) {
        bool fired = stepLayer(anim, anim, dt, durationOf);
        for (PhaseShiftLayer& layer : anim.layers) {
            if (layer.isGroup) continue;
            fired = stepLayer(anim, layer, dt, durationOf) || fired;
        }
        if (fired) consumeTriggers(anim);
    }

    /// Single-duration form: every state of the base graph lasts `stateDuration`.
    static void stepStateMachine(PhaseShift& anim, f32 dt, f32 stateDuration) {
        stepStateMachine(anim, dt, [stateDuration](const PhaseShiftStateMachine&, const FixedString<32>&) {
            return stateDuration;
        });
    }

    static f32 skeletalStateDuration(const ImportedSkin* skin, const PhaseShiftStateMachine& sm,
                                     const FixedString<32>& name) {
        if (!skin || name.empty()) return 0.0f;
        const AnimationState* state = sm.states.get(name);
        const i32 clip = clipForAnimatorState(*skin, name.cStr(), state ? state->motion.cStr() : nullptr);
        if (clip < 0 || clip >= static_cast<i32>(skin->clips.size())) return 0.0f;
        return skin->clips[static_cast<size_t>(clip)].duration;
    }

    /// Moves a graph to `target` with a crossfade. Exit sends it back through Entry to the default state.
    static void enterState(PhaseShiftStateMachine& sm, const AnimationTransition& transition,
                           const StateDurationFn& durationOf) {
        FixedString<32> target = transition.toState;
        if (target == FixedString<32>(kAnimatorExitState)) target = sm.defaultState;
        if (target.empty() || !sm.states.contains(target)) return;
        if (transition.blendTime > 0.0f && !sm.currentState.empty()) {
            sm.fadeFromState = sm.currentState;
            sm.fadeFromTime = sm.timeInState;
            sm.fadeElapsed = 0.0f;
            sm.fadeDuration = transition.blendTime;
            sm.fadeCurve = transition.blendCurve;
        } else {
            sm.fadeFromState = FixedString<32>();
            sm.fadeDuration = 0.0f;
        }
        sm.previousState = sm.currentState;
        sm.currentState = target;
        const f32 duration = durationOf ? durationOf(sm, target) : 0.0f;
        sm.timeInState = duration > 0.0f ? std::clamp(transition.offset, 0.0f, 1.0f) * duration : 0.0f;
    }

    static bool stepLayer(PhaseShift& anim, PhaseShiftStateMachine& sm, f32 dt, const StateDurationFn& durationOf) {
        if (sm.currentState.empty()) {
            if (sm.defaultState.empty()) return false;
            sm.currentState = sm.defaultState;
            sm.timeInState = 0.0f;
            sm.fadeDuration = 0.0f;
        }
        const AnimationState* state = sm.states.get(sm.currentState);
        const f32 speed = state ? state->speed : 1.0f;
        const f32 step = dt * anim.playbackScale;
        sm.timeInState += step * speed;
        if (sm.fadeDuration > 0.0f) {
            const AnimationState* from = sm.states.get(sm.fadeFromState);
            sm.fadeFromTime += step * (from ? from->speed : 1.0f);
            sm.fadeElapsed += step;
            if (sm.fadeElapsed >= sm.fadeDuration) {
                sm.fadeDuration = 0.0f;
                sm.fadeElapsed = 0.0f;
                sm.fadeFromState = FixedString<32>();
            }
        }
        const f32 duration = durationOf ? durationOf(sm, sm.currentState) : -1.0f;
        for (const AnimationTransition& transition : sm.anyStateTransitions) {
            if (transition.toState == sm.currentState) continue;
            if (transitionReady(transition, anim, sm.timeInState, duration)) {
                enterState(sm, transition, durationOf);
                return true;
            }
        }
        if (!state) return false;
        for (const AnimationTransition& transition : state->transitions) {
            if (transitionReady(transition, anim, sm.timeInState, duration)) {
                const AnimationTransition chosen = transition;
                enterState(sm, chosen, durationOf);
                return true;
            }
        }
        return false;
    }

    void play(ECS::World& world, ECS::Entity e, const char* stateName) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (!anim) return;
        anim->previousState = anim->currentState;
        anim->currentState  = stateName;
        anim->timeInState   = 0.0f;
        anim->paused        = false;
    }

    void pause(ECS::World& world, ECS::Entity e) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->paused = true;
    }

    void resume(ECS::World& world, ECS::Entity e) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->paused = false;
    }

    void setSpeed(ECS::World& world, ECS::Entity e, f32 speed) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->playbackScale = speed;
    }

    bool isPlaying(ECS::World& world, ECS::Entity e, const char* stateName) const {
        const PhaseShift* anim = world.get<PhaseShift>(e);
        if (!anim || anim->paused) return false;
        return anim->currentState == FixedString<32>(stateName);
    }

    void addParameter(ECS::World& world, ECS::Entity e, const char* name, ParameterType type) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->addParameter(name, type);
    }

    void setBool(ECS::World& world, ECS::Entity e, const char* name, bool value) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->setBool(name, value);
    }

    void setFloat(ECS::World& world, ECS::Entity e, const char* name, f32 value) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->setFloat(name, value);
    }

    void setInt(ECS::World& world, ECS::Entity e, const char* name, i32 value) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->setInt(name, value);
    }

    void setTrigger(ECS::World& world, ECS::Entity e, const char* name) {
        PhaseShift* anim = world.get<PhaseShift>(e);
        if (anim) anim->setTrigger(name);
    }

    bool getBool(ECS::World& world, ECS::Entity e, const char* name) const {
        const PhaseShift* anim = world.get<PhaseShift>(e);
        return anim ? anim->getBool(name) : false;
    }

    f32 getFloat(ECS::World& world, ECS::Entity e, const char* name) const {
        const PhaseShift* anim = world.get<PhaseShift>(e);
        return anim ? anim->getFloat(name) : 0.0f;
    }

    i32 getInt(ECS::World& world, ECS::Entity e, const char* name) const {
        const PhaseShift* anim = world.get<PhaseShift>(e);
        return anim ? anim->getInt(name) : 0;
    }

private:
    /// Value conditions read: the smoothed float when smoothing is on, the raw float otherwise.
    static f32 floatOf(const PhaseShiftParameter& p) {
        return p.smoothTime > 0.0f ? p.smoothed : p.floatValue;
    }

    /// Advances the critically damped springs of every smoothed float parameter.
    static void tickParameterSmoothing(ECS::World& world, f32 dt) {
        ECS::ComponentQuery q;
        q.with<PhaseShift>();
        world.forEach<PhaseShift>(q, [dt](ECS::Entity, PhaseShift& ps) {
            for (PhaseShiftParameter& p : ps.parameters) {
                if (p.type != ParameterType::Float || p.smoothTime <= 0.0f) continue;
                p.smoothed = smoothDamp(p.smoothed, p.floatValue, p.smoothVelocity, p.smoothTime, dt);
            }
        });
    }

    static bool evaluateCondition(const TransitionCondition& cond, const PhaseShift& anim) {
        const PhaseShiftParameter* p = anim.findParameter(cond.parameterName);
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
                    case ConditionOperator::Greater:        return floatOf(*p) >  cond.floatValue;
                    case ConditionOperator::Less:           return floatOf(*p) <  cond.floatValue;
                    case ConditionOperator::GreaterOrEqual: return floatOf(*p) >= cond.floatValue;
                    case ConditionOperator::LessOrEqual:    return floatOf(*p) <= cond.floatValue;
                    case ConditionOperator::Equals:         return floatOf(*p) == cond.floatValue;
                    case ConditionOperator::NotEquals:      return floatOf(*p) != cond.floatValue;
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

    static void consumeTriggers(PhaseShift& anim) {
        for (auto& p : anim.parameters) {
            if (p.type == ParameterType::Trigger) {
                p.triggered = false;
            }
        }
    }

    /// Exit time is normalized: 1 waits for the clip's end. With no conditions the exit time alone fires it.
    static bool transitionReady(const AnimationTransition& t, const PhaseShift& anim, f32 timeInState, f32 duration) {
        if (t.hasExitTime && duration > 0.0f) {
            if (timeInState < std::max(t.exitTime, 0.0f) * duration) return false;
        }
        if (t.conditions.empty() && !t.legacyCondition) return t.hasExitTime;
        if (!t.conditions.empty()) {
            for (const auto& cond : t.conditions) {
                if (!evaluateCondition(cond, anim)) return false;
            }
            return true;
        }
        return t.legacyCondition();
    }

    static void evaluateTransitions(PhaseShift& anim, f32 stateDuration = -1.0f) {
        const AnimationState* state = anim.states.get(anim.currentState);
        if (!state) return;

        const f32 duration = stateDuration >= 0.0f ? stateDuration : (state->clip ? state->clip->duration() : 0.0f);
        for (const auto& t : state->transitions) {
            if (transitionReady(t, anim, anim.timeInState, duration)) {
                anim.previousState = anim.currentState;
                anim.currentState  = t.toState == FixedString<32>(kAnimatorExitState) ? anim.defaultState : t.toState;
                anim.timeInState   = 0.0f;
                consumeTriggers(anim);
                return;
            }
        }
    }

    static void checkFrameEvents(PhaseShift& anim, u32 currentFrame) {
        for (const auto& [frame, eventName] : anim.frameEvents) {
            if (frame == currentFrame) {
                anim.onFrameEvent(eventName);
            }
        }
    }
};

}  // namespace Caffeine::Animation