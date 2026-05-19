#pragma once
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "animation/AnimationComponents.hpp"
#include "containers/FixedString.hpp"
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <memory>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

enum class EasingType {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut
};

enum class TrackType {
    Sprite,
    Transform,
    Event,
    Audio
};

struct Keyframe {
    f32 time;
    std::variant<i32, Vec3, FixedString<32>> value;
    EasingType easing = EasingType::Linear;
};

class AnimationTrack {
public:
    virtual ~AnimationTrack() = default;
    virtual void addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) = 0;
    virtual void removeKeyframe(usize index) = 0;
    virtual TrackType getType() const = 0;

    std::vector<Keyframe> keyframes;
    FixedString<32> targetPropertyName;
};

class SpriteTrack : public AnimationTrack {
public:
    TrackType getType() const override { return TrackType::Sprite; }
    void addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) override;
    void removeKeyframe(usize index) override;
};

class TransformTrack : public AnimationTrack {
public:
    TrackType getType() const override { return TrackType::Transform; }
    void addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) override;
    void removeKeyframe(usize index) override;
};

class EventTrack : public AnimationTrack {
public:
    TrackType getType() const override { return TrackType::Event; }
    void addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) override;
    void removeKeyframe(usize index) override;
};

class AnimationTimelinePanel {
public:
    AnimationTimelinePanel() = default;

    void setClip(Animation::AnimationClip* clip);
    Animation::AnimationClip* getClip() const { return m_clip; }

    void render(f32 deltaTime = 0.016f);
    void renderHeader();
    void renderTimeline();
    void renderTracks();
    void handleInput();

    void play();
    void stop();
    void pause();
    bool isPlaying() const { return m_isPlaying; }
    bool isLooping() const { return m_looping; }
    void setLooping(bool loop) { m_looping = loop; }

    f32 currentTime() const { return m_currentTime; }
    void setCurrentTime(f32 time) { m_currentTime = time; }

    bool onionSkinningEnabled() const { return m_onionSkinningEnabled; }
    void setOnionSkinning(bool enabled) { m_onionSkinningEnabled = enabled; }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open() { m_open = true; }

private:
    void addKeyframeToSelectedTrack(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value);
    void deleteSelectedKeyframe();
    void moveSelectedKeyframe(f32 newTime);

    std::variant<i32, Vec3, FixedString<32>> interpolateValue(AnimationTrack* track, f32 time) const;
    f32 applyEasing(f32 t, EasingType easing) const;

    Animation::AnimationClip* m_clip = nullptr;
    std::vector<std::unique_ptr<AnimationTrack>> m_tracks;

    f32 m_currentTime = 0.0f;
    bool m_isPlaying = false;
    bool m_looping = true;
    bool m_onionSkinningEnabled = false;
    bool m_open = true;

    usize m_selectedTrack = 0;
    usize m_selectedKeyframe = 0;
    bool m_isDraggingKeyframe = false;
};

} // namespace Caffeine::Editor