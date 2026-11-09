#pragma once

#include "assets/AssetPipeline.hpp"

namespace Caffeine::Assets {

class TextureCompiler final : public IAssetCompiler {
public:
    bool Compile(AssetImportContext& ctx) override;

    std::vector<std::string> GetSupportedExtensions() override {
        return { ".png", ".jpg", ".jpeg" };
    }
};

} // namespace Caffeine::Assets
