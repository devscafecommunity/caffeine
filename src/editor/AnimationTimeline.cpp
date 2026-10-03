#include "editor/AnimationTimeline.hpp"
#include "animation/ClipAsset.hpp"
#include "animation/SkinLibrary.hpp"
#include "debug/LogSystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "editor/AnimationEditorLink.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Caffeine::Editor {

void SpriteTrack::addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) {
    if (std::holds_alternative<i32>(value) || std::holds_alternative<FixedString<32>>(value)) {
        keyframes.push_back({time, value, EasingType::Linear});
    }
}

void SpriteTrack::removeKeyframe(usize index) {
    if (index < keyframes.size()) {
        keyframes.erase(keyframes.begin() + index);
    }
}

void TransformTrack::addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) {
    if (std::holds_alternative<Vec3>(value)) {
        keyframes.push_back({time, value, EasingType::Linear});
    }
}

void TransformTrack::removeKeyframe(usize index) {
    if (index < keyframes.size()) {
        keyframes.erase(keyframes.begin() + index);
    }
}

void EventTrack::addKeyframe(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) {
    if (std::holds_alternative<FixedString<32>>(value)) {
        keyframes.push_back({time, value, EasingType::Linear});
    }
}

void EventTrack::removeKeyframe(usize index) {
    if (index < keyframes.size()) {
        keyframes.erase(keyframes.begin() + index);
    }
}

void AnimationTimelinePanel::setClip(Animation::AnimationClip* clip) {
    m_clip = clip;
    m_tracks.clear();
    if (clip) {
        m_tracks.push_back(std::make_unique<SpriteTrack>());
        m_tracks.back()->targetPropertyName = "Sprite";
    }
}

void AnimationTimelinePanel::play() {
    m_isPlaying = true;
}

void AnimationTimelinePanel::stop() {
    m_isPlaying = false;
    m_currentTime = 0.0f;
    m_applyTime = true;
}

void AnimationTimelinePanel::pause() {
    m_isPlaying = false;
    m_applyTime = true;
}

f32 AnimationTimelinePanel::applyEasing(f32 t, EasingType easing) const {
    switch (easing) {
        case EasingType::EaseIn:    return t * t;
        case EasingType::EaseOut:   return t * (2.0f - t);
        case EasingType::EaseInOut: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case EasingType::Linear:
        default:                    return t;
    }
}

std::variant<i32, Vec3, FixedString<32>> AnimationTimelinePanel::interpolateValue(AnimationTrack* track, f32 time) const {
    if (!track || track->keyframes.empty()) return i32(0);

    const auto& keyframes = track->keyframes;
    auto it = keyframes.begin();
    while (it != keyframes.end() && it->time <= time) ++it;

    if (it == keyframes.begin()) return keyframes.front().value;
    if (it == keyframes.end())   return keyframes.back().value;

    const auto& k1 = *(it - 1);
    const auto& k2 = *it;
    f32 duration = k2.time - k1.time;
    if (duration <= 0.0f) return k2.value;

    f32 alpha = applyEasing((time - k1.time) / duration, k1.easing);

    if (std::holds_alternative<Vec3>(k1.value) && std::holds_alternative<Vec3>(k2.value)) {
        Vec3 v1 = std::get<Vec3>(k1.value);
        Vec3 v2 = std::get<Vec3>(k2.value);
        return Vec3{v1.x + (v2.x - v1.x) * alpha, v1.y + (v2.y - v1.y) * alpha, v1.z + (v2.z - v1.z) * alpha};
    }
    return k2.value;
}

void AnimationTimelinePanel::addKeyframeToSelectedTrack(f32 time, const std::variant<i32, Vec3, FixedString<32>>& value) {
    if (m_selectedTrack < m_tracks.size()) {
        m_tracks[m_selectedTrack]->addKeyframe(time, value);
    }
}

void AnimationTimelinePanel::deleteSelectedKeyframe() {
    if (m_selectedTrack < m_tracks.size() && m_selectedKeyframe < m_tracks[m_selectedTrack]->keyframes.size()) {
        m_tracks[m_selectedTrack]->removeKeyframe(m_selectedKeyframe);
        m_selectedKeyframe = 0;
    }
}

void AnimationTimelinePanel::moveSelectedKeyframe(f32 newTime) {
    if (m_selectedTrack < m_tracks.size() && m_selectedKeyframe < m_tracks[m_selectedTrack]->keyframes.size()) {
        m_tracks[m_selectedTrack]->keyframes[m_selectedKeyframe].time = newTime;
    }
}

void AnimationTimelinePanel::adoptMotion() {
    m_ownedClip = {};
    m_ownedClip.name = m_motion.name.c_str();
    m_ownedClip.fps = m_motion.fps == 0 ? 12 : m_motion.fps;
    m_ownedClip.loop = m_motion.loop;
    m_ownedClip.frames.clear();
    for (const Animation::SpriteFrameKey& key : m_motion.spriteFrames) {
        Animation::FrameRect rect;
        rect.x = key.x;
        rect.y = key.y;
        rect.w = key.w;
        rect.h = key.h;
        m_ownedClip.frames.push_back(rect);
    }
    if (m_ownedClip.frames.empty()) m_ownedClip.frames.push_back({});
    setClip(&m_ownedClip);
}

void AnimationTimelinePanel::newClip() {
    m_motion = {};
    m_motion.name = "Clip";
    m_motion.fps = 12;
    m_motion.loop = true;
    m_motion.duration = 1.0f;
    m_motion.spriteFrames.push_back({0.0f, 0, 0, 0, 32, 32});
    m_motion.spriteFrames.push_back({0.5f, 1, 32, 0, 32, 32});
    adoptMotion();
}

void AnimationTimelinePanel::loadClipFile(const std::filesystem::path& path) {
    if (!Animation::loadMotionClip(path, m_motion)) return;
    adoptMotion();
}

void AnimationTimelinePanel::saveClipFile(const std::filesystem::path& path) {
    m_motion.name = m_ownedClip.name.cStr();
    m_motion.loop = m_ownedClip.loop;
    m_motion.fps = m_ownedClip.fps;
    m_motion.duration = m_ownedClip.duration();
    if (m_motion.spriteFrames.size() != m_ownedClip.frames.size()) {
        m_motion.spriteFrames.clear();
        const f32 step = m_ownedClip.fps > 0 ? 1.0f / static_cast<f32>(m_ownedClip.fps) : 0.1f;
        for (size_t i = 0; i < m_ownedClip.frames.size(); ++i) {
            const Animation::FrameRect& rect = m_ownedClip.frames[i];
            m_motion.spriteFrames.push_back(
                {step * static_cast<f32>(i), static_cast<u32>(i), rect.x, rect.y, rect.w, rect.h});
        }
    }
    Animation::saveMotionClip(path, m_motion);
}

Animation::SkinnedPose* AnimationTimelinePanel::selectedPose() const {
    if (!m_scene) return nullptr;
    return m_scene->get<Animation::SkinnedPose>(ECS::Entity(m_selectedEntity, m_scene));
}

std::filesystem::path AnimationTimelinePanel::animationFolder() const {
    const std::filesystem::path root = m_projectRoot.empty() ? std::filesystem::path(".") : std::filesystem::path(m_projectRoot);
    return root / "assets" / "raw" / "Animations";
}

void AnimationTimelinePanel::stripsEdited() {
    m_timelineDrivesClip = true;
    m_applyTime = true;
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose) return;
    pose->stripsInitialized = true;
    if (Animation::Animator* animator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene))) {
        pushStripFadesToAnimator(*pose, Animation::findImportedSkin(pose->meshPath), *animator);
    }
}

static f32 stripClipDuration(const Animation::ClipStrip& strip, const Animation::ImportedSkin* skin) {
    if (!skin || strip.clip < 0 || strip.clip >= static_cast<i32>(skin->clips.size())) return 1.0f;
    return std::max(skin->clips[static_cast<size_t>(strip.clip)].duration, 0.05f);
}

void AnimationTimelinePanel::splitSelectedStrip() {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose || m_selectedStrip < 0 || m_selectedStrip >= static_cast<i32>(pose->strips.size())) return;
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    Animation::ClipStrip& strip = pose->strips[static_cast<size_t>(m_selectedStrip)];
    const f32 length = Animation::clipStripLength(strip, skin);
    f32 cut = m_currentTime - strip.start;
    if (cut <= 0.02f || cut >= length - 0.02f) return;
    Animation::ClipStrip head = strip;
    Animation::ClipStrip tail = strip;
    if (strip.repeat > 1) {
        const f32 pass = length / static_cast<f32>(strip.repeat);
        const i32 headPasses = std::clamp(static_cast<i32>(std::round(cut / pass)), 1, strip.repeat - 1);
        cut = pass * static_cast<f32>(headPasses);
        head.repeat = headPasses;
        tail.repeat = strip.repeat - headPasses;
    } else {
        const f32 in = strip.clipIn;
        const f32 out = strip.clipOut < 0.0f ? stripClipDuration(strip, skin) : strip.clipOut;
        const f32 source = cut * std::max(strip.speed, 0.01f);
        if (strip.reverse) {
            head.clipIn = out - source;
            head.clipOut = out;
            tail.clipIn = in;
            tail.clipOut = out - source;
        } else {
            head.clipIn = in;
            head.clipOut = in + source;
            tail.clipIn = in + source;
            tail.clipOut = strip.clipOut;
        }
    }
    head.fadeOut = 0.0f;
    head.loopBlend = 0.0f;
    tail.fadeIn = 0.0f;
    tail.start = strip.start + cut;
    strip = head;
    pose->strips.insert(pose->strips.begin() + m_selectedStrip + 1, tail);
    m_selectedStrip += 1;
    stripsEdited();
}

void AnimationTimelinePanel::duplicateSelectedStrip() {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose || m_selectedStrip < 0 || m_selectedStrip >= static_cast<i32>(pose->strips.size())) return;
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    Animation::ClipStrip copy = pose->strips[static_cast<size_t>(m_selectedStrip)];
    copy.start += Animation::clipStripLength(copy, skin);
    pose->strips.push_back(copy);
    m_selectedStrip = static_cast<i32>(pose->strips.size()) - 1;
    stripsEdited();
}

void AnimationTimelinePanel::deleteSelectedStrip() {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose || m_selectedStrip < 0 || m_selectedStrip >= static_cast<i32>(pose->strips.size())) return;
    pose->strips.erase(pose->strips.begin() + m_selectedStrip);
    m_selectedStrip = -1;
    stripsEdited();
}

void AnimationTimelinePanel::buildStripsFromAnimator() {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose) return;
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    const Animation::Animator* animator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene));
    if (!skin || !animator) return;
    std::string current = animator->defaultState.cStr();
    if (current.empty() && !animator->states.empty()) current = animator->states.begin()->key.cStr();
    std::vector<Animation::ClipStrip> chain;
    std::vector<std::string> visited;
    f32 cursor = 0.0f;
    f32 blend = 0.0f;
    Animation::KeyCurve blendCurve;
    while (!current.empty() && chain.size() < 12 &&
           std::find(visited.begin(), visited.end(), current) == visited.end()) {
        visited.push_back(current);
        const Animation::AnimationState* state = animator->states.get(FixedString<32>(current.c_str()));
        if (!state) break;
        const i32 clip = Animation::clipForAnimatorState(*skin, current.c_str(), state->motion.cStr());
        if (clip < 0) break;
        Animation::ClipStrip strip;
        strip.clip = clip;
        strip.speed = state->speed > 0.0f ? state->speed : 1.0f;
        strip.start = std::max(0.0f, cursor - blend);
        strip.fadeInCurve = blendCurve;
        std::snprintf(strip.state, sizeof(strip.state), "%s", current.c_str());
        const f32 length = Animation::clipStripLength(strip, skin);
        cursor = strip.start + length;
        chain.push_back(strip);

        const Animation::AnimationTransition* next = nullptr;
        for (const Animation::AnimationTransition& transition : state->transitions) {
            if (transition.toState == FixedString<32>(Animation::kAnimatorExitState)) continue;
            if (!next || (transition.hasExitTime && !next->hasExitTime)) next = &transition;
        }
        if (!next) break;
        blend = std::min(next->blendTime, length * 0.9f);
        blendCurve = next->blendCurve;
        current = next->toState.cStr();
    }
    if (chain.empty()) return;
    pose->strips.erase(std::remove_if(pose->strips.begin(), pose->strips.end(),
                                      [](const Animation::ClipStrip& strip) { return strip.track == 0; }),
                       pose->strips.end());
    pose->strips.insert(pose->strips.begin(), chain.begin(), chain.end());
    m_selectedStrip = 0;
    stripsEdited();
}

void AnimationTimelinePanel::openAnimationPath(const std::string& path) {
    const std::string file = std::filesystem::path(path).filename().string();
    if (!Animation::isPoseAnimationFile(path)) {
        loadClipFile(path);
        m_assetStatus = "Opened " + file;
        return;
    }
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose) {
        m_assetStatus = "Select the character " + file + " belongs to, then open it again.";
        return;
    }
    if (!Animation::loadPoseAnimation(path, *pose, Animation::findImportedSkin(pose->meshPath))) {
        m_assetStatus = "Could not read " + file;
        return;
    }
    m_selectedStrip = -1;
    m_keySelected = false;
    m_currentTime = 0.0f;
    m_isPlaying = false;
    stripsEdited();
    m_assetStatus = "Opened " + file;
}

bool AnimationTimelinePanel::saveAnimationPath(const std::filesystem::path& path, const std::string& name) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    bool ok = false;
    if (Animation::SkinnedPose* pose = m_drivingBones ? selectedPose() : nullptr) {
        ok = Animation::savePoseAnimation(path.string(), name, *pose, Animation::findImportedSkin(pose->meshPath));
    } else {
        m_ownedClip.name = name.c_str();
        saveClipFile(path);
        ok = std::filesystem::exists(path, ec);
    }
    m_assetStatus = (ok ? "Saved " : "Could not save ") + path.filename().string();
    return ok;
}

}

#ifdef CF_HAS_IMGUI

#include "editor/CurveWidget.hpp"
#include "editor/FilePicker.hpp"

#include <cctype>

namespace Caffeine::Editor {

static constexpr f32 k_TrackLabelWidth = 160.0f;
static constexpr f32 k_TrackHeight     = 28.0f;
static constexpr f32 k_RulerHeight     = 22.0f;
static constexpr f32 k_KeyDiamondSize  = 5.0f;

static const ImU32 k_ColTrackBg       = IM_COL32(35,  35,  42,  255);
static const ImU32 k_ColTrackBgAlt    = IM_COL32(30,  30,  38,  255);
static const ImU32 k_ColTrackLabel    = IM_COL32(50,  55,  70,  255);
static const ImU32 k_ColTrackLabelSel = IM_COL32(40,  80, 140,  255);
static const ImU32 k_ColRulerBg       = IM_COL32(25,  25,  32,  255);
static const ImU32 k_ColRulerTick     = IM_COL32(90,  90, 110,  255);
static const ImU32 k_ColRulerText     = IM_COL32(150,150, 170,  255);
static const ImU32 k_ColPlayhead      = IM_COL32(245, 245, 250, 255);

static const ImU32 k_ColKeyframe      = IM_COL32(255,210,  60,  255);
static const ImU32 k_ColKeyframeSel   = IM_COL32(255,255, 130,  255);
static const ImU32 k_ColKeyframeHover = IM_COL32(255,240, 120,  255);
static const ImU32 k_ColProgress      = IM_COL32(60, 100, 220,  180);

void AnimationTimelinePanel::setScene(ECS::World* world, u32 selectedEntity, i32 selectedBone) {
    m_scene = world;
    m_selectedEntity = selectedEntity;
    if (selectedBone >= 0) m_objectRow = false;
    m_editBone = selectedBone;
}

bool AnimationTimelinePanel::consumePickedBone(i32& bone) {
    if (m_pickedBone < -1) return false;
    bone = m_pickedBone;
    m_pickedBone = -2;
    return true;
}

void AnimationTimelinePanel::render(f32 deltaTime) {
    AnimationEditorLink& link = AnimationEditorLink::get();
    if (m_selectedEntity != m_driveEntity) {
        if (m_scene && m_driveEntity != 0xFFFFFFFFu) {
            if (Animation::SkinnedPose* previous = m_scene->get<Animation::SkinnedPose>(ECS::Entity(m_driveEntity, m_scene))) {
                previous->timelineHold = false;
                previous->overrideMix.clear();
            }
        }
        m_driveEntity = m_selectedEntity;
        m_timelineDrivesClip = false;
        m_isPlaying = false;
        m_selectedStrip = -1;
        m_keySelected = false;
        m_stripDrag = StripDrag::None;
        m_extraTracks = 0;
        m_viewSpan = 0.0f;
        m_viewStart = 0.0f;
        m_currentTime = 0.0f;
    }
    if (!link.openAnimRequest.empty()) {
        const std::string path = link.openAnimRequest;
        link.openAnimRequest.clear();
        m_open = true;
        openAnimationPath(path);
    }
    if (!m_open) {
        if (Animation::SkinnedPose* held = selectedPose()) {
            held->timelineHold = false;
            held->overrideMix.clear();
        }
        return;
    }

    f32 drivenDuration = 0.0f;
    m_driveName.clear();
    m_drivingBones = false;
    Animation::SkinnedPose* pose = selectedPose();
    const Animation::ImportedSkin* skin = pose ? Animation::findImportedSkin(pose->meshPath) : nullptr;
    if (pose && skin && (!skin->boneNames.empty() || !skin->clips.empty())) {
        m_drivingBones = true;
        Animation::ensureDefaultStrips(*pose, skin);
        drivenDuration = Animation::stripsDuration(*pose, skin);
        for (const Animation::BoneKeyframe& key : pose->keys) drivenDuration = std::max(drivenDuration, key.time);
        for (const Animation::ObjectKeyframe& key : pose->objectKeys) drivenDuration = std::max(drivenDuration, key.time);
        if (drivenDuration <= 0.0f) drivenDuration = 1.0f;
    } else {
        pose = nullptr;
    }
    if (!m_drivingBones && m_clip) drivenDuration = m_clip->duration();
    m_timelineDuration = drivenDuration;

    if (pose) {
        const Animation::Animator* animator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene));
        auto selectStripFor = [&](const std::string& state) {
            for (size_t i = 0; i < pose->strips.size(); ++i) {
                if (stripStateName(pose->strips[i], animator, skin) != state) continue;
                m_selectedStrip = static_cast<i32>(i);
                m_keySelected = false;
                if (!link.animatorPreview) {
                    m_currentTime = pose->strips[i].start;
                    m_applyTime = true;
                }
                return;
            }
        };
        if (link.selectionSerial != m_linkSelectionSeen) {
            m_linkSelectionSeen = link.selectionSerial;
            if (!link.selectedState.empty()) selectStripFor(link.selectedState);
        }
        if (link.transitionSerial != m_linkTransitionSeen) {
            m_linkTransitionSeen = link.transitionSerial;
            const std::vector<size_t> order = baseTrackOrder(*pose);
            for (size_t i = 1; i < order.size(); ++i) {
                if (stripStateName(pose->strips[order[i - 1]], animator, skin) != link.selectedFrom ||
                    stripStateName(pose->strips[order[i]], animator, skin) != link.selectedTo) {
                    continue;
                }
                m_selectedStrip = static_cast<i32>(order[i]);
                m_keySelected = false;
                if (!link.animatorPreview) {
                    m_currentTime = pose->strips[order[i]].start;
                    m_applyTime = true;
                }
                break;
            }
        }
    }

    const bool animatorOwns = pose && link.animatorPreview && link.entity == m_selectedEntity;
    if (animatorOwns) {
        m_isPlaying = false;
        m_timelineDrivesClip = false;
        m_applyTime = false;
        pose->timelineHold = false;
        pose->overrideMix.clear();
        const Animation::Animator* animator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene));
        for (const Animation::ClipStrip& strip : pose->strips) {
            if (stripStateName(strip, animator, skin) != link.previewState) continue;
            const f32 length = Animation::clipStripLength(strip, skin);
            if (length > 0.0f) m_currentTime = strip.start + std::fmod(std::max(link.previewTime, 0.0f), length);
            break;
        }
        m_driveName = "Animator: " + link.previewState;
    }

    if (m_isPlaying && drivenDuration > 0.0f) {
        m_currentTime += deltaTime;
        if (m_currentTime >= drivenDuration) {
            if (m_looping) {
                m_currentTime = std::fmod(m_currentTime, drivenDuration);
            } else {
                m_currentTime = drivenDuration;
                m_isPlaying = false;
            }
        }
    }
    if (m_isPlaying || m_applyTime || (pose && skin && !animatorOwns)) m_timelineDrivesClip = true;

    if (pose && skin && m_timelineDrivesClip && !animatorOwns) {
        ECS::Entity selected(m_selectedEntity, m_scene);
        Animation::buildStripMix(*pose, skin, m_currentTime, pose->overrideMix);
        pose->timelineHold = true;
        pose->overrideFrame = Animation::skinFrameCounter();
        pose->time = m_currentTime;
        pose->sampleKeys = !pose->keys.empty();
        if ((m_isPlaying || m_applyTime) && !pose->objectKeys.empty()) {
            const Animation::ObjectKeyframe* before = nullptr;
            const Animation::ObjectKeyframe* after = nullptr;
            for (const Animation::ObjectKeyframe& key : pose->objectKeys) {
                if (key.time <= m_currentTime + 0.0001f) {
                    if (!before || key.time >= before->time) before = &key;
                } else if (!after || key.time < after->time) {
                    after = &key;
                }
            }
            Vec3 position{};
            bool have = false;
            if (before && after && after->time > before->time) {
                const f32 alpha = Animation::evaluateKeyCurve(
                    before->curve, std::clamp((m_currentTime - before->time) / (after->time - before->time), 0.0f, 1.0f));
                position = Vec3(before->position.x + (after->position.x - before->position.x) * alpha,
                                before->position.y + (after->position.y - before->position.y) * alpha,
                                before->position.z + (after->position.z - before->position.z) * alpha);
                have = true;
            } else if (before) {
                position = before->position;
                have = true;
            } else if (after) {
                position = after->position;
                have = true;
            }
            if (have) {
                if (ECS::Transform* transform = m_scene->get<ECS::Transform>(selected)) transform->position = position;
                if (ECS::Position3D* position3 = m_scene->get<ECS::Position3D>(selected)) position3->position = position;
            }
        }

        const Animation::Animator* animator = m_scene->get<Animation::Animator>(selected);
        link.entity = m_selectedEntity;
        link.timelineFrame = Animation::skinFrameCounter();
        link.timelinePlaying = m_isPlaying;
        link.timelineState.clear();
        link.timelineFadeFrom.clear();
        link.timelineFade = 1.0f;
        link.timelineStateProgress = 0.0f;
        const std::vector<size_t> order = baseTrackOrder(*pose);
        i32 at = -1;
        for (size_t i = 0; i < order.size(); ++i) {
            const Animation::ClipStrip& strip = pose->strips[order[i]];
            const f32 length = Animation::clipStripLength(strip, skin);
            if (!strip.muted && m_currentTime >= strip.start && m_currentTime < strip.start + length) at = static_cast<i32>(i);
        }
        if (at >= 0) {
            const Animation::ClipStrip& strip = pose->strips[order[static_cast<size_t>(at)]];
            const f32 length = std::max(Animation::clipStripLength(strip, skin), 0.0001f);
            link.timelineState = stripStateName(strip, animator, skin);
            link.timelineStateProgress = (m_currentTime - strip.start) / length;
            const f32 fade = stripCrossfade(*pose, order[static_cast<size_t>(at)], skin);
            if (at > 0 && fade > 0.0f && m_currentTime - strip.start < fade) {
                const Animation::ClipStrip& previous = pose->strips[order[static_cast<size_t>(at - 1)]];
                if (m_currentTime < previous.start + Animation::clipStripLength(previous, skin)) {
                    link.timelineFadeFrom = stripStateName(previous, animator, skin);
                    link.timelineFade = Animation::evaluateKeyCurve(strip.fadeInCurve, (m_currentTime - strip.start) / fade);
                }
            }
        }
    }
    if (pose && !animatorOwns) {
        for (const Animation::ClipStrip& strip : pose->strips) {
            const f32 length = Animation::clipStripLength(strip, skin);
            if (strip.muted || m_currentTime < strip.start || m_currentTime >= strip.start + length) continue;
            if (strip.clip >= 0 && strip.clip < static_cast<i32>(skin->clipNames.size())) {
                m_driveName = skin->clipNames[static_cast<size_t>(strip.clip)];
            }
        }
        if (m_driveName.empty()) m_driveName = "Pose";
    }

    if (m_scene && (m_isPlaying || m_applyTime) && !m_drivingBones) {
        ECS::Entity selected(m_selectedEntity, m_scene);
        for (const std::unique_ptr<AnimationTrack>& track : m_tracks) {
            if (!track || track->getType() != TrackType::Transform) continue;
            const auto value = interpolateValue(track.get(), m_currentTime);
            if (!std::holds_alternative<Vec3>(value)) continue;
            const Vec3 position = std::get<Vec3>(value);
            if (ECS::Transform* transform = m_scene->get<ECS::Transform>(selected)) transform->position = position;
            if (ECS::Position3D* position3 = m_scene->get<ECS::Position3D>(selected)) position3->position = position;
        }
    }
    m_applyTime = false;

    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 200), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Animation Timeline", &m_open, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
        renderHeader();
        ImGui::Separator();
        renderTimeline();
        renderAssetPopups();
        if (m_pendingLoad) {
            if (auto picked = FilePicker::pickPath(FilePicker::Mode::PickFile, "Load Animation", {})) {
                openAnimationPath(picked->string());
                m_pendingLoad = false;
            } else if (FilePicker::consumeCloseEvent("Load Animation")) {
                m_pendingLoad = false;
            }
        }
        if (m_pendingSave) {
            if (auto picked = FilePicker::pickPath(FilePicker::Mode::SaveFile, "Save Animation", {})) {
                std::filesystem::path path = *picked;
                if (path.extension() != ".anim") path += ".anim";
                saveAnimationPath(path, path.stem().string());
                m_pendingSave = false;
            } else if (FilePicker::consumeCloseEvent("Save Animation")) {
                m_pendingSave = false;
            }
        }
    }
    ImGui::End();
}

void AnimationTimelinePanel::renderHeader() {
    Animation::SkinnedPose* pose = m_drivingBones ? selectedPose() : nullptr;
    if (pose) {
        const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
        ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1.0f), "%s", m_driveName.empty() ? "Pose" : m_driveName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  %d bones  %.2fs", pose->humanoid.matched ? "humanoid" : "skeletal",
                            skin ? static_cast<int>(skin->boneNames.size()) : 0, m_timelineDuration);
    } else if (m_clip) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", m_clip->name.cStr());
        ImGui::SameLine();
        ImGui::TextDisabled("%.2fs  %u fps", m_clip->duration(), m_clip->fps);
    } else {
        ImGui::TextDisabled("No clip");
    }

    ImGui::SameLine(0, 12.0f);
    if (m_isPlaying) {
        if (ImGui::SmallButton("  Pause  ")) pause();
    } else {
        if (ImGui::SmallButton("  Play  ")) play();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("  Stop  ")) stop();
    ImGui::SameLine(0, 12.0f);
    ImGui::Checkbox("Loop", &m_looping);
    if (m_clip) m_clip->loop = m_looping;
    ImGui::SameLine();
    ImGui::Checkbox("Onion Skin", &m_onionSkinningEnabled);
    ImGui::SameLine();
    if (ImGui::SmallButton("New")) {
        if (pose) ImGui::OpenPopup("##timeline_new");
        else newClip();
    }
    if (ImGui::BeginPopup("##timeline_new")) {
        if (pose) {
            if (ImGui::MenuItem("Empty Timeline")) {
                pose->strips.clear();
                pose->keys.clear();
                pose->objectKeys.clear();
                m_selectedStrip = -1;
                m_keySelected = false;
                stripsEdited();
            }
            if (ImGui::MenuItem("Active Clip")) {
                pose->strips.clear();
                pose->stripsInitialized = false;
                Animation::ensureDefaultStrips(*pose, Animation::findImportedSkin(pose->meshPath));
                m_selectedStrip = pose->strips.empty() ? -1 : 0;
                stripsEdited();
            }
            if (ImGui::MenuItem("All Imported Clips")) {
                pose->strips.clear();
                pose->stripsInitialized = true;
                const Animation::ImportedSkin* imported = Animation::findImportedSkin(pose->meshPath);
                f32 start = 0.0f;
                for (i32 clip = 0; imported && clip < static_cast<i32>(imported->clips.size()); ++clip) {
                    Animation::ClipStrip strip;
                    strip.clip = clip;
                    strip.start = start;
                    strip.loopBlend = std::min(0.12f, Animation::clipStripLength(strip, imported) * 0.12f);
                    pose->strips.push_back(strip);
                    start += Animation::clipStripLength(strip, imported);
                }
                m_selectedStrip = pose->strips.empty() ? -1 : 0;
                stripsEdited();
            }
            const bool hasAnimator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene)) != nullptr;
            if (ImGui::MenuItem("From Animator", nullptr, false, hasAnimator)) buildStripsFromAnimator();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open...")) {
        m_openLoadPopup = true;
        m_assetFilter[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Save...")) {
        m_openSavePopup = true;
        if (m_assetName[0] == '\0') {
            std::string base = pose ? std::filesystem::path(pose->meshPath).stem().string() : std::string(m_motion.name);
            if (base.empty()) base = "Animation";
            std::snprintf(m_assetName, sizeof(m_assetName), "%s", base.c_str());
        }
    }
    if (m_timelineDuration > 0.0f) {
        ImGui::SameLine(0, 16.0f);
        ImGui::TextDisabled("Time: %.3f / %.2f", m_currentTime, m_timelineDuration);
    }
    if (!m_assetStatus.empty()) {
        ImGui::SameLine(0, 16.0f);
        ImGui::TextDisabled("%s", m_assetStatus.c_str());
    }
}

void AnimationTimelinePanel::renderAssetPopups() {
    if (m_openSavePopup) {
        ImGui::OpenPopup("Save Animation Asset");
        m_openSavePopup = false;
    }
    if (m_openLoadPopup) {
        ImGui::OpenPopup("Open Animation Asset");
        m_openLoadPopup = false;
    }
    const std::filesystem::path folder = animationFolder();
    if (ImGui::BeginPopupModal("Save Animation Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Saved to the project folder assets/raw/Animations");
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", m_assetName, sizeof(m_assetName), ImGuiInputTextFlags_EnterReturnsTrue);
        std::string name = m_assetName;
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".anim") == 0) name.resize(name.size() - 5);
        const bool valid = !name.empty() && name.find_first_of("/\\:*?\"<>|") == std::string::npos;
        const std::filesystem::path path = folder / (name + ".anim");
        std::error_code ec;
        const bool exists = valid && std::filesystem::exists(path, ec);
        if (exists) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s.anim exists. Saving replaces it.", name.c_str());
        if (!valid && !name.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "The name has invalid characters.");
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button(exists ? "Replace" : "Save") || (enter && valid)) {
            saveAnimationPath(path, name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            m_pendingSave = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Open Animation Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::filesystem::path assets = folder.parent_path().parent_path();
        ImGui::SetNextItemWidth(420.0f);
        ImGui::InputTextWithHint("##anim_filter", "Filter", m_assetFilter, sizeof(m_assetFilter));
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        if (std::filesystem::is_directory(assets, ec)) {
            for (std::filesystem::recursive_directory_iterator it(
                     assets, std::filesystem::directory_options::skip_permission_denied, ec), end;
                 it != end && files.size() < 500; it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file(ec) && it->path().extension() == ".anim") files.push_back(it->path());
            }
        }
        std::sort(files.begin(), files.end());
        std::string filter = m_assetFilter;
        std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return std::tolower(c); });
        std::string chosen;
        ImGui::BeginChild("##anim_files", ImVec2(420.0f, 240.0f), true);
        int shown = 0;
        for (const std::filesystem::path& file : files) {
            const std::string relative = file.lexically_relative(assets.parent_path()).generic_string();
            std::string lower = relative;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!filter.empty() && lower.find(filter) == std::string::npos) continue;
            ++shown;
            const bool pose = Animation::isPoseAnimationFile(file.string());
            ImGui::TextDisabled(pose ? "[3D]" : "[2D]");
            ImGui::SameLine();
            if (ImGui::Selectable(relative.c_str())) chosen = file.string();
        }
        if (shown == 0) ImGui::TextDisabled(files.empty() ? "No .anim files in this project yet." : "Nothing matches the filter.");
        ImGui::EndChild();
        if (!chosen.empty()) {
            openAnimationPath(chosen);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Browse...")) {
            m_pendingLoad = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

struct RowKey {
    usize index = 0;
    f32 time = 0.0f;
};

static void collectRowKeys(const Animation::SkinnedPose& pose, i32 rowBone, std::vector<RowKey>& out) {
    out.clear();
    if (rowBone < 0) {
        for (usize i = 0; i < pose.objectKeys.size(); ++i) out.push_back({i, pose.objectKeys[i].time});
    } else {
        for (usize i = 0; i < pose.keys.size(); ++i) {
            if (pose.keys[i].bone == rowBone) out.push_back({i, pose.keys[i].time});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const RowKey& a, const RowKey& b) { return a.time < b.time; });
}

static Animation::KeyCurve* rowKeyCurve(Animation::SkinnedPose& pose, i32 rowBone, usize index) {
    if (rowBone < 0) return index < pose.objectKeys.size() ? &pose.objectKeys[index].curve : nullptr;
    if (index >= pose.keys.size() || pose.keys[index].bone != rowBone) return nullptr;
    return &pose.keys[index].curve;
}

static f32* rowKeyTime(Animation::SkinnedPose& pose, i32 rowBone, usize index) {
    if (rowBone < 0) return index < pose.objectKeys.size() ? &pose.objectKeys[index].time : nullptr;
    if (index >= pose.keys.size() || pose.keys[index].bone != rowBone) return nullptr;
    return &pose.keys[index].time;
}

static void applyInterp(Animation::KeyCurve& curve, Animation::KeyInterp mode) {
    if (mode == Animation::KeyInterp::Custom) {
        if (curve.mode != Animation::KeyInterp::Custom) {
            Animation::KeyCurve shaped = curve.mode == Animation::KeyInterp::Linear ||
                                                 curve.mode == Animation::KeyInterp::Constant
                                             ? Animation::KeyCurve{}
                                             : Animation::keyCurvePreset(curve.mode);
            curve = shaped;
        }
        curve.mode = Animation::KeyInterp::Custom;
        return;
    }
    curve = Animation::keyCurvePreset(mode);
}

static const Animation::KeyInterp k_InterpModes[] = {
    Animation::KeyInterp::Linear,   Animation::KeyInterp::EaseIn,    Animation::KeyInterp::EaseOut,
    Animation::KeyInterp::EaseInOut, Animation::KeyInterp::Constant, Animation::KeyInterp::Custom,
};

static ImU32 clipFill(int index, bool active) {
    static const ImU32 colors[] = {
        IM_COL32(86, 114, 156, 255),
        IM_COL32(78, 132, 112, 255),
        IM_COL32(156, 118, 78, 255),
        IM_COL32(132, 96, 150, 255),
    };
    ImU32 color = colors[index % 4];
    if (!active) color = (color & 0x00FFFFFFu) | 0xAA000000u;
    return color;
}

static ImU32 propertyFill(int index, int alpha = 235) {
    static const ImU32 colors[] = {
        IM_COL32(196, 92, 140, 255),
        IM_COL32(64, 158, 96, 255),
        IM_COL32(196, 148, 58, 255),
        IM_COL32(72, 132, 196, 255),
    };
    const int slot = index < 0 ? -index : index;
    return (colors[slot % 4] & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << 24);
}

void AnimationTimelinePanel::renderTimeline() {
    f32 duration = m_timelineDuration;
    if (duration <= 0.0f && m_clip) duration = m_clip->duration();
    if (duration <= 0.0f && !m_clip && !m_drivingBones) {
        ImGui::Spacing();
        ImGui::TextDisabled("  No animation clip selected.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 0.7f), "  Tracks");
        ImGui::TextDisabled("  Select a skinned mesh, or add a clip.");
        return;
    }
    if (duration <= 0.0f) duration = 1.0f;

    Animation::SkinnedPose* pose = m_drivingBones ? selectedPose() : nullptr;
    const bool boneMode = pose != nullptr;
    const Animation::ImportedSkin* skin = pose ? Animation::findImportedSkin(pose->meshPath) : nullptr;
    const i32 selectedRow = m_objectRow ? -1 : m_editBone;
    if (boneMode) duration = std::max(duration + std::max(0.5f, duration * 0.25f), 1.0f);
    if (boneMode && (m_selectedStrip < -1 || m_selectedStrip >= static_cast<i32>(pose->strips.size()))) m_selectedStrip = -1;

    if (boneMode) {
        ECS::Entity selected(m_selectedEntity, m_scene);
        auto entityPosition = [&]() -> Vec3 {
            if (const ECS::Position3D* position3 = m_scene->get<ECS::Position3D>(selected)) return position3->position;
            if (const ECS::Transform* transform = m_scene->get<ECS::Transform>(selected)) return transform->position;
            return {};
        };
        auto recordKey = [&]() {
            const auto sameTime = [&](f32 time) { return std::fabs(time - m_currentTime) < 0.02f; };
            if (m_objectRow || m_editBone < 0) {
                for (usize i = 0; i < pose->objectKeys.size(); ++i) {
                    if (sameTime(pose->objectKeys[i].time)) {
                        pose->objectKeys[i].position = entityPosition();
                        m_selectedKeyframe = i;
                        m_keySelected = true;
                        return;
                    }
                }
                Animation::ObjectKeyframe key;
                key.time = m_currentTime;
                key.position = entityPosition();
                pose->objectKeys.push_back(key);
                m_objectRow = true;
                m_selectedKeyframe = pose->objectKeys.size() - 1;
                m_keySelected = true;
                return;
            }
            const u32 boneCount = skin ? skin->skeleton.boneCount() : 0;
            if (m_editBone >= static_cast<i32>(boneCount)) return;
            if (pose->offsets.size() < boneCount) pose->offsets.resize(boneCount);
            const Animation::BonePose& live = pose->offsets[static_cast<size_t>(m_editBone)];
            for (usize i = 0; i < pose->keys.size(); ++i) {
                Animation::BoneKeyframe& key = pose->keys[i];
                if (key.bone == m_editBone && sameTime(key.time)) {
                    key.rotation = live.rotation;
                    key.translation = live.translation;
                    m_selectedKeyframe = i;
                    m_keySelected = true;
                    return;
                }
            }
            Animation::BoneKeyframe key;
            key.time = m_currentTime;
            key.bone = m_editBone;
            key.rotation = live.rotation;
            key.translation = live.translation;
            pose->keys.push_back(key);
            m_selectedKeyframe = pose->keys.size() - 1;
            m_keySelected = true;
        };
        if (ImGui::Button("Key")) recordKey();
        ImGui::SameLine();
        Animation::KeyCurve* curve = m_keySelected ? rowKeyCurve(*pose, selectedRow, m_selectedKeyframe) : nullptr;
        if (!curve) m_keySelected = false;
        if (ImGui::Button("Delete") && (curve || m_selectedStrip >= 0 || !pose->keys.empty() || !pose->objectKeys.empty())) {
            if (curve) {
                if (selectedRow < 0) {
                    pose->objectKeys.erase(pose->objectKeys.begin() + static_cast<std::ptrdiff_t>(m_selectedKeyframe));
                } else {
                    pose->keys.erase(pose->keys.begin() + static_cast<std::ptrdiff_t>(m_selectedKeyframe));
                }
                m_keySelected = false;
                curve = nullptr;
            } else if (Animation::deleteKeysNearTime(*pose, selectedRow, m_currentTime)) {
                m_keySelected = false;
            } else if (m_selectedStrip >= 0) {
                deleteSelectedStrip();
            }
            m_applyTime = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Deletes the selected key, keys at the playhead, or the selected clip.");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_selectedStrip < 0 || !skin || m_editBone < 0);
        if (ImGui::Button("Edit Keys") && m_selectedStrip >= 0 && skin && m_editBone >= 0) {
            Animation::bakeClipToKeys(*pose, *skin, pose->strips[static_cast<size_t>(m_selectedStrip)].clip, m_editBone);
            m_objectRow = false;
            m_keySelected = false;
            m_applyTime = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(m_selectedStrip < 0 ? "Select a clip first."
                                                  : m_editBone < 0 ? "Select a bone in the viewport or the inspector, then copy its keys."
                                                                   : "Copies this bone's channel from the clip onto editable keys.");
        }
        if (curve) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            int mode = static_cast<int>(curve->mode);
            const char* modes[] = {"Linear", "Constant", "Ease In", "Ease Out", "Ease In Out", "Custom"};
            if (ImGui::Combo("Interpolation", &mode, modes, 6)) {
                applyInterp(*curve, static_cast<Animation::KeyInterp>(mode));
                m_applyTime = true;
            }
        }

        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::Button("+ Clip")) ImGui::OpenPopup("##timeline_add_clip");
        if (ImGui::BeginPopup("##timeline_add_clip")) {
            const i32 track = m_selectedStrip >= 0 ? pose->strips[static_cast<size_t>(m_selectedStrip)].track : 0;
            ImGui::TextDisabled("Add at %.2fs", m_currentTime);
            for (size_t i = 0; skin && i < skin->clipNames.size(); ++i) {
                if (ImGui::MenuItem(skin->clipNames[i].c_str())) {
                    Animation::ClipStrip strip;
                    strip.clip = static_cast<i32>(i);
                    strip.track = track;
                    strip.start = m_currentTime;
                    pose->strips.push_back(strip);
                    m_selectedStrip = static_cast<i32>(pose->strips.size()) - 1;
                    m_keySelected = false;
                    stripsEdited();
                }
            }
            if (!skin || skin->clipNames.empty()) ImGui::TextDisabled("This mesh has no imported clips.");
            ImGui::EndPopup();
        }
        const bool hasStrip = m_selectedStrip >= 0;
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasStrip);
        if (ImGui::Button("Split")) splitSelectedStrip();
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) duplicateSelectedStrip();
        ImGui::SameLine();
        if (ImGui::Button("Delete Clip")) deleteSelectedStrip();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("+ Track")) ++m_extraTracks;
        ImGui::SameLine();
        const bool hasAnimator = m_scene->get<Animation::Animator>(selected) != nullptr;
        ImGui::BeginDisabled(!hasAnimator);
        if (ImGui::Button("From Animator")) buildStripsFromAnimator();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(hasAnimator ? "Lay out the base track by following the Animator from its default state."
                                          : "Add an Animator to this entity first.");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_snap);
    } else {
        if (ImGui::Button("+ Track")) {
            auto track = std::make_unique<TransformTrack>();
            track->targetPropertyName = FixedString<32>("Track");
            m_tracks.push_back(std::move(track));
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Key") && m_scene) {
            ECS::Entity selected(m_selectedEntity, m_scene);
            Vec3 position{};
            if (const ECS::Position3D* position3 = m_scene->get<ECS::Position3D>(selected)) position = position3->position;
            else if (const ECS::Transform* transform = m_scene->get<ECS::Transform>(selected)) position = transform->position;
            AnimationTrack* track = m_selectedTrack < m_tracks.size() ? m_tracks[m_selectedTrack].get() : nullptr;
            if (!track || track->getType() != TrackType::Transform) {
                track = nullptr;
                for (const std::unique_ptr<AnimationTrack>& existing : m_tracks) {
                    if (existing && existing->getType() == TrackType::Transform) {
                        track = existing.get();
                        break;
                    }
                }
            }
            if (!track) {
                auto created = std::make_unique<TransformTrack>();
                created->targetPropertyName = FixedString<32>("Position");
                m_tracks.push_back(std::move(created));
                track = m_tracks.back().get();
                m_selectedTrack = m_tracks.size() - 1;
            }
            track->addKeyframe(m_currentTime, position);
        }
        ImGui::SameLine();
        if (ImGui::Button("Del Key")) deleteSelectedKeyframe();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiIO& io = ImGui::GetIO();
    const f32 minSpan = std::min(0.05f, duration);
    f32 viewSpan = std::clamp(m_viewSpan > 0.0f ? m_viewSpan : duration, minSpan, duration);

    ImGui::SameLine(0.0f, 16.0f);
    if (ImGui::SmallButton("-")) viewSpan = std::min(duration, viewSpan * 1.25f);
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) viewSpan = std::max(minSpan, viewSpan * 0.8f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) {
        viewSpan = duration;
        m_viewStart = 0.0f;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f%%", duration / std::max(viewSpan, 0.0001f) * 100.0f);
    ImGui::TextDisabled(boneMode ? "Drag clips to move, edges to trim, top corners to fade. "
                                   "Delete removes a key or clip. Edit Keys copies the clip onto the timeline. Ctrl+wheel zooms."
                                 : "Wheel scrolls tracks.  Ctrl+wheel zooms.  Shift+wheel or middle drag pans.");

    if (boneMode && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            if (m_keySelected) {
                if (selectedRow < 0 && m_selectedKeyframe < pose->objectKeys.size()) {
                    pose->objectKeys.erase(pose->objectKeys.begin() + static_cast<std::ptrdiff_t>(m_selectedKeyframe));
                } else if (m_selectedKeyframe < pose->keys.size()) {
                    pose->keys.erase(pose->keys.begin() + static_cast<std::ptrdiff_t>(m_selectedKeyframe));
                }
                m_keySelected = false;
                m_applyTime = true;
            } else if (Animation::deleteKeysNearTime(*pose, selectedRow, m_currentTime)) {
                m_applyTime = true;
            } else if (m_selectedStrip >= 0) {
                deleteSelectedStrip();
            }
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelectedStrip();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) splitSelectedStrip();
    }

    const f32 inspectorW = boneMode ? 300.0f : 0.0f;
    const f32 fullWidth = ImGui::GetContentRegionAvail().x;
    const f32 availWidth = std::max(240.0f, fullWidth - (boneMode ? inspectorW + style.ItemSpacing.x : 0.0f));
    const f32 regionH = std::max(ImGui::GetContentRegionAvail().y, k_TrackHeight * 3.0f);

    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const f32 trackWidth = std::max(1.0f, availWidth - k_TrackLabelWidth - style.ScrollbarSize);
    const f32 laneLeft = cursor.x + k_TrackLabelWidth;
    const f32 laneRight = laneLeft + trackWidth;

    f32 viewStart = std::clamp(m_viewStart, 0.0f, std::max(0.0f, duration - viewSpan));
    const bool overLanes = io.MousePos.x >= cursor.x && io.MousePos.x <= cursor.x + availWidth;
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && overLanes) {
        const f32 pointer = std::clamp((io.MousePos.x - laneLeft) / trackWidth, 0.0f, 1.0f);
        const f32 pointerTime = viewStart + pointer * viewSpan;
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            viewSpan = std::clamp(viewSpan * (io.MouseWheel > 0.0f ? 0.8f : 1.25f), minSpan, duration);
            viewStart = pointerTime - pointer * viewSpan;
        } else if (io.MouseWheelH != 0.0f || (io.KeyShift && io.MouseWheel != 0.0f)) {
            const f32 delta = io.MouseWheelH != 0.0f ? io.MouseWheelH : io.MouseWheel;
            viewStart -= delta * viewSpan * 0.12f;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            viewStart -= (io.MouseDelta.x / trackWidth) * viewSpan;
        }
    }
    viewStart = std::clamp(viewStart, 0.0f, std::max(0.0f, duration - viewSpan));
    m_viewStart = viewStart;
    m_viewSpan = viewSpan;

    auto timeToX = [&](f32 t) -> f32 { return laneLeft + ((t - viewStart) / viewSpan) * trackWidth; };
    auto xToTime = [&](f32 x) -> f32 { return viewStart + ((x - laneLeft) / trackWidth) * viewSpan; };

    dl->AddRectFilled(cursor, ImVec2(cursor.x + availWidth, cursor.y + k_RulerHeight), k_ColRulerBg);
    dl->PushClipRect(ImVec2(laneLeft, cursor.y), ImVec2(laneRight, cursor.y + k_RulerHeight), true);
    {
        const int numTicks = std::max(1, static_cast<int>(trackWidth / 48.0f));
        const f32 tickInterval = viewSpan / static_cast<f32>(numTicks);
        const int firstTick = static_cast<int>(std::ceil(viewStart / tickInterval));
        for (int tick = firstTick; tick * tickInterval <= viewStart + viewSpan + 0.0001f; ++tick) {
            const f32 t = static_cast<f32>(tick) * tickInterval;
            const f32 x = timeToX(t);
            const bool isMajor = (tick % 5 == 0);
            const f32 tickH = isMajor ? k_RulerHeight * 0.6f : k_RulerHeight * 0.3f;
            dl->AddLine(ImVec2(x, cursor.y + k_RulerHeight - tickH), ImVec2(x, cursor.y + k_RulerHeight), k_ColRulerTick);
            if (isMajor) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.2f", t);
                dl->AddText(ImVec2(x + 2.0f, cursor.y + 2.0f), k_ColRulerText, buf);
            }
        }
        if (boneMode && m_timelineDuration > 0.0f) {
            const f32 endX = timeToX(m_timelineDuration);
            dl->AddLine(ImVec2(endX, cursor.y), ImVec2(endX, cursor.y + k_RulerHeight), IM_COL32(230, 120, 90, 200), 1.5f);
        }
        const f32 px = timeToX(m_currentTime);
        dl->AddLine(ImVec2(px, cursor.y), ImVec2(px, cursor.y + k_RulerHeight), k_ColPlayhead, 1.0f);
        dl->AddTriangleFilled(ImVec2(px - 5.0f, cursor.y), ImVec2(px + 5.0f, cursor.y), ImVec2(px, cursor.y + 8.0f),
                              k_ColPlayhead);
    }
    dl->PopClipRect();
    {
        char timeText[32];
        snprintf(timeText, sizeof(timeText), "%.3fs", m_currentTime);
        dl->AddText(ImVec2(cursor.x + 6.0f, cursor.y + 3.0f), k_ColRulerText, timeText);
    }
    ImGui::InvisibleButton("ruler_click", ImVec2(availWidth, k_RulerHeight));
    if (ImGui::IsItemActive() && io.MouseClickedPos[0].x >= laneLeft) {
        m_currentTime = std::clamp(xToTime(io.MousePos.x), 0.0f, duration);
        m_applyTime = true;
    }

    const f32 hScrollH = 12.0f;
    const f32 childH = std::max(k_TrackHeight * 2.0f, regionH - k_RulerHeight - hScrollH - style.ItemSpacing.y * 2.0f);
    ImGui::BeginChild("##timeline_tracks", ImVec2(availWidth, childH), false,
                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f && !io.KeyCtrl && !io.KeyShift && io.MouseWheelH == 0.0f) {
        ImGui::SetScrollY(std::clamp(ImGui::GetScrollY() - io.MouseWheel * k_TrackHeight * 2.0f, 0.0f, ImGui::GetScrollMaxY()));
    }
    if (boneMode) renderBoneRows(laneLeft, trackWidth, duration);
    else renderSpriteRows(laneLeft, trackWidth, duration);
    {
        ImDrawList* childDraw = ImGui::GetWindowDrawList();
        const ImVec2 childPos = ImGui::GetWindowPos();
        const f32 px = timeToX(m_currentTime);
        if (px >= laneLeft && px <= laneRight) {
            childDraw->AddLine(ImVec2(px, childPos.y), ImVec2(px, childPos.y + ImGui::GetWindowHeight()), k_ColPlayhead, 1.5f);
        }
    }
    ImGui::EndChild();

    {
        const ImVec2 bar = ImGui::GetCursorScreenPos();
        const ImVec2 trackMin(laneLeft, bar.y + 2.0f);
        const ImVec2 trackMax(laneRight, bar.y + hScrollH - 2.0f);
        dl->AddRectFilled(trackMin, trackMax, IM_COL32(28, 30, 36, 255), 4.0f);
        const f32 thumb0 = laneLeft + (m_viewStart / duration) * trackWidth;
        const f32 thumb1 = laneLeft + ((m_viewStart + m_viewSpan) / duration) * trackWidth;
        ImGui::InvisibleButton("##timeline_hscroll", ImVec2(availWidth, hScrollH));
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(thumb0, trackMin.y), ImVec2(std::max(thumb1, thumb0 + 8.0f), trackMax.y),
                          active ? IM_COL32(150, 160, 180, 255) : hovered ? IM_COL32(120, 128, 146, 255)
                                                                          : IM_COL32(92, 98, 112, 255),
                          4.0f);
        if (ImGui::IsItemClicked() && (io.MousePos.x < thumb0 || io.MousePos.x > thumb1)) {
            const f32 center = ((io.MousePos.x - laneLeft) / trackWidth) * duration;
            m_viewStart = center - m_viewSpan * 0.5f;
        } else if (active) {
            m_viewStart += (io.MouseDelta.x / trackWidth) * duration;
        }
        m_viewStart = std::clamp(m_viewStart, 0.0f, std::max(0.0f, duration - m_viewSpan));
    }
    ImGui::EndGroup();

    if (!boneMode) return;
    bool curvePanel = false;
    if (m_keySelected) {
        std::vector<RowKey> keys;
        collectRowKeys(*pose, selectedRow, keys);
        for (usize i = 0; i + 1 < keys.size(); ++i) {
            if (keys[i].index == m_selectedKeyframe) curvePanel = true;
        }
    }
    ImGui::SameLine();
    ImGui::BeginChild("##timeline_inspector", ImVec2(inspectorW, regionH), true);
    if (curvePanel) {
        renderCurveEditor();
    } else if (m_selectedStrip >= 0) {
        renderStripInspector();
    } else {
        ImGui::TextDisabled("Nothing selected");
        ImGui::Spacing();
        ImGui::TextWrapped("Click a clip to edit its trim, speed, repeat, fades and state binding. "
                           "Click the segment between two keys to edit its curve.");
    }
    ImGui::EndChild();
}

void AnimationTimelinePanel::renderBoneRows(f32 laneLeft, f32 trackWidth, f32 duration) {
    ECS::Entity selected(m_selectedEntity, m_scene);
    Animation::SkinnedPose* pose = m_scene->get<Animation::SkinnedPose>(selected);
    if (!pose) return;
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const f32 rowWidth = ImGui::GetContentRegionAvail().x;
    const f32 laneRight = laneLeft + trackWidth;
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const f32 windowBottom = windowPos.y + ImGui::GetWindowHeight();
    auto timeToX = [&](f32 t) -> f32 { return laneLeft + ((t - m_viewStart) / m_viewSpan) * trackWidth; };
    auto xToTime = [&](f32 x) -> f32 { return m_viewStart + ((x - laneLeft) / trackWidth) * m_viewSpan; };

    auto drawTrackChrome = [&](ImVec2 rowPos, const char* label, bool selectedRow, ImU32 pip, f32 indent) {
        const ImU32 rowBg = selectedRow ? IM_COL32(38, 44, 58, 255) : k_ColTrackBg;
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + rowWidth, rowPos.y + k_TrackHeight), rowBg);
        dl->AddLine(ImVec2(rowPos.x, rowPos.y + k_TrackHeight - 1.0f), ImVec2(rowPos.x + rowWidth, rowPos.y + k_TrackHeight - 1.0f),
                    IM_COL32(22, 22, 28, 255));
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + k_TrackLabelWidth - 2.0f, rowPos.y + k_TrackHeight),
                          selectedRow ? k_ColTrackLabelSel : k_ColTrackLabel);
        dl->AddRectFilled(ImVec2(rowPos.x + indent, rowPos.y + 6.0f),
                          ImVec2(rowPos.x + indent + 4.0f, rowPos.y + k_TrackHeight - 6.0f), pip, 1.0f);
        dl->PushClipRect(rowPos, ImVec2(rowPos.x + k_TrackLabelWidth - 4.0f, rowPos.y + k_TrackHeight), true);
        dl->AddText(ImVec2(rowPos.x + indent + 10.0f, rowPos.y + 6.0f), IM_COL32(230, 234, 240, 255), label);
        dl->PopClipRect();
    };
    auto pushLaneClip = [&](ImVec2 rowPos) {
        dl->PushClipRect(ImVec2(laneLeft, std::max(rowPos.y, windowPos.y)),
                         ImVec2(laneRight, std::min(rowPos.y + k_TrackHeight, windowBottom)), true);
    };

    renderStripRows(laneLeft, trackWidth);

    std::vector<i32> rows;
    rows.push_back(-1);
    if (m_editBone >= 0) rows.push_back(m_editBone);
    for (const Animation::BoneKeyframe& key : pose->keys) {
        if (std::find(rows.begin(), rows.end(), key.bone) == rows.end()) rows.push_back(key.bone);
    }

    bool openContext = false;
    std::vector<RowKey> keys;
    for (i32 rowBone : rows) {
        const bool rowSelected = rowBone < 0 ? m_objectRow : (!m_objectRow && m_editBone == rowBone);
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        std::string label = "Object Position";
        if (rowBone >= 0) {
            label = (skin && rowBone < static_cast<i32>(skin->boneNames.size()))
                        ? skin->boneNames[static_cast<size_t>(rowBone)]
                        : ("Bone " + std::to_string(rowBone));
        }
        drawTrackChrome(rowPos, label.c_str(), rowSelected, propertyFill(rowBone < 0 ? 2 : rowBone), 18.0f);
        ImGui::InvisibleButton(("pose_row_" + std::to_string(rowBone)).c_str(), ImVec2(rowWidth, k_TrackHeight),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool rowHovered = ImGui::IsItemHovered();
        const bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool rowContext = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const bool inLane = io.MousePos.x >= laneLeft && io.MousePos.x <= laneRight;
        bool consumed = false;
        auto selectKey = [&](usize index, bool moveHead) {
            m_objectRow = rowBone < 0;
            m_editBone = rowBone;
            m_pickedBone = rowBone;
            m_selectedKeyframe = index;
            m_keySelected = true;
            if (moveHead) {
                if (const f32* time = rowKeyTime(*pose, rowBone, index)) {
                    m_currentTime = *time;
                    m_applyTime = true;
                }
            }
        };

        collectRowKeys(*pose, rowBone, keys);
        const ImU32 color = propertyFill(rowBone < 0 ? 2 : rowBone, 90);
        const ImU32 colorSelected = propertyFill(rowBone < 0 ? 2 : rowBone, 200);
        pushLaneClip(rowPos);
        const f32 top = rowPos.y + 7.0f;
        const f32 bottom = rowPos.y + k_TrackHeight - 7.0f;
        for (usize k = 0; k + 1 < keys.size(); ++k) {
            const f32 x0 = timeToX(keys[k].time);
            const f32 x1 = timeToX(keys[k + 1].time);
            if (x1 < laneLeft || x0 > laneRight) continue;
            const Animation::KeyCurve* curve = rowKeyCurve(*pose, rowBone, keys[k].index);
            if (!curve) continue;
            const bool segmentSelected = m_keySelected && rowSelected && m_selectedKeyframe == keys[k].index;
            const bool segmentHovered = rowHovered && inLane && io.MousePos.x > x0 + k_KeyDiamondSize + 1.0f &&
                                        io.MousePos.x < x1 - k_KeyDiamondSize - 1.0f;
            dl->AddRectFilled(ImVec2(x0, rowPos.y + 4.0f), ImVec2(x1, rowPos.y + k_TrackHeight - 4.0f),
                              segmentSelected ? colorSelected : (segmentHovered ? propertyFill(rowBone < 0 ? 2 : rowBone, 140) : color), 3.0f);
            ImVec2 points[25];
            for (int s = 0; s < 25; ++s) {
                const f32 u = static_cast<f32>(s) / 24.0f;
                const f32 v = std::clamp(Animation::evaluateKeyCurve(*curve, u), -0.2f, 1.2f);
                points[s] = ImVec2(x0 + (x1 - x0) * u, bottom - (bottom - top) * v);
            }
            dl->AddPolyline(points, 25, IM_COL32(255, 255, 255, segmentSelected ? 235 : 130), 0, segmentSelected ? 1.8f : 1.2f);
            if (segmentHovered) {
                ImGui::SetTooltip("%s  (%.2fs to %.2fs)\nClick to edit the curve. Right-click to change it.",
                                  Animation::keyInterpName(curve->mode), keys[k].time, keys[k + 1].time);
                if (rowClicked) {
                    selectKey(keys[k].index, false);
                    consumed = true;
                }
                if (rowContext) {
                    selectKey(keys[k].index, false);
                    m_contextBone = rowBone;
                    m_contextKey = keys[k].index;
                    openContext = true;
                    consumed = true;
                }
            }
        }
        for (const RowKey& key : keys) {
            const f32 kx = timeToX(key.time);
            if (kx < laneLeft - k_KeyDiamondSize || kx > laneRight + k_KeyDiamondSize) continue;
            const f32 ky = rowPos.y + k_TrackHeight * 0.5f;
            const Animation::KeyCurve* curve = rowKeyCurve(*pose, rowBone, key.index);
            const bool selectedKey = m_keySelected && rowSelected && m_selectedKeyframe == key.index;
            const bool hovered = rowHovered && std::fabs(io.MousePos.x - kx) <= k_KeyDiamondSize + 2.0f &&
                                 std::fabs(io.MousePos.y - ky) <= k_KeyDiamondSize + 2.0f;
            const ImU32 fill = selectedKey ? k_ColKeyframeSel : (hovered ? k_ColKeyframeHover : k_ColKeyframe);
            const f32 size = k_KeyDiamondSize + (selectedKey ? 1.5f : 0.0f);
            if (curve && curve->mode == Animation::KeyInterp::Constant) {
                dl->AddRectFilled(ImVec2(kx - size, ky - size), ImVec2(kx + size, ky + size), fill);
                dl->AddRect(ImVec2(kx - size, ky - size), ImVec2(kx + size, ky + size), IM_COL32(0, 0, 0, 160));
            } else if (curve && curve->mode != Animation::KeyInterp::Linear) {
                dl->AddCircleFilled(ImVec2(kx, ky), size, fill, 16);
                dl->AddCircle(ImVec2(kx, ky), size, IM_COL32(0, 0, 0, 160), 16);
            } else {
                dl->AddQuadFilled(ImVec2(kx - size, ky), ImVec2(kx, ky - size), ImVec2(kx + size, ky), ImVec2(kx, ky + size), fill);
                dl->AddQuad(ImVec2(kx - size, ky), ImVec2(kx, ky - size), ImVec2(kx + size, ky), ImVec2(kx, ky + size),
                            IM_COL32(0, 0, 0, 160));
            }
            if (hovered && rowClicked) {
                selectKey(key.index, true);
                m_isDraggingKeyframe = true;
                consumed = true;
            }
            if (hovered && rowContext) {
                selectKey(key.index, false);
                m_contextBone = rowBone;
                m_contextKey = key.index;
                openContext = true;
                consumed = true;
            }
        }
        dl->PopClipRect();

        if ((rowClicked || rowContext) && !consumed) {
            m_objectRow = rowBone < 0;
            m_editBone = rowBone;
            m_pickedBone = rowBone;
            m_keySelected = false;
            if (rowClicked && inLane) {
                m_currentTime = std::clamp(xToTime(io.MousePos.x), 0.0f, duration);
                m_applyTime = true;
            }
        }
    }

    if (m_isDraggingKeyframe) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_isDraggingKeyframe = false;
        } else if (m_keySelected && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            if (f32* time = rowKeyTime(*pose, m_objectRow ? -1 : m_editBone, m_selectedKeyframe)) {
                f32 newTime = std::clamp(xToTime(io.MousePos.x), 0.0f, duration);
                if (!io.KeyAlt) newTime = std::round(newTime * 60.0f) / 60.0f;
                *time = newTime;
                m_currentTime = newTime;
                m_applyTime = true;
            }
        }
    }

    if (openContext) ImGui::OpenPopup("##timeline_key_ctx");
    if (ImGui::BeginPopup("##timeline_key_ctx")) {
        Animation::KeyCurve* curve = rowKeyCurve(*pose, m_contextBone, m_contextKey);
        if (!curve) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextDisabled("Interpolation to the next key");
            for (Animation::KeyInterp mode : k_InterpModes) {
                if (ImGui::MenuItem(Animation::keyInterpName(mode), nullptr, curve->mode == mode)) {
                    applyInterp(*curve, mode);
                    m_applyTime = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Key")) {
                if (m_contextBone < 0) pose->objectKeys.erase(pose->objectKeys.begin() + static_cast<std::ptrdiff_t>(m_contextKey));
                else pose->keys.erase(pose->keys.begin() + static_cast<std::ptrdiff_t>(m_contextKey));
                m_keySelected = false;
                m_applyTime = true;
            }
        }
        ImGui::EndPopup();
    }
}

void AnimationTimelinePanel::renderSpriteRows(f32 laneLeft, f32 trackWidth, f32 duration) {
    if (m_tracks.empty()) {
        ImGui::TextDisabled("  No tracks. Use '+ Track' to add one.");
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const f32 rowWidth = ImGui::GetContentRegionAvail().x;
    const f32 laneRight = laneLeft + trackWidth;
    auto timeToX = [&](f32 t) -> f32 { return laneLeft + ((t - m_viewStart) / m_viewSpan) * trackWidth; };
    auto xToTime = [&](f32 x) -> f32 { return m_viewStart + ((x - laneLeft) / trackWidth) * m_viewSpan; };

    for (usize i = 0; i < m_tracks.size(); ++i) {
        auto& track = m_tracks[i];
        const bool isSelected = (i == m_selectedTrack);
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + rowWidth, rowPos.y + k_TrackHeight), (i % 2 == 0) ? k_ColTrackBg : k_ColTrackBgAlt);
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + k_TrackLabelWidth - 2.0f, rowPos.y + k_TrackHeight),
                          isSelected ? k_ColTrackLabelSel : k_ColTrackLabel);
        const char* typeTag = track->getType() == TrackType::Sprite    ? "[S]"
                              : track->getType() == TrackType::Transform ? "[T]"
                              : track->getType() == TrackType::Event     ? "[E]"
                                                                         : "[?]";
        dl->AddText(ImVec2(rowPos.x + 4.0f, rowPos.y + 6.0f), IM_COL32(200, 200, 200, 255), typeTag);
        dl->AddText(ImVec2(rowPos.x + 28.0f, rowPos.y + 6.0f), IM_COL32(220, 220, 230, 255), track->targetPropertyName.cStr());

        ImGui::InvisibleButton(("track_row_" + std::to_string(i)).c_str(), ImVec2(rowWidth, k_TrackHeight),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool rowHovered = ImGui::IsItemHovered();
        const bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool rowContext = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        bool consumed = false;

        dl->PushClipRect(ImVec2(laneLeft, rowPos.y), ImVec2(laneRight, rowPos.y + k_TrackHeight), true);
        for (usize j = 0; j < track->keyframes.size(); ++j) {
            const f32 kx = timeToX(track->keyframes[j].time);
            const f32 ky = rowPos.y + k_TrackHeight * 0.5f;
            const bool kfSelected = isSelected && (j == m_selectedKeyframe);
            const bool hovered = rowHovered && std::fabs(io.MousePos.x - kx) <= k_KeyDiamondSize + 2.0f &&
                                 io.MousePos.x >= laneLeft && io.MousePos.x <= laneRight;
            const ImU32 color = kfSelected ? k_ColKeyframeSel : hovered ? k_ColKeyframeHover : k_ColKeyframe;
            dl->AddQuadFilled(ImVec2(kx - k_KeyDiamondSize, ky), ImVec2(kx, ky - k_KeyDiamondSize),
                              ImVec2(kx + k_KeyDiamondSize, ky), ImVec2(kx, ky + k_KeyDiamondSize), color);
            if (hovered && rowClicked) {
                m_selectedTrack = i;
                m_selectedKeyframe = j;
                m_isDraggingKeyframe = true;
                consumed = true;
            }
            if (hovered && rowContext) {
                m_selectedTrack = i;
                m_selectedKeyframe = j;
                deleteSelectedKeyframe();
                consumed = true;
                break;
            }
        }
        dl->PopClipRect();
        if (rowClicked && !consumed) {
            m_selectedTrack = i;
            if (io.MousePos.x >= laneLeft) {
                m_currentTime = std::clamp(xToTime(io.MousePos.x), 0.0f, duration);
                m_applyTime = true;
            }
        }
    }

    if (m_isDraggingKeyframe) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_isDraggingKeyframe = false;
        else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            moveSelectedKeyframe(std::clamp(xToTime(io.MousePos.x), 0.0f, duration));
        }
    }

    if (m_selectedTrack < m_tracks.size()) {
        auto& track = m_tracks[m_selectedTrack];
        ImGui::Text("Selected: %s   Keyframes: %zu", track->targetPropertyName.cStr(), track->keyframes.size());
        if (m_selectedKeyframe < track->keyframes.size()) {
            ImGui::SameLine(0, 16.0f);
            ImGui::TextDisabled("Key %zu @ %.3fs", m_selectedKeyframe, track->keyframes[m_selectedKeyframe].time);
        }
    }
}

void AnimationTimelinePanel::renderCurveEditor() {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose) return;
    const i32 rowBone = m_objectRow ? -1 : m_editBone;
    std::vector<RowKey> keys;
    collectRowKeys(*pose, rowBone, keys);
    usize nextIndex = 0;
    bool found = false;
    for (usize i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i].index == m_selectedKeyframe) {
            nextIndex = keys[i + 1].index;
            found = true;
            break;
        }
    }
    Animation::KeyCurve* curve = rowKeyCurve(*pose, rowBone, m_selectedKeyframe);
    f32* startTime = rowKeyTime(*pose, rowBone, m_selectedKeyframe);
    f32* endTime = rowKeyTime(*pose, rowBone, nextIndex);
    if (!found || !curve || !startTime || !endTime) return;

    const f32 width = ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("Key Curve");
    ImGui::TextDisabled("%.3fs to %.3fs", *startTime, *endTime);
    if (editKeyCurve("key_curve", *curve, width, 140.0f)) m_applyTime = true;
    ImGui::Separator();
    ImGui::PushItemWidth(width - 90.0f);
    if (ImGui::DragFloat("Start", startTime, 0.005f, 0.0f, *endTime, "%.3fs")) m_applyTime = true;
    if (ImGui::DragFloat("End", endTime, 0.005f, *startTime, 3600.0f, "%.3fs")) m_applyTime = true;
    if (curve->mode == Animation::KeyInterp::Custom) {
        f32 handles[4] = {curve->x1, curve->y1, curve->x2, curve->y2};
        if (ImGui::DragFloat4("Handles", handles, 0.005f)) {
            curve->x1 = std::clamp(handles[0], 0.0f, 1.0f);
            curve->y1 = handles[1];
            curve->x2 = std::clamp(handles[2], 0.0f, 1.0f);
            curve->y2 = handles[3];
            m_applyTime = true;
        }
    }
    if (rowBone >= 0 && m_selectedKeyframe < pose->keys.size()) {
        Animation::BoneKeyframe& key = pose->keys[m_selectedKeyframe];
        constexpr f32 kToDeg = 57.2957795f;
        f32 degrees[3] = {key.rotation.x * kToDeg, key.rotation.y * kToDeg, key.rotation.z * kToDeg};
        if (ImGui::DragFloat3("Rotation", degrees, 0.5f, -360.0f, 360.0f, "%.1f")) {
            key.rotation = Vec3(degrees[0] / kToDeg, degrees[1] / kToDeg, degrees[2] / kToDeg);
            m_applyTime = true;
        }
        if (ImGui::DragFloat3("Offset", &key.translation.x, 0.01f)) m_applyTime = true;
    } else if (rowBone < 0 && m_selectedKeyframe < pose->objectKeys.size()) {
        if (ImGui::DragFloat3("Position", &pose->objectKeys[m_selectedKeyframe].position.x, 0.01f)) m_applyTime = true;
    }
    ImGui::PopItemWidth();
}

void AnimationTimelinePanel::renderStripRows(f32 laneLeft, f32 trackWidth) {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose) return;
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    Animation::Animator* animator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene));
    const AnimationEditorLink& link = AnimationEditorLink::get();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const f32 rowWidth = ImGui::GetContentRegionAvail().x;
    const f32 laneRight = laneLeft + trackWidth;
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const f32 windowBottom = windowPos.y + ImGui::GetWindowHeight();
    constexpr f32 rowH = 38.0f;
    auto timeToX = [&](f32 t) -> f32 { return laneLeft + ((t - m_viewStart) / m_viewSpan) * trackWidth; };
    auto xToTime = [&](f32 x) -> f32 { return m_viewStart + ((x - laneLeft) / trackWidth) * m_viewSpan; };
    auto clipName = [&](i32 clip) -> const char* {
        return (skin && clip >= 0 && clip < static_cast<i32>(skin->clipNames.size()))
                   ? skin->clipNames[static_cast<size_t>(clip)].c_str()
                   : "Clip";
    };
    if (m_selectedStrip >= static_cast<i32>(pose->strips.size())) m_selectedStrip = -1;

    i32 trackCount = 1 + m_extraTracks;
    for (const Animation::ClipStrip& strip : pose->strips) trackCount = std::max(trackCount, strip.track + 1);
    m_stripRowsTop = ImGui::GetCursorScreenPos().y;

    const f32 snapRadius = 8.0f / trackWidth * m_viewSpan;
    auto snapTime = [&](f32 t, i32 skip, bool& snapped) -> f32 {
        snapped = false;
        if (!m_snap || io.KeyAlt) return t;
        f32 best = t;
        f32 bestDistance = snapRadius;
        auto consider = [&](f32 candidate) {
            const f32 distance = std::fabs(candidate - t);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
                snapped = true;
            }
        };
        consider(0.0f);
        consider(m_currentTime);
        for (size_t i = 0; i < pose->strips.size(); ++i) {
            if (static_cast<i32>(i) == skip) continue;
            consider(pose->strips[i].start);
            consider(pose->strips[i].start + Animation::clipStripLength(pose->strips[i], skin));
        }
        return best;
    };

    bool openStripMenu = false;
    for (i32 track = 0; track < trackCount; ++track) {
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + rowWidth, rowPos.y + rowH), track % 2 ? k_ColTrackBgAlt : k_ColTrackBg);
        dl->AddLine(ImVec2(rowPos.x, rowPos.y + rowH - 1.0f), ImVec2(rowPos.x + rowWidth, rowPos.y + rowH - 1.0f),
                    IM_COL32(22, 22, 28, 255));
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + k_TrackLabelWidth - 2.0f, rowPos.y + rowH), k_ColTrackLabel);
        dl->AddRectFilled(ImVec2(rowPos.x + 8.0f, rowPos.y + 8.0f), ImVec2(rowPos.x + 12.0f, rowPos.y + rowH - 8.0f),
                          IM_COL32(120, 170, 230, 255), 1.0f);
        const std::string label = track == 0 ? "Clips" : "Clips " + std::to_string(track + 1);
        dl->AddText(ImVec2(rowPos.x + 18.0f, rowPos.y + 4.0f), IM_COL32(230, 234, 240, 255), label.c_str());
        dl->AddText(ImVec2(rowPos.x + 18.0f, rowPos.y + 20.0f), IM_COL32(140, 146, 160, 255),
                    track == 0 ? "synced with Animator" : "blends over lower");

        ImGui::InvisibleButton(("strip_row_" + std::to_string(track)).c_str(), ImVec2(rowWidth, rowH),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool rowHovered = ImGui::IsItemHovered();
        const bool clickedLeft = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool clickedRight = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const bool doubleClicked = rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        const bool inLane = io.MousePos.x >= laneLeft && io.MousePos.x <= laneRight;
        const f32 top = rowPos.y + 3.0f;
        const f32 bottom = rowPos.y + rowH - 3.0f;

        dl->PushClipRect(ImVec2(laneLeft, std::max(rowPos.y, windowPos.y)),
                         ImVec2(laneRight, std::min(rowPos.y + rowH, windowBottom)), true);
        i32 hit = -1;
        StripDrag hitZone = StripDrag::None;
        for (size_t i = 0; i < pose->strips.size(); ++i) {
            const Animation::ClipStrip& strip = pose->strips[i];
            if (strip.track != track) continue;
            const f32 length = Animation::clipStripLength(strip, skin);
            const f32 x0 = timeToX(strip.start);
            const f32 x1 = timeToX(strip.start + length);
            const bool isSelected = static_cast<i32>(i) == m_selectedStrip;
            const std::string state = stripStateName(strip, animator, skin);
            const bool live = !state.empty() && ((link.animatorPreview && link.previewState == state) ||
                                                 (m_timelineDrivesClip && link.timelineState == state && track == 0));
            const bool hoveredStrip = rowHovered && inLane && io.MousePos.x >= x0 && io.MousePos.x <= x1;

            ImU32 fill = strip.muted ? IM_COL32(70, 72, 80, 200) : clipFill(strip.clip, true);
            dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bottom), fill, 4.0f);
            if (isSelected) dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bottom), IM_COL32(255, 255, 255, 28), 4.0f);
            for (i32 r = 1; r < strip.repeat; ++r) {
                const f32 x = timeToX(strip.start + length * static_cast<f32>(r) / static_cast<f32>(strip.repeat));
                for (f32 y = top + 2.0f; y < bottom; y += 6.0f) {
                    dl->AddLine(ImVec2(x, y), ImVec2(x, std::min(y + 3.0f, bottom)), IM_COL32(255, 255, 255, 120));
                }
            }

            const f32 overlap = Animation::clipStripOverlapIn(*pose, i, skin);
            const f32 fadeIn = std::max(strip.fadeIn, overlap);
            if (fadeIn > 0.0f) {
                const f32 xf = timeToX(strip.start + fadeIn);
                ImVec2 points[17];
                for (int k = 0; k < 17; ++k) {
                    const f32 u = static_cast<f32>(k) / 16.0f;
                    const f32 v = std::clamp(Animation::evaluateKeyCurve(strip.fadeInCurve, u), 0.0f, 1.0f);
                    points[k] = ImVec2(x0 + (xf - x0) * u, bottom - (bottom - top) * v);
                }
                for (int k = 0; k < 16; ++k) {
                    dl->AddQuadFilled(ImVec2(points[k].x, top), ImVec2(points[k + 1].x, top), points[k + 1], points[k],
                                      IM_COL32(0, 0, 0, 90));
                }
                dl->AddPolyline(points, 17, IM_COL32(255, 255, 255, 210), 0, 1.5f);
                if (overlap > 0.0f) {
                    const f32 xo = timeToX(strip.start + overlap);
                    for (f32 x = x0; x < xo; x += 7.0f) {
                        dl->AddLine(ImVec2(x, bottom), ImVec2(std::min(x + 7.0f, xo), top), IM_COL32(255, 230, 140, 45));
                    }
                }
            }
            if (strip.fadeOut > 0.0f) {
                const f32 xs = timeToX(strip.start + length - strip.fadeOut);
                ImVec2 points[17];
                for (int k = 0; k < 17; ++k) {
                    const f32 u = static_cast<f32>(k) / 16.0f;
                    const f32 v = 1.0f - std::clamp(Animation::evaluateKeyCurve(strip.fadeOutCurve, u), 0.0f, 1.0f);
                    points[k] = ImVec2(xs + (x1 - xs) * u, bottom - (bottom - top) * v);
                }
                for (int k = 0; k < 16; ++k) {
                    dl->AddQuadFilled(ImVec2(points[k].x, top), ImVec2(points[k + 1].x, top), points[k + 1], points[k],
                                      IM_COL32(0, 0, 0, 90));
                }
                dl->AddPolyline(points, 17, IM_COL32(255, 255, 255, 210), 0, 1.5f);
            }

            if (x1 - x0 > 30.0f) {
                const f32 textX = std::max(x0, laneLeft) + 6.0f;
                dl->PushClipRect(ImVec2(std::max(x0, laneLeft), top), ImVec2(std::min(x1, laneRight) - 2.0f, bottom), true);
                dl->AddText(ImVec2(textX, top + 2.0f), strip.muted ? IM_COL32(170, 170, 176, 255) : IM_COL32(245, 245, 248, 255),
                            clipName(strip.clip));
                char info[96];
                int used = 0;
                if (std::fabs(strip.speed - 1.0f) > 0.001f) used += std::snprintf(info + used, sizeof(info) - used, "%.2fx  ", strip.speed);
                if (strip.repeat > 1) used += std::snprintf(info + used, sizeof(info) - used, "x%d  ", strip.repeat);
                if (strip.reverse) used += std::snprintf(info + used, sizeof(info) - used, "rev  ");
                if (strip.muted) used += std::snprintf(info + used, sizeof(info) - used, "muted  ");
                if (!state.empty()) std::snprintf(info + used, sizeof(info) - used, "> %s", state.c_str());
                else if (used == 0) std::snprintf(info, sizeof(info), "%.2fs", length);
                dl->AddText(ImVec2(textX, top + 17.0f), IM_COL32(220, 226, 236, 170), info);
                dl->PopClipRect();
            }

            const f32 handleIn = timeToX(strip.start + strip.fadeIn);
            const f32 handleOut = timeToX(strip.start + length - strip.fadeOut);
            if (isSelected || hoveredStrip) {
                dl->AddCircleFilled(ImVec2(handleIn, top + 4.0f), 4.0f, IM_COL32(255, 255, 255, 230), 12);
                dl->AddCircleFilled(ImVec2(handleOut, top + 4.0f), 4.0f, IM_COL32(255, 255, 255, 230), 12);
            }
            const ImU32 outline = isSelected ? IM_COL32(255, 255, 255, 240)
                                  : live     ? IM_COL32(120, 230, 140, 255)
                                             : IM_COL32(0, 0, 0, 110);
            dl->AddRect(ImVec2(x0, top), ImVec2(x1, bottom), outline, 4.0f, 0, isSelected || live ? 2.0f : 1.0f);

            if (hoveredStrip) {
                hit = static_cast<i32>(i);
                if (std::fabs(io.MousePos.x - handleIn) <= 6.0f && io.MousePos.y <= top + 10.0f) hitZone = StripDrag::FadeIn;
                else if (std::fabs(io.MousePos.x - handleOut) <= 6.0f && io.MousePos.y <= top + 10.0f) hitZone = StripDrag::FadeOut;
                else if (io.MousePos.x - x0 <= 6.0f) hitZone = StripDrag::TrimLeft;
                else if (x1 - io.MousePos.x <= 6.0f) hitZone = StripDrag::TrimRight;
                else hitZone = StripDrag::Move;
            }
        }
        dl->PopClipRect();

        if (hit >= 0 && m_stripDrag == StripDrag::None) {
            if (hitZone != StripDrag::Move) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            const char* hint = hitZone == StripDrag::FadeIn    ? "Fade in"
                               : hitZone == StripDrag::FadeOut  ? "Fade out"
                               : hitZone == StripDrag::TrimLeft ? "Trim start"
                               : hitZone == StripDrag::TrimRight ? "Trim end (drag past the clip end to repeat)"
                                                                 : nullptr;
            if (hint) ImGui::SetTooltip("%s", hint);
        }
        if (clickedLeft) {
            if (hit >= 0) {
                m_selectedStrip = hit;
                m_keySelected = false;
                m_stripDrag = hitZone;
                m_stripDragOrigin = pose->strips[static_cast<size_t>(hit)];
                m_stripDragMouseTime = xToTime(io.MousePos.x);
                const std::string state = stripStateName(pose->strips[static_cast<size_t>(hit)], animator, skin);
                if (!state.empty()) {
                    AnimationEditorLink& mutableLink = AnimationEditorLink::get();
                    mutableLink.focusState = state;
                    ++mutableLink.focusSerial;
                }
            } else if (inLane) {
                m_selectedStrip = -1;
                m_currentTime = std::max(0.0f, xToTime(io.MousePos.x));
                m_applyTime = true;
            }
        }
        if (doubleClicked && hit >= 0) {
            m_currentTime = pose->strips[static_cast<size_t>(hit)].start;
            m_applyTime = true;
        }
        if (clickedRight) {
            m_contextStrip = hit;
            m_contextTime = std::max(0.0f, xToTime(io.MousePos.x));
            m_contextTrack = track;
            if (hit >= 0) {
                m_selectedStrip = hit;
                m_keySelected = false;
            }
            openStripMenu = true;
        }
    }

    if (m_stripDrag != StripDrag::None) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || m_selectedStrip < 0) {
            m_stripDrag = StripDrag::None;
            stripsEdited();
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
            Animation::ClipStrip& strip = pose->strips[static_cast<size_t>(m_selectedStrip)];
            const Animation::ClipStrip& origin = m_stripDragOrigin;
            const f32 mouseTime = xToTime(io.MousePos.x);
            const f32 delta = mouseTime - m_stripDragMouseTime;
            const f32 length = Animation::clipStripLength(origin, skin);
            const f32 speed = std::max(origin.speed, 0.01f);
            const f32 full = stripClipDuration(origin, skin);
            const f32 sourceOut = origin.clipOut < 0.0f ? full : origin.clipOut;
            bool snapped = false;
            switch (m_stripDrag) {
                case StripDrag::Move: {
                    f32 start = std::max(0.0f, origin.start + delta);
                    const f32 snappedStart = snapTime(start, m_selectedStrip, snapped);
                    if (snapped) {
                        start = snappedStart;
                    } else {
                        const f32 snappedEnd = snapTime(start + length, m_selectedStrip, snapped);
                        if (snapped) start = std::max(0.0f, snappedEnd - length);
                    }
                    strip.start = start;
                    const i32 row = static_cast<i32>(std::floor((io.MousePos.y - m_stripRowsTop) / rowH));
                    strip.track = std::clamp(row, 0, trackCount);
                    m_extraTracks = std::max(m_extraTracks, strip.track);
                    break;
                }
                case StripDrag::TrimLeft: {
                    if (origin.repeat > 1) break;
                    const f32 start = snapTime(origin.start + delta, m_selectedStrip, snapped);
                    const f32 shift = (start - origin.start) * speed;
                    if (!origin.reverse) {
                        const f32 in = std::clamp(origin.clipIn + shift, 0.0f, sourceOut - 0.02f);
                        strip.clipIn = in;
                        strip.start = std::max(0.0f, origin.start + (in - origin.clipIn) / speed);
                    } else {
                        const f32 out = std::clamp(sourceOut - shift, origin.clipIn + 0.02f, full);
                        strip.clipOut = out >= full - 0.0001f ? -1.0f : out;
                        strip.start = std::max(0.0f, origin.start + (sourceOut - out) / speed);
                    }
                    break;
                }
                case StripDrag::TrimRight: {
                    const f32 end = snapTime(origin.start + length + delta, m_selectedStrip, snapped);
                    const f32 wanted = std::max(0.02f, end - origin.start);
                    if (origin.repeat > 1) {
                        const f32 pass = length / static_cast<f32>(origin.repeat);
                        strip.repeat = std::clamp(static_cast<i32>(std::round(wanted / pass)), 1, 64);
                        break;
                    }
                    const f32 maxPass = origin.reverse ? sourceOut / speed : (full - origin.clipIn) / speed;
                    if (wanted <= maxPass + 0.0001f) {
                        strip.repeat = 1;
                        if (!origin.reverse) {
                            const f32 out = origin.clipIn + wanted * speed;
                            strip.clipOut = out >= full - 0.0001f ? -1.0f : out;
                        } else {
                            strip.clipIn = std::max(0.0f, sourceOut - wanted * speed);
                        }
                    } else {
                        if (!origin.reverse) strip.clipOut = -1.0f;
                        else strip.clipIn = 0.0f;
                        strip.repeat = std::clamp(static_cast<i32>(std::round(wanted / maxPass)), 1, 64);
                    }
                    break;
                }
                case StripDrag::FadeIn:
                    strip.fadeIn = std::clamp(mouseTime - strip.start, 0.0f, std::max(0.0f, length - strip.fadeOut));
                    break;
                case StripDrag::FadeOut:
                    strip.fadeOut = std::clamp(strip.start + length - mouseTime, 0.0f, std::max(0.0f, length - strip.fadeIn));
                    break;
                case StripDrag::None:
                    break;
            }
            if (m_stripDrag != StripDrag::Move) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            const f32 newLength = Animation::clipStripLength(strip, skin);
            ImGui::SetTooltip("Start %.2fs  Length %.2fs%s\nFade %.2fs / %.2fs", strip.start, newLength,
                              strip.repeat > 1 ? "  (repeats)" : "", strip.fadeIn, strip.fadeOut);
            m_timelineDrivesClip = true;
            m_applyTime = true;
        }
    }

    if (openStripMenu) ImGui::OpenPopup("##strip_ctx");
    if (ImGui::BeginPopup("##strip_ctx")) {
        if (m_contextStrip >= 0 && m_contextStrip < static_cast<i32>(pose->strips.size())) {
            Animation::ClipStrip& strip = pose->strips[static_cast<size_t>(m_contextStrip)];
            ImGui::TextDisabled("%s", clipName(strip.clip));
            ImGui::Separator();
            if (ImGui::MenuItem("Split at Playhead", "Ctrl+K")) {
                m_selectedStrip = m_contextStrip;
                splitSelectedStrip();
            } else if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                m_selectedStrip = m_contextStrip;
                duplicateSelectedStrip();
            } else if (ImGui::MenuItem("Delete", "Del")) {
                m_selectedStrip = m_contextStrip;
                deleteSelectedStrip();
            } else {
                ImGui::Separator();
                bool changed = false;
                if (ImGui::MenuItem("Mute", nullptr, strip.muted)) {
                    strip.muted = !strip.muted;
                    changed = true;
                }
                if (ImGui::MenuItem("Reverse", nullptr, strip.reverse)) {
                    strip.reverse = !strip.reverse;
                    changed = true;
                }
                if (ImGui::BeginMenu("Speed")) {
                    for (f32 value : {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f}) {
                        char label[16];
                        std::snprintf(label, sizeof(label), "%.2fx", value);
                        if (ImGui::MenuItem(label, nullptr, std::fabs(strip.speed - value) < 0.001f)) {
                            strip.speed = value;
                            changed = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Reset Trim")) {
                    strip.clipIn = 0.0f;
                    strip.clipOut = -1.0f;
                    strip.repeat = 1;
                    changed = true;
                }
                if (ImGui::MenuItem("Reset Fades")) {
                    strip.fadeIn = 0.0f;
                    strip.fadeOut = 0.0f;
                    strip.fadeInCurve = {};
                    strip.fadeOutCurve = {};
                    changed = true;
                }
                if (ImGui::BeginMenu("Move to Track")) {
                    for (i32 track = 0; track <= trackCount; ++track) {
                        const std::string label = track == trackCount ? "New Track" : "Clips " + std::to_string(track + 1);
                        if (ImGui::MenuItem(label.c_str(), nullptr, strip.track == track)) {
                            strip.track = track;
                            m_extraTracks = std::max(m_extraTracks, track);
                            changed = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (animator && ImGui::BeginMenu("Bind to State")) {
                    if (ImGui::MenuItem("Automatic (by clip name)", nullptr, strip.state[0] == '\0')) {
                        strip.state[0] = '\0';
                        changed = true;
                    }
                    for (const auto& pair : animator->states) {
                        if (ImGui::MenuItem(pair.key.cStr(), nullptr, std::string(strip.state) == pair.key.cStr())) {
                            std::snprintf(strip.state, sizeof(strip.state), "%s", pair.key.cStr());
                            changed = true;
                        }
                    }
                    ImGui::EndMenu();
                }
                if (animator && strip.track == 0) {
                    const std::vector<size_t> order = baseTrackOrder(*pose);
                    const auto at = std::find(order.begin(), order.end(), static_cast<size_t>(m_contextStrip));
                    if (at != order.end() && at != order.begin()) {
                        const Animation::ClipStrip& previous = pose->strips[*(at - 1)];
                        const std::string from = stripStateName(previous, animator, skin);
                        const std::string to = stripStateName(strip, animator, skin);
                        if (!from.empty() && !to.empty() && from != to) {
                            Animation::AnimationTransition* existing = findTransition(*animator, from, to);
                            const std::string label = existing ? "Sync Crossfade To Animator (" + from + " > " + to + ")"
                                                               : "Create Transition In Animator (" + from + " > " + to + ")";
                            if (ImGui::MenuItem(label.c_str())) {
                                if (!existing) {
                                    Animation::AnimationState* source = animator->states.get(FixedString<32>(from.c_str()));
                                    if (source) {
                                        Animation::AnimationTransition transition;
                                        transition.toState = to.c_str();
                                        transition.hasExitTime = true;
                                        const f32 previousLength = std::max(Animation::clipStripLength(previous, skin), 0.0001f);
                                        const f32 fade = stripCrossfade(*pose, static_cast<size_t>(m_contextStrip), skin);
                                        transition.exitTime = std::clamp((previousLength - fade) / previousLength, 0.0f, 1.0f);
                                        source->transitions.push_back(transition);
                                    }
                                }
                                changed = true;
                            }
                        }
                    }
                }
                if (changed) stripsEdited();
            }
        } else {
            ImGui::TextDisabled("Track %d at %.2fs", m_contextTrack + 1, m_contextTime);
            ImGui::Separator();
            if (ImGui::BeginMenu("Add Clip Here")) {
                for (size_t i = 0; skin && i < skin->clipNames.size(); ++i) {
                    if (ImGui::MenuItem(skin->clipNames[i].c_str())) {
                        Animation::ClipStrip strip;
                        strip.clip = static_cast<i32>(i);
                        strip.track = m_contextTrack;
                        strip.start = m_contextTime;
                        pose->strips.push_back(strip);
                        m_selectedStrip = static_cast<i32>(pose->strips.size()) - 1;
                        stripsEdited();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Add Track")) ++m_extraTracks;
            bool trackEmpty = true;
            for (const Animation::ClipStrip& strip : pose->strips) {
                if (strip.track == m_contextTrack) trackEmpty = false;
            }
            if (ImGui::MenuItem("Remove Track", nullptr, false,
                                trackEmpty && m_contextTrack > 0 && m_contextTrack == trackCount - 1 && m_extraTracks > 0)) {
                --m_extraTracks;
            }
            if (ImGui::MenuItem("Build From Animator", nullptr, false, animator != nullptr)) buildStripsFromAnimator();
        }
        ImGui::EndPopup();
    }
}

void AnimationTimelinePanel::renderStripInspector() {
    Animation::SkinnedPose* pose = selectedPose();
    if (!pose || m_selectedStrip < 0 || m_selectedStrip >= static_cast<i32>(pose->strips.size())) return;
    const Animation::ImportedSkin* skin = Animation::findImportedSkin(pose->meshPath);
    Animation::Animator* animator = m_scene->get<Animation::Animator>(ECS::Entity(m_selectedEntity, m_scene));
    AnimationEditorLink& link = AnimationEditorLink::get();
    Animation::ClipStrip& strip = pose->strips[static_cast<size_t>(m_selectedStrip)];
    const f32 width = ImGui::GetContentRegionAvail().x;
    bool changed = false;
    auto clipName = [&](i32 clip) -> const char* {
        return (skin && clip >= 0 && clip < static_cast<i32>(skin->clipNames.size()))
                   ? skin->clipNames[static_cast<size_t>(clip)].c_str()
                   : "Clip";
    };

    ImGui::TextUnformatted("Clip");
    ImGui::BeginDisabled(m_editBone < 0);
    if (ImGui::Button("Edit Bone Keys") && skin && m_editBone >= 0) {
        Animation::bakeClipToKeys(*pose, *skin, strip.clip, m_editBone);
        m_objectRow = false;
        m_applyTime = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(m_editBone < 0 ? "Select a bone first."
                                         : "Copies this bone's channel from the clip onto editable keys.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("All Joints") && skin) {
        Animation::bakeClipToKeys(*pose, *skin, strip.clip, -1);
        m_applyTime = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies every non-finger joint. Heavy on long clips.");
    ImGui::SameLine();
    if (ImGui::Button("Delete Clip")) deleteSelectedStrip();
    ImGui::PushItemWidth(width - 92.0f);
    if (ImGui::BeginCombo("Source", clipName(strip.clip))) {
        for (size_t i = 0; skin && i < skin->clipNames.size(); ++i) {
            if (ImGui::Selectable(skin->clipNames[i].c_str(), static_cast<i32>(i) == strip.clip)) {
                strip.clip = static_cast<i32>(i);
                strip.clipIn = 0.0f;
                strip.clipOut = -1.0f;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    const std::string bound = stripStateName(strip, animator, skin);
    const std::string statePreview = strip.state[0] != '\0' ? std::string(strip.state)
                                     : bound.empty()        ? std::string("None")
                                                            : "Auto: " + bound;
    ImGui::BeginDisabled(animator == nullptr);
    if (ImGui::BeginCombo("State", statePreview.c_str())) {
        if (ImGui::Selectable("Automatic (by clip name)", strip.state[0] == '\0')) {
            strip.state[0] = '\0';
            changed = true;
        }
        for (const auto& pair : animator->states) {
            if (ImGui::Selectable(pair.key.cStr(), std::string(strip.state) == pair.key.cStr())) {
                std::snprintf(strip.state, sizeof(strip.state), "%s", pair.key.cStr());
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    int track = strip.track + 1;
    if (ImGui::InputInt("Track", &track)) {
        strip.track = std::max(0, track - 1);
        m_extraTracks = std::max(m_extraTracks, strip.track);
        changed = true;
    }

    ImGui::SeparatorText("Timing");
    const f32 full = stripClipDuration(strip, skin);
    changed |= ImGui::DragFloat("Start", &strip.start, 0.01f, 0.0f, 3600.0f, "%.3fs");
    f32 trimOut = strip.clipOut < 0.0f ? full : strip.clipOut;
    if (ImGui::DragFloat("Trim In", &strip.clipIn, 0.01f, 0.0f, trimOut - 0.02f, "%.3fs")) {
        strip.clipIn = std::clamp(strip.clipIn, 0.0f, trimOut - 0.02f);
        changed = true;
    }
    if (ImGui::DragFloat("Trim Out", &trimOut, 0.01f, strip.clipIn + 0.02f, full, "%.3fs")) {
        trimOut = std::clamp(trimOut, strip.clipIn + 0.02f, full);
        strip.clipOut = trimOut >= full - 0.0001f ? -1.0f : trimOut;
        changed = true;
    }
    ImGui::TextDisabled("Uses %.2fs of %.2fs", trimOut - strip.clipIn, full);
    if (ImGui::DragFloat("Speed", &strip.speed, 0.01f, 0.05f, 8.0f, "%.2fx")) {
        strip.speed = std::clamp(strip.speed, 0.05f, 8.0f);
        changed = true;
    }
    f32 length = Animation::clipStripLength(strip, skin);
    if (ImGui::DragFloat("Length", &length, 0.01f, 0.05f, 3600.0f, "%.3fs")) {
        const f32 source = (trimOut - strip.clipIn) * static_cast<f32>(std::max(strip.repeat, 1));
        strip.speed = std::clamp(source / std::max(length, 0.05f), 0.05f, 8.0f);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stretches the clip: changes its speed to fit this length.");
    if (ImGui::InputInt("Repeat", &strip.repeat)) {
        strip.repeat = std::clamp(strip.repeat, 1, 64);
        changed = true;
    }
    changed |= ImGui::Checkbox("Reverse", &strip.reverse);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Mute", &strip.muted);

    ImGui::SeparatorText("Blending");
    length = Animation::clipStripLength(strip, skin);
    changed |= ImGui::SliderFloat("Weight", &strip.weight, 0.0f, 1.0f, "%.2f");
    if (ImGui::DragFloat("Fade In", &strip.fadeIn, 0.01f, 0.0f, std::max(0.0f, length - strip.fadeOut), "%.2fs")) {
        strip.fadeIn = std::clamp(strip.fadeIn, 0.0f, std::max(0.0f, length - strip.fadeOut));
        changed = true;
    }
    const f32 overlap = Animation::clipStripOverlapIn(*pose, static_cast<size_t>(m_selectedStrip), skin);
    if (overlap > 0.0f) ImGui::TextDisabled("Crossfade from the clip before: %.2fs", overlap);
    if (strip.fadeIn > 0.0f || overlap > 0.0f) changed |= editKeyCurve("fade_in_curve", strip.fadeInCurve, width, 80.0f);
    if (ImGui::DragFloat("Fade Out", &strip.fadeOut, 0.01f, 0.0f, std::max(0.0f, length - strip.fadeIn), "%.2fs")) {
        strip.fadeOut = std::clamp(strip.fadeOut, 0.0f, std::max(0.0f, length - strip.fadeIn));
        changed = true;
    }
    if (strip.fadeOut > 0.0f) changed |= editKeyCurve("fade_out_curve", strip.fadeOutCurve, width, 80.0f);
    if (ImGui::DragFloat("Loop Blend", &strip.loopBlend, 0.01f, 0.0f, std::max(0.0f, length * 0.5f), "%.2fs")) {
        strip.loopBlend = std::clamp(strip.loopBlend, 0.0f, std::max(0.0f, length * 0.5f));
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blends the end of the clip into its start so repeats loop seamlessly.");

    if (animator) {
        ImGui::SeparatorText("Animator");
        if (bound.empty()) {
            ImGui::TextWrapped("Bind this clip to a state to sync its crossfades with the Animator.");
        } else {
            ImGui::Text("State: %s", bound.c_str());
            if (ImGui::SmallButton("Show In Animator")) {
                link.focusState = bound;
                ++link.focusSerial;
            }
            if (strip.track == 0) {
                const std::vector<size_t> order = baseTrackOrder(*pose);
                const auto at = std::find(order.begin(), order.end(), static_cast<size_t>(m_selectedStrip));
                if (at != order.end() && at != order.begin()) {
                    const std::string from = stripStateName(pose->strips[*(at - 1)], animator, skin);
                    if (!from.empty() && from != bound) {
                        if (const Animation::AnimationTransition* transition = findTransition(*animator, from, bound)) {
                            ImGui::TextDisabled("%s > %s  blend %.2fs  %s", from.c_str(), bound.c_str(), transition->blendTime,
                                                Animation::keyInterpName(transition->blendCurve.mode));
                        } else {
                            ImGui::TextDisabled("No transition %s > %s yet.", from.c_str(), bound.c_str());
                            ImGui::TextDisabled("Right-click the clip to create it.");
                        }
                    }
                }
            } else {
                ImGui::TextDisabled("Only the first track syncs crossfades.");
            }
        }
    }
    ImGui::PopItemWidth();
    if (changed) stripsEdited();
}

void AnimationTimelinePanel::renderTracks() {}
void AnimationTimelinePanel::handleInput()  {}

}

#endif
