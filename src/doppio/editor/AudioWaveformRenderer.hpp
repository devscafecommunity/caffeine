#pragma once
#include "core/Types.hpp"
#include <vector>
#include <cstdint>

namespace Caffeine::Editor {

class AudioWaveformRenderer {
public:
    struct WaveformData {
        std::vector<f32> leftChannel;
        std::vector<f32> rightChannel;
        u32 sampleRate;
        bool isStereo;
    };

    static WaveformData generateWaveform(
        const std::vector<u8>& cafBlob,
        u32 targetWidth = 256
    );

    static u32 renderWaveformTexture(
        const WaveformData& data,
        u32 width = 256,
        u32 height = 64
    );
};

} // namespace Caffeine::Editor
