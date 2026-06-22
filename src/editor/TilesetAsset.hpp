#pragma once
#include "core/Types.hpp"
#include <string>
#include <vector>
#include <algorithm>

#ifdef CF_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace Caffeine::Editor {

struct TileUV {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

struct TilesetAsset {
    std::string path;
    i32 tileWidth  = 32;
    i32 tileHeight = 32;
    i32 columns    = 0;
    i32 rows       = 0;
    std::vector<TileUV> tiles;

    void* textureHandle = nullptr;
    i32   textureW = 0, textureH = 0;

    bool isLoaded() const { return textureHandle != nullptr; }

    void computeUVs() {
        tiles.clear();
        if (textureW <= 0 || textureH <= 0 || tileWidth <= 0 || tileHeight <= 0) return;
        columns = textureW / tileWidth;
        rows    = textureH / tileHeight;
        for (i32 row = 0; row < rows; ++row) {
            for (i32 col = 0; col < columns; ++col) {
                TileUV uv;
                uv.u0 = static_cast<float>(col * tileWidth)  / textureW;
                uv.v0 = static_cast<float>(row * tileHeight) / textureH;
                uv.u1 = static_cast<float>((col + 1) * tileWidth)  / textureW;
                uv.v1 = static_cast<float>((row + 1) * tileHeight) / textureH;
                tiles.push_back(uv);
            }
        }
    }

    i32 tileCount() const { return static_cast<i32>(tiles.size()); }

    void destroy() {
#ifdef CF_HAS_SDL3
        if (textureHandle) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(textureHandle));
            textureHandle = nullptr;
        }
#endif
    }
};

} // namespace Caffeine::Editor
