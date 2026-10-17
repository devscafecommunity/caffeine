#pragma once

#include "core/Types.hpp"
#include "math/Vec2.hpp"

#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Caffeine::Spatial {

/// Optional uniform grid for 2D radius / AABB queries.
template <typename T>
class SpatialGrid2D {
public:
    explicit SpatialGrid2D(f32 cellSize = 16.0f) : m_cellSize(std::max(0.25f, cellSize)) {}

    void clear() { m_cells.clear(); }

    void insert(T id, Vec2 position) {
        const auto key = cellKey(position);
        m_cells[key].push_back(id);
    }

    void queryRadius(Vec2 center, f32 radius, const std::function<void(T)>& fn) const {
        if (!fn) return;
        const f32 r = std::max(0.0f, radius);
        const int minCx = static_cast<int>(std::floor((center.x - r) / m_cellSize));
        const int maxCx = static_cast<int>(std::floor((center.x + r) / m_cellSize));
        const int minCy = static_cast<int>(std::floor((center.y - r) / m_cellSize));
        const int maxCy = static_cast<int>(std::floor((center.y + r) / m_cellSize));
        for (int cy = minCy; cy <= maxCy; ++cy) {
            for (int cx = minCx; cx <= maxCx; ++cx) {
                const auto it = m_cells.find(pack(cx, cy));
                if (it == m_cells.end()) continue;
                for (const T& id : it->second) fn(id);
            }
        }
    }

    f32 cellSize() const { return m_cellSize; }

private:
    static u64 pack(int cx, int cy) {
        return (static_cast<u64>(static_cast<u32>(cx)) << 32) |
               static_cast<u32>(cy);
    }

    std::pair<int, int> cellKey(Vec2 p) const {
        return {static_cast<int>(std::floor(p.x / m_cellSize)),
                static_cast<int>(std::floor(p.y / m_cellSize))};
    }

    f32 m_cellSize;
    std::unordered_map<u64, std::vector<T>> m_cells;
};

}  // namespace Caffeine::Spatial
