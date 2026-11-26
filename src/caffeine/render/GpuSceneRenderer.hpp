#pragma once
#include <map>
#include <utility>

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
#include "render/MicromeshGpu.hpp"
#include "render/PostProcessStack.hpp"
#include "render/PhotonCascades.hpp"
#include "render/TextureQuality.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "render/RenderFeatures.hpp"
#include "scene/WeatherSystem.hpp"
#include "spatial/Octree.hpp"

#include <string>
#include <memory>
#include <unordered_map>
#include <array>
#include <vector>

namespace Caffeine::Editor {
struct EditorContext;
}

namespace Caffeine::Render {

/// Minimal deferred data written by every scene pipeline alongside the HDR
/// colour target (spec §5.3 / §6.1). Attachment order is fixed:
///   0 = HDR colour (kHdrFormat)
///   1 = albedo          RGBA8, a = 1 when the surface is GI-relevant
///   2 = normal+material RGBA8 (rg = octahedral world normal, b = roughness, a = metallic)
///   3 = direct          RGBA16F, D_t = direct diffuse + emissive (no IBL/SSR)
/// Packing normal and material frees the fourth attachment for D_t; four is
/// the maximum a render pass may use (and RGBA8/RGBA16F are guaranteed colour
/// target formats). Keeping the same four attachments on every pass lets
/// probes and planar reflections reuse the scene pipelines.
struct GBufferTargets {
    RHI::Texture* albedo         = nullptr;
    RHI::Texture* normalMaterial = nullptr;
    RHI::Texture* direct         = nullptr;

    bool valid() const { return albedo && normalMaterial && direct; }
};

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

/// Volumetric cloud/mist parameters resolved from the scene for the sky pass.
struct GpuCloudSettings {
    bool enabled = false;
    u32  steps = 0;
    f32  coverage = 0.0f;
    f32  density = 0.0f;
    f32  baseAltitude = 0.0f;
    f32  topAltitude = 0.0f;
    f32  detail = 0.0f;
    f32  seed = 0.0f;
    f32  windSpeed = 0.0f;
    f32  windDirection = 0.0f;
    f32  mistDensity = 0.0f;
    f32  mistHeight = 0.0f;
    f32  mistFalloff = 0.0f;
    f32  time = 0.0f;
    Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    Vec3 sunColor{1.0f, 1.0f, 1.0f};
    f32  sunIntensity = 1.0f;
    f32  sunScatter = 1.0f;
    Vec3 tint{1.0f, 1.0f, 1.0f};  ///< multiplies the sun-lit colour
    f32  darkness = 0.0f;         ///< 0..1 storm
    f32  skyLight = 1.0f;         ///< sky light inside the clouds
    f32  silverLining = 0.0f;     ///< bright rim toward the sun
    bool groundShadows = false;   ///< the clouds shade the ground
    f32  shadowStrength = 0.7f;
    f32  sunShafts = 0.0f;        ///< light shafts when the sun is in view
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
    /// Editor orthographic view (Isometric or explicit projection toggle).
    bool orthographic = false;
    /// Keeps temporal history (TAA, eye adaptation, SSR) apart per viewport. 0 = derive from
    /// the colour target.
    u64  viewId = 0;
    f32  deltaTime = 1.0f / 60.0f;
    bool drawSky = true;
    /// Pose-only editor redraw: skip TAA/probe settle so Play does not leave the viewport looping.
    bool skipTemporalSettle = false;
    bool overrideAmbient = false;
    Vec3 ambientColor{0.03f, 0.03f, 0.035f};
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

    /// What the last scene frame cost and drew: the numbers a GPU-driven renderer is tuned with.
    /// Times are CPU-side (encoding); SDL3 GPU has no portable GPU timestamps.
    struct RenderStats {
        u32 meshesGathered = 0;   ///< draws that survived frustum culling and upload
        u32 frustumCulled = 0;
        u32 drawCalls = 0;        ///< indexed draws issued (an instanced batch is one)
        u32 instancedDrawCalls = 0;
        u64 triangles = 0;        ///< submitted, counting every instance
        u32 gpuWaterDraws = 0;    ///< water surfaces whose waves run in the vertex shader
        u32 micromeshDraws = 0;   ///< meshes drawn as a GPU-selected cluster cut
        u32 shadowDraws = 0;        ///< caster submissions into the sun's cascades (a mesh counts once per cascade)
        u32 shadowDrawsCulled = 0;  ///< casters skipped because they lie outside a cascade
        f32 gatherMs = 0.0f;
        f32 shadowsMs = 0.0f;
        f32 opaqueMs = 0.0f;
        f32 translucentMs = 0.0f;
        f32 totalMs = 0.0f;
    };
    const RenderStats& lastFrameStats() const { return m_stats; }

    /// The lightning of the latest frame (the flash that lit the scene, and the bolt in the sky).
    const Scene::LightningState& lightning() const { return m_lightning; }
    f32 weatherWindGust() const { return m_gust; }
    /// The weather's clock (seconds): what the strikes and the thunder are timed on.
    f32 weatherClock() const { return m_cloudTime; }
    /// The weather resolved for the latest frame.
    const Scene::WeatherResolved& weather() const { return m_weather; }
    /// Whether the scene has lightning running.
    bool lightningActive() const { return m_weather.lightning; }
    /// The thunder that has reached the listener since the last call (at most `capacity`), on the weather's clock.
    u32 pollThunder(Scene::ThunderEvent* out, u32 capacity);

    /// What the last frame's Micromesh draws kept, summed over every entity. Reads the GPU
    /// arguments back (blocking): for tools and tests, never call it per frame.
    struct MicromeshFrameStats {
        u32 draws = 0;
        u32 clusters = 0;
        u64 triangles = 0;        ///< triangles of the selected clusters
        u64 sourceTriangles = 0;  ///< triangles those meshes have in full (no culling, no cut)
        u32 views = 0;            ///< views cut this frame: the camera and each shadow cascade
        u32 arenaUsed = 0;        ///< indices the cuts of all views took from the arena
        u32 arenaCapacity = 0;
        u32 overflowed = 0;       ///< (view, instance) cuts that did not fit and were not drawn
        bool occlusionActive = false;  ///< the camera cut tested against last frame's depth
    };
    MicromeshFrameStats readMicromeshStats();
    /// Photon's voxel volume: what it holds and what the latest update cost.
    /// (The first view that uses Photon: the editor's own.)
    const PhotonStats& photonStats() const {
        static const PhotonStats none;
        return m_photonViews.empty() ? none : m_photonViews.front().second->cascades.stats();
    }
    /// Throws the triangle volume away; the next frame voxelises the scene again.
    void requestPhotonRebuild() {
        for (auto& entry : m_photonViews) {
            entry.second->flags = ~0u;
            entry.second->geometry = PhotonGeometry{};
            entry.second->cascades.requestFullRebuild();
        }
    }
    /// The index arena's allocations this frame (blocking read-back; tools and tests).
    u32 micromeshArenaUsed() { return m_micromeshArena.readUsed(); }
    u32 micromeshArenaCapacity() const { return m_micromeshArena.capacity(); }

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
        /// Draw only this sub-mesh (material slots: one draw per sub-mesh, each with its own material); -1 = all.
        i32 onlySubMesh = -1;
        std::string meshPath;
        std::string customTexturePath;
        std::string customNormalPath;
        std::string ormMapPath;
        std::string emissionMapPath;
        bool hasAuthoredMaterial = false;
        Vec2 uvTiling{1.0f, 1.0f};
        Vec2 uvOffset{0.0f, 0.0f};
        bool uvObjectScale = true; ///< UVs follow the object's scale (see MaterialSurface)
        f32  normalStrength = 1.0f;
        bool normalDirectX = false;
        f32  aoStrength = 1.0f;
        f32  heightScale = 0.0f;     ///< parallax depth (UV units); 0 = off
        f32  parallaxSteps = 32.0f;
        /// Geometric displacement: `mesh` is a subdivided copy and the vertex shaders push it
        /// along its normals by the height in the normal map's alpha.
        bool displaced = false;
        RHI::Texture* displacementTexture = nullptr; ///< normal map (height in alpha)
        f32  displacementScale = 0.0f; ///< metres
        f32  displacementEps = 0.01f;  ///< finite-difference step, tiled UV units
        f32  displacementMetresPerUv = 1.0f; ///< world metres per tiled UV unit
        u8   alphaMode = 0;
        f32  alphaCutoff = 0.5f;
        f32  transmission = 0.0f;
        f32  ior = 1.5f;
        f32  clearcoat = 0.0f;
        f32  clearcoatRoughness = 0.03f;
        f32  windStrength = 0.0f;  ///< sway per metre of height (0 = static)
        f32  windSpeed = 1.0f;
        f32  windFlutter = 0.0f;
        f32  translucency = 0.0f;  ///< light through thin leaves
        /// Run-time modifiers (material's combined with the Mesh Renderer's).
        Vec3 modTint{1.0f, 1.0f, 1.0f};
        bool isWater = false;        ///< a water surface: ripple detail, depth colour, foam
        bool gpuWater = false;       ///< waves are displaced in the vertex shader
        u32  waveCount = 0;
        f32  waterTime = 0.0f;
        f32  waterFoamThreshold = 0.72f;
        f32  waveData[16 * 8] = {};  ///< (dirX, dirZ, k, omega, amplitude, steepness, phase, 0) per wave
        f32  modWetness = 0.0f;
        f32  modSnow = 0.0f;
        f32  modAge = 0.0f;
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
        /// Instances in a buffer of their own (terrain vegetation); null: the renderer's shared instance buffer.
        RHI::Buffer* batchBuffer = nullptr;
        Vec3 aabbMin{};
        Vec3 aabbMax{};
        bool hasAabb = false;
        f32  viewerDistance = 0.0f;
        f32  micromeshPixels = 0.0f;  ///< > 0: draw as a Micromesh cut with this error in pixels
        bool micromeshOcclusion = false;  ///< also cull against last frame's depth

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
        GBufferTargets gbuffer;
        /// P5 multi-bounce feedback: S(t) and S(t-1), RGBA16F (alpha = validity).
        RHI::Texture* sBuffer[2] = {nullptr, nullptr};
        u32 sIndex = 0;
        bool sValid = false;
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
        GBufferTargets gbuffer;
        u32 resolution = 0;
        u32 mipLevels = 1;
        u64 sceneHash = 0;
        bool valid = false;
        u64 lastUsed = 0;
    };

    /// State shared by every draw of one scene pass.
    struct ScenePassContext {
        ViewTargets* view = nullptr;       ///< null for probe / planar passes
        /// G-buffer written by this pass. Pipelines always declare four colour
        /// attachments, so every scene pass must point at a valid set.
        GBufferTargets gbuffer;
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
    /// Scales the directional lights by what the weather does to the sun (overcast sky, rain) and adds the
    /// lightning's flash as a light from the bolt's direction.
    void applyWeatherToLights(Scene::LightingData& lights) const;
    /// Works out the lightning and the gust for this frame from the weather and the clock.
    void updateWeatherDynamics();
    /// The fog and rain the composite pass draws this frame.
    PostWeather buildPostWeather(const GpuSceneCamera& camera, u32 width, u32 height) const;
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
    /// Foliage wind for a vertex uniform block: strength, speed, flutter, time in seconds.
    void setWind(float* out, const MeshDraw& draw) const;
    void pushShadowDraw(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                        const Vec3& lightPos, int mode, u32 indexCount, u32 firstIndex = 0);
    void pushShadowDrawsForMesh(RHI::CommandBuffer* cmd, const MeshDraw& draw, const Mat4& mvp,
                                const Vec3& lightPos, int mode);
    /// Photon's triangle voxeliser reads the whole scene's solid geometry (not only what the camera sees). This
    /// walks the scene, hashes each object (mesh, transform, pose, colour), and when something changed rebuilds
    /// the triangle list, reusing the triangles of the objects that did not, and hands Photon the world boxes
    /// of the objects that changed so only their neighbourhood is voxelised again.
    bool updatePhotonGeometry(ECS::World& world, const std::string& projectRoot,
                              const ECS::PhotonGIComponent& settings);
    std::vector<MeshDraw> gatherMeshDraws(ECS::World& world, const Vec3& cameraPos,
                                          const Spatial::Frustum& frustum,
                                          const std::string& projectRoot,
                                          const GpuSceneRenderOptions& options);
    /// The draws of every `World Instances` entity: the visible chunks of its `.cinst` bake, one draw per
    /// instance and model part (world/WorldInstanceDraw.hpp), batched like any mesh draw.
    void gatherWorldInstanceDraws(ECS::World& world, const Vec3& cameraPos, const Spatial::Frustum& frustum,
                                  const std::string& projectRoot, const GpuSceneRenderOptions& options,
                                  std::vector<MeshDraw>& draws);
    /// A `.mat`'s surface on a draw (albedo, maps, alpha, modifiers, displacement); nothing for an empty path.
    void applyAuthoredMaterial(MeshDraw& draw, const std::string& materialPath, const Mat4& worldMatrix,
                               const std::string& projectRoot);
    void applyCoarseOcclusion(std::vector<MeshDraw>& draws, const GpuSceneCamera& camera,
                              const GpuSceneRenderOptions& options) const;
    /// Adds the draws of every terrain's vegetation (one instanced draw per chunk and species variant).
    void appendVegetationDraws(ECS::World& world, const Vec3& cameraPos, const Spatial::Frustum& frustum,
                               const std::string& projectRoot, std::vector<MeshDraw>& draws);
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
    RHI::Shader* m_shadowCutoutFrag = nullptr;
    RHI::Shader* m_foliageVert = nullptr;        ///< scene_foliage.vert (per-vertex colour stream)
    RHI::Shader* m_shadowFoliageVert = nullptr;
    RenderStats m_stats;     ///< the last finished frame
    RenderStats m_building;  ///< the frame being recorded
    RHI::Shader* m_waterVert = nullptr;   ///< water.vert: Gerstner waves on the GPU

    // Micromeshes (doppio/docs/micromeshes.md): a mesh whose Mesh Renderer has "Micromesh" on is cut into
    // clusters once (per mesh asset and sub-mesh), and every frame compute passes pick the cut of every instance
    // of it, for the camera and for each shadow cascade, into a shared index arena; each instance is then one
    // indexed draw through scene_lit.frag into the same G-buffer.
    struct MicromeshAsset {
        MicromeshGpu gpu;
        u32 revision = 0;
        bool ready = false;
        std::vector<const MeshDraw*> users;  ///< this frame: the draws of this asset, in instance order
        u32 viewsUsed = 0;
    };
    /// The micromesh asset and instance a draw's sub-mesh is, this frame.
    struct MicromeshDrawRef {
        std::vector<std::pair<MicromeshAsset*, u32>> parts;  ///< (asset, instance) per sub-mesh
    };
    static constexpr u32 kMicromeshViews = 12;  ///< camera + shadow cascades + reflection passes per frame
    static constexpr u32 kMicromeshArenaStart = 4u * 1024u * 1024u;  ///< 16 MB; grows by itself up to the max
    static constexpr u32 kMicromeshArenaMax = 64u * 1024u * 1024u;   ///< 256 MB
    RHI::Shader* m_micromeshVert = nullptr;
    RHI::Shader* m_shadowMicromeshVert = nullptr;
    RHI::Pipeline* m_micromeshPipeline = nullptr;
    RHI::Pipeline* m_shadowMicromeshPipeline = nullptr;
    MicromeshArena m_micromeshArena;
    MicromeshHzb m_micromeshHzb;  ///< last frame's depth, farthest-depth pyramid
    std::map<std::pair<const Assets::Mesh3D*, u32>, std::unique_ptr<MicromeshAsset>> m_micromeshAssets;
    std::unordered_map<const MeshDraw*, MicromeshDrawRef> m_micromeshDraws;  ///< valid for the frame
    u32 m_micromeshNextView = 0;       ///< views handed out this frame
    u32 m_micromeshMainView = ~0u;
    u32 m_micromeshFrame = 0;
    bool m_micromeshOcclusionActive = false;
    MicromeshAsset* micromeshAssetFor(Assets::Mesh3D* mesh, u32 subMesh, u32 firstIndex, u32 indexCount);
    /// Once per frame, after the draws are final and before any pass: resets the arena, groups the draws that
    /// use Micromesh by asset and uploads their matrices.
    void beginMicromeshFrame(RHI::CommandBuffer* cmd, const std::vector<MeshDraw>& draws);
    /// Compute: the cut of every Micromesh instance for one view. Returns the view index or ~0u (none left).
    u32 cullMicromeshView(RHI::CommandBuffer* cmd, const MicromeshCullParams& params);
    bool microDrawParts(const MeshDraw* draw, const MicromeshDrawRef*& out) const;
    void releaseMicromeshes();
    RHI::Shader* m_foliageInstancedVert = nullptr;
    RHI::Pipeline* m_scenePipeline = nullptr;
    RHI::Pipeline* m_wireframePipeline = nullptr;
    RHI::Pipeline* m_terrainPipeline = nullptr;
    RHI::Pipeline* m_shadowPipeline = nullptr;
    RHI::Pipeline* m_shadowCutoutPipeline = nullptr; ///< alpha-tested shadows for cut-out cards
    RHI::Pipeline* m_foliagePipeline = nullptr;      ///< meshes with per-vertex colour / wind weight
    RHI::Pipeline* m_shadowFoliagePipeline = nullptr;
    RHI::Pipeline* m_waterPipeline = nullptr;       ///< transmissive pass (depth write)
    RHI::Pipeline* m_waterBlendPipeline = nullptr;  ///< blended pass
    RHI::Pipeline* m_foliageInstancedPipeline = nullptr; ///< instanced + colour stream
    RHI::Pipeline* m_shadowFoliageCutoutPipeline = nullptr;
    std::string m_shadowProjectRoot;
    TextureQualitySettings m_shadowTextureQuality{};
    // Geometric displacement (vertex-stage height sampler).
    RHI::Shader* m_displaceVert = nullptr;
    RHI::Shader* m_shadowDisplaceVert = nullptr;
    RHI::Pipeline* m_displacePipeline = nullptr;
    RHI::Pipeline* m_shadowDisplacePipeline = nullptr;
    RHI::Shader* m_instancedVert = nullptr;
    RHI::Pipeline* m_instancedPipeline = nullptr;
    RHI::Buffer* m_instanceBuffer = nullptr;
    std::unordered_map<u32, RHI::Buffer*> m_skinBuffers;
    std::unordered_map<u32, u64> m_skinUploadGen;
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
    GpuCloudSettings m_cloudSettings{};
    f32 m_cloudTime = 0.0f;
    /// Seconds of simulation (the sum of the render deltas): wind and foliage sway by it. Stands still at delta 0.
    f32 m_simTime = 0.0f;
    /// The scene's weather as of the latest scene pass (identity when the scene has none).
    Scene::WeatherResolved m_weather{};
    /// The lightning at this frame's moment, the wind's surge, and the clock thunder was last handed out up to.
    Scene::LightningState m_lightning{};
    f32 m_gust = 1.0f;
    f32 m_thunderLast = 0.0f;
    /// The primary sun after the weather's scale: what the fog glows with.
    Vec3 m_weatherSunDirection{0.0f, 1.0f, 0.0f};
    Vec3 m_weatherSunColor{0.0f, 0.0f, 0.0f};
    f32 m_weatherSunIntensity = 0.0f;
    /// Bound to cube sampler slots that have nothing to sample (a 2D texture there is invalid).
    RHI::Texture* m_fallbackCube = nullptr;
    RHI::Texture* m_reflectionColor = nullptr;
    RHI::Texture* m_reflectionDepth = nullptr;
    GBufferTargets m_reflectionGbuffer;
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
    /// Photon's compute path, **one per view**: its targets are sized to the view and its far field is centred on
    /// that view's camera, so views of different sizes or places would otherwise rebuild each other's every frame.
    /// `geometry` is what the instance was last given and `flags` the settings it was gathered under (see
    /// updatePhotonGeometry); `calmFrames` counts frames since the scene (geometry or lights) last changed and
    /// `temporalWeight` is the weight the resolve gives the present frame.
    struct PhotonView {
        PhotonCascades cascades;
        PhotonGeometry geometry;
        u32 flags = ~0u;
        u64 lightHash = 0;
        u32 calmFrames = 0;
        f32 temporalWeight = 0.4f;
        u64 lastUsed = 0;
    };
    std::vector<std::pair<u64, std::unique_ptr<PhotonView>>> m_photonViews;
    /// The instance of the view being rendered (null while it does not use the compute path).
    PhotonView* m_pv = nullptr;
    PhotonView& photonViewFor(u64 viewId);
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
    struct DisplacedMesh {
        Assets::Mesh3D mesh;
        f32 metersPerUv = 1.0f;
        f32 edgeUv = 1.0f;
        u64 sourceRevision = 0;
        bool valid = false;
    };
    std::map<std::pair<const Assets::Mesh3D*, u32>, DisplacedMesh> m_displacedMeshes;
    DisplacedMesh* displacedMeshFor(const Assets::Mesh3D* source, u32 levels);
    std::unordered_map<u32, u32> m_entityLod;
    /// The largest model radius seen per World Instances entity (pivot to farthest vertex): grows chunk bounds.
    std::unordered_map<u32, f32> m_worldInstanceRadius;
    bool m_ready = false;
};

}  // namespace Caffeine::Render
