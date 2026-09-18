#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/TerrainComponents.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"

#include <string>
#include <unordered_map>

namespace Caffeine::Terrain {

class TerrainSplatmap;

struct TerrainGpuTextures {
    RHI::Texture* splatMap = nullptr;
    RHI::Texture* layers[ECS::kTerrainSplatLayerCount] = {};
    RHI::Texture* albedo = nullptr;
    u32 uploadedSplatRevision = 0;
    std::string cachedLayerPaths[ECS::kTerrainSplatLayerCount];
    std::string cachedAlbedoPath;
    bool useSplatmap = false;
};

class TerrainGpuTextureCache {
public:
    static TerrainGpuTextureCache& instance();

    void sync(RHI::RenderDevice* device, ECS::Entity entity,
              const ECS::TerrainComponent& terrain,
              const TerrainSplatmap* splatmap,
              const std::string& projectRoot);
    const TerrainGpuTextures* get(ECS::Entity entity) const;
    bool hasRenderableTextures(ECS::Entity entity) const;
    void invalidateEntity(ECS::Entity entity, RHI::RenderDevice* device);
    RHI::Texture* whiteTexture(RHI::RenderDevice* device);
    void removeEntity(ECS::Entity entity, RHI::RenderDevice* device);
    void releaseAll(RHI::RenderDevice* device);

private:
    RHI::Texture* ensureWhiteTexture(RHI::RenderDevice* device);
    RHI::Texture* loadTexture(RHI::RenderDevice* device, const std::string& path,
                              const std::string& projectRoot);
    void releaseTextures(TerrainGpuTextures& gpu, RHI::RenderDevice* device);

    std::unordered_map<u32, TerrainGpuTextures> m_entries;
    RHI::Texture* m_whiteTexture = nullptr;
};

}  // namespace Caffeine::Terrain
#endif
