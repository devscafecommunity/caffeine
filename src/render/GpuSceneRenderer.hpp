#pragma once

#include "../core/Types.hpp"
#include "../ecs/World.hpp"
#include "../ecs/Entity.hpp"
#include "../math/Mat4.hpp"
#include "../math/Vec3.hpp"
#include "../rhi/RenderDevice.hpp"
#include "../rhi/CommandBuffer.hpp"
#include "../scene/LightingSystem.hpp"
#include "../assets/MeshTypes.hpp"
#include "render/GpuPointShadowMap.hpp"
#include "render/GpuDirectionalShadowMap.hpp"
#include "render/GpuSpotShadowMap.hpp"
#include "render/TextureQuality.hpp"
#include "spatial/Octree.hpp"

#include <string>
#include <vector>

namespace Caffeine::Editor {
struct EditorContext;
}

namespace Caffeine::Render {

struct GpuSceneCamera {
    Vec3 position{};
    Vec3 focus{};
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    f32 fovRad = 1.0472f;
    f32 nearClip = 0.1f;
    f32 farClip = 10000.0f;
};

struct GpuSceneRenderOptions {
    bool wireframeMeshes = false;
    bool enableShadows   = true;
    TextureQualitySettings textureQuality{};
    std::vector<Vec3>      textureQualityViewers;
};

class GpuSceneRenderer {
public:
    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool isReady() const { return m_ready; }

    u32 render(RHI::CommandBuffer* cmd, ECS::World& world, const Editor::EditorContext& ctx,
               RHI::Texture* colorTarget, RHI::Texture* depthTarget, u32 width, u32 height,
               const std::string& projectRoot,
               const GpuSceneRenderOptions& options = {});

    u32 renderWithCamera(RHI::CommandBuffer* cmd, ECS::World& world, const GpuSceneCamera& camera,
                         RHI::Texture* colorTarget, RHI::Texture* depthTarget, u32 width,
                         u32 height, const std::string& projectRoot,
                         const GpuSceneRenderOptions& options = {});

private:
    struct MeshDraw {
        ECS::Entity entity;
        Assets::Mesh3D* mesh = nullptr;
        const ECS::TerrainComponent* terrainSettings = nullptr;
        Mat4 worldMatrix = Mat4::identity();
        Vec4 albedo{1, 1, 1, 1};
        f32 metallic = 0.0f;
        f32 roughness = 0.5f;
        f32 shininess = 32.0f;
        std::string meshPath;
        std::string customTexturePath;
        std::string customNormalPath;
        bool castShadows = true;
        bool receiveShadows = true;
        bool isTerrain = false;
        f32  viewerDistance = 0.0f;
    };

    bool createPipelines();
    bool ensureMeshUploaded(Assets::Mesh3D* mesh);
    void renderDirectionalShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                                  const Scene::SceneLighting& lighting,
                                  const std::vector<MeshDraw>& draws,
                                  const GpuSceneCamera& camera);
    void renderPointShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                            const Scene::SceneLighting& lighting,
                            const std::vector<MeshDraw>& draws);
    void renderSpotShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                           const Scene::SceneLighting& lighting,
                           const std::vector<MeshDraw>& draws);
    u32 renderMeshes(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws,
                     const Mat4& vp, const Vec3& cameraPos, const Mat4& cameraView,
                     const Scene::SceneLighting& lighting,
                     RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                     u32 width, u32 height, const std::string& projectRoot,
                     const GpuSceneRenderOptions& options);
    void pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                        const Vec3& lightPos, int mode, u32 indexCount, u32 firstIndex = 0);
    void pushShadowDrawsForMesh(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                                const Vec3& lightPos, int mode);
    std::vector<MeshDraw> gatherMeshDraws(ECS::World& world, const Vec3& cameraPos,
                                          const Spatial::Frustum& frustum,
                                          const std::string& projectRoot,
                                          const GpuSceneRenderOptions& options);

    RHI::RenderDevice* m_device = nullptr;
    RHI::Shader* m_sceneVert = nullptr;
    RHI::Shader* m_sceneFrag = nullptr;
    RHI::Shader* m_terrainFrag = nullptr;
    RHI::Shader* m_shadowVert = nullptr;
    RHI::Shader* m_shadowFrag = nullptr;
    RHI::Pipeline* m_scenePipeline = nullptr;
    RHI::Pipeline* m_wireframePipeline = nullptr;
    RHI::Pipeline* m_terrainPipeline = nullptr;
    RHI::Pipeline* m_shadowPipeline = nullptr;
    RHI::Sampler* m_sampler = nullptr;
    RHI::Sampler* m_repeatSampler = nullptr;
    GpuPointShadowMap m_pointShadows;
    GpuDirectionalShadowMap m_directionalShadows;
    GpuSpotShadowMap m_spotShadows;
    bool m_ready = false;
};

}  // namespace Caffeine::Render
