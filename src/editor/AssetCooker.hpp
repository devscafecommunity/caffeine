#pragma once
#include "core/Types.hpp"
#include "BuildSystem.hpp"
#include <string>

namespace Caffeine::Editor {

class AssetCooker {
public:
    static bool CookTextures(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress);
    static bool CookShaders(const std::string& assetsDir, const std::string& outputDir, BuildProgress& progress);
    static bool LoadBuildCache(const std::string& cacheFile);
    static bool SaveBuildCache(const std::string& cacheFile);

private:
    static bool ShouldCookAsset(const std::string& assetPath);
};

}
