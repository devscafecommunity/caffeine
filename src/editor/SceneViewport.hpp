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
#include "render/SkyboxRenderer.hpp"
#include "render/GpuSceneRenderer.hpp"
#include "terrain/TerrainLodSystem.hpp"
#include "terrain/TerrainSplatmap.hpp"
#include "assets/MeshTypes.hpp"
#include "ecs/MeshComponents.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

struct MeshDrawTexture {
    const u8* pixels = nullptr;
    u32 width = 0;
    u32 height = 0;
    int channels = 0;
    bool flipV = false;
    Vec4 materialAlbedo{1.0f, 1.0f, 1.0f, 1.0f};
    f32  materialMetallic = 0.0f;
    f32  materialRoughness = 0.5f;
    bool hasMaterial = false;
};

struct TerrainSplatDrawContext {
    const Terrain::TerrainSplatmap* splatmap = nullptr;
    MeshDrawTexture layers[ECS::kTerrainSplatLayerCount]{};
    u32 layerCount = 0;
    const ECS::TerrainComponent* settings = nullptr;
    Mat4 worldMatrixInverse;
    bool active = false;
};

#ifdef CF_HAS_IMGUI
struct MeshCpuRasterizer {
    int width = 0;
    int height = 0;
    int panelW = 0;
    int panelH = 0;
    ImVec2 origin{};
    std::vector<u8> color;
    std::vector<f32> depth;

    void begin(ImVec2 origin_, ImVec2 size, int maxDim = 1920);
    void clear(u8 r, u8 g, u8 b, u8 a = 0);
};
#endif

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
    void setFrameCommandBuffer(RHI::CommandBuffer* cmd) { m_frameCmd = cmd; }
    RHI::CommandBuffer* frameCommandBuffer() const { return m_frameCmd; }
    RHI::Texture* colorTarget() const { return m_colorTarget; }
    bool gpuSceneReady() const { return m_gpuSceneReady; }
    bool cameraPreviewGpuReady() const { return m_gpuSceneReady && m_useGpuScene; }
    RHI::Texture* cameraPreviewColorTarget() const { return m_previewColorTarget; }
    bool renderCameraPreviewGpu(RHI::CommandBuffer* cmd, ECS::World& world, EditorContext& ctx,
                                const Mat4& view, const Mat4& proj, const Vec3& cameraPos,
                                const Vec3& focus, f32 fovRad, f32 nearClip, f32 farClip,
                                u32 width, u32 height, const std::string& projectRoot);
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

     static Mat4   computeVP3D(ImVec2 viewportSize, const EditorContext& ctx);
     static ImVec2 projectToScreenVP(Vec3 worldPos, ImVec2 origin, ImVec2 viewportSize,
                                     const Mat4& vp);

    #ifdef CF_HAS_IMGUI
    void drawSceneMeshesForCamera(ECS::World& world, EditorContext& ctx, ImDrawList* dl,
                                  const Mat4& vp, const Vec3& camPos,
                                  ImVec2 origin, ImVec2 panelSize,
                                  ECS::Entity skipEntity = ECS::Entity::INVALID,
                                  int maxRasterDim = 1920);
    bool drawSkyboxForView(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize,
                           ECS::World& world, const EditorContext& ctx,
                           const Render::SkyboxCamera& camera,
                           bool respectEditorToggle = true,
                           Render::SkyboxRenderer* renderer = nullptr);
    #endif

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
     bool drawSkybox(ImDrawList* drawList, ImVec2 origin, ImVec2 viewportSize,
                     ECS::World& world, EditorContext& ctx);
     void drawNavigationWidget(ECS::World& world, EditorContext& ctx, ImVec2 origin, ImVec2 viewportSize);
     void resizeCanvasIfNeeded(u32 newWidth, u32 newHeight);
     void resizePreviewCanvasIfNeeded(u32 newWidth, u32 newHeight);
     std::string resolveSpritePath(const std::string& spriteName, const EditorContext& ctx) const;
     void releaseSpriteTextures();

     // Ray-AABB intersection test (returns t_enter distance, or -1 if no hit)
     // Used for object selection and culling
     f32 rayIntersectsAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                           const Vec3& aabbMin, const Vec3& aabbMax);

     // Find closest entity under a ray (for click-to-select)
     // Returns INVALID if no hit
     ECS::Entity raycastSelectEntity(const Vec3& rayOrigin, const Vec3& rayDir,
                                     ECS::World& world,
                                     const std::string& projectRoot = "");

     struct SpriteTextureCacheEntry {
        std::unique_ptr<ImTextureData> texture;
        int width = 0;
        int height = 0;
        bool loadFailed = false;
    };

    struct FileTextureCacheEntry {
        std::vector<u8> pixels;
        u32 width = 0;
        u32 height = 0;
        int channels = 0;
        bool loaded = false;
    };

    MeshDrawTexture resolveDrawTexture(const Assets::Mesh3D* mesh,
                                       const ECS::MeshFilterComponent* filter,
                                       const std::string& resolvedMeshPath,
                                       const std::string& projectRoot);
    MeshDrawTexture resolveTextureFromPath(const std::string& path,
                                           const std::string& projectRoot);
    TerrainSplatDrawContext resolveTerrainSplatContext(ECS::World& world, ECS::Entity entity,
                                                       const Mat4& worldMatrix,
                                                       const std::string& projectRoot);

    void drawCustomMeshGeometry(ECS::World& world, EditorContext& ctx, ECS::Entity entity,
                                ECS::MeshFilterComponent* meshFilter, const Mat4& worldMatrix,
                                ImDrawList* dl, const Mat4& vpMat, const Vec3& camPos,
                                ImVec2 origin, ImVec2 panelSize,
                                const std::function<Vec3(const Vec3&, const Vec3&, bool)>& lightColorAt,
                                bool drawTextured, bool wireMode, bool receiveShadows,
                                MeshCpuRasterizer* rasterizer = nullptr,
                                Assets::Mesh3D* meshOverride = nullptr,
                                const TerrainSplatDrawContext* splatContext = nullptr);
    void drawTerrainChunkDebug(ImDrawList* dl, const Mat4& worldMatrix,
                               const std::vector<Terrain::TerrainDrawChunk>& chunks,
                               ImVec2 origin, ImVec2 viewportSize, const EditorContext& ctx);

    void blitMeshRasterizer(ImDrawList* dl, MeshCpuRasterizer& rasterizer,
                            SpriteTextureCacheEntry& texEntry);
    void retireImTexture(std::unique_ptr<ImTextureData>& texture);
    void pruneRetiredTextures();
    std::unordered_map<std::string, SpriteTextureCacheEntry> m_spriteTextureCache;
    std::unordered_map<std::string, FileTextureCacheEntry> m_fileTextureCache;
    SpriteTextureCacheEntry m_meshRasterTexture;
    SpriteTextureCacheEntry m_camMeshRasterTexture;
    Render::SkyboxRenderer m_skyboxRenderer;
    Render::GpuSceneRenderer m_gpuSceneRenderer;
    RHI::CommandBuffer* m_frameCmd = nullptr;
    bool m_gpuSceneReady = false;
    bool m_useGpuScene = true;
    std::vector<std::unique_ptr<ImTextureData>> m_retiredTextures;
    bool m_lastGpuSceneActive = false;
    u32 m_lastGpuMeshDrawCount = 0;
#endif

    bool m_open = true;
    bool m_initialized = false;
    TransformGizmo m_Gizmo;
    bool m_gizmoDragging = false;
    bool m_terrainSculptDragging = false;
    bool m_boxSelecting = false;
    Vec3 m_terrainBrushHitWorld{};
    bool m_terrainBrushHitValid = false;
    Terrain::TerrainCullStats m_terrainCullStats{};
    std::vector<Terrain::TerrainDrawChunk> m_terrainDrawChunks;
    int  m_hoveredAxis   = 0;
    int  m_gizmoDragAxis = 0;
    ImVec2 m_axisRawDirs[3] = {};
    ImVec2 m_gizmoScreenOrigin = {};
    ImVec2 m_boxSelectStart = { 0.0f, 0.0f };
    ProjectionMode m_projectionMode = ProjectionMode::Perspective;
    MeshPreviewMode m_meshPreviewMode = MeshPreviewMode::Textured;
    WireframeDensity m_wireframeDensity = WireframeDensity::Medium;
#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_device = nullptr;
    RHI::Texture* m_colorTarget = nullptr;
    RHI::Texture* m_depthTarget = nullptr;
    RHI::Texture* m_previewColorTarget = nullptr;
    RHI::Texture* m_previewDepthTarget = nullptr;
    Config m_config;
    u32 m_lastCanvasWidth = 0;
    u32 m_lastCanvasHeight = 0;
    u32 m_previewCanvasWidth = 0;
    u32 m_previewCanvasHeight = 0;
#endif
};

} // namespace Caffeine::Editor