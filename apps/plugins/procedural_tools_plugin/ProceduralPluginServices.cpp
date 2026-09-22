#include "caffeine/plugin/PluginAPI.hpp"
#include "caffeine/plugin/PluginServices.hpp"
#include "editor/EditorContext.hpp"
#include "ProceduralBridge.hpp"

#include <filesystem>
#include <iostream>

namespace {

bool fillSetupResponse(void* response, std::size_t responseSize, Caffeine::ECS::Entity director,
                       Caffeine::ECS::Entity terrain) {
    if (response && responseSize >= sizeof(Caffeine::Editor::ProceduralSetupResponse)) {
        auto* out = static_cast<Caffeine::Editor::ProceduralSetupResponse*>(response);
        out->directorEntityId = director.id();
        out->terrainEntityId = terrain.id();
    }
    return true;
}

bool proceduralInstallScripts(void* editorContext, const void* request, std::size_t requestSize,
                              void* response, std::size_t responseSize) {
    (void)request;
    (void)requestSize;
    (void)response;
    (void)responseSize;
    if (!editorContext) return false;
    auto* ctx = static_cast<Caffeine::Editor::EditorContext*>(editorContext);

    std::filesystem::path projectRoot;
    if (!ctx->currentScenePath.empty()) {
        projectRoot = std::filesystem::path(ctx->currentScenePath).parent_path().parent_path();
    }
    if (projectRoot.empty()) return false;

    std::string error;
    if (!Caffeine::Editor::ProceduralBridge::installScriptTemplates(projectRoot, error)) {
        std::cerr << "[ProceduralPlugin] installScripts: " << error << '\n';
        return false;
    }
    return true;
}

bool proceduralCreateStreamingSetup(void* editorContext, const void* request,
                                    std::size_t requestSize, void* response,
                                    std::size_t responseSize) {
    if (!editorContext || !request ||
        requestSize < sizeof(Caffeine::Editor::ProceduralStreamingSetupRequest)) {
        return false;
    }
    auto* ctx = static_cast<Caffeine::Editor::EditorContext*>(editorContext);
    if (!ctx->activeWorld) return false;

    const auto* req = static_cast<const Caffeine::Editor::ProceduralStreamingSetupRequest*>(request);
    Caffeine::Editor::ProceduralStreamingSetup setup;
    setup.seed = req->seed;
    setup.chunkSize = req->chunkSizeMeters;
    setup.viewRadius = req->viewRadiusChunks;
    setup.scriptPath = req->scriptPath;
    setup.terrainProfile = req->terrainProfile;
    setup.structureProfile = req->structureProfile;

    Caffeine::ECS::World& world = *ctx->activeWorld;
    Caffeine::ECS::Entity director;
    Caffeine::ECS::Entity terrain;
    std::string error;
    if (!Caffeine::Editor::ProceduralBridge::createStreamingSetup(world, setup, director, terrain,
                                                                  error)) {
        std::cerr << "[ProceduralPlugin] createStreamingSetup: " << error << '\n';
        return false;
    }

    fillSetupResponse(response, responseSize, director, terrain);
    ctx->selectedEntity = director;
    ctx->isDirty = true;
    return true;
}

bool proceduralLoadBenchmark(void* editorContext, const void* request, std::size_t requestSize,
                             void* response, std::size_t responseSize) {
    if (!editorContext || !request ||
        requestSize < sizeof(Caffeine::Editor::ProceduralLoadBenchmarkRequest)) {
        return false;
    }
    auto* ctx = static_cast<Caffeine::Editor::EditorContext*>(editorContext);
    if (!ctx->activeWorld) return false;

    const auto* req = static_cast<const Caffeine::Editor::ProceduralLoadBenchmarkRequest*>(request);
    Caffeine::ECS::World& world = *ctx->activeWorld;
    Caffeine::ECS::Entity director;
    Caffeine::ECS::Entity terrain;
    std::string error;
    if (!Caffeine::Editor::ProceduralBridge::loadBenchmark(world, req->benchmarkName, req->seed,
                                                           req->chunkSizeMeters,
                                                           req->viewRadiusChunks, director, terrain,
                                                           error)) {
        std::cerr << "[ProceduralPlugin] loadBenchmark: " << error << '\n';
        return false;
    }

    fillSetupResponse(response, responseSize, director, terrain);
    ctx->selectedEntity = director;
    ctx->isDirty = true;
    return true;
}

}  // namespace

extern "C" void caffeine_procedural_plugin_register_services(
    const Caffeine::Editor::PluginHostApi* host) {
    if (!host || !host->registerService) return;
    const char* name = "Procedural Tools";
    host->registerService(host->editorContext, name,
                          Caffeine::Editor::kServiceProceduralInstallScripts,
                          &proceduralInstallScripts);
    host->registerService(host->editorContext, name,
                          Caffeine::Editor::kServiceProceduralCreateStreamingSetup,
                          &proceduralCreateStreamingSetup);
    host->registerService(host->editorContext, name,
                          Caffeine::Editor::kServiceProceduralLoadBenchmark,
                          &proceduralLoadBenchmark);
    host->registerService(host->editorContext, name,
                          Caffeine::Editor::kServiceProceduralCreateDirector,
                          &proceduralCreateStreamingSetup);
}
