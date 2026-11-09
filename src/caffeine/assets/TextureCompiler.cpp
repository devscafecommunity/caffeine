#include "assets/TextureCompiler.hpp"
#include "core/io/CafWriter.hpp"

// stb_image: one C file must define the implementation before the include.
// We are that file.
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace Caffeine::Assets {

bool TextureCompiler::Compile(AssetImportContext& ctx) {
    int width  = 0;
    int height = 0;
    int channels = 0;

    stbi_uc* pixels = stbi_load(
        ctx.SourcePath.string().c_str(),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha
    );

    if (!pixels) {
        ctx.Logs.push_back(std::string("TextureCompiler: failed to load ") +
            ctx.SourcePath.string() + " — " +
            (stbi_failure_reason() ? stbi_failure_reason() : "unknown error"));
        ctx.Success = false;
        return false;
    }

    ctx.Logs.push_back("TextureCompiler: loaded " +
        std::to_string(width) + "x" + std::to_string(height) +
        " (" + std::to_string(channels) + " channels)");

    TextureMetadata meta{};
    meta.width     = static_cast<u32>(width);
    meta.height    = static_cast<u32>(height);
    meta.format    = 0;
    meta.mipLevels = 1;
    meta.reserved  = 0;

    u64 pixelDataSize = static_cast<u64>(width) * static_cast<u64>(height) * 4;

    auto result = IO::CafWriter::write(
        ctx.DestinationPath.string().c_str(),
        AssetType::Texture,
        CAF_FLAG_NONE,
        &meta,
        sizeof(meta),
        pixels,
        pixelDataSize
    );

    stbi_image_free(pixels);

    if (!result.success) {
        ctx.Logs.push_back("TextureCompiler: CafWriter::write failed for " +
            ctx.DestinationPath.string());
        ctx.Success = false;
        return false;
    }

    ctx.Logs.push_back("TextureCompiler: wrote " +
        std::to_string(result.bytesWritten) + " bytes to " +
        ctx.DestinationPath.filename().string());
    ctx.Success = true;
    return true;
}

} // namespace Caffeine::Assets
