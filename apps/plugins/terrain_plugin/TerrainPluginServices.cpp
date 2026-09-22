#include "caffeine/plugin/PluginAPI.hpp"
#include "caffeine/plugin/PluginServices.hpp"
#include "editor/EditorContext.hpp"
#include "ecs/TerrainComponents.hpp"
#include "TerrainBridge.hpp"

#include <cstring>
#include <iostream>

namespace {

bool terrainGenerateUltra(void* editorContext, const void* request, std::size_t requestSize,
                          void* response, std::size_t responseSize) {
    (void)response;
    (void)responseSize;
    if (!editorContext || !request || requestSize < sizeof(Caffeine::Editor::TerrainGenerateUltraRequest)) {
        return false;
    }
    auto* ctx = static_cast<Caffeine::Editor::EditorContext*>(editorContext);
    if (!ctx->activeWorld) return false;

    const auto* req = static_cast<const Caffeine::Editor::TerrainGenerateUltraRequest*>(request);
    Caffeine::ECS::World& world = *ctx->activeWorld;
    Caffeine::ECS::Entity entity(req->entityId, &world);
    if (!entity.isValid() || !world.has<Caffeine::ECS::TerrainComponent>(entity)) return false;

    std::string error;
    const bool ok = Caffeine::Editor::TerrainBridge::generateUltraRealistic(
        world, entity, req->preset, req->seed, req->worldSizeMeters, req->gridSize, error);
    if (!ok) {
        std::cerr << "[TerrainPlugin] terrain.generateUltra: " << error << '\n';
        return false;
    }
    ctx->isDirty = true;
    return true;
}

bool terrainImportHeightmap(void* editorContext, const void* request, std::size_t requestSize,
                            void* response, std::size_t responseSize) {
    (void)response;
    (void)responseSize;
    if (!editorContext || !request ||
        requestSize < sizeof(Caffeine::Editor::TerrainImportHeightmapRequest)) {
        return false;
    }
    auto* ctx = static_cast<Caffeine::Editor::EditorContext*>(editorContext);
    if (!ctx->activeWorld) return false;

    const auto* req = static_cast<const Caffeine::Editor::TerrainImportHeightmapRequest*>(request);
    Caffeine::ECS::World& world = *ctx->activeWorld;
    Caffeine::ECS::Entity entity(req->entityId, &world);
    if (!entity.isValid()) return false;

    std::string error;
    const bool ok = Caffeine::Editor::TerrainBridge::importHeightmap(
        world, entity, req->heightmapPath, req->worldSizeX, req->worldSizeZ, error);
    if (!ok) {
        std::cerr << "[TerrainPlugin] terrain.importHeightmap: " << error << '\n';
        return false;
    }
    ctx->isDirty = true;
    return true;
}

}  // namespace

extern "C" void caffeine_terrain_plugin_register_services(
    const Caffeine::Editor::PluginHostApi* host) {
    if (!host || !host->registerService) return;
    host->registerService(host->editorContext, "Terrain Generator",
                          Caffeine::Editor::kServiceTerrainGenerateUltra, &terrainGenerateUltra);
    host->registerService(host->editorContext, "Terrain Generator",
                          Caffeine::Editor::kServiceTerrainImportHeightmap, &terrainImportHeightmap);
}
