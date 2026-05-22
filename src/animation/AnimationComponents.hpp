#pragma once

#include "core/Types.hpp"
#include "containers/FixedString.hpp"
#include "containers/HashMap.hpp"

#include <vector>
#include <functional>
#include <cmath>

namespace Caffeine::Animation {

using namespace Caffeine;

struct FrameRect {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 w = 0.0f;
    f32 h = 0.0f;
};

struct AnimationClip {
    FixedString<32>        name;
    u32                    fps   = 12;
    std::vector<FrameRect> frames;
    bool                   loop  = true;

    f32 duration() const {
        if (fps == 0 || frames.empty()) return 0.0f;
        return static_cast<f32>(frames.size()) / static_cast<f32>(fps);
    }
};

enum class ParameterType : u8 {
    Bool,
    Float,
    Int,
    Trigger
};

struct AnimatorParameter {
    FixedString<32> name;
    ParameterType   type = ParameterType::Bool;

    union {
        bool boolValue  = false;
        f32  floatValue;
        i32  intValue;
    };

    bool triggered = false;
};

enum class ConditionOperator : u8 {
    Equals,
    NotEquals,
    Greater,
    Less,
    GreaterOrEqual,
    LessOrEqual
};

struct TransitionCondition {
    FixedString<32>    parameterName;
    ConditionOperator  op = ConditionOperator::Equals;

    union {
        bool boolValue  = false;
        f32  floatValue;
        i32  intValue;
    };
};

struct AnimationTransition {
    FixedString<32>                   toState;
    std::vector<TransitionCondition>  conditions;  // all must be satisfied
    f32                               blendTime   = 0.1f;
    bool                              hasExitTime = false;

    std::function<bool()> legacyCondition;
};

struct AnimationState {
    FixedString<32>                  name;
    const AnimationClip*             clip  = nullptr;
    f32                              speed = 1.0f;
    std::vector<AnimationTransition> transitions;
};

struct Animator {
    HashMap<FixedString<32>, AnimationState>     states;
    FixedString<32>                              currentState;
    FixedString<32>                              previousState;
    f32                                          timeInState   = 0.0f;
    f32                                          blendWeight   = 1.0f;
    f32                                          playbackScale = 1.0f;
    bool                                         paused        = false;
    std::vector<AnimatorParameter>               parameters;
    std::vector<std::pair<u32, FixedString<32>>> frameEvents;
    std::function<void(const FixedString<32>&)>  onFrameEvent;

    void addParameter(const char* name, ParameterType type) {
        if (findParameter(FixedString<32>(name))) return;
        AnimatorParameter p;
        p.name = name;
        p.type = type;
        parameters.push_back(p);
    }

    void setBool(const char* name, bool value) {
        if (auto* p = findParameter(FixedString<32>(name))) {
            p->boolValue = value;
        }
    }

    void setFloat(const char* name, f32 value) {
        if (auto* p = findParameter(FixedString<32>(name))) {
            p->floatValue = value;
        }
    }

    void setInt(const char* name, i32 value) {
        if (auto* p = findParameter(FixedString<32>(name))) {
            p->intValue = value;
        }
    }

    void setTrigger(const char* name) {
        if (auto* p = findParameter(FixedString<32>(name))) {
            p->triggered = true;
        }
    }

    bool getBool(const char* name) const {
        const auto* p = findParameter(FixedString<32>(name));
        return p ? p->boolValue : false;
    }

    f32 getFloat(const char* name) const {
        const auto* p = findParameter(FixedString<32>(name));
        return p ? p->floatValue : 0.0f;
    }

    i32 getInt(const char* name) const {
        const auto* p = findParameter(FixedString<32>(name));
        return p ? p->intValue : 0;
    }

    AnimatorParameter* findParameter(const FixedString<32>& name) {
        for (auto& p : parameters) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }

    const AnimatorParameter* findParameter(const FixedString<32>& name) const {
        for (const auto& p : parameters) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }
};

}  // namespace Caffeine::Animation