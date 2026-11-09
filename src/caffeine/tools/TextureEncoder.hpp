#pragma once

#include "tools/PipelineTypes.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"
#include <vector>
#include <cmath>
#include <cstring>
#include <string_view>
#include <cstdio>

namespace Caffeine::Tools {
using namespace Caffeine;

class TextureEncoder {
public:
    static ConversionResult encodeRaw(
        std::string_view outputPath,
        const u8* pixels, u32 width, u32 height, u32 channels,
        const TextureEncodeOptions& opts = {})
    {
        ConversionResult result;
        
        if (!pixels || width == 0 || height == 0) {
            result.errorMessage = "Invalid texture data: null pixels or zero dimensions";
            return result;
        }
        
        std::vector<MipLevel> mips;
        
        if (opts.generateMipmaps) {
            mips = generateMipmaps(pixels, width, height, channels, opts.maxMipLevels);
        } else {
            MipLevel baseMip;
            baseMip.width = width;
            baseMip.height = height;
            baseMip.data.resize(width * height * channels);
            std::memcpy(baseMip.data.data(), pixels, width * height * channels);
            mips.push_back(baseMip);
        }
        
        std::vector<u8> payload;
        for (const auto& mip : mips) {
            payload.insert(payload.end(), mip.data.begin(), mip.data.end());
        }
        
        TextureMetadata meta;
        meta.width = width;
        meta.height = height;
        meta.format = 0;
        meta.mipLevels = static_cast<u32>(mips.size());
        meta.reserved = 0;
        
        u16 flags = CAF_FLAG_NONE;
        if (mips.size() > 1) {
            flags |= CAF_FLAG_MIPCHAIN;
        }
        
        auto writeResult = IO::CafWriter::write(
            outputPath.data(),
            AssetType::Texture,
            flags,
            &meta, sizeof(meta),
            payload.data(), payload.size()
        );
        
        result.success = writeResult.success;
        if (!writeResult.success) {
            result.errorMessage = "Failed to write CAF file";
        }
        result.inputBytes = width * height * channels;
        result.outputBytes = writeResult.bytesWritten;
        result.compressionRatio = result.inputBytes > 0 
            ? static_cast<f32>(result.outputBytes) / static_cast<f32>(result.inputBytes) 
            : 0.0f;
        
        return result;
    }
    
    static std::vector<MipLevel> generateMipmaps(
        const u8* pixels, u32 width, u32 height, u32 channels,
        u32 maxLevels = 0)
    {
        std::vector<MipLevel> mips;
        
        MipLevel baseMip;
        baseMip.width = width;
        baseMip.height = height;
        baseMip.data.resize(width * height * channels);
        std::memcpy(baseMip.data.data(), pixels, width * height * channels);
        mips.push_back(baseMip);
        
        u32 currentWidth = width;
        u32 currentHeight = height;
        const u8* currentData = pixels;
        std::vector<u8> tempBuffer;
        
        while ((currentWidth > 1 || currentHeight > 1) && 
               (maxLevels == 0 || mips.size() < maxLevels))
        {
            u32 nextWidth = currentWidth > 1 ? currentWidth / 2 : 1;
            u32 nextHeight = currentHeight > 1 ? currentHeight / 2 : 1;
            
            MipLevel nextMip;
            nextMip.width = nextWidth;
            nextMip.height = nextHeight;
            nextMip.data.resize(nextWidth * nextHeight * channels);
            
            for (u32 y = 0; y < nextHeight; ++y) {
                for (u32 x = 0; x < nextWidth; ++x) {
                    u32 srcX = x * 2;
                    u32 srcY = y * 2;
                    
                    for (u32 c = 0; c < channels; ++c) {
                        u32 sum = 0;
                        u32 count = 0;
                        
                        for (u32 dy = 0; dy < 2 && (srcY + dy) < currentHeight; ++dy) {
                            for (u32 dx = 0; dx < 2 && (srcX + dx) < currentWidth; ++dx) {
                                u32 srcIdx = ((srcY + dy) * currentWidth + (srcX + dx)) * channels + c;
                                sum += currentData[srcIdx];
                                ++count;
                            }
                        }
                        
                        u32 dstIdx = (y * nextWidth + x) * channels + c;
                        nextMip.data[dstIdx] = static_cast<u8>(sum / count);
                    }
                }
            }
            
            tempBuffer = nextMip.data;
            currentData = tempBuffer.data();
            currentWidth = nextWidth;
            currentHeight = nextHeight;
            
            mips.push_back(nextMip);
        }
        
        return mips;
    }
    
    ConversionResult encode(std::string_view inputPath,
                            std::string_view outputPath,
                            const TextureEncodeOptions& opts = {})
    {
        (void)inputPath;
        (void)outputPath;
        (void)opts;
        ConversionResult result;
        result.errorMessage = "stb_image not linked - file-based encoding unavailable";
        return result;
    }
};

} // namespace Caffeine::Tools
