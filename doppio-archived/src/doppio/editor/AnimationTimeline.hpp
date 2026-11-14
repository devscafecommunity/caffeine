#pragma once
#include "animation/ClipAsset.hpp"
#include "animation/SkinLibrary.hpp"
#include "ecs/World.hpp"
#include "core/Types.hpp"
#include "math/Vec3.hpp"
#include "animation/AnimationComponents.hpp"
#include "containers/FixedString.hpp"
#include <vector>
#include <string>
#include <filesystem>
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

    void setScene(ECS::World* world, u32 selectedEntity, i32 selectedBone);
    void setProjectRoot(const std::string& root) { m_projectRoot = root; }
    /// Project folder new .anim files go to.
    std::filesystem::path animationFolder() const;
    /// The timeline row click selects a joint. -1 is the object itself.
    bool consumePickedBone(i32& bone);
    void newClip();
    void loadClipFile(const std::filesystem::path& path);
    void saveClipFile(const std::filesystem::path& path);

private:
    void adoptMotion();
    void addKeyframeToSelectedTrack(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value);
    void deleteSelectedKeyframe();
    void moveSelectedKeyframe(f32 newTime);

    std::variant<i32, Vec3, FixedString<32>> interpolateValue(AnimationTrack* track, f32 time) const;
    f32 applyEasing(f32 t, EasingType easing) const;

    ECS::World* m_scene = nullptr;
    u32 m_selectedEntity = 0xFFFFFFFFu;
    Animation::AnimationClip* m_clip = nullptr;
    Animation::MotionClip m_motion;
    Animation::AnimationClip m_ownedClip;
    bool m_pendingLoad = false;
    bool m_pendingSave = false;
    std::vector<std::unique_ptr<AnimationTrack>> m_tracks;

    f32 m_currentTime = 0.0f;
    f32 m_timelineDuration = 0.0f;
    std::string m_driveName;
    bool m_drivingBones = false;
    bool m_applyTime = false;
    bool m_isPlaying = false;
    bool m_looping = true;
    bool m_onionSkinningEnabled = false;
    bool m_open = true;

    f32 m_viewStart = 0.0f;
    f32 m_viewSpan = 0.0f;

    void renderBoneRows(f32 laneLeft, f32 trackWidth, f32 duration);
    void renderSpriteRows(f32 laneLeft, f32 trackWidth, f32 duration);
    void renderCurveEditor();
    void renderStripRows(f32 laneLeft, f32 trackWidth);
    void renderStripInspector();
    void renderAssetPopups();
    void splitSelectedStrip();
    void duplicateSelectedStrip();
    void deleteSelectedStrip();
    void buildStripsFromAnimator();
    void stripsEdited();
    void openAnimationPath(const std::string& path);
    bool saveAnimationPath(const std::filesystem::path& path, const std::string& name);
    Animation::SkinnedPose* selectedPose() const;

    enum class StripDrag { None, Move, TrimLeft, TrimRight, FadeIn, FadeOut };

    std::string m_projectRoot;
    u32 m_driveEntity = 0xFFFFFFFFu;
    i32 m_selectedStrip = -1;
    StripDrag m_stripDrag = StripDrag::None;
    Animation::ClipStrip m_stripDragOrigin;
    f32 m_stripDragMouseTime = 0.0f;
    f32 m_stripRowsTop = 0.0f;
    i32 m_extraTracks = 0;
    bool m_snap = true;
    i32 m_contextStrip = -1;
    f32 m_contextTime = 0.0f;
    i32 m_contextTrack = 0;
    u64 m_linkSelectionSeen = 0;
    u64 m_linkTransitionSeen = 0;
    char m_assetFilter[64] = {};
    char m_assetName[64] = {};
    bool m_openSavePopup = false;
    bool m_openLoadPopup = false;
    std::string m_assetStatus;

    usize m_selectedTrack = 0;
    usize m_selectedKeyframe = 0;
    bool m_isDraggingKeyframe = false;
    /// Bone mode: a key (and the segment that leaves it) is selected.
    bool m_keySelected = false;
    i32 m_contextBone = -2;
    usize m_contextKey = 0;
    int m_dragHandle = 0;
    bool m_scrubbing = false;
    i32 m_editBone = -1;
    i32 m_pickedBone = -2;
    bool m_objectRow = false;
    bool m_timelineDrivesClip = false;
};

} // namespace Caffeine::Editor