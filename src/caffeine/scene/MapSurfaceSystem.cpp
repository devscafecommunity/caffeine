#include "scene/MapSurfaceSystem.hpp"

#include "ecs/ComponentQuery.hpp"
#include "ecs/MapComponents.hpp"
#include "events/Events.hpp"
#include "scene/HierarchySystem.hpp"

#include <cmath>
#include <unordered_set>

namespace Caffeine::Scene {

namespace {

struct ChunkKey {
    i32 x = 0;
    i32 y = 0;
    u32 entityId = 0;
    bool operator==(const ChunkKey& o) const {
        return x == o.x && y == o.y && entityId == o.entityId;
    }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return (static_cast<size_t>(k.x) * 73856093u) ^ (static_cast<size_t>(k.y) * 19349663u) ^
               k.entityId;
    }
};

std::unordered_set<ChunkKey, ChunkKeyHash>& requestedChunks() {
    static std::unordered_set<ChunkKey, ChunkKeyHash> s;
    return s;
}

}  // namespace

void tickMapSurfaceChunkRequests(ECS::World& world, Events::EventBus& eventBus,
                               const Render::Camera2D* camera, bool cameraValid) {
    if (!cameraValid || !camera) return;

    auto& s_requested = requestedChunks();

    const Vec2 camPos = camera->position();
    const Vec2 half = camera->size() * 0.5f;
    const Vec2 viewMin = camPos - half;
    const Vec2 viewMax = camPos + half;

    ECS::ComponentQuery q;
    q.with<ECS::MapSurface2DComponent>();
    world.forEach<ECS::MapSurface2DComponent>(q, [&](ECS::Entity entity, ECS::MapSurface2DComponent& map) {
        if (isEffectivelyDisabled(world, entity)) return;
        if (!map.enableChunkRequests || map.chunkSize <= 0.0f) return;

        const f32 cs = map.chunkSize;
        const i32 minCx = static_cast<i32>(std::floor(
            (std::max(viewMin.x, map.regionMin.x) - map.regionMin.x) / cs));
        const i32 maxCx = static_cast<i32>(std::floor(
            (std::min(viewMax.x, map.regionMax.x) - map.regionMin.x) / cs));
        const i32 minCy = static_cast<i32>(std::floor(
            (std::max(viewMin.y, map.regionMin.y) - map.regionMin.y) / cs));
        const i32 maxCy = static_cast<i32>(std::floor(
            (std::min(viewMax.y, map.regionMax.y) - map.regionMin.y) / cs));

        for (i32 cy = minCy; cy <= maxCy; ++cy) {
            for (i32 cx = minCx; cx <= maxCx; ++cx) {
                const ChunkKey key{cx, cy, entity.id()};
                if (s_requested.count(key)) continue;
                s_requested.insert(key);

                Events::OnMapSurfaceChunkRequest evt{};
                evt.surfaceEntityId = entity.id();
                evt.chunkX = cx;
                evt.chunkY = cy;
                evt.worldMinX = map.regionMin.x + static_cast<f32>(cx) * cs;
                evt.worldMinY = map.regionMin.y + static_cast<f32>(cy) * cs;
                evt.chunkSize = cs;
                eventBus.publish(evt);
            }
        }
    });
}

void resetMapSurfaceChunkRequestCache() {
    requestedChunks().clear();
}

}  // namespace Caffeine::Scene
