#pragma once

#include "assets/MaterialTypes.hpp"
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "render/GpuSceneRenderer.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/RenderDevice.hpp"

#include <string>

namespace Caffeine::Render {

struct MaterialPreviewSettings {
    /// Camera orbit around the sphere, in degrees.
    f32 yawDegrees = 0.0f;
    f32 pitchDegrees = 12.0f;
    std::string environmentPath;
    f32 environmentExposure = 1.0f;
    bool showFloor = true;
    u32 probeResolution = 128;
};

/// Material preview drawn by the scene renderer: a sphere wearing the material, a floor, a sun
/// and the sky, with a reflection probe so metals mirror their surroundings exactly as in a scene.
class MaterialPreviewRenderer {
public:
    static constexpr const char* kPreviewMaterialKey = "__caffeine_material_preview__.mat";
    static constexpr const char* kFloorMaterialKey = "__caffeine_material_preview_floor__.mat";

    MaterialPreviewRenderer() = default;
    ~MaterialPreviewRenderer();
    MaterialPreviewRenderer(const MaterialPreviewRenderer&) = delete;
    MaterialPreviewRenderer& operator=(const MaterialPreviewRenderer&) = delete;

    bool init(RHI::RenderDevice* device);
    void shutdown();
    bool isReady() const { return m_ready; }

    /// Distinct cache keys so two previews can render in the same frame.
    void setMaterialKeys(const char* previewKey, const char* floorKey);
    bool wantsMoreFrames() const { return m_renderer.needsAnotherFrame(); }

    /// Renders when the material, settings or size changed; otherwise keeps the last image.
    bool render(RHI::CommandBuffer* cmd, const Assets::MaterialSurface& surface, u32 pixelSize,
                const MaterialPreviewSettings& settings, const std::string& projectRoot = "");

    /// Renders a mesh framed on the preview floor, using the mesh's own materials.
    bool renderMesh(RHI::CommandBuffer* cmd, const std::string& meshPath, u32 pixelSize,
                    const MaterialPreviewSettings& settings, const std::string& projectRoot = "");

    RHI::Texture* colorTexture() const { return m_hasImage ? m_color : nullptr; }
    u32 pixelSize() const { return m_pixelSize; }

private:
    void buildScene();
    bool ensureTargets(u32 pixelSize);
    void applySettings(const MaterialPreviewSettings& settings);
    void placeSphere();
    void placeMesh(const std::string& meshPath, const std::string& projectRoot);
    bool renderPlaced(RHI::CommandBuffer* cmd, const Assets::MaterialSurface* surface, u32 pixelSize,
                      const MaterialPreviewSettings& settings, const std::string& projectRoot,
                      bool bindPreviewMaterial);

    RHI::RenderDevice* m_device = nullptr;
    GpuSceneRenderer m_renderer;
    ECS::World m_world;
    ECS::Entity m_sphere = ECS::Entity::INVALID;
    ECS::Entity m_floor = ECS::Entity::INVALID;
    RHI::Texture* m_color = nullptr;
    RHI::Texture* m_depth = nullptr;
    u32 m_pixelSize = 0;
    u64 m_lastHash = 0;
    bool m_hasImage = false;
    bool m_ready = false;
    std::string m_previewKey = kPreviewMaterialKey;
    std::string m_floorKey = kFloorMaterialKey;
    std::string m_subjectPath;
    f32 m_focusY = 0.0f;
};

}  // namespace Caffeine::Render
