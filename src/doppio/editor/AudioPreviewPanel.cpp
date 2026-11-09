#include "caffeine/ui/UI.hpp"
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

    using namespace Caffeine::UIKit;
    using namespace Caffeine::UIKit::Elements;

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
    if (m_peaksDirty) {
        computeWaveformPeaks(m_currentAsset);
        m_peaksDirty = false;
    }

    const float duration = (m_currentAsset && m_currentAsset->sampleRate > 0)
        ? static_cast<float>(m_currentAsset->sampleCount) /
              static_cast<float>(m_currentAsset->sampleRate)
        : 0.0f;
    float progress = duration > 0.0f ? m_progress / duration : 0.0f;
    float volume = m_volume;
    float pitch = m_pitch;

    Panel root("Audio Preview", "Waveform and playback", "bell", &m_open);
    UIContext ui;

    auto& transport = root.emplace<Card>("transport");
    transport.emplace<Label>(m_assetName[0] ? m_assetName : "No asset loaded",
                             TextTone::Normal)
        .setHeading(true);

    const bool isPlaying = m_previewSource && m_previewSource->isPlaying();
    auto& controls = transport.emplace<Row>("transport_controls");
    controls.emplace<Button>(isPlaying ? "Pause" : "Play", ButtonVariant::Primary)
        .setOnClick([this, isPlaying] {
            if (isPlaying) {
                if (m_previewSource) m_previewSource->pause();
            } else {
                play();
            }
        });
    controls.emplace<Button>("Stop").setOnClick([this] { stop(); });
    controls.emplace<Checkbox>("Loop", &m_looping)
        .setOnChange([this](bool loop) {
            m_looping = loop;
            if (m_previewSource) m_previewSource->setLoop(loop);
        });
    if (duration > 0.0f) {
        char timeBuffer[64];
        std::snprintf(timeBuffer, sizeof(timeBuffer), "%.1f / %.1f s", m_progress, duration);
        controls.emplace<Label>(timeBuffer, TextTone::Muted);
    }

    if (duration > 0.0f) {
        auto& slider = transport.emplace<SliderFloat>("", &progress, 0.0f, 1.0f, "");
        slider.setGrow(true);
    }

    auto& waveformCard = root.emplace<Card>("waveform");
    waveformCard.emplace<Label>("Waveform", TextTone::Accent).setHeading(true);
    if (!m_currentAsset || !m_currentAsset->pcmData || m_peaks.empty()) {
        waveformCard.emplace<Label>(
            "No audio data - drag .wav/.ogg files from Asset Browser", TextTone::Muted);
    } else {
        auto& canvas = waveformCard.emplace<Canvas>("waveform_canvas");
        canvas.setHeight(90.0f);
        canvas.setOnDraw([this, duration](UIContext&, ImDrawList* drawList,
                                         const ImVec2& origin, const ImVec2& size) {
            drawList->AddRectFilled(origin,
                                    ImVec2(origin.x + size.x, origin.y + size.y),
                                    IM_COL32(20, 20, 30, 255));
            const float centerY = origin.y + size.y * 0.5f;
            drawList->AddLine(ImVec2(origin.x, centerY),
                              ImVec2(origin.x + size.x, centerY),
                              IM_COL32(60, 60, 80, 255));
            const float progressRatio =
                duration > 0.0f ? std::clamp(m_progress / duration, 0.0f, 1.0f) : 0.0f;
            const float barWidth = size.x / static_cast<float>(m_peaks.size());
            for (size_t i = 0; i < m_peaks.size(); ++i) {
                const float x = origin.x + static_cast<float>(i) * barWidth + barWidth * 0.5f;
                const float minY = centerY - m_peaks[i].max * (size.y * 0.4f);
                const float maxY = centerY - m_peaks[i].min * (size.y * 0.4f);
                const float t = static_cast<float>(i) / static_cast<float>(m_peaks.size());
                const u32 color = (t <= progressRatio) ? IM_COL32(100, 200, 255, 200)
                                                       : IM_COL32(100, 120, 140, 120);
                drawList->AddLine(ImVec2(x, minY), ImVec2(x, maxY), color,
                                  std::max(1.0f, barWidth * 0.8f));
            }
            const float playheadX = origin.x + size.x * progressRatio;
            drawList->AddLine(ImVec2(playheadX, origin.y),
                              ImVec2(playheadX, origin.y + size.y),
                              IM_COL32(255, 255, 100, 200), 1.0f);
        });
    }

    auto& settings = root.emplace<Card>("settings");
    settings.emplace<Label>("Settings", TextTone::Accent).setHeading(true);
    auto& grid = settings.emplace<PropertyGrid>("audio_settings");
    grid.typedRow<FloatProperty>("Volume", volume, 0.0f, 1.0f, 0.01f, "%.2f", true);
    grid.typedRow<FloatProperty>("Pitch", pitch, 0.1f, 4.0f, 0.01f, "%.1f", true);

    ui.draw(root);

    if (duration > 0.0f && progress != m_progress / duration) {
        m_progress = progress * duration;
        if (m_previewSource) m_previewSource->seek(m_progress);
    }
    if (volume != m_volume) {
        m_volume = volume;
        if (m_previewSource) m_previewSource->setVolume(m_volume);
    }
    if (pitch != m_pitch) {
        m_pitch = pitch;
        if (m_previewSource) m_previewSource->setPitch(m_pitch);
    }
}

} // namespace Caffeine::Editor

#endif // CF_HAS_IMGUI
