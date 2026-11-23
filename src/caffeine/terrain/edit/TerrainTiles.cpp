#include "terrain/edit/TerrainTiles.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {

TileGrid::TileGrid(u32 resolutionX, u32 resolutionZ, f64 sizeXMetres, f64 sizeZMetres)
    : m_resX(resolutionX), m_resZ(resolutionZ) {
    if (!valid()) return;
    const u32 cellsX = m_resX - 1;
    const u32 cellsZ = m_resZ - 1;
    m_tilesX = std::max(1u, (cellsX + kTileCells - 1) / kTileCells);
    m_tilesZ = std::max(1u, (cellsZ + kTileCells - 1) / kTileCells);
    m_cellX = sizeXMetres / static_cast<f64>(cellsX);
    m_cellZ = sizeZMetres / static_cast<f64>(cellsZ);
}

TileCoord TileGrid::coordOf(u32 index) const {
    return TileCoord{static_cast<i32>(index % m_tilesX), static_cast<i32>(index / m_tilesX)};
}

TileCoord TileGrid::tileOfVertex(u32 vertexX, u32 vertexZ) const {
    return TileCoord{static_cast<i32>(std::min(vertexX / kTileCells, m_tilesX - 1)),
                     static_cast<i32>(std::min(vertexZ / kTileCells, m_tilesZ - 1))};
}

TerrainVertexBounds TileGrid::owned(TileCoord tile) const {
    TerrainVertexBounds bounds;
    bounds.minX = static_cast<u32>(tile.x) * kTileCells;
    bounds.minZ = static_cast<u32>(tile.z) * kTileCells;
    // The last tile of a row or a column also owns the final vertex.
    bounds.maxX = static_cast<u32>(tile.x) + 1 == m_tilesX ? m_resX - 1 : bounds.minX + kTileCells - 1;
    bounds.maxZ = static_cast<u32>(tile.z) + 1 == m_tilesZ ? m_resZ - 1 : bounds.minZ + kTileCells - 1;
    bounds.valid = true;
    return bounds;
}

TerrainVertexBounds TileGrid::meshRegion(TileCoord tile) const {
    TerrainVertexBounds bounds = owned(tile);
    bounds.minX = bounds.minX > 0 ? bounds.minX - 1 : 0;
    bounds.minZ = bounds.minZ > 0 ? bounds.minZ - 1 : 0;
    bounds.maxX = std::min(bounds.maxX + 1, m_resX - 1);
    bounds.maxZ = std::min(bounds.maxZ + 1, m_resZ - 1);
    return bounds;
}

i64 TileGrid::vertexXCm(u32 vertexX) const {
    return static_cast<i64>(std::llround(static_cast<f64>(vertexX) * m_cellX * 100.0));
}

i64 TileGrid::vertexZCm(u32 vertexZ) const {
    return static_cast<i64>(std::llround(static_cast<f64>(vertexZ) * m_cellZ * 100.0));
}

TileCoord TileGrid::tileAtCm(i64 xCm, i64 zCm) const {
    const f64 vx = m_cellX > 0.0 ? static_cast<f64>(xCm) / (m_cellX * 100.0) : 0.0;
    const f64 vz = m_cellZ > 0.0 ? static_cast<f64>(zCm) / (m_cellZ * 100.0) : 0.0;
    const u32 x = static_cast<u32>(std::clamp(std::llround(vx), 0ll, static_cast<long long>(m_resX - 1)));
    const u32 z = static_cast<u32>(std::clamp(std::llround(vz), 0ll, static_cast<long long>(m_resZ - 1)));
    return tileOfVertex(x, z);
}

DirtyTiles::DirtyTiles(const TileGrid& grid) : m_grid(grid), m_dirty(grid.tileCount(), 0) {}

void DirtyTiles::mark(const TerrainVertexBounds& changed) {
    if (!changed.valid || !m_grid.valid()) return;
    if (!m_changed.valid) {
        m_changed = changed;
    } else {
        m_changed.minX = std::min(m_changed.minX, changed.minX);
        m_changed.minZ = std::min(m_changed.minZ, changed.minZ);
        m_changed.maxX = std::max(m_changed.maxX, changed.maxX);
        m_changed.maxZ = std::max(m_changed.maxZ, changed.maxZ);
    }
    const u32 minX = changed.minX > 0 ? changed.minX - 1 : 0;
    const u32 minZ = changed.minZ > 0 ? changed.minZ - 1 : 0;
    const u32 maxX = std::min(changed.maxX + 1, m_grid.resolutionX() - 1);
    const u32 maxZ = std::min(changed.maxZ + 1, m_grid.resolutionZ() - 1);
    const TileCoord first = m_grid.tileOfVertex(minX, minZ);
    const TileCoord last = m_grid.tileOfVertex(maxX, maxZ);
    for (i32 z = first.z; z <= last.z; ++z) {
        for (i32 x = first.x; x <= last.x; ++x) {
            u8& flag = m_dirty[m_grid.index(TileCoord{x, z})];
            if (!flag) {
                flag = 1;
                ++m_count;
            }
        }
    }
}

bool DirtyTiles::isDirty(TileCoord tile) const {
    if (!m_grid.valid() || tile.x < 0 || tile.z < 0 || static_cast<u32>(tile.x) >= m_grid.tilesX() ||
        static_cast<u32>(tile.z) >= m_grid.tilesZ()) {
        return false;
    }
    return m_dirty[m_grid.index(tile)] != 0;
}

std::vector<TileCoord> DirtyTiles::tiles() const {
    std::vector<TileCoord> out;
    out.reserve(m_count);
    for (u32 i = 0; i < m_dirty.size(); ++i) {
        if (m_dirty[i]) out.push_back(m_grid.coordOf(i));
    }
    return out;
}

TerrainVertexBounds DirtyTiles::region() const {
    TerrainVertexBounds region;
    for (u32 i = 0; i < m_dirty.size(); ++i) {
        if (!m_dirty[i]) continue;
        const TerrainVertexBounds mesh = m_grid.meshRegion(m_grid.coordOf(i));
        if (!region.valid) {
            region = mesh;
        } else {
            region.minX = std::min(region.minX, mesh.minX);
            region.minZ = std::min(region.minZ, mesh.minZ);
            region.maxX = std::max(region.maxX, mesh.maxX);
            region.maxZ = std::max(region.maxZ, mesh.maxZ);
        }
    }
    return region;
}

void DirtyTiles::clear() {
    std::fill(m_dirty.begin(), m_dirty.end(), 0);
    m_count = 0;
    m_changed = TerrainVertexBounds{};
}

QuantizedTile quantizeHeights(const f32* normalised, size_t count, f32 maxHeightMetres) {
    QuantizedTile tile;
    if (count == 0) return tile;
    std::vector<i64> centimetres(count);
    i64 low = 0, high = 0;
    for (size_t i = 0; i < count; ++i) {
        centimetres[i] = static_cast<i64>(std::llround(static_cast<f64>(normalised[i]) * maxHeightMetres * 100.0));
        if (i == 0 || centimetres[i] < low) low = centimetres[i];
        if (i == 0 || centimetres[i] > high) high = centimetres[i];
    }
    if (high - low > 65535) {
        tile.wide = true;
        tile.wideValues.assign(normalised, normalised + count);
        return tile;
    }
    tile.baseCm = static_cast<i32>(low);
    tile.offsets.resize(count);
    for (size_t i = 0; i < count; ++i) tile.offsets[i] = static_cast<u16>(centimetres[i] - low);
    return tile;
}

void dequantizeHeights(const QuantizedTile& tile, f32 maxHeightMetres, f32* out) {
    if (tile.wide) {
        std::copy(tile.wideValues.begin(), tile.wideValues.end(), out);
        return;
    }
    const f64 scale = maxHeightMetres > 0.0f ? 1.0 / (100.0 * static_cast<f64>(maxHeightMetres)) : 0.0;
    for (size_t i = 0; i < tile.offsets.size(); ++i) {
        out[i] = static_cast<f32>((static_cast<f64>(tile.baseCm) + static_cast<f64>(tile.offsets[i])) * scale);
    }
}

}  // namespace Caffeine::Terrain
