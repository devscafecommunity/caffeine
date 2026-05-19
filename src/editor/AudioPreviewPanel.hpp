#pragma once
#include "core/Types.hpp"
#include "audio/AudioSystem.hpp"
#include "assets/AssetTypes.hpp"
#include <vector>

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

namespace Caffeine::Editor {

class AudioPreviewPanel {
public:
    AudioPreviewPanel();
    ~AudioPreviewPanel();

    bool init();
    void shutdown();

#ifdef CF_HAS_IMGUI
    void onImGuiRender();
#endif

    void loadAsset(Assets::AudioClip* clip, const char* name);
    void play();
    void stop();

    bool isOpen() const { return m_open; }
    void open()  { m_open = true; }
    void close() { m_open = false; }

    Audio::AudioSystem* previewSystem() { return m_previewSystem; }

private:
#ifdef CF_HAS_IMGUI
    void renderPlaybackControls();
    void renderWaveform();
    void renderSettings();
    void computeWaveformPeaks(const Assets::AudioClip* clip);
#endif

    Audio::AudioSystem* m_previewSystem = nullptr;
    Audio::AudioSource* m_previewSource = nullptr;
    Assets::AudioClip*  m_currentAsset  = nullptr;
    char m_assetName[256] = {};

    float m_volume = 1.0f;
    float m_pitch  = 1.0f;
    bool  m_looping = false;

    struct Peak { float min; float max; };
    std::vector<Peak> m_peaks;
    bool m_peaksDirty = false;

    float m_progress = 0.0f;

    bool m_open = true;
    bool m_initialized = false;
};

} // namespace Caffeine::Editor
