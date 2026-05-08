#pragma once

#include "tools/PipelineTypes.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"
#include <vector>
#include <cstdio>
#include <string_view>
#include <cstring>

namespace Caffeine::Tools {
using namespace Caffeine;

class AudioEncoder {
public:
    static ConversionResult encodeRaw(
        std::string_view outputPath,
        const i16* samples, u32 sampleCount,
        u32 sampleRate, u16 channels,
        const AudioEncodeOptions& opts = {})
    {
        ConversionResult result;
        
        if (!samples || sampleCount == 0) {
            result.errorMessage = "Invalid audio data: null samples or zero count";
            return result;
        }
        
        AudioMetadata meta;
        meta.sampleRate = sampleRate;
        meta.channels = channels;
        meta.bitsPerSample = 16;
        meta.sampleCount = sampleCount;
        meta.reserved = 0;
        
        const void* payload = samples;
        u64 payloadSize = sampleCount * channels * sizeof(i16);
        
        auto writeResult = IO::CafWriter::write(
            outputPath.data(),
            AssetType::Audio,
            CAF_FLAG_NONE,
            &meta, sizeof(meta),
            payload, payloadSize
        );
        
        result.success = writeResult.success;
        if (!writeResult.success) {
            result.errorMessage = "Failed to write CAF file";
        }
        result.inputBytes = payloadSize;
        result.outputBytes = writeResult.bytesWritten;
        result.compressionRatio = result.inputBytes > 0 
            ? static_cast<f32>(result.outputBytes) / static_cast<f32>(result.inputBytes) 
            : 0.0f;
        
        return result;
    }
    
    ConversionResult encode(std::string_view inputPath,
                            std::string_view outputPath,
                            const AudioEncodeOptions& opts = {})
    {
        std::string pathStr(inputPath);
        if (pathStr.size() >= 4 && pathStr.substr(pathStr.size() - 4) == ".wav") {
            return encodeWav(inputPath, outputPath, opts);
        }
        
        ConversionResult result;
        result.errorMessage = "Unsupported audio format (only .wav supported)";
        return result;
    }

private:
    ConversionResult encodeWav(std::string_view inputPath,
                               std::string_view outputPath,
                               const AudioEncodeOptions& opts)
    {
        ConversionResult result;
        
        FILE* f = std::fopen(inputPath.data(), "rb");
        if (!f) {
            result.errorMessage = "Failed to open WAV file";
            return result;
        }
        
        std::fseek(f, 0, SEEK_END);
        long fileSize = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        
        if (fileSize < 44) {
            std::fclose(f);
            result.errorMessage = "File too small to be valid WAV";
            return result;
        }
        
        std::vector<u8> buffer(fileSize);
        std::fread(buffer.data(), 1, fileSize, f);
        std::fclose(f);
        
        if (std::memcmp(buffer.data(), "RIFF", 4) != 0) {
            result.errorMessage = "Invalid WAV file: missing RIFF header";
            return result;
        }
        
        if (std::memcmp(buffer.data() + 8, "WAVE", 4) != 0) {
            result.errorMessage = "Invalid WAV file: missing WAVE header";
            return result;
        }
        
        if (std::memcmp(buffer.data() + 12, "fmt ", 4) != 0) {
            result.errorMessage = "Invalid WAV file: missing fmt chunk";
            return result;
        }
        
        u16 audioFormat;
        u16 numChannels;
        u32 sampleRate;
        u16 bitsPerSample;
        
        std::memcpy(&audioFormat, buffer.data() + 20, 2);
        std::memcpy(&numChannels, buffer.data() + 22, 2);
        std::memcpy(&sampleRate, buffer.data() + 24, 4);
        std::memcpy(&bitsPerSample, buffer.data() + 34, 2);
        
        if (audioFormat != 1) {
            result.errorMessage = "Unsupported WAV format (only PCM supported)";
            return result;
        }
        
        if (bitsPerSample != 16) {
            result.errorMessage = "Unsupported bit depth (only 16-bit supported)";
            return result;
        }
        
        if (std::memcmp(buffer.data() + 36, "data", 4) != 0) {
            result.errorMessage = "Invalid WAV file: missing data chunk";
            return result;
        }
        
        u32 dataSize;
        std::memcpy(&dataSize, buffer.data() + 40, 4);
        
        if (fileSize < 44 + dataSize) {
            result.errorMessage = "Truncated WAV file";
            return result;
        }
        
        const i16* samples = reinterpret_cast<const i16*>(buffer.data() + 44);
        u32 sampleCount = dataSize / (numChannels * sizeof(i16));
        
        return encodeRaw(outputPath, samples, sampleCount, sampleRate, numChannels, opts);
    }
};

} // namespace Caffeine::Tools
