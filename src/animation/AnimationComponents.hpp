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

struct AnimationTransition {
    FixedString<32>       toState;
    std::function<bool()> condition;
    f32                   blendTime   = 0.1f;
    bool                  hasExitTime = false;
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
    std::vector<std::pair<u32, FixedString<32>>> frameEvents;
    std::function<void(const FixedString<32>&)>  onFrameEvent;
};

}  // namespace Caffeine::Animation
