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
#include "render/GpuEnvironmentMap.hpp"
#include "render/PostProcessStack.hpp"
#include "render/TextureQuality.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "render/RenderFeatures.hpp"
#include "spatial/Octree.hpp"

#include <string>
#include <unordered_map>
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

/// Editor ground grid drawn by the GPU on y = 0 (depth-tested, anti-aliased, fades out).
struct GpuGridOverlay {
    bool enabled = false;
    f32  cellSize = 1.0f;
    f32  majorEvery = 10.0f;
    f32  fadeDistance = 200.0f;
    f32  opacity = 1.0f;
};

struct GpuSceneRenderOptions {
    bool wireframeMeshes = false;
    bool enableShadows   = true;
    /// Directional CSM count for the primary sun. Editor uses 1 to stay inside 16 ms.
    u32  directionalCascadeCount = 4;
    /// Values > 1 bias terrain toward coarser LOD (editor preview).
    f32  terrainLodDistanceScale = 1.0f;
    TextureQualitySettings textureQuality{};
    std::vector<Vec3>      textureQualityViewers;
    RenderFeatureSettings features{};
    /// When true, `features` is replaced each frame from ForwardRenderFeaturesComponent.
    bool resolveFeaturesFromScene = true;
    /// False while the editor camera is moving, so planar/probe passes stay off.
    bool cameraSettled = true;
    bool clipBelowEnabled = false;
    f32  clipBelowY = 0.0f;
    u32  colorLayer = 0;
    /// Sky background / reflection fallback. An enabled SkyboxComponent in the world wins
    /// when `resolveEnvironmentFromScene` is set. The sky does not light the scene unless IBL is on.
    std::string environmentPath;
    f32  environmentExposure = 1.0f;
    bool resolveEnvironmentFromScene = true;
    /// Skips this entity (a probe never captures the object it belongs to).
    ECS::Entity excludeEntity = ECS::Entity::INVALID;

    /// Post-processing: the PostProcessComponent on `postProcessCamera`, else the first enabled
    /// one in the world, else `postProcess` (defaults: ACES + TAA).
    bool resolvePostProcessFromScene = true;
    ECS::Entity postProcessCamera = ECS::Entity::INVALID;
    ECS::PostProcessComponent postProcess = PostProcessStack::defaults();
    /// When set, replaces anti-aliasing after the camera post stack is resolved.
    /// Used by the editor viewport so it does not follow the game camera.
    bool overrideAntiAliasing = false;
    ECS::PostProcessAntiAliasing antiAliasingOverride{};
    /// Internal resolution multiplier (0.5 = faster, 2 = supersampled).
    f32  renderScale = 1.0f;
    /// Keeps temporal history (TAA, eye adaptation, SSR) apart per viewport. 0 = derive from
    /// the colour target.
    u64  viewId = 0;
    f32  deltaTime = 1.0f / 60.0f;
    bool drawSky = true;
    GpuGridOverlay grid{};
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

    /// Renders the scene in linear HDR, then runs the post-processing stack into `colorTarget`
    /// (RGBA8). `depthTarget` is optional; depth lives in internal, sampleable targets.
    u32 renderWithCamera(RHI::CommandBuffer* cmd, ECS::World& world, const GpuSceneCamera& camera,
                         RHI::Texture* colorTarget, RHI::Texture* depthTarget, u32 width,
                         u32 height, const std::string& projectRoot,
                         const GpuSceneRenderOptions& options = {});

    /// True while the image is still converging (TAA, eye adaptation, SSR feedback, probes
    /// waiting for capture). Callers that redraw on demand should keep drawing.
    bool needsAnotherFrame() const { return m_needsAnotherFrame; }

    /// Effective post-processing for a camera: its component, the scene's, or the defaults.
    static ECS::PostProcessComponent resolvePostProcess(ECS::World& world, ECS::Entity camera,
                                                        const ECS::PostProcessComponent& fallback);

private:
    struct MeshDraw {
        ECS::Entity entity;
        Assets::Mesh3D* mesh = nullptr;
        const std::vector<Assets::Vertex3D>* skinnedVertices = nullptr;
        const ECS::TerrainComponent* terrainSettings = nullptr;
        Mat4 worldMatrix = Mat4::identity();
        Vec4 albedo{1, 1, 1, 1};
        Vec3 emission{0, 0, 0};
        f32 metallic = 0.0f;
        f32 roughness = 0.5f;
        f32 reflectance = 0.04f;
        f32 shininess = 32.0f;
        std::string meshPath;
        std::string customTexturePath;
        std::string customNormalPath;
        std::string ormMapPath;
        std::string emissionMapPath;
        bool hasAuthoredMaterial = false;
        Vec2 uvTiling{1.0f, 1.0f};
        Vec2 uvOffset{0.0f, 0.0f};
        f32  normalStrength = 1.0f;
        f32  aoStrength = 1.0f;
        u8   alphaMode = 0;
        f32  alphaCutoff = 0.5f;
        f32  transmission = 0.0f;
        f32  ior = 1.5f;
        f32  clearcoat = 0.0f;
        f32  clearcoatRoughness = 0.03f;
        Vec3 sheenColor{0.0f, 0.0f, 0.0f};
        f32  sheenRoughness = 0.5f;
        f32  iridescence = 0.0f;
        f32  iridescenceThickness = 400.0f;
        f32  iridescenceIor = 1.3f;
        f32  reflection = 0.0f;
        i32  ssrStepsOverride = -1;
        f32  ssrRoughnessGate = -1.0f;
        bool reflectPerformance = false;
        bool reflectQuality = false;
        f32  ssrResolution = 0.5f;
        f32  ssrMaxSteps = 32.0f;
        f32  ssrTemporalFrames = 4.0f;
        f32  ssrDistance = 15.0f;
        f32  ssrSamples = 64.0f;
        f32  ssrDenoise = 0.7f;
        f32  ssrProbeBlend = 8.0f;
        f32  ssrBounces = 1.0f;
        i32  probeIndex = -1;
        bool castShadows = true;
        bool receiveShadows = true;
        bool isTerrain = false;
        bool planarReceiver = false;
        bool skipColorDraw = false;
        u32  batchCount = 1;
        u32  batchByteOffset = 0;
        Vec3 aabbMin{};
        Vec3 aabbMax{};
        bool hasAabb = false;
        f32  viewerDistance = 0.0f;

        bool isTranslucent() const { return transmission > 0.0f || alphaMode == 2; }
        bool isGlossy() const {
            return !isTerrain && (reflection >= 0.2f || metallic >= 0.5f || roughness <= 0.35f ||
                                  transmission > 0.0f || clearcoat > 0.0f);
        }
    };

    /// Per-viewport HDR targets. Colour/depth ping-pong so this frame can read the last one
    /// (screen-space reflections).
    struct ViewTargets {
        u64 viewId = 0;
        u32 width = 0;
        u32 height = 0;
        u64 lastUsed = 0;
        RHI::Texture* color[2] = {nullptr, nullptr};
        RHI::Texture* depth[2] = {nullptr, nullptr};
        RHI::Texture* opaqueCopy = nullptr;
        u32 current = 0;
        bool hasPrevious = false;
        Mat4 prevViewProj = Mat4::identity();
        Mat4 prevView = Mat4::identity();
        Mat4 prevProj = Mat4::identity();
        Vec3 prevCameraPos{};
        u64 prevSceneHash = 0;
        u32 frameIndex = 0;
        u32 settleFrames = 0;
    };

    /// Cubemap captured around one glossy object.
    struct ReflectionProbe {
        const ECS::World* world = nullptr;
        ECS::Entity entity = ECS::Entity::INVALID;
        Vec3 origin{};
        RHI::Texture* cube = nullptr;
        RHI::Texture* depth = nullptr;
        u32 resolution = 0;
        u32 mipLevels = 1;
        u64 sceneHash = 0;
        bool valid = false;
        u64 lastUsed = 0;
    };

    /// State shared by every draw of one scene pass.
    struct ScenePassContext {
        ViewTargets* view = nullptr;       ///< null for probe / planar passes
        Mat4 viewProj = Mat4::identity();
        f32 ssrIntensity = 0.0f;
        f32 ssrMaxRoughness = 0.0f;
        u32 ssrSteps = 0;
        f32 ssrMaxDistance = 0.0f;
        bool opaqueCopyBound = false;
        /// Cube faces must not cycle: that would discard the faces already rendered.
        bool cycleTargets = true;
        u64 sceneHash = 0;
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
                     const GpuSceneCamera& camera, const Scene::SceneLighting& lighting,
                     RHI::Texture* colorTarget, RHI::Texture* depthTarget,
                     u32 width, u32 height, const std::string& projectRoot,
                     const GpuSceneRenderOptions& options, ScenePassContext& pass);
    void drawMeshList(RHI::CommandBuffer* cmd, const std::vector<const MeshDraw*>& draws,
                      const GpuSceneCamera& camera, const Scene::SceneLighting& lighting,
                      const std::string& projectRoot, const GpuSceneRenderOptions& options,
                      const ScenePassContext& pass, RHI::Pipeline* basePipeline);
    void drawSky(RHI::CommandBuffer* cmd, const GpuSceneCamera& camera, u32 targetHeight);
    void drawGrid(RHI::CommandBuffer* cmd, const GpuSceneCamera& camera, const GpuGridOverlay& grid);
    /// Scene into an HDR target: gather, shadows, opaque, sky, grid, translucent.
    u32 renderSceneHdr(RHI::CommandBuffer* cmd, ECS::World& world, const GpuSceneCamera& camera,
                       RHI::Texture* colorTarget, RHI::Texture* depthTarget, u32 width,
                       u32 height, const std::string& projectRoot,
                       const GpuSceneRenderOptions& options, ScenePassContext& pass);
    ViewTargets* acquireViewTargets(u64 viewId, u32 width, u32 height);
    void releaseViewTargets(ViewTargets& view);
    void updateReflectionProbes(RHI::CommandBuffer* cmd, ECS::World& world,
                                std::vector<MeshDraw>& draws, const GpuSceneCamera& camera,
                                const std::string& projectRoot,
                                const GpuSceneRenderOptions& options, u64 sceneHash,
                                bool allowCapture);
    bool captureProbe(RHI::CommandBuffer* cmd, ECS::World& world, ReflectionProbe& probe,
                      const GpuSceneCamera& camera, const std::string& projectRoot,
                      const GpuSceneRenderOptions& options);
    void releaseProbe(ReflectionProbe& probe);
    void pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                        const Vec3& lightPos, int mode, u32 indexCount, u32 firstIndex = 0);
    void pushShadowDrawsForMesh(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                                const Vec3& lightPos, int mode);
    std::vector<MeshDraw> gatherMeshDraws(ECS::World& world, const Vec3& cameraPos,
                                          const Spatial::Frustum& frustum,
                                          const std::string& projectRoot,
                                          const GpuSceneRenderOptions& options);
    void applyCoarseOcclusion(std::vector<MeshDraw>& draws, const GpuSceneCamera& camera,
                              const GpuSceneRenderOptions& options) const;
    void assignInstanceBatches(std::vector<MeshDraw>& draws, const GpuSceneRenderOptions& options,
                               std::vector<float>& instanceMatrices) const;
    bool ensureReflectionTargets(u32 width, u32 height);
    void renderPlanarReflection(RHI::CommandBuffer* cmd, ECS::World& world,
                                const GpuSceneCamera& camera, u32 width, u32 height,
                                const std::string& projectRoot, const GpuSceneRenderOptions& options);
    void resolveEnvironment(ECS::World& world, const std::string& projectRoot,
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
    RHI::Shader* m_instancedVert = nullptr;
    RHI::Pipeline* m_instancedPipeline = nullptr;
    RHI::Buffer* m_instanceBuffer = nullptr;
    std::unordered_map<u32, RHI::Buffer*> m_skinBuffers;
    RHI::Buffer* vertexBufferFor(const MeshDraw& draw);
    RHI::Pipeline* m_blendPipeline = nullptr;
    RHI::Shader* m_fullscreenVert = nullptr;
    RHI::Shader* m_skyFrag = nullptr;
    RHI::Shader* m_gridFrag = nullptr;
    RHI::Shader* m_effectVert = nullptr;
    RHI::Shader* m_effectFrag = nullptr;
    RHI::Pipeline* m_effectPipeline = nullptr;
    Assets::Mesh3D m_effectMesh;
    RHI::Pipeline* m_skyPipeline = nullptr;
    RHI::Pipeline* m_gridPipeline = nullptr;
    /// Bound to cube sampler slots that have nothing to sample (a 2D texture there is invalid).
    RHI::Texture* m_fallbackCube = nullptr;
    RHI::Texture* m_reflectionColor = nullptr;
    RHI::Texture* m_reflectionDepth = nullptr;
    u32 m_reflectionWidth = 0;
    u32 m_reflectionHeight = 0;
    Mat4 m_reflectionVP = Mat4::identity();
    bool m_reflectionValid = false;
    bool m_materialPlanar = false;
    std::vector<ReflectionProbe> m_probes;
    bool m_probesPending = false;
    std::vector<ViewTargets> m_views;
    u64 m_frameCounter = 0;
    bool m_needsAnotherFrame = false;
    PostProcessStack m_post;
    /// True while rendering into the planar target or probe faces; those passes must not
    /// sample the textures they write to.
    bool m_inReflectionPass = false;
    GpuEnvironmentMap m_environment;
    f32 m_environmentExposure = 1.0f;
    RHI::Sampler* m_sampler = nullptr;
    RHI::Sampler* m_repeatSampler = nullptr;
    RHI::Sampler* m_pointSampler = nullptr;
    GpuPointShadowMap m_pointShadows;
    GpuDirectionalShadowMap m_directionalShadows;
    struct MeshLodCache {
        Assets::Mesh3D medium;
        Assets::Mesh3D far;
        bool built = false;
    };

    Assets::Mesh3D* selectMeshLod(const Assets::Mesh3D* source, ECS::Entity entity, f32 distance);
    void destroyMeshGpu(Assets::Mesh3D& mesh);

    GpuSpotShadowMap m_spotShadows;
    u32 m_activeCascadeCount = 4;
    std::unordered_map<const Assets::Mesh3D*, MeshLodCache> m_meshLods;
    std::unordered_map<u32, u32> m_entityLod;
    bool m_ready = false;
};

}  // namespace Caffeine::Render
