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

enum class KeyInterp : u8 {
    Linear,
    Constant,
    EaseIn,
    EaseOut,
    EaseInOut,
    Custom
};

/// Shape of a segment or a fade. Custom uses a cubic Bezier with x in [0, 1].
struct KeyCurve {
    KeyInterp mode = KeyInterp::Linear;
    f32 x1 = 0.33f;
    f32 y1 = 0.33f;
    f32 x2 = 0.67f;
    f32 y2 = 0.67f;
};

/// Reserved transition target: the layer leaves through Exit and re-enters at its default state.
inline constexpr const char* kAnimatorExitState = "Exit";

struct AnimationTransition {
    FixedString<32>                   toState;
    std::vector<TransitionCondition>  conditions;  // all must be satisfied
    f32                               blendTime   = 0.1f;
    bool                              hasExitTime = false;
    /// Normalized source time the transition may fire at, when hasExitTime is set.
    f32                               exitTime    = 1.0f;
    /// Normalized time the target state starts at.
    f32                               offset      = 0.0f;
    KeyCurve                          blendCurve;

    std::function<bool()> legacyCondition;
};

struct AnimationState {
    FixedString<32>                  name;
    const AnimationClip*             clip  = nullptr;
    f32                              speed = 1.0f;
    std::vector<AnimationTransition> transitions;
    /// Imported skeletal clip this state plays. Empty means a clip with the state's own name.
    FixedString<32>                  motion;
    f32                              editorX = 0.0f;
    f32                              editorY = 0.0f;
    bool                             hasEditorPosition = false;
};

/// One state graph. The Animator is the base layer; extra layers carry their own graph.
struct AnimatorStateMachine {
    HashMap<FixedString<32>, AnimationState>     states;
    FixedString<32>                              currentState;
    FixedString<32>                              previousState;
    /// Entry points here. Play mode starts in this state.
    FixedString<32>                              defaultState;
    f32                                          timeInState   = 0.0f;
    /// Checked before the current state's own transitions, from any state.
    std::vector<AnimationTransition>             anyStateTransitions;

    /// Crossfade from the previous state. The pose blends while fadeElapsed < fadeDuration.
    FixedString<32>                              fadeFromState;
    f32                                          fadeFromTime  = 0.0f;
    f32                                          fadeElapsed   = 0.0f;
    f32                                          fadeDuration  = 0.0f;
    KeyCurve                                     fadeCurve;

    f32 entryX = 40.0f, entryY = 120.0f;
    f32 anyX = 40.0f, anyY = 40.0f;
    f32 exitX = 40.0f, exitY = 220.0f;

    bool fading() const { return fadeDuration > 0.0f && fadeElapsed < fadeDuration && !fadeFromState.empty(); }
};

enum class LayerBlendMode : u8 {
    Override,
    Additive
};

/// Body regions a layer mask can include. Bits of AnimatorLayer::mask.
enum class BodyMask : u32 {
    Root = 1u << 0,
    Body = 1u << 1,
    Head = 1u << 2,
    LeftArm = 1u << 3,
    RightArm = 1u << 4,
    LeftLeg = 1u << 5,
    RightLeg = 1u << 6,
    All = 0x7Fu
};

struct AnimatorLayer : AnimatorStateMachine {
    FixedString<32> name;
    /// Group this layer sits under in the layer tree. Empty is the top level.
    FixedString<32> parentGroup;
    /// Groups hold other layers and scale their weight. They have no graph of their own.
    bool            isGroup  = false;
    bool            muted    = false;
    bool            expanded = true;
    f32             weight   = 1.0f;
    LayerBlendMode  blend    = LayerBlendMode::Override;
    u32             mask     = static_cast<u32>(BodyMask::All);
};

struct Animator : AnimatorStateMachine {
    // Clip storage for deserialized scenes (state.clip may point here).
    std::vector<AnimationClip>                   embeddedClips;
    /// Layers above the base graph, in evaluation order.
    std::vector<AnimatorLayer>                   layers;
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