#include "terrain/paint/TerrainPaintStroke.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

void include(LayerBounds& bounds, u32 minX, u32 minZ, u32 maxX, u32 maxZ) {
    if (!bounds.valid) {
        bounds = {minX, minZ, maxX, maxZ, true};
        return;
    }
    bounds.minX = std::min(bounds.minX, minX);
    bounds.minZ = std::min(bounds.minZ, minZ);
    bounds.maxX = std::max(bounds.maxX, maxX);
    bounds.maxZ = std::max(bounds.maxZ, maxZ);
}

LayerBounds write(TerrainLayerMap& map, const LayerStrokeRecord& record, bool after) {
    for (const LayerBlock& block : record.blocks) {
        // A record made before the terrain was resized no longer fits: leave such a map alone.
        if (block.maxX >= map.resolutionX() || block.maxZ >= map.resolutionZ()) continue;
        const std::vector<LayerTexel>& values = after ? block.after : block.before;
        const u32 width = block.maxX - block.minX + 1;
        for (u32 z = block.minZ; z <= block.maxZ; ++z) {
            for (u32 x = block.minX; x <= block.maxX; ++x) {
                map.setRaw(x, z, values[static_cast<usize>(z - block.minZ) * width + (x - block.minX)]);
            }
        }
    }
    return record.region;
}

}  // namespace

usize LayerStrokeRecord::bytes() const {
    usize total = 0;
    for (const LayerBlock& block : blocks) total += (block.before.size() + block.after.size()) * sizeof(LayerTexel);
    return total;
}

LayerBounds LayerStrokeRecord::applyBefore(TerrainLayerMap& map) const { return write(map, *this, false); }
LayerBounds LayerStrokeRecord::applyAfter(TerrainLayerMap& map) const { return write(map, *this, true); }

LayerStrokeRecord diffLayerMaps(const TerrainLayerMap& before, const TerrainLayerMap& after) {
    LayerStrokeRecord record;
    if (before.resolutionX() != after.resolutionX() || before.resolutionZ() != after.resolutionZ()) return record;
    const u32 resX = after.resolutionX(), resZ = after.resolutionZ();
    const u32 block = TerrainPaintStroke::kBlock;
    for (u32 bz = 0; bz * block < resZ; ++bz) {
        for (u32 bx = 0; bx * block < resX; ++bx) {
            LayerBlock entry;
            entry.blockX = bx;
            entry.blockZ = bz;
            entry.minX = bx * block;
            entry.minZ = bz * block;
            entry.maxX = std::min(entry.minX + block - 1, resX - 1);
            entry.maxZ = std::min(entry.minZ + block - 1, resZ - 1);
            u32 loX = ~0u, loZ = ~0u, hiX = 0, hiZ = 0;
            for (u32 z = entry.minZ; z <= entry.maxZ; ++z) {
                for (u32 x = entry.minX; x <= entry.maxX; ++x) {
                    entry.before.push_back(before.at(x, z));
                    entry.after.push_back(after.at(x, z));
                    if (!(before.at(x, z) == after.at(x, z))) {
                        loX = std::min(loX, x);
                        loZ = std::min(loZ, z);
                        hiX = std::max(hiX, x);
                        hiZ = std::max(hiZ, z);
                    }
                }
            }
            if (loX == ~0u) continue;
            include(record.region, loX, loZ, hiX, hiZ);
            record.blocks.push_back(std::move(entry));
        }
    }
    return record;
}

TerrainPaintStroke::TerrainPaintStroke(TerrainLayerMap& map, const TerrainHeightmap& heightmap,
                                       const ECS::TerrainComponent& settings, PaintBrush brush)
    : m_map(map),
      m_height(heightmap),
      m_sizeX(settings.worldSizeX),
      m_sizeZ(settings.worldSizeZ),
      m_maxHeight(std::max(settings.maxHeight, 0.001f)),
      m_brush(std::move(brush)) {
    m_stepX = m_sizeX / static_cast<f64>(std::max(map.resolutionX(), 2u) - 1);
    m_stepZ = m_sizeZ / static_cast<f64>(std::max(map.resolutionZ(), 2u) - 1);
    StrokeBrush& shape = m_brush.shape;
    shape.radiusM = std::max(shape.radiusM, 0.01f);
    if (shape.spacingM <= 0.0f) shape.spacingM = shape.radiusM * 0.25f;
    shape.spacingM = std::max(shape.spacingM, 0.001f);
    m_brush.layer = std::min(m_brush.layer, TerrainLayerMap::kMaxLayers - 1);
    m_brush.fromLayer = std::min(m_brush.fromLayer, TerrainLayerMap::kMaxLayers - 1);
}

u32 TerrainPaintStroke::addPoint(const StrokePoint& point) {
    if (m_finished || m_map.empty()) return 0;
    if (!m_hasLast) {
        m_hasLast = true;
        m_lastX = point.x;
        m_lastZ = point.z;
        m_lastPressure = point.pressure;
        m_carry = 0.0;
        stamp(point.x, point.z, point.pressure);
        return 1;
    }
    const f64 dx = point.x - m_lastX;
    const f64 dz = point.z - m_lastZ;
    const f64 length = std::sqrt(dx * dx + dz * dz);
    u32 laid = 0;
    if (length > 0.0) {
        m_dirX = static_cast<f32>(dx / length);
        m_dirZ = static_cast<f32>(dz / length);
        const f64 spacing = static_cast<f64>(m_brush.shape.spacingM);
        f64 along = spacing - m_carry;
        f64 lastStamp = -m_carry;
        while (along <= length) {
            const f64 t = along / length;
            stamp(m_lastX + dx * t, m_lastZ + dz * t, m_lastPressure + (point.pressure - m_lastPressure) * static_cast<f32>(t));
            ++laid;
            lastStamp = along;
            along += spacing;
        }
        m_carry = length - lastStamp;
    }
    m_lastX = point.x;
    m_lastZ = point.z;
    m_lastPressure = point.pressure;
    return laid;
}

void TerrainPaintStroke::stamp(f64 x, f64 z, f32 pressure) {
    for (const StampParams& params : placeStamp(m_brush.shape, m_logical++, x, z, pressure, m_dirX, m_dirZ)) applyStamp(params);
}

void TerrainPaintStroke::touch(u32 blockX, u32 blockZ) {
    const u64 key = (static_cast<u64>(blockZ) << 32) | blockX;
    if (m_touched.count(key)) return;
    LayerBlock block;
    block.blockX = blockX;
    block.blockZ = blockZ;
    block.minX = blockX * kBlock;
    block.minZ = blockZ * kBlock;
    block.maxX = std::min(block.minX + kBlock - 1, m_map.resolutionX() - 1);
    block.maxZ = std::min(block.minZ + kBlock - 1, m_map.resolutionZ() - 1);
    for (u32 z = block.minZ; z <= block.maxZ; ++z) {
        for (u32 x = block.minX; x <= block.maxX; ++x) block.before.push_back(m_map.at(x, z));
    }
    m_touched.emplace(key, std::move(block));
}

void TerrainPaintStroke::applyStamp(const StampParams& params) {
    const u32 resX = m_map.resolutionX(), resZ = m_map.resolutionZ();
    const f64 gridX = (params.x + m_sizeX * 0.5) / m_stepX;
    const f64 gridZ = (params.z + m_sizeZ * 0.5) / m_stepZ;
    const i64 minX = std::max<i64>(0, static_cast<i64>(std::floor(gridX - params.radius / m_stepX)));
    const i64 maxX = std::min<i64>(static_cast<i64>(resX) - 1, static_cast<i64>(std::ceil(gridX + params.radius / m_stepX)));
    const i64 minZ = std::max<i64>(0, static_cast<i64>(std::floor(gridZ - params.radius / m_stepZ)));
    const i64 maxZ = std::min<i64>(static_cast<i64>(resZ) - 1, static_cast<i64>(std::ceil(gridZ + params.radius / m_stepZ)));
    const u32 stampNumber = m_stamps++;
    if (minX > maxX || minZ > maxZ) return;

    const StrokeBrush& shape = m_brush.shape;
    const TerrainFrame frame{&m_height, static_cast<f32>(m_sizeX), static_cast<f32>(m_sizeZ), m_maxHeight};
    struct Cell {
        u32 x, z;
        f32 amount;
    };
    std::vector<Cell> cells;
    // Every texel's share is decided before any is written, so the protection masks and a smoothing neighbourhood see
    // the layers as they were: the result does not depend on the order of the texels.
    for (i64 z = minZ; z <= maxZ; ++z) {
        for (i64 x = minX; x <= maxX; ++x) {
            const f64 dx = (static_cast<f64>(x) - gridX) * m_stepX;
            const f64 dz = (static_cast<f64>(z) - gridZ) * m_stepZ;
            const f64 dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= params.radius) continue;
            f32 weight = shapeWeight(shape, params, dx, dz, dist);
            if (weight <= 0.0f) continue;  // a signed stamp's negative half does not paint
            if (!shape.protect.empty()) {
                weight *= protectionWeight(shape, m_masks, frame, static_cast<f32>(static_cast<f64>(x) * m_stepX),
                                           static_cast<f32>(static_cast<f64>(z) * m_stepZ));
            }
            const f32 amount = std::clamp(weight * params.strength, 0.0f, 1.0f);
            if (amount > 0.0f) cells.push_back({static_cast<u32>(x), static_cast<u32>(z), amount});
        }
    }
    if (cells.empty()) return;
    for (const Cell& cell : cells) touch(cell.x / kBlock, cell.z / kBlock);

    if (m_brush.mode == PaintMode::Smooth) {
        std::vector<LayerTexel> next;
        next.reserve(cells.size());
        for (const Cell& cell : cells) next.push_back(m_map.smoothed(cell.x, cell.z, cell.amount, stampNumber));
        for (usize i = 0; i < cells.size(); ++i) m_map.setRaw(cells[i].x, cells[i].z, next[i]);
    } else {
        for (const Cell& cell : cells) {
            switch (m_brush.mode) {
                case PaintMode::Paint: m_map.blendToward(cell.x, cell.z, m_brush.layer, cell.amount, stampNumber); break;
                case PaintMode::Erase: m_map.eraseLayer(cell.x, cell.z, m_brush.layer, cell.amount, stampNumber); break;
                case PaintMode::Replace:
                    m_map.replaceLayer(cell.x, cell.z, m_brush.fromLayer, m_brush.layer, cell.amount, stampNumber);
                    break;
                case PaintMode::Smooth: break;
            }
        }
    }
    include(m_dirty, static_cast<u32>(minX), static_cast<u32>(minZ), static_cast<u32>(maxX), static_cast<u32>(maxZ));
}

LayerStrokeRecord TerrainPaintStroke::finish() {
    LayerStrokeRecord record;
    m_finished = true;
    record.stamps = m_stamps;
    std::vector<u64> keys;
    keys.reserve(m_touched.size());
    for (const auto& entry : m_touched) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());  // the same stroke always writes its record in the same order
    for (u64 key : keys) {
        LayerBlock& block = m_touched[key];
        for (u32 z = block.minZ; z <= block.maxZ; ++z) {
            for (u32 x = block.minX; x <= block.maxX; ++x) block.after.push_back(m_map.at(x, z));
        }
        // Only the texels that really differ count: a block the stroke left as it was is not kept.
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
        include(record.region, loX, loZ, hiX, hiZ);
        record.blocks.push_back(std::move(block));
    }
    return record;
}

}  // namespace Caffeine::Terrain
