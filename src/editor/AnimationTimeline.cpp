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

}

#ifdef CF_HAS_IMGUI

namespace Caffeine::Editor {

static constexpr f32 k_TrackLabelWidth = 110.0f;
static constexpr f32 k_TrackHeight     = 22.0f;
static constexpr f32 k_RulerHeight     = 20.0f;
static constexpr f32 k_KeyDiamondSize  = 5.0f;

static const ImU32 k_ColTrackBg       = IM_COL32(35,  35,  42,  255);
static const ImU32 k_ColTrackBgAlt    = IM_COL32(30,  30,  38,  255);
static const ImU32 k_ColTrackLabel    = IM_COL32(50,  55,  70,  255);
static const ImU32 k_ColTrackLabelSel = IM_COL32(40,  80, 140,  255);
static const ImU32 k_ColRulerBg       = IM_COL32(25,  25,  32,  255);
static const ImU32 k_ColRulerTick     = IM_COL32(90,  90, 110,  255);
static const ImU32 k_ColRulerText     = IM_COL32(150,150, 170,  255);
static const ImU32 k_ColPlayhead      = IM_COL32(255, 80,  80,  255);
static const ImU32 k_ColKeyframe      = IM_COL32(255,210,  60,  255);
static const ImU32 k_ColKeyframeSel   = IM_COL32(255,255, 130,  255);
static const ImU32 k_ColKeyframeHover = IM_COL32(255,240, 120,  255);
static const ImU32 k_ColProgress      = IM_COL32(60, 100, 220,  180);

void AnimationTimelinePanel::render(f32 deltaTime) {
    if (!m_open) return;

    if (m_isPlaying && m_clip) {
        m_currentTime += deltaTime;
        if (m_currentTime >= m_clip->duration()) {
            if (m_looping) m_currentTime = 0.0f;
            else           m_isPlaying = false;
        }
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 200), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Animation Timeline", &m_open)) {
        renderHeader();
        ImGui::Separator();
        renderTimeline();
    }
    ImGui::End();
}

void AnimationTimelinePanel::renderHeader() {
    ImGui::Text("Animation:");
    ImGui::SameLine();
    if (m_clip) {
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
        if (ImGui::SmallButton("  Play  "))  play();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("  Stop  ")) stop();
    ImGui::SameLine(0, 12.0f);
    ImGui::Checkbox("Loop", &m_looping);
    if (m_clip) m_clip->loop = m_looping;
    ImGui::SameLine();
    ImGui::Checkbox("Onion Skin", &m_onionSkinningEnabled);

    if (m_clip) {
        ImGui::SameLine(0, 16.0f);
        ImGui::TextDisabled("Time: %.3f / %.2f", m_currentTime, m_clip->duration());
    }
}

void AnimationTimelinePanel::renderTimeline() {
    if (!m_clip) {
        ImGui::Spacing();
        ImGui::TextDisabled("  No animation clip selected.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,0.7f), "  Tracks");
        ImGui::TextDisabled("  No tracks. Add a clip to create tracks.");
        return;
    }

    f32 duration = m_clip->duration();
    if (duration <= 0.0f) duration = 1.0f;

    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2 cursor   = ImGui::GetCursorScreenPos();
    f32 availWidth  = ImGui::GetContentRegionAvail().x;
    f32 trackWidth  = availWidth - k_TrackLabelWidth;
    if (trackWidth < 1.0f) trackWidth = 1.0f;

    auto timeToX = [&](f32 t) -> f32 {
        return cursor.x + k_TrackLabelWidth + (t / duration) * trackWidth;
    };
    auto xToTime = [&](f32 x) -> f32 {
        return ((x - cursor.x - k_TrackLabelWidth) / trackWidth) * duration;
    };

    dl->AddRectFilled(cursor,
        ImVec2(cursor.x + availWidth, cursor.y + k_RulerHeight),
        k_ColRulerBg);

    {
        f32 minTickSpacing = 40.0f;
        int numTicks = static_cast<int>(trackWidth / minTickSpacing);
        if (numTicks < 1) numTicks = 1;
        f32 tickInterval = duration / static_cast<f32>(numTicks);

        for (int i = 0; i <= numTicks; ++i) {
            f32 t  = static_cast<f32>(i) * tickInterval;
            f32 x  = timeToX(t);
            bool isMajor = (i % 5 == 0) || (numTicks <= 5);
            f32 tickH = isMajor ? k_RulerHeight * 0.6f : k_RulerHeight * 0.3f;
            dl->AddLine(
                ImVec2(x, cursor.y + k_RulerHeight - tickH),
                ImVec2(x, cursor.y + k_RulerHeight),
                k_ColRulerTick);

            if (isMajor) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.2f", t);
                dl->AddText(ImVec2(x + 2.0f, cursor.y + 2.0f), k_ColRulerText, buf);
            }
        }
    }

    {
        f32 px = timeToX(m_currentTime);
        dl->AddLine(ImVec2(px, cursor.y), ImVec2(px, cursor.y + k_RulerHeight), k_ColPlayhead, 1.0f);
        dl->AddTriangleFilled(
            ImVec2(px - 5.0f, cursor.y),
            ImVec2(px + 5.0f, cursor.y),
            ImVec2(px, cursor.y + 8.0f),
            k_ColPlayhead);
    }

    ImGui::InvisibleButton("ruler_click", ImVec2(availWidth, k_RulerHeight));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        f32 clickX = ImGui::GetIO().MousePos.x;
        f32 t = xToTime(clickX);
        m_currentTime = std::max(0.0f, std::min(t, duration));
    }
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + k_RulerHeight + 2.0f));

    if (ImGui::Button("+ Track")) {
        auto track = std::make_unique<TransformTrack>();
        track->targetPropertyName = FixedString<32>("Track");
        m_tracks.push_back(std::move(track));
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Key") && !m_tracks.empty()) {
        addKeyframeToSelectedTrack(m_currentTime, i32(0));
    }
    ImGui::SameLine();
    if (ImGui::Button("Del Key")) {
        deleteSelectedKeyframe();
    }
    ImGui::Spacing();

    if (m_tracks.empty()) {
        ImGui::TextDisabled("  No tracks. Use '+ Track' to add one.");
        return;
    }

    ImVec2 trackAreaStart = ImGui::GetCursorScreenPos();

    for (usize i = 0; i < m_tracks.size(); ++i) {
        auto& track = m_tracks[i];
        bool isSelected = (i == m_selectedTrack);

        ImVec2 rowPos = ImGui::GetCursorScreenPos();
        ImU32 rowBg   = (i % 2 == 0) ? k_ColTrackBg : k_ColTrackBgAlt;
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + availWidth, rowPos.y + k_TrackHeight), rowBg);

        ImU32 labelBg = isSelected ? k_ColTrackLabelSel : k_ColTrackLabel;
        dl->AddRectFilled(rowPos, ImVec2(rowPos.x + k_TrackLabelWidth - 2.0f, rowPos.y + k_TrackHeight), labelBg);

        const char* typeTag =
            track->getType() == TrackType::Sprite    ? "[S]" :
            track->getType() == TrackType::Transform ? "[T]" :
            track->getType() == TrackType::Event     ? "[E]" : "[?]";

        dl->AddText(ImVec2(rowPos.x + 4.0f, rowPos.y + 4.0f), IM_COL32(200,200,200,255), typeTag);
        dl->AddText(ImVec2(rowPos.x + 28.0f, rowPos.y + 4.0f), IM_COL32(220,220,230,255), track->targetPropertyName.cStr());

        ImGui::InvisibleButton(("track_row_" + std::to_string(i)).c_str(), ImVec2(availWidth, k_TrackHeight));
        if (ImGui::IsItemClicked()) m_selectedTrack = i;

        for (usize j = 0; j < track->keyframes.size(); ++j) {
            const auto& kf = track->keyframes[j];
            f32 kx = timeToX(kf.time);
            f32 ky = rowPos.y + k_TrackHeight * 0.5f;

            bool kfSelected = isSelected && (j == m_selectedKeyframe);

            ImVec2 kfMin(kx - k_KeyDiamondSize, ky);
            ImVec2 kfMax(kx + k_KeyDiamondSize, ky);
            ImVec2 kfTop(kx, ky - k_KeyDiamondSize);
            ImVec2 kfBot(kx, ky + k_KeyDiamondSize);

            bool hovered = ImGui::IsMouseHoveringRect(ImVec2(kx - k_KeyDiamondSize, ky - k_KeyDiamondSize),
                                                      ImVec2(kx + k_KeyDiamondSize, ky + k_KeyDiamondSize));

            ImU32 kfColor = kfSelected   ? k_ColKeyframeSel  :
                            hovered       ? k_ColKeyframeHover :
                                            k_ColKeyframe;

            dl->AddQuadFilled(kfMin, kfTop, kfMax, kfBot, kfColor);
            dl->AddQuad(kfMin, kfTop, kfMax, kfBot, IM_COL32(0,0,0,120));

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_selectedTrack    = i;
                m_selectedKeyframe = j;
            }

            if (kfSelected && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                f32 newTime = xToTime(ImGui::GetIO().MousePos.x);
                newTime = std::max(0.0f, std::min(newTime, duration));
                moveSelectedKeyframe(newTime);
            }

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                m_selectedTrack    = i;
                m_selectedKeyframe = j;
                deleteSelectedKeyframe();
            }
        }
    }

    ImVec2 trackAreaEnd = ImGui::GetCursorScreenPos();

    {
        f32 px = timeToX(m_currentTime);
        f32 topY = trackAreaStart.y;
        f32 botY = trackAreaEnd.y;
        dl->AddLine(ImVec2(px, topY), ImVec2(px, botY), k_ColPlayhead, 1.5f);
    }

    if (m_selectedTrack < m_tracks.size()) {
        auto& track = m_tracks[m_selectedTrack];
        ImGui::Separator();
        ImGui::Text("Selected: %s   Keyframes: %zu", track->targetPropertyName.cStr(), track->keyframes.size());
        if (m_selectedKeyframe < track->keyframes.size()) {
            const auto& kf = track->keyframes[m_selectedKeyframe];
            ImGui::SameLine(0, 16.0f);
            ImGui::TextDisabled("Key %zu @ %.3fs", m_selectedKeyframe, kf.time);
        }
    }
}

void AnimationTimelinePanel::renderTracks() {}
void AnimationTimelinePanel::handleInput()  {}

}

#endif
