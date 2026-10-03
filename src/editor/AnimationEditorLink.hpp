#pragma once

#include "animation/AnimationComponents.hpp"
#include "animation/SkinLibrary.hpp"
#include "core/Types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace Caffeine::Editor {

/// State the Animation Timeline and the Animator Controller share, so each can follow the other.
struct AnimationEditorLink {
    u32 entity = 0xFFFFFFFFu;

    /// Timeline side, refreshed every frame the timeline drives the pose.
    u64 timelineFrame = 0;
    std::string timelineState;
    std::string timelineFadeFrom;
    f32 timelineFade = 1.0f;
    f32 timelineStateProgress = 0.0f;
    bool timelinePlaying = false;

    /// Animator side.
    bool animatorPreview = false;
    std::string previewState;
    f32 previewTime = 0.0f;
    std::string selectedState;
    u64 selectionSerial = 0;
    std::string selectedFrom;
    std::string selectedTo;
    u64 transitionSerial = 0;

    /// Timeline asks the animator to select and frame a state.
    std::string focusState;
    u64 focusSerial = 0;

    /// Asset Browser asks the timeline to open an .anim file.
    std::string openAnimRequest;

    static AnimationEditorLink& get() {
        static AnimationEditorLink link;
        return link;
    }

    bool timelineActive(u64 frame) const { return timelineFrame + 2 >= frame && !timelineState.empty(); }
};

/// The animator state a strip stands for: its binding, or the base-layer state that plays its clip.
inline std::string stripStateName(const Animation::ClipStrip& strip, const Animation::Animator* animator,
                                  const Animation::ImportedSkin* skin) {
    if (strip.state[0] != '\0') return strip.state;
    if (!animator || !skin || strip.clip < 0 || strip.clip >= static_cast<i32>(skin->clipNames.size())) return {};
    const std::string& clip = skin->clipNames[static_cast<size_t>(strip.clip)];
    for (const auto& pair : animator->states) {
        const char* motion = pair.value.motion.empty() ? pair.key.cStr() : pair.value.motion.cStr();
        if (clip == motion) return pair.key.cStr();
    }
    return {};
}

/// Track-0 strips sorted by start. Consecutive pairs are the crossfades the animator mirrors.
inline std::vector<size_t> baseTrackOrder(const Animation::SkinnedPose& pose) {
    std::vector<size_t> order;
    for (size_t i = 0; i < pose.strips.size(); ++i) {
        if (pose.strips[i].track == 0) order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return pose.strips[a].start < pose.strips[b].start; });
    return order;
}

/// Crossfade length between a strip and the one before it: overlap, or the explicit fade in.
inline f32 stripCrossfade(const Animation::SkinnedPose& pose, size_t index, const Animation::ImportedSkin* skin) {
    return std::max(Animation::clipStripOverlapIn(pose, index, skin), pose.strips[index].fadeIn);
}

inline Animation::AnimationTransition* findTransition(Animation::AnimatorStateMachine& sm, const std::string& from,
                                                      const std::string& to) {
    Animation::AnimationState* state = sm.states.get(FixedString<32>(from.c_str()));
    if (!state) return nullptr;
    for (Animation::AnimationTransition& transition : state->transitions) {
        if (to == transition.toState.cStr()) return &transition;
    }
    return nullptr;
}

/// Timeline to animator: crossfades between bound strips become the matching transitions' blend.
inline void pushStripFadesToAnimator(const Animation::SkinnedPose& pose, const Animation::ImportedSkin* skin,
                                     Animation::Animator& animator) {
    const std::vector<size_t> order = baseTrackOrder(pose);
    for (size_t i = 1; i < order.size(); ++i) {
        const std::string from = stripStateName(pose.strips[order[i - 1]], &animator, skin);
        const std::string to = stripStateName(pose.strips[order[i]], &animator, skin);
        if (from.empty() || to.empty() || from == to) continue;
        if (Animation::AnimationTransition* transition = findTransition(animator, from, to)) {
            transition->blendTime = stripCrossfade(pose, order[i], skin);
            transition->blendCurve = pose.strips[order[i]].fadeInCurve;
        }
    }
}

/// Animator to timeline: each bound pair takes the transition's blend as an overlap. Later strips ripple.
inline void pullTransitionToStrips(Animation::SkinnedPose& pose, const Animation::ImportedSkin* skin,
                                   const Animation::Animator& animator, const std::string& from, const std::string& to,
                                   const Animation::AnimationTransition& transition) {
    const std::vector<size_t> order = baseTrackOrder(pose);
    for (size_t i = 1; i < order.size(); ++i) {
        Animation::ClipStrip& previous = pose.strips[order[i - 1]];
        Animation::ClipStrip& next = pose.strips[order[i]];
        if (stripStateName(previous, &animator, skin) != from || stripStateName(next, &animator, skin) != to) continue;
        const f32 wantedStart = previous.start + Animation::clipStripLength(previous, skin) - transition.blendTime;
        const f32 shift = wantedStart - next.start;
        next.fadeIn = 0.0f;
        next.fadeInCurve = transition.blendCurve;
        for (size_t j = i; j < order.size(); ++j) pose.strips[order[j]].start = std::max(0.0f, pose.strips[order[j]].start + shift);
    }
}

}  // namespace Caffeine::Editor
