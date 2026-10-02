#include "render/MaterialPreviewRenderer.hpp"

#include "assets/MaterialCache.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "math/Quat.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Render {
namespace {

constexpr f32 kDegToRad = 3.14159265f / 180.0f;
constexpr f32 kCameraDistance = 1.9f;
constexpr f32 kFovDegrees = 40.0f;
constexpr f32 kFloorY = -0.5f;

struct Fnv {
    u64 h = 1469598103934665603ull;
    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    }
    template <typename T>
    void value(const T& v) { bytes(&v, sizeof(v)); }
    void text(const std::string& s) {
        bytes(s.data(), s.size());
        value(s.size());
    }
};

u64 hashPreviewInputs(const Assets::MaterialSurface& s, u32 pixelSize,
                      const MaterialPreviewSettings& settings) {
    Fnv f;
    f.value(s.albedo);
    f.value(s.metallic);
    f.value(s.roughness);
    f.value(s.reflectance);
    f.value(s.emission);
    f.value(s.emissionStrength);
    f.text(s.albedoMap);
    f.text(s.normalMap);
    f.text(s.ormMap);
    f.text(s.emissionMap);
    f.value(s.uvTiling);
    f.value(s.uvOffset);
    f.value(s.normalStrength);
    f.value(s.aoStrength);
    f.value(s.alphaMode);
    f.value(s.alphaCutoff);
    f.value(s.transmission);
    f.value(s.ior);
    f.value(s.clearcoat);
    f.value(s.clearcoatRoughness);
    f.value(s.sheenColor);
    f.value(s.sheenRoughness);
    f.value(s.iridescence);
    f.value(s.iridescenceThickness);
    f.value(s.iridescenceIor);
    f.value(pixelSize);
    f.value(settings.yawDegrees);
    f.value(settings.pitchDegrees);
    f.text(settings.environmentPath);
    f.value(settings.environmentExposure);
    f.value(settings.showFloor);
    f.value(settings.probeResolution);
    return f.h;
}

u64 hashWithRoot(u64 h, const std::string& projectRoot) {
    Fnv f;
    f.h = h;
    f.text(projectRoot);
    return f.h;
}

}  // namespace

MaterialPreviewRenderer::~MaterialPreviewRenderer() {
    shutdown();
}

bool MaterialPreviewRenderer::init(RHI::RenderDevice* device) {
    shutdown();
    if (!device || !device->isInitialized()) return false;
    m_device = device;
    if (!m_renderer.init(device)) {
        m_device = nullptr;
        return false;
    }

    buildScene();
    m_ready = true;
    return true;
}

void MaterialPreviewRenderer::shutdown() {
    if (m_device) {
        if (m_color) m_device->destroyTexture(m_color);
        if (m_depth) m_device->destroyTexture(m_depth);
    }
    m_renderer.shutdown();
    m_world.destroyAll();
    m_sphere = ECS::Entity::INVALID;
    m_floor = ECS::Entity::INVALID;
    m_color = nullptr;
    m_depth = nullptr;
    m_pixelSize = 0;
    m_lastHash = 0;
    m_hasImage = false;
    m_ready = false;
    m_device = nullptr;
}

void MaterialPreviewRenderer::buildScene() {
    m_world.destroyAll();

    m_sphere = m_world.create("Preview Sphere");
    m_world.add<ECS::Position3D>(m_sphere);
    m_world.add<ECS::Rotation3D>(m_sphere);
    m_world.add<ECS::Scale3D>(m_sphere);
    ECS::MeshFilterComponent sphereFilter;
    sphereFilter.primitive = ECS::MeshPrimitive::Sphere;
    sphereFilter.customMaterialPath = kPreviewMaterialKey;
    m_world.add<ECS::MeshFilterComponent>(m_sphere, sphereFilter);
    m_world.add<ECS::MeshRendererComponent>(m_sphere);

    m_floor = m_world.create("Preview Floor");
    ECS::Position3D floorPos;
    floorPos.position = Vec3(0.0f, kFloorY, 0.0f);
    m_world.add<ECS::Position3D>(m_floor, floorPos);
    m_world.add<ECS::Rotation3D>(m_floor);
    ECS::Scale3D floorScale;
    floorScale.scale = Vec3(8.0f, 1.0f, 8.0f);
    m_world.add<ECS::Scale3D>(m_floor, floorScale);
    ECS::MeshFilterComponent floorFilter;
    floorFilter.primitive = ECS::MeshPrimitive::Plane;
    floorFilter.customMaterialPath = kFloorMaterialKey;
    m_world.add<ECS::MeshFilterComponent>(m_floor, floorFilter);
    m_world.add<ECS::MeshRendererComponent>(m_floor);

    const ECS::Entity sun = m_world.create("Preview Sun");
    m_world.add<ECS::Position3D>(sun);
    const Vec3 sunTravel = Vec3(-0.45f, -0.72f, -0.52f).normalized();
    const Quat aim = Quat::lookAt(-1.0f * sunTravel, Vec3(0.0f, 1.0f, 0.0f));
    ECS::Rotation3D sunRotation;
    sunRotation.quaternion = Vec4(aim.x, aim.y, aim.z, aim.w);
    m_world.add<ECS::Rotation3D>(sun, sunRotation);
    ECS::LightComponent light;
    light.intensity = 1.6f;
    m_world.add<ECS::LightComponent>(sun, light);
    ECS::DirectionalLightComponent directional;
    directional.shadowDistance = 6.0f;
    m_world.add<ECS::DirectionalLightComponent>(sun, directional);
}

bool MaterialPreviewRenderer::ensureTargets(u32 pixelSize) {
    if (m_color && m_depth && m_pixelSize == pixelSize) return true;
    if (m_color) m_device->destroyTexture(m_color);
    if (m_depth) m_device->destroyTexture(m_depth);
    m_color = nullptr;
    m_depth = nullptr;
    m_hasImage = false;

    RHI::TextureDesc colorDesc;
    colorDesc.width = pixelSize;
    colorDesc.height = pixelSize;
    colorDesc.format = RHI::TextureFormat::R8G8B8A8_UNORM;
    colorDesc.usage = RHI::TextureUsage::Sampler | RHI::TextureUsage::ColorTarget;
    m_color = m_device->createTexture(colorDesc);

    RHI::TextureDesc depthDesc;
    depthDesc.width = pixelSize;
    depthDesc.height = pixelSize;
    depthDesc.format = RHI::TextureFormat::D32_FLOAT;
    depthDesc.usage = RHI::TextureUsage::DepthStencil;
    m_depth = m_device->createTexture(depthDesc);

    m_pixelSize = (m_color && m_depth) ? pixelSize : 0;
    return m_color && m_depth;
}

void MaterialPreviewRenderer::applySettings(const MaterialPreviewSettings& settings) {
    if (auto* filter = m_world.get<ECS::MeshFilterComponent>(m_floor)) {
        filter->primitive = settings.showFloor ? ECS::MeshPrimitive::Plane : ECS::MeshPrimitive::Custom;
    }
}

bool MaterialPreviewRenderer::render(RHI::CommandBuffer* cmd, const Assets::MaterialSurface& surface,
                                     u32 pixelSize, const MaterialPreviewSettings& settings,
                                     const std::string& projectRoot) {
    if (!m_ready || !cmd || pixelSize < 16) return false;
    if (!ensureTargets(pixelSize)) return false;

    const u64 hash = hashWithRoot(hashPreviewInputs(surface, pixelSize, settings), projectRoot);
    // Probes, SSR and TAA converge over a few frames; keep drawing until they settle.
    if (m_hasImage && hash == m_lastHash && !m_renderer.needsAnotherFrame()) return true;

    // Keys are resolved against the same project root the renderer receives.
    Assets::MaterialSurface published = surface;
    published.valid = true;
    Assets::MaterialCache::instance().publish(kPreviewMaterialKey, projectRoot, published);
    Assets::MaterialSurface floor;
    floor.valid = true;
    floor.name = "Preview Floor";
    floor.albedo = Vec4(0.42f, 0.42f, 0.44f, 1.0f);
    floor.roughness = 0.85f;
    Assets::MaterialCache::instance().publish(kFloorMaterialKey, projectRoot, floor);
    applySettings(settings);

    const f32 yaw = settings.yawDegrees * kDegToRad;
    const f32 pitch = std::clamp(settings.pitchDegrees, -80.0f, 80.0f) * kDegToRad;
    GpuSceneCamera camera;
    camera.focus = Vec3(0.0f, 0.0f, 0.0f);
    camera.position = Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch)) *
                      kCameraDistance;
    camera.fovRad = kFovDegrees * kDegToRad;
    camera.nearClip = 0.05f;
    camera.farClip = 200.0f;
    camera.view = Mat4::lookAt(camera.position, camera.focus, Vec3(0.0f, 1.0f, 0.0f));
    camera.proj = Mat4::perspective(camera.fovRad, 1.0f, camera.nearClip, camera.farClip);

    GpuSceneRenderOptions options;
    options.resolveFeaturesFromScene = false;
    options.resolveEnvironmentFromScene = false;
    options.environmentPath = settings.environmentPath;
    options.environmentExposure = settings.environmentExposure;
    options.enableShadows = true;
    options.directionalCascadeCount = 1;
    options.cameraSettled = true;
    options.features.reflections = ReflectionMode::Probe;
    options.features.probeResolution = std::clamp(settings.probeResolution, 32u, 256u);
    options.features.maxReflectionProbes = 1;
    options.features.occlusion = OcclusionMode::Off;
    options.features.volumetrics = VolumetricQuality::Off;

    m_renderer.renderWithCamera(cmd, m_world, camera, m_color, m_depth, pixelSize, pixelSize,
                                projectRoot, options);
    m_lastHash = hash;
    m_hasImage = true;
    return true;
}

}  // namespace Caffeine::Render
