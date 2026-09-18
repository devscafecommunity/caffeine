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

#include <string>

namespace Caffeine::Editor {
struct EditorContext;
}

namespace Caffeine::Render {

class GpuSceneRenderer {
public:
    bool init(RHI::RenderDevice* device);
    void shutdown();

    bool isReady() const { return m_ready; }

    u32 render(RHI::CommandBuffer* cmd, ECS::World& world, const Editor::EditorContext& ctx,
               RHI::Texture* colorTarget, RHI::Texture* depthTarget, u32 width, u32 height,
               const std::string& projectRoot);

private:
    struct MeshDraw {
        ECS::Entity entity;
        Assets::Mesh3D* mesh = nullptr;
        const ECS::TerrainComponent* terrainSettings = nullptr;
        Mat4 worldMatrix = Mat4::identity();
        Vec4 albedo{1, 1, 1, 1};
        f32 metallic = 0.0f;
        f32 roughness = 0.5f;
        bool castShadows = true;
        bool receiveShadows = true;
        bool isTerrain = false;
    };

    bool createPipelines();
    bool ensureMeshUploaded(Assets::Mesh3D* mesh);
    void renderDirectionalShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                                  const Scene::SceneLighting& lighting,
                                  const std::vector<MeshDraw>& draws,
                                  const Vec3& focus);
    void renderPointShadows(RHI::CommandBuffer* cmd, ECS::World& world,
                            const Scene::SceneLighting& lighting,
                            const std::vector<MeshDraw>& draws);
    u32 renderMeshes(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws,
                     const Mat4& vp, const Vec3& cameraPos,
                     const Scene::SceneLighting& lighting,
                     RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                     u32 width, u32 height);
    void pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                        const Vec3& lightPos, int mode);

    RHI::RenderDevice* m_device = nullptr;
    RHI::Shader* m_sceneVert = nullptr;
    RHI::Shader* m_sceneFrag = nullptr;
    RHI::Shader* m_terrainFrag = nullptr;
    RHI::Shader* m_shadowVert = nullptr;
    RHI::Shader* m_shadowFrag = nullptr;
    RHI::Pipeline* m_scenePipeline = nullptr;
    RHI::Pipeline* m_terrainPipeline = nullptr;
    RHI::Pipeline* m_shadowPipeline = nullptr;
    RHI::Sampler* m_sampler = nullptr;
    RHI::Sampler* m_repeatSampler = nullptr;
    GpuPointShadowMap m_pointShadows;
    GpuDirectionalShadowMap m_directionalShadows;
    bool m_ready = false;
};

}  // namespace Caffeine::Render
