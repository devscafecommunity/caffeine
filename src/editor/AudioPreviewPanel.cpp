#include "editor/AudioPreviewPanel.hpp"

#ifdef CF_HAS_IMGUI

#include <cstdio>
#include <cmath>
#include <algorithm>

namespace Caffeine::Editor {

AudioPreviewPanel::AudioPreviewPanel() {}

AudioPreviewPanel::~AudioPreviewPanel() { shutdown(); }

bool AudioPreviewPanel::init() {
    if (m_initialized) return true;
    m_previewSystem = new Audio::AudioSystem();
    if (!m_previewSystem->init(8, 1)) {
        delete m_previewSystem;
        m_previewSystem = nullptr;
        return false;
    }
    m_initialized = true;
    return true;
}

void AudioPreviewPanel::shutdown() {
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
    if (m_previewSystem) {
        m_previewSystem->shutdown();
        delete m_previewSystem;
        m_previewSystem = nullptr;
    }
    m_currentAsset = nullptr;
    m_initialized = false;
}

void AudioPreviewPanel::loadAsset(Assets::AudioClip* clip, const char* name) {
    m_currentAsset = clip;
    if (name) {
        size_t len = std::strlen(name);
        if (len >= sizeof(m_assetName)) len = sizeof(m_assetName) - 1;
        std::memcpy(m_assetName, name, len);
        m_assetName[len] = '\0';
    } else {
        m_assetName[0] = '\0';
    }
    m_peaksDirty = true;
    m_progress = 0.0f;
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
}

void AudioPreviewPanel::play() {
    if (!m_initialized || !m_currentAsset) return;
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
    float duration = (m_currentAsset->sampleRate > 0)
        ? static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate)
        : 0.0f;
    Audio::AudioClip* audioClip = m_previewSystem->registerClip(
        m_currentAsset->pcmData,
        m_currentAsset->pcmDataSize,
        m_currentAsset->sampleRate,
        m_currentAsset->channels,
        m_currentAsset->bitsPerSample,
        duration);
    if (!audioClip) return;
    m_previewSource = m_previewSystem->playSFX(audioClip, m_volume, 0.0f);
    if (m_previewSource) {
        m_previewSource->setLoop(m_looping);
    }
}

void AudioPreviewPanel::stop() {
    if (m_previewSource) {
        m_previewSource->stop();
        m_previewSource = nullptr;
    }
    m_progress = 0.0f;
}

void AudioPreviewPanel::computeWaveformPeaks(const Assets::AudioClip* clip) {
    if (!clip || !clip->pcmData || clip->pcmDataSize == 0) return;
    const int kNumPeaks = 200;
    m_peaks.resize(kNumPeaks);
    u32 bytesPerSample = (clip->bitsPerSample / 8);
    if (bytesPerSample == 0) return;
    u32 totalSamples = static_cast<u32>(clip->pcmDataSize / bytesPerSample);
    if (totalSamples == 0) return;
    u32 samplesPerPeak = std::max(1u, totalSamples / static_cast<u32>(kNumPeaks));
    for (int i = 0; i < kNumPeaks; ++i) {
        float minVal = 0.0f, maxVal = 0.0f;
        u32 startSample = i * samplesPerPeak;
        u32 endSample = std::min(startSample + samplesPerPeak, totalSamples);
        for (u32 s = startSample; s < endSample; ++s) {
            u64 offset = static_cast<u64>(s) * bytesPerSample;
            if (offset + bytesPerSample > clip->pcmDataSize) break;
            float sample;
            if (clip->bitsPerSample == 16) {
                i16 val;
                std::memcpy(&val, clip->pcmData + offset, sizeof(i16));
                sample = val / 32768.0f;
            } else if (clip->bitsPerSample == 8) {
                sample = (clip->pcmData[offset] - 128) / 128.0f;
            } else {
                sample = 0.0f;
            }
            minVal = std::min(minVal, sample);
            maxVal = std::max(maxVal, sample);
        }
        m_peaks[i] = {minVal, maxVal};
    }
}

void AudioPreviewPanel::onImGuiRender() {
    if (!m_open) return;
    ImGui::Begin("Audio Preview", &m_open);
    if (m_previewSource && m_previewSource->isPlaying()) {
        m_progress = m_previewSource->currentTime();
    } else if (m_previewSource && !m_previewSource->isPlaying() && !m_previewSource->isPaused()) {
        if (m_looping) {
            play();
        } else {
            m_previewSource = nullptr;
            m_progress = 0.0f;
        }
    }
    renderPlaybackControls();
    renderWaveform();
    renderSettings();
    ImGui::End();
}

void AudioPreviewPanel::renderPlaybackControls() {
    char title[320];
    snprintf(title, sizeof(title), "%s", m_assetName[0] ? m_assetName : "No asset loaded");
    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    bool isPlaying = m_previewSource && m_previewSource->isPlaying();
    if (ImGui::Button(isPlaying ? "||" : "|>")) {
        if (isPlaying) {
            m_previewSource->pause();
        } else {
            play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("[]")) {
        stop();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &m_looping);
    if (m_previewSource) {
        m_previewSource->setLoop(m_looping);
    }
    float duration = (m_currentAsset && m_currentAsset->sampleRate > 0)
        ? static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate)
        : 0.0f;
    if (duration > 0.0f) {
        char timeBuf[64];
        snprintf(timeBuf, sizeof(timeBuf), " %.1f/%.1f", m_progress, duration);
        ImGui::SameLine();
        ImGui::TextUnformatted(timeBuf);
        float prog = m_progress / duration;
        if (ImGui::SliderFloat("##progress", &prog, 0.0f, 1.0f, "")) {
            m_progress = prog * duration;
            if (m_previewSource) {
                m_previewSource->seek(m_progress);
            }
        }
    }
}

void AudioPreviewPanel::renderWaveform() {
    if (!m_currentAsset || !m_currentAsset->pcmData) {
        ImGui::TextUnformatted("No audio data - drag .wav/.ogg files from Asset Browser");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                [[maybe_unused]] const char* path = static_cast<const char*>(payload->Data);
                // TODO: Load audio asset from path
                // loadAsset(...);
            }
            ImGui::EndDragDropTarget();
        }
        return;
    }
    if (m_peaksDirty) {
        computeWaveformPeaks(m_currentAsset);
        m_peaksDirty = false;
    }
    if (m_peaks.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    float canvasWidth = ImGui::GetContentRegionAvail().x;
    float canvasHeight = 80.0f;
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasWidth, canvasPos.y + canvasHeight),
                      IM_COL32(20, 20, 30, 255));
    float centerY = canvasPos.y + canvasHeight * 0.5f;
    dl->AddLine(ImVec2(canvasPos.x, centerY), ImVec2(canvasPos.x + canvasWidth, centerY),
                IM_COL32(60, 60, 80, 255));
    float duration = (m_currentAsset->sampleRate > 0)
        ? static_cast<float>(m_currentAsset->sampleCount) / static_cast<float>(m_currentAsset->sampleRate)
        : 0.0f;
    float progressRatio = duration > 0.0f ? m_progress / duration : 0.0f;
    float barWidth = canvasWidth / static_cast<float>(m_peaks.size());
    for (size_t i = 0; i < m_peaks.size(); ++i) {
        float x = canvasPos.x + static_cast<float>(i) * barWidth + barWidth * 0.5f;
        float minY = centerY - m_peaks[i].max * (canvasHeight * 0.4f);
        float maxY = centerY - m_peaks[i].min * (canvasHeight * 0.4f);
        float t = static_cast<float>(i) / static_cast<float>(m_peaks.size());
        u32 color = (t <= progressRatio) ? IM_COL32(100, 200, 255, 200) : IM_COL32(100, 120, 140, 120);
        dl->AddLine(ImVec2(x, minY), ImVec2(x, maxY), color, std::max(1.0f, barWidth * 0.8f));
    }
    float progX = canvasPos.x + canvasWidth * progressRatio;
    dl->AddLine(ImVec2(progX, canvasPos.y), ImVec2(progX, canvasPos.y + canvasHeight),
                IM_COL32(255, 255, 100, 200), 1.0f);
     ImGui::Dummy(ImVec2(canvasWidth, canvasHeight));
     if (ImGui::BeginDragDropTarget()) {
         if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
             [[maybe_unused]] const char* path = static_cast<const char*>(payload->Data);
             // TODO: Load audio asset from path
             // loadAsset(...);
         }
         ImGui::EndDragDropTarget();
     }
}

void AudioPreviewPanel::renderSettings() {
    ImGui::Separator();
    ImGui::TextUnformatted("Settings");
    if (ImGui::SliderFloat("Volume", &m_volume, 0.0f, 1.0f, "%.2f")) {
        if (m_previewSource) {
            m_previewSource->setVolume(m_volume);
        }
    }
    if (ImGui::SliderFloat("Pitch", &m_pitch, 0.1f, 4.0f, "%.1f")) {
        if (m_previewSource) {
            m_previewSource->setPitch(m_pitch);
        }
    }
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
