#include "editor/AudioWaveformRenderer.hpp"
#include "core/io/CafTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Caffeine::Editor {

AudioWaveformRenderer::WaveformData AudioWaveformRenderer::generateWaveform(
    const std::vector<u8>& cafBlob,
    u32 targetWidth
) {
    WaveformData data{};
    data.sampleRate = 0;
    data.isStereo = false;
    
    if (cafBlob.size() < sizeof(CafHeader)) {
        return data;
    }

    CafHeader header{};
    std::memcpy(&header, cafBlob.data(), sizeof(CafHeader));

    if (header.type != AssetType::Audio) {
        return data;
    }

    if (cafBlob.size() < sizeof(CafHeader) + sizeof(AudioMetadata)) {
        return data;
    }

    AudioMetadata audioMeta{};
    std::memcpy(&audioMeta, 
                cafBlob.data() + sizeof(CafHeader),
                sizeof(AudioMetadata));

    data.sampleRate = audioMeta.sampleRate;
    data.isStereo = (audioMeta.channels == 2);

    u32 payloadOffset = sizeof(CafHeader) + static_cast<u32>(header.metadataSize);
    if (cafBlob.size() < payloadOffset) {
        return data;
    }

    const i16* samples = reinterpret_cast<const i16*>(cafBlob.data() + payloadOffset);
    u32 sampleCount = (cafBlob.size() - payloadOffset) / sizeof(i16);

    if (sampleCount == 0) {
        return data;
    }

    u32 samplesPerBin = std::max(1u, sampleCount / (targetWidth * audioMeta.channels));
    
    for (u32 i = 0; i < targetWidth; i++) {
        u32 startIdx = i * samplesPerBin * audioMeta.channels;
        u32 endIdx = std::min(startIdx + samplesPerBin * audioMeta.channels, sampleCount);
        
        f32 minLeft = 0.0f, maxLeft = 0.0f;
        f32 minRight = 0.0f, maxRight = 0.0f;
        
        for (u32 j = startIdx; j < endIdx; j++) {
            f32 normalized = static_cast<f32>(samples[j]) / 32768.0f;
            normalized = std::clamp(normalized, -1.0f, 1.0f);
            
            u32 sampleIndex = j - startIdx;
            if (sampleIndex % audioMeta.channels == 0) {
                minLeft = std::min(minLeft, normalized);
                maxLeft = std::max(maxLeft, normalized);
            } else if (audioMeta.channels == 2) {
                minRight = std::min(minRight, normalized);
                maxRight = std::max(maxRight, normalized);
            }
        }
        
        data.leftChannel.push_back(std::max(maxLeft - minLeft, 0.0f));
        if (data.isStereo) {
            data.rightChannel.push_back(std::max(maxRight - minRight, 0.0f));
        }
    }

    return data;
}

u32 AudioWaveformRenderer::renderWaveformTexture(
    const WaveformData& data,
    u32 width,
    u32 height
) {
    if (data.leftChannel.empty()) {
        return 0;
    }
    
    return 0;
}

} // namespace Caffeine::Editor
