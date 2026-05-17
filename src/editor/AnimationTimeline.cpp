#include "editor/AnimationTimeline.hpp"
#include "debug/LogSystem.hpp"
#include <algorithm>
#include <cmath>

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
    // TODO (blocked): m_currentTime advances only in render() with delta time.
    // Currently no delta time is passed to render(). When render signature changes to
    // render(f32 deltaTime), add: if (m_isPlaying) m_currentTime += deltaTime;
}

void AnimationTimelinePanel::stop() {
    m_isPlaying = false;
    m_currentTime = 0.0f;
}

void AnimationTimelinePanel::pause() {
    m_isPlaying = false;
}

f32 AnimationTimelinePanel::applyEasing(f32 t, EasingType easing) const {
    switch (easing) {
        case EasingType::EaseIn:
            return t * t;
        case EasingType::EaseOut:
            return t * (2.0f - t);
        case EasingType::EaseInOut:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
        case EasingType::Linear:
        default:
            return t;
    }
}

std::variant<i32, Vec3, FixedString<32>> AnimationTimelinePanel::interpolateValue(AnimationTrack* track, f32 time) const {
    if (!track || track->keyframes.empty()) {
        return i32(0);
    }

    const auto& keyframes = track->keyframes;
    auto it = keyframes.begin();
    while (it != keyframes.end() && it->time <= time) {
        ++it;
    }

    if (it == keyframes.begin()) {
        return keyframes.front().value;
    }
    if (it == keyframes.end()) {
        return keyframes.back().value;
    }

    const auto& k1 = *(it - 1);
    const auto& k2 = *it;

    f32 duration = k2.time - k1.time;
    if (duration <= 0.0f) return k2.value;

    f32 alpha = (time - k1.time) / duration;
    alpha = applyEasing(alpha, k1.easing);

    if (std::holds_alternative<Vec3>(k1.value) && std::holds_alternative<Vec3>(k2.value)) {
        Vec3 v1 = std::get<Vec3>(k1.value);
        Vec3 v2 = std::get<Vec3>(k2.value);
        return Vec3{
            v1.x + (v2.x - v1.x) * alpha,
            v1.y + (v2.y - v1.y) * alpha,
            v1.z + (v2.z - v1.z) * alpha
        };
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

}

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

void AnimationTimelinePanel::render(f32 deltaTime) {
    if (!m_open) return;

    if (m_isPlaying && m_clip) {
        m_currentTime += deltaTime;
        if (m_currentTime >= m_clip->duration()) {
            if (m_looping) {
                m_currentTime = 0.0f;
            } else {
                m_isPlaying = false;
            }
        }
    }

    if (ImGui::Begin("Animation Timeline", &m_open)) {
        renderHeader();
        ImGui::Separator();
        renderTimeline();
        ImGui::Separator();
        renderTracks();
    }
    ImGui::End();
}

void AnimationTimelinePanel::renderHeader() {
    ImGui::Text("Animation: %s", m_clip ? m_clip->name.cStr() : "No clip");
    ImGui::SameLine();

    if (m_isPlaying) {
        if (ImGui::Button("Pause")) pause();
    } else {
        if (ImGui::Button("Play")) play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) stop();
    ImGui::SameLine();

    ImGui::Checkbox("Loop", &m_looping);
    if (m_clip) m_clip->loop = m_looping;

    ImGui::SameLine();
    ImGui::Checkbox("Onion Skin", &m_onionSkinningEnabled);

    if (m_clip) {
        ImGui::SameLine();
        ImGui::Text("Duration: %.2fs  FPS: %u", m_clip->duration(), m_clip->fps);
    }
}

void AnimationTimelinePanel::renderTimeline() {
    if (!m_clip) {
        ImGui::Text("No animation clip selected");
        return;
    }

    f32 duration = m_clip->duration();
    if (duration <= 0.0f) duration = 1.0f;

    ImGui::Text("Timeline");
    ImGui::SameLine();

    f32 timelineWidth = ImGui::GetContentRegionAvail().x - 100.0f;
    ImGui::SameLine(100.0f);

    f32 markerTime = m_currentTime;
    f32 normalizedTime = (duration > 0.0f) ? (markerTime / duration) : 0.0f;
    if (normalizedTime < 0.0f) normalizedTime = 0.0f;
    if (normalizedTime > 1.0f) normalizedTime = 1.0f;

    ImVec2 timelinePos = ImGui::GetCursorScreenPos();
    ImVec2 timelineSize(timelineWidth, 20.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(timelinePos, ImVec2(timelinePos.x + timelineSize.x, timelinePos.y + timelineSize.y), IM_COL32(50, 50, 50, 255));

    drawList->AddRectFilled(timelinePos, ImVec2(timelinePos.x + timelineSize.x * normalizedTime, timelinePos.y + timelineSize.y), IM_COL32(100, 150, 255, 255));

    ImGui::SetCursorScreenPos(ImVec2(timelinePos.x, timelinePos.y + 25.0f));

     if (ImGui::Button("Add Keyframe")) {
         if (m_tracks.empty() == false) {
             [[maybe_unused]] FixedString<32> frameName = "frame_0";
             addKeyframeToSelectedTrack(m_currentTime, i32(0));
         }
     }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        deleteSelectedKeyframe();
    }
}

void AnimationTimelinePanel::renderTracks() {
    ImGui::Text("Tracks");

    if (m_tracks.empty()) {
        ImGui::Text("No tracks. Add a clip to create tracks.");
        return;
    }

    for (usize i = 0; i < m_tracks.size(); ++i) {
        auto& track = m_tracks[i];

        bool isSelected = (i == m_selectedTrack);
        if (ImGui::Selectable(track->targetPropertyName.cStr(), isSelected)) {
            m_selectedTrack = i;
        }

        ImGui::SameLine();
        ImGui::Text("[%s]", 
            track->getType() == TrackType::Sprite ? "S" :
            track->getType() == TrackType::Transform ? "T" :
            track->getType() == TrackType::Event ? "E" : "?");

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Keyframes: %zu", track->keyframes.size());
        }
    }

    ImGui::Separator();

    if (m_selectedTrack < m_tracks.size()) {
        auto& track = m_tracks[m_selectedTrack];
        ImGui::Text("Track: %s", track->targetPropertyName.cStr());
        ImGui::Text("Keyframes: %zu", track->keyframes.size());

        for (usize i = 0; i < track->keyframes.size(); ++i) {
            const auto& kf = track->keyframes[i];
            bool isKeyframeSelected = (i == m_selectedKeyframe);

            std::string label = "Key " + std::to_string(i) + " @ " + std::to_string(kf.time) + "s";

            if (ImGui::Selectable(label.c_str(), isKeyframeSelected)) {
                m_selectedKeyframe = i;
            }
        }
    }
}

void AnimationTimelinePanel::handleInput() {
    // TODO (blocked): Requires timeline UI interaction model.
    // When timeline is clicked, update m_currentTime. When keyframes are dragged,
    // call moveSelectedKeyframe(). When right-click on keyframe, call deleteSelectedKeyframe().
}

}

#endif