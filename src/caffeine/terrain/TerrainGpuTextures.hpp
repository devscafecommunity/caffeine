#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/TerrainComponents.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

class TerrainSplatmap;
class TerrainLayerMap;
class TerrainPalette;
struct LayerBounds;

struct TerrainGpuTextures {
    RHI::Texture* splatMap = nullptr;
    RHI::Texture* layers[ECS::kTerrainSplatLayerCount] = {};
    RHI::Texture* albedo = nullptr;
    RHI::Texture* normalMap = nullptr;
    u32 uploadedSplatRevision = 0;

    // --- layered painting (phase 2): an index map and a weight map (RGBA8, four layers a texel, read without
    // filtering) and one array slice per layer in use.
    bool useLayers = false;
    RHI::Texture* indexMap = nullptr;
    RHI::Texture* weightMap = nullptr;
    RHI::Texture* layerArray = nullptr;
    u32 uploadedLayerRevision = 0;
    u32 arraySize = 0;                    // pixels a side of every slice
    std::vector<u32> slotLayers;          // palette index of each array slice
    std::string arrayKey;                 // what the array was built from (paths, tints, size)
    f32 layerTile[64] = {};               // metres per repeat, by slice
    f32 layerRotation[64] = {};           // radians, by slice
    u64 arrayBytes = 0;                   // VRAM the array takes
    std::string cachedLayerPaths[ECS::kTerrainSplatLayerCount];
    std::string cachedAlbedoPath;
    std::string cachedNormalPath;
    bool useSplatmap = false;
};

class TerrainGpuTextureCache {
public:
    static TerrainGpuTextureCache& instance();

    /// Brings the entity's textures up to date. With layers, `dirty` is the texels changed since the last call (null:
    /// all of them). Returns true when the layer maps are up to date (the caller may clear its dirty rectangle).
    bool sync(RHI::RenderDevice* device, ECS::Entity entity,
              const ECS::TerrainComponent& terrain,
              const TerrainSplatmap* splatmap,
              const TerrainLayerMap* layers,
              const TerrainPalette* palette,
              const LayerBounds* dirty,
              const std::string& projectRoot);
    const TerrainGpuTextures* get(ECS::Entity entity) const;
    bool hasRenderableTextures(ECS::Entity entity) const;
    void invalidateEntity(ECS::Entity entity, RHI::RenderDevice* device);
    RHI::Texture* whiteTexture(RHI::RenderDevice* device);
    RHI::Texture* textureFromPath(RHI::RenderDevice* device, const std::string& path,
                                 const std::string& projectRoot);
    void removeEntity(ECS::Entity entity, RHI::RenderDevice* device);
    void releaseAll(RHI::RenderDevice* device);

private:
    RHI::Texture* ensureWhiteTexture(RHI::RenderDevice* device);
    RHI::Texture* loadTexture(RHI::RenderDevice* device, const std::string& path,
                              const std::string& projectRoot);
    void releaseTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device);
    void releaseLayerTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device);
    bool syncLayers(RHI::RenderDevice* device, TerrainGpuTextures& gpu, const ECS::TerrainComponent& terrain,
                    const TerrainLayerMap& layers, const TerrainPalette& palette, const LayerBounds* dirty,
                    const std::string& projectRoot);

    std::unordered_map<u32, TerrainGpuTextures> m_entries;
    RHI::Texture* m_whiteTexture = nullptr;
};

}  // namespace Caffeine::Terrain
#endif
