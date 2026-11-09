#pragma once

#include "core/Types.hpp"
#include "math/Vec3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace Caffeine::Navigation {

/// Grid navigation volume. The ground plane is XZ (3D) and maps 2D Y onto Z.
struct NavVolume {
    i32 width = 16;
    i32 height = 16;
    f32 cellSize = 1.0f;
    Vec3 origin{};
    std::vector<u8> blocked;

    void resize(i32 w, i32 h) {
        width = std::max(1, w);
        height = std::max(1, h);
        blocked.assign(static_cast<size_t>(width * height), 0);
    }

    bool inBounds(i32 x, i32 y) const {
        return x >= 0 && y >= 0 && x < width && y < height;
    }

    bool isBlocked(i32 x, i32 y) const {
        if (!inBounds(x, y) || blocked.empty()) return !inBounds(x, y);
        return blocked[static_cast<size_t>(y * width + x)] != 0;
    }

    void setBlocked(i32 x, i32 y, bool value) {
        if (blocked.size() != static_cast<size_t>(width * height)) resize(width, height);
        if (!inBounds(x, y)) return;
        blocked[static_cast<size_t>(y * width + x)] = value ? 1 : 0;
    }

    void worldToCell(const Vec3& world, i32& x, i32& z) const {
        const f32 size = cellSize > 0.0f ? cellSize : 1.0f;
        x = static_cast<i32>(std::floor((world.x - origin.x) / size));
        z = static_cast<i32>(std::floor((world.z - origin.z) / size));
    }

    Vec3 cellCenter(i32 x, i32 z) const {
        const f32 size = cellSize > 0.0f ? cellSize : 1.0f;
        return Vec3(origin.x + (static_cast<f32>(x) + 0.5f) * size, origin.y,
                    origin.z + (static_cast<f32>(z) + 0.5f) * size);
    }

    /// 4-connected A*. `out` is cleared and filled with cell centers from start to goal.
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const {
        out.clear();
        if (width <= 0 || height <= 0) return false;
        i32 sx = 0, sz = 0, gx = 0, gz = 0;
        worldToCell(from, sx, sz);
        worldToCell(to, gx, gz);
        if (!inBounds(sx, sz) || !inBounds(gx, gz)) return false;
        if (isBlocked(sx, sz) || isBlocked(gx, gz)) return false;
        if (sx == gx && sz == gz) {
            out.push_back(cellCenter(gx, gz));
            return true;
        }

        const int cellCount = width * height;
        std::vector<f32> cost(static_cast<size_t>(cellCount), 1.0e30f);
        std::vector<i32> parent(static_cast<size_t>(cellCount), -1);
        std::vector<u8> closed(static_cast<size_t>(cellCount), 0);

        auto index = [&](i32 x, i32 y) { return y * width + x; };
        auto heuristic = [&](i32 x, i32 y) {
            return static_cast<f32>(std::abs(gx - x) + std::abs(gz - y));
        };

        struct OpenNode {
            f32 score;
            i32 index;
            bool operator<(const OpenNode& other) const { return score > other.score; }
        };
        std::priority_queue<OpenNode> open;
        const i32 start = index(sx, sz);
        cost[static_cast<size_t>(start)] = 0.0f;
        open.push({heuristic(sx, sz), start});

        const i32 neighbors[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        bool found = false;
        while (!open.empty()) {
            const i32 current = open.top().index;
            open.pop();
            if (closed[static_cast<size_t>(current)]) continue;
            closed[static_cast<size_t>(current)] = 1;
            const i32 cx = current % width;
            const i32 cy = current / width;
            if (cx == gx && cy == gz) {
                found = true;
                break;
            }
            for (const auto& step : neighbors) {
                const i32 nx = cx + step[0];
                const i32 ny = cy + step[1];
                if (!inBounds(nx, ny) || isBlocked(nx, ny)) continue;
                const i32 next = index(nx, ny);
                if (closed[static_cast<size_t>(next)]) continue;
                const f32 nextCost = cost[static_cast<size_t>(current)] + 1.0f;
                if (nextCost >= cost[static_cast<size_t>(next)]) continue;
                cost[static_cast<size_t>(next)] = nextCost;
                parent[static_cast<size_t>(next)] = current;
                open.push({nextCost + heuristic(nx, ny), next});
            }
        }
        if (!found) return false;

        std::vector<i32> chain;
        for (i32 cursor = index(gx, gz); cursor >= 0; cursor = parent[static_cast<size_t>(cursor)]) {
            chain.push_back(cursor);
            if (cursor == start) break;
        }
        std::reverse(chain.begin(), chain.end());
        out.reserve(chain.size());
        for (i32 cell : chain) {
            out.push_back(cellCenter(cell % width, cell / width));
        }
        return !out.empty();
    }
};

enum class NavMode : u8 { Idle = 0, Patrol = 1, Follow = 2, Scripted = 3 };

static constexpr int kMaxPatrolPoints = 16;

/// Agent that follows a path. The mode is generic: idle, patrol, follow, or a Lua script.
struct NavAgent {
    Vec3 destination{};
    f32 speed = 3.0f;
    f32 arriveRadius = 0.25f;
    NavMode mode = NavMode::Idle;
    u32 followEntity = u32_max;
    u32 patrolCount = 0;
    u32 patrolIndex = 0;
    Vec3 patrol[kMaxPatrolPoints]{};
    char behaviorScript[256] = {};
    bool hasDestination = false;
    bool planDirty = true;
    Vec3 plannedDestination{};
    std::vector<Vec3> path;
    u32 pathCursor = 0;
};

}  // namespace Caffeine::Navigation
