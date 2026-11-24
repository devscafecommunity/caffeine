#include "terrain/paint/TerrainAutoLayer.hpp"

#include <algorithm>
#include <unordered_map>

namespace Caffeine::Terrain {

LayerStrokeRecord autoLayer(TerrainLayerMap& map, const TerrainHeightmap& heightmap, const ECS::TerrainComponent& settings,
                            const TerrainMasks* masks, const BiomeTable& rules, const AutoLayerOptions& options) {
    LayerStrokeRecord record;
    if (map.empty()) return record;
    const u32 resX = map.resolutionX(), resZ = map.resolutionZ();
    LayerBounds area = options.region;
    if (!area.valid) area = {0, 0, resX - 1, resZ - 1, true};
    area.maxX = std::min(area.maxX, resX - 1);
    area.maxZ = std::min(area.maxZ, resZ - 1);
    area.minX = std::min(area.minX, area.maxX);
    area.minZ = std::min(area.minZ, area.maxZ);

    const f32 sizeX = settings.worldSizeX, sizeZ = settings.worldSizeZ;
    const f32 stepX = sizeX / static_cast<f32>(std::max(resX, 2u) - 1);
    const f32 stepZ = sizeZ / static_cast<f32>(std::max(resZ, 2u) - 1);
    const TerrainFrame frame{&heightmap, sizeX, sizeZ, std::max(settings.maxHeight, 0.001f)};
    const TerrainMasks builtin;
    const TerrainMasks& source = masks ? *masks : builtin;
    const u32 fallback = std::min(options.fallbackLayer, TerrainLayerMap::kMaxLayers - 1);

    // The layers the rules name, once.
    std::vector<u32> ruled;
    for (const BiomeRule& rule : rules.rules()) {
        if (rule.classId < TerrainLayerMap::kMaxLayers) ruled.push_back(rule.classId);
    }

    const u32 blockX0 = area.minX / TerrainPaintStroke::kBlock, blockX1 = area.maxX / TerrainPaintStroke::kBlock;
    const u32 blockZ0 = area.minZ / TerrainPaintStroke::kBlock, blockZ1 = area.maxZ / TerrainPaintStroke::kBlock;
    for (u32 bz = blockZ0; bz <= blockZ1; ++bz) {
        for (u32 bx = blockX0; bx <= blockX1; ++bx) {
            LayerBlock block;
            block.blockX = bx;
            block.blockZ = bz;
            block.minX = bx * TerrainPaintStroke::kBlock;
            block.minZ = bz * TerrainPaintStroke::kBlock;
            block.maxX = std::min(block.minX + TerrainPaintStroke::kBlock - 1, resX - 1);
            block.maxZ = std::min(block.minZ + TerrainPaintStroke::kBlock - 1, resZ - 1);
            for (u32 z = block.minZ; z <= block.maxZ; ++z) {
                for (u32 x = block.minX; x <= block.maxX; ++x) block.before.push_back(map.at(x, z));
            }
            for (u32 z = std::max(block.minZ, area.minZ); z <= std::min(block.maxZ, area.maxZ); ++z) {
                for (u32 x = std::max(block.minX, area.minX); x <= std::min(block.maxX, area.maxX); ++x) {
                    std::vector<std::pair<u32, f32>> weights;
                    f32 total = 0.0f;
                    for (u32 layer : ruled) {
                        const f32 density = rules.density(source, frame, layer, static_cast<f32>(x) * stepX, static_cast<f32>(z) * stepZ);
                        if (density > 0.0f) {
                            weights.emplace_back(layer, density);
                            total += density;
                        }
                    }
                    if (total <= 0.0f) weights.emplace_back(fallback, 1.0f);
                    map.assign(x, z, weights, 0);
                }
            }
            for (u32 z = block.minZ; z <= block.maxZ; ++z) {
                for (u32 x = block.minX; x <= block.maxX; ++x) block.after.push_back(map.at(x, z));
            }
            u32 loX = ~0u, loZ = ~0u, hiX = 0, hiZ = 0;
            const u32 width = block.maxX - block.minX + 1;
            for (usize i = 0; i < block.after.size(); ++i) {
                if (block.after[i] == block.before[i]) continue;
                const u32 x = block.minX + static_cast<u32>(i % width), z = block.minZ + static_cast<u32>(i / width);
                loX = std::min(loX, x);
                loZ = std::min(loZ, z);
                hiX = std::max(hiX, x);
                hiZ = std::max(hiZ, z);
            }
            if (loX == ~0u) continue;
            if (!record.region.valid) {
                record.region = {loX, loZ, hiX, hiZ, true};
            } else {
                record.region.minX = std::min(record.region.minX, loX);
                record.region.minZ = std::min(record.region.minZ, loZ);
                record.region.maxX = std::max(record.region.maxX, hiX);
                record.region.maxZ = std::max(record.region.maxZ, hiZ);
            }
            record.blocks.push_back(std::move(block));
        }
    }
    return record;
}

}  // namespace Caffeine::Terrain
