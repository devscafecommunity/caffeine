#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "scene/SceneComponents.hpp"
#include "math/Math.hpp"
#include "editor/EditorContext.hpp"
#include "editor/TransformGizmo.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#include "rhi/CommandBuffer.hpp"
#endif

#ifdef CF_HAS_IMGUI
#include <imgui.h>
#endif

#include "physics/PhysicsComponents2D.hpp"
#include "ecs/CameraComponents.hpp"

namespace Caffeine::Editor {

class SceneViewport {
public:
    enum class ProjectionMode : u8 {
        Orthographic,
        Perspective
    };

    enum class MeshPreviewMode : u8 {
        Wireframe,
        Textured
    };

    enum class WireframeDensity : u8 {
        Low,
        Medium,
        High
    };

#ifdef CF_HAS_SDL3
    struct Config {
        u32  width       = 1280;
        u32  height      = 720;
        bool grid        = true;
        f32  gridSpacing = 64.0f;
        f32  gridWidth   = 2000.0f;

        static Config defaults() { return {}; }
    };
#endif

    SceneViewport() = default;

#ifdef CF_HAS_SDL3
    bool init(RHI::RenderDevice* device, Config cfg = Config::defaults());
    void shutdown();
    RHI::Texture* colorTarget() const { return m_colorTarget; }
#endif

    void render(ECS::World& world, EditorContext& ctx);

    ProjectionMode projectionMode() const { return m_projectionMode; }
    void setProjectionMode(ProjectionMode mode) { m_projectionMode = mode; }
    void toggleProjectionMode() {
        m_projectionMode = (m_projectionMode == ProjectionMode::Perspective)
            ? ProjectionMode::Orthographic
            : ProjectionMode::Perspective;
    }

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }
    void open()  { m_open = true; }

    static ImVec2 projectToScreen(Vec3 worldPos, ImVec2 origin, ImVec2 viewportSize,
                                   const EditorContext& ctx);

private:
#ifdef CF_HAS_IMGUI
    void drawGizmo(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    void drawSprites(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    void drawEmptyEntities(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    void drawPhysicsDebug(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    void drawCameraFrustums(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    void drawLightGizmos(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);

#ifdef CF_HAS_IMGUI
    void createOrUpdateLightGizmoEntities(ECS::World& world);
#endif
    void handleGizmoInput(ECS::World& world, EditorContext& ctx, ImVec2 viewportSize);
    void drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx);
    void drawGrid3D(ImDrawList* dl, ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx);
    void drawNavigationWidget(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
    std::string resolveSpritePath(const std::string& spriteName, const EditorContext& ctx) const;
    void releaseSpriteTextures();

    struct SpriteTextureCacheEntry {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
        bool loadFailed = false;
    };

    std::unordered_map<std::string, SpriteTextureCacheEntry> m_spriteTextureCache;
#endif

    bool m_open = true;
    bool m_initialized = false;
    TransformGizmo m_Gizmo;
    bool m_gizmoDragging = false;
    bool m_boxSelecting = false;
    int  m_hoveredAxis   = 0;
    int  m_gizmoDragAxis = 0;
    ImVec2 m_axisRawDirs[3] = {};
    ImVec2 m_gizmoScreenOrigin = {};
    ImVec2 m_boxSelectStart = { 0.0f, 0.0f };
    ProjectionMode m_projectionMode = ProjectionMode::Perspective;
    MeshPreviewMode m_meshPreviewMode = MeshPreviewMode::Wireframe;
    WireframeDensity m_wireframeDensity = WireframeDensity::Medium;
#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    Config m_config;
#endif
};

} // namespace Caffeine::Editor