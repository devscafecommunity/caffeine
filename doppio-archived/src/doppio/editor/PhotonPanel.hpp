#pragma once

#include "caffeine/photon/Photon.hpp"
#include "core/Types.hpp"
#include "ecs/Entity.hpp"

#include <string>

namespace Caffeine::ECS {
class World;
}

namespace Caffeine::Editor {

class EditorContext;

/// Native Photon GI authoring panel. Owns the presets and the static AO bake
/// request queue; the actual GPU/CPU bake runs between frames.
class PhotonPanel {
public:
    void render(ECS::World& world, EditorContext& ctx);
    void processPendingBake(ECS::World& world, EditorContext& ctx);

    void setRenderDevice(void* device) {
        m_renderDevice = device;
        m_gpuAvailable = device != nullptr;
    }
    void setProcessedDirectory(const std::string& dir) { m_processedDirectory = dir; }
    void open() { m_open = true; }
    void close() { m_open = false; }
    /// Queue a bake without going through the panel buttons (menus/tests).
    void queueBakeSelected() { m_bakeSelectedPending = true; m_status = "Bake queued for the selected mesh."; }
    void queueBakeScene() { m_bakeScenePending = true; m_status = "Scene AO bake queued."; }
    bool isOpen() const { return m_open; }

private:
    ECS::Entity ensureTarget(ECS::World& world, EditorContext& ctx, bool create);
    void applyPreset(ECS::PhotonGIComponent& gi, int preset) const;

    bool m_open = false;
    int m_preset = 3;  // Balanced
    int m_aoResolution = 1;
    float m_aoRays = 16.0f;
    float m_aoMaxDistance = 2.5f;
    float m_aoBias = 0.02f;
    bool m_useGpu = true;
    bool m_gpuAvailable = false;
    bool m_bakeSelectedPending = false;
    bool m_bakeScenePending = false;
    void* m_renderDevice = nullptr;
    std::string m_processedDirectory = "assets/processed";
    std::string m_status;
};

}  // namespace Caffeine::Editor
