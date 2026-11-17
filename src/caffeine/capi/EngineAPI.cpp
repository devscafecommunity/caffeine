#include "caffeine/capi/EngineAPI.h"

#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "render/GpuSceneRenderer.hpp"
#include "render/GpuProceduralMeshes.hpp"
#include "render/GpuTextureCache.hpp"
#include "render/MaterialPreviewRenderer.hpp"
#include "rhi/RenderDevice.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "physics/PhysicsComponents3D.hpp"
#include "scene/EditorCameraMath.hpp"
#include "scene/HierarchySystem.hpp"
#include "scene/NaturalFluidSystem.hpp"
#include "scene/TerrainSystem.hpp"
#include "ecs/TerrainComponents.hpp"
#include "terrain/TerrainCache.hpp"
#include "caffeine/environment/NaturalFluids.hpp"
#include "scene/SceneComponents.hpp"
#include "assets/MaterialCache.hpp"
#include "assets/MaterialFile.hpp"
#include "assets/MaterialPresets.hpp"
#include "assets/MeshCache.hpp"
#include "assets/TextureMapGenerator.hpp"

#include "caffeine/photon/PhotonBake.hpp"

#include <stb/stb_image.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "script/ScriptTypes.hpp"
#ifdef CF_HAS_DRYAD
#include "script/DryadHost.hpp"
#endif
#include "scene/EditorContext.hpp"
#include "scene/EditorSceneSerializer.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

struct AssetEntry {
    std::string name;
    std::string relativePath;
    std::string kind;
    uint64_t sizeBytes = 0;
    bool isDirectory = false;
};

std::string assetKind(const std::filesystem::path& path, bool isDirectory) {
    if (isDirectory) return "folder";
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".gif" ||
        ext == ".hdr" || ext == ".exr") {
        return "image";
    }
    if (ext == ".obj" || ext == ".glb" || ext == ".gltf") return "mesh";
    if (ext == ".mat" || ext == ".material") return "material";
    if (ext == ".lua" || ext == ".dyd") return "script";
    if (ext == ".caf" || ext == ".cap") return "asset";
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") return "audio";
    if (ext == ".anim") return "animation";
    return "file";
}

struct CaffeineSession {
    Caffeine::ECS::World world;
    Caffeine::Editor::EditorContext context;
    std::string scenePath;
    float lastDelta = 0.0f;
    uint64_t frameIndex = 0;

    std::unique_ptr<Caffeine::RHI::RenderDevice> renderDevice;
    std::unique_ptr<Caffeine::Render::GpuSceneRenderer> sceneRenderer;
    std::unique_ptr<Caffeine::Render::MaterialPreviewRenderer> materialPreview;
    Caffeine::RHI::Texture* colorTarget = nullptr;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    CaffeineCamera camera{0.65f, 0.35f, 10.0f, {0.0f, 0.0f, 0.0f}};
    bool cameraInitialized = false;
    CaffeineViewportSettings viewportSettings{1, 1, 1.5f};
    std::string assetRoot;
    std::vector<AssetEntry> assets;

    // Async CPU readback: submit a download copy and poll its fence instead of
    // blocking the UI thread in SDL_WaitForGPUIdle.
    SDL_GPUTransferBuffer* readbackTransfer = nullptr;
    SDL_GPUFence* readbackFence = nullptr;
    uint32_t readbackCapacity = 0;
    uint32_t readbackWidth = 0;
    uint32_t readbackHeight = 0;
    bool readbackPending = false;

    // Material preview readback, separate from the viewport fence.
    SDL_GPUTransferBuffer* previewTransfer = nullptr;
    SDL_GPUFence* previewFence = nullptr;
    uint32_t previewSize = 0;
    Caffeine::Render::MaterialPreviewSettings previewLight; // sun/shadow state only
    bool previewPending = false;

    void releasePreviewReadbackFence() {
        if (previewFence && renderDevice) {
            SDL_ReleaseGPUFence(renderDevice->nativeDevice(), previewFence);
        }
        previewFence = nullptr;
    }

    void releasePreviewReadback() {
        releasePreviewReadbackFence();
        if (previewTransfer && renderDevice) {
            SDL_ReleaseGPUTransferBuffer(renderDevice->nativeDevice(), previewTransfer);
        }
        previewTransfer = nullptr;
        previewSize = 0;
        previewPending = false;
    }

    void scanAssets() {
        assets.clear();
        if (assetRoot.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(assetRoot, ec) ||
            !std::filesystem::is_directory(assetRoot, ec)) {
            return;
        }
        auto iterator = std::filesystem::recursive_directory_iterator(
            assetRoot, std::filesystem::directory_options::skip_permission_denied, ec);
        const auto end = std::filesystem::recursive_directory_iterator();
        for (; !ec && iterator != end && assets.size() < 2000; ++iterator) {
            if (iterator.depth() > 4) {
                iterator.disable_recursion_pending();
                continue;
            }
            const std::filesystem::path path = iterator->path();
            const std::string name = path.filename().string();
            if (name.empty() || name[0] == '.') {
                if (iterator->is_directory(ec)) iterator.disable_recursion_pending();
                continue;
            }
            const bool directory = iterator->is_directory(ec);
            AssetEntry entry;
            entry.name = name;
            entry.relativePath = std::filesystem::relative(path, assetRoot, ec).generic_string();
            if (ec) {
                ec.clear();
                entry.relativePath = path.filename().generic_string();
            }
            entry.kind = assetKind(path, directory);
            entry.sizeBytes = directory ? 0 : iterator->file_size(ec);
            entry.isDirectory = directory;
            assets.push_back(std::move(entry));
        }
        std::sort(assets.begin(), assets.end(), [](const AssetEntry& a, const AssetEntry& b) {
            return a.relativePath < b.relativePath;
        });
    }

    ~CaffeineSession() {
        releasePreviewReadback();
        releaseReadback();
        shutdownRender();
    }

    void releaseReadbackFence() {
        if (readbackFence && renderDevice) {
            SDL_ReleaseGPUFence(renderDevice->nativeDevice(), readbackFence);
        }
        readbackFence = nullptr;
    }

    void releaseReadback() {
        if (!renderDevice) {
            readbackTransfer = nullptr;
            readbackFence = nullptr;
            readbackPending = false;
            readbackCapacity = 0;
            return;
        }
        releaseReadbackFence();
        if (readbackTransfer) {
            SDL_ReleaseGPUTransferBuffer(renderDevice->nativeDevice(), readbackTransfer);
            readbackTransfer = nullptr;
        }
        readbackPending = false;
        readbackCapacity = 0;
        readbackWidth = 0;
        readbackHeight = 0;
    }

    void shutdownRender() {
        if (materialPreview) {
            materialPreview->shutdown();
            materialPreview.reset();
        }
        if (sceneRenderer) sceneRenderer->shutdown();
        // The texture cache and the procedural/imported meshes are process-wide. The device
        // below is leaked on purpose, so a later session would otherwise inherit their handles
        // (white texture, shadow-map users, mesh buffers) from a device it does not own.
        if (renderDevice) {
            Caffeine::Render::GpuTextureCache::instance().releaseAll(renderDevice.get());
            Caffeine::Render::GpuProceduralMeshes::releaseGpuResources(renderDevice.get());
            Caffeine::Assets::MeshCache::getInstance().releaseGpuResources(renderDevice.get());
        }
        if (renderDevice && colorTarget) renderDevice->destroyTexture(colorTarget);
        colorTarget = nullptr;
        renderWidth = 0;
        renderHeight = 0;
        sceneRenderer.reset();
        // SDL_DestroyGPUDevice unloads Vulkan entry points that other GPU
        // stacks in the host (wgpu/Iced) may still hold. The device is tiny
        // and an editor owns one session, so let the process reclaim it.
        if (renderDevice) renderDevice.release();
    }

    bool ensureRender(uint32_t width, uint32_t height) {
        if (!renderDevice) {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
            renderDevice = std::make_unique<Caffeine::RHI::RenderDevice>();
            Caffeine::RHI::RenderConfig config;
            config.width = width;
            config.height = height;
            if (!renderDevice->initOffscreen(config)) {
                renderDevice.reset();
                return false;
            }
            sceneRenderer = std::make_unique<Caffeine::Render::GpuSceneRenderer>();
            if (!sceneRenderer->init(renderDevice.get())) {
                sceneRenderer.reset();
                renderDevice->shutdown();
                renderDevice.reset();
                return false;
            }
        }
        if (renderWidth != width || renderHeight != height || !colorTarget) {
            if (colorTarget) renderDevice->destroyTexture(colorTarget);
            Caffeine::RHI::TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.format = Caffeine::RHI::TextureFormat::R8G8B8A8_UNORM;
            desc.usage = Caffeine::RHI::TextureUsage::Sampler |
                         Caffeine::RHI::TextureUsage::ColorTarget;
            colorTarget = renderDevice->createTexture(desc);
            renderWidth = width;
            renderHeight = height;
            if (!colorTarget) return false;
        }
        return true;
    }
};

namespace {

struct FieldDesc {
    const char* name;
    CaffeineFieldKind kind;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    float step = 0.0f;
    std::vector<const char*> options;
    std::function<bool(CaffeineSession&, Caffeine::ECS::Entity, CaffeineFieldValue&)> get;
    std::function<bool(CaffeineSession&, Caffeine::ECS::Entity,
                       const CaffeineFieldValue&)> set;
};

struct ComponentDesc {
    uint32_t id;
    const char* name;
    std::function<bool(CaffeineSession&, Caffeine::ECS::Entity)> has;
    std::function<void(CaffeineSession&, Caffeine::ECS::Entity)> add;
    std::function<void(CaffeineSession&, Caffeine::ECS::Entity)> remove;
    std::vector<FieldDesc> fields;
};

template <typename T>
ComponentDesc component(uint32_t id, const char* name, std::vector<FieldDesc> fields) {
    return ComponentDesc{
        id,
        name,
        [](CaffeineSession& session, Caffeine::ECS::Entity entity) {
            return session.world.has<T>(entity);
        },
        [](CaffeineSession& session, Caffeine::ECS::Entity entity) {
            session.world.add<T>(entity, T{});
        },
        [](CaffeineSession& session, Caffeine::ECS::Entity entity) {
            session.world.remove<T>(entity);
        },
        std::move(fields)};
}

CaffeineFieldValue baseValue(CaffeineFieldKind kind) {
    CaffeineFieldValue value{};
    value.kind = kind;
    return value;
}

template <typename T>
FieldDesc f32Field(const char* name, float T::*member, float minValue = 0.0f,
                   float maxValue = 0.0f, float step = 0.0f) {
    return FieldDesc{
        name, CAFFEINE_FIELD_F32, minValue, maxValue, step, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_F32);
            value.vec[0] = (*component).*member;
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            (*component).*member = value.vec[0];
            return true;
        }};
}

template <typename T>
FieldDesc boolField(const char* name, bool T::*member) {
    return FieldDesc{
        name, CAFFEINE_FIELD_BOOL, 0.0f, 0.0f, 0.0f, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_BOOL);
            value.boolean = (*component).*member ? 1 : 0;
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            (*component).*member = value.boolean != 0;
            return true;
        }};
}

template <typename T>
FieldDesc i32Field(const char* name, int32_t T::*member, float minValue = 0.0f,
                   float maxValue = 0.0f, float step = 1.0f) {
    return FieldDesc{
        name, CAFFEINE_FIELD_I32, minValue, maxValue, step, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_I32);
            value.integer = (*component).*member;
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            (*component).*member = value.integer;
            return true;
        }};
}

template <typename T>
FieldDesc u32Field(const char* name, uint32_t T::*member) {
    return FieldDesc{
        name, CAFFEINE_FIELD_U32, 0.0f, 0.0f, 1.0f, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_U32);
            value.integer = static_cast<int32_t>((*component).*member);
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            (*component).*member = static_cast<uint32_t>(value.integer);
            return true;
        }};
}

template <typename T>
FieldDesc vec2Field(const char* name, Caffeine::Vec2 T::*member, float step = 0.1f) {
    return FieldDesc{
        name, CAFFEINE_FIELD_VEC2, 0.0f, 0.0f, step, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_VEC2);
            const Caffeine::Vec2& vec = (*component).*member;
            value.vec[0] = vec.x;
            value.vec[1] = vec.y;
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            Caffeine::Vec2& vec = (*component).*member;
            vec.x = value.vec[0];
            vec.y = value.vec[1];
            return true;
        }};
}

template <typename T>
FieldDesc vec3Field(const char* name, Caffeine::Vec3 T::*member, float step = 0.1f) {
    return FieldDesc{
        name, CAFFEINE_FIELD_VEC3, 0.0f, 0.0f, step, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_VEC3);
            const Caffeine::Vec3& vec = (*component).*member;
            value.vec[0] = vec.x;
            value.vec[1] = vec.y;
            value.vec[2] = vec.z;
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            Caffeine::Vec3& vec = (*component).*member;
            vec.x = value.vec[0];
            vec.y = value.vec[1];
            vec.z = value.vec[2];
            return true;
        }};
}

template <typename T>
FieldDesc vec4Field(const char* name, Caffeine::Vec4 T::*member, float minValue = 0.0f,
                    float maxValue = 0.0f, float step = 0.01f) {
    return FieldDesc{
        name, CAFFEINE_FIELD_VEC4, minValue, maxValue, step, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_VEC4);
            const Caffeine::Vec4& vec = (*component).*member;
            value.vec[0] = vec.x;
            value.vec[1] = vec.y;
            value.vec[2] = vec.z;
            value.vec[3] = vec.w;
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            Caffeine::Vec4& vec = (*component).*member;
            vec.x = value.vec[0];
            vec.y = value.vec[1];
            vec.z = value.vec[2];
            vec.w = value.vec[3];
            return true;
        }};
}

template <typename T>
FieldDesc colorField(const char* name, Caffeine::Vec4 T::*member) {
    FieldDesc field = vec4Field<T>(name, member, 0.0f, 1.0f, 0.01f);
    field.kind = CAFFEINE_FIELD_COLOR;
    auto original = field.get;
    field.get = [original](CaffeineSession& session, Caffeine::ECS::Entity entity,
                           CaffeineFieldValue& value) {
        if (!original(session, entity, value)) return false;
        value.kind = CAFFEINE_FIELD_COLOR;
        return true;
    };
    return field;
}

template <typename T>
FieldDesc stringField(const char* name, std::string T::*member) {
    return FieldDesc{
        name, CAFFEINE_FIELD_STRING, 0.0f, 0.0f, 0.0f, {},
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_STRING);
            std::strncpy(value.text, ((*component).*member).c_str(),
                         sizeof(value.text) - 1);
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            (*component).*member = std::string(value.text);
            return true;
        }};
}

template <typename T, typename E>
FieldDesc enumField(const char* name, E T::*member, std::vector<const char*> options) {
    return FieldDesc{
        name, CAFFEINE_FIELD_ENUM, 0.0f,
        static_cast<float>(options.size() - 1), 1.0f, std::move(options),
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 CaffeineFieldValue& value) {
            const auto* component = session.world.get<T>(entity);
            if (!component) return false;
            value = baseValue(CAFFEINE_FIELD_ENUM);
            value.integer = static_cast<int32_t>((*component).*member);
            return true;
        },
        [member](CaffeineSession& session, Caffeine::ECS::Entity entity,
                 const CaffeineFieldValue& value) {
            auto* component = session.world.get<T>(entity);
            if (!component) return false;
            (*component).*member = static_cast<E>(value.integer);
            return true;
        }};
}

const std::vector<ComponentDesc>& componentRegistry() {
    using namespace Caffeine;
    static const std::vector<ComponentDesc> registry = {
        component<ECS::Transform>(1, "Transform",
            {vec3Field<ECS::Transform>("Position", &ECS::Transform::position),
             vec3Field<ECS::Transform>("Rotation", &ECS::Transform::rotation),
             vec3Field<ECS::Transform>("Scale", &ECS::Transform::scale)}),
        component<ECS::Camera3DComponent>(2, "Camera 3D",
            {f32Field<ECS::Camera3DComponent>("Field of view", &ECS::Camera3DComponent::fov, 10.0f, 170.0f, 1.0f),
             f32Field<ECS::Camera3DComponent>("Near clip", &ECS::Camera3DComponent::nearClip, 0.001f, 100.0f, 0.01f),
             f32Field<ECS::Camera3DComponent>("Far clip", &ECS::Camera3DComponent::farClip, 1.0f, 100000.0f, 10.0f),
             f32Field<ECS::Camera3DComponent>("Aspect ratio", &ECS::Camera3DComponent::aspectRatio, 0.1f, 10.0f, 0.01f)}),
        component<ECS::LightComponent>(3, "Light",
            {colorField<ECS::LightComponent>("Color", &ECS::LightComponent::color),
             f32Field<ECS::LightComponent>("Intensity", &ECS::LightComponent::intensity, 0.0f, 1000.0f, 0.05f)}),
        component<ECS::DirectionalLightComponent>(4, "Directional Light",
            {f32Field<ECS::DirectionalLightComponent>("Shadow distance", &ECS::DirectionalLightComponent::shadowDistance, 0.0f, 10000.0f, 1.0f),
             boolField<ECS::DirectionalLightComponent>("Cast shadows", &ECS::DirectionalLightComponent::castShadows)}),
        component<ECS::PointLightComponent>(5, "Point Light",
            {f32Field<ECS::PointLightComponent>("Radius", &ECS::PointLightComponent::radius, 0.0f, 10000.0f, 0.1f),
             boolField<ECS::PointLightComponent>("Cast shadows", &ECS::PointLightComponent::castShadows)}),
        component<ECS::SpotLightComponent>(6, "Spot Light",
            {f32Field<ECS::SpotLightComponent>("Radius", &ECS::SpotLightComponent::radius, 0.0f, 10000.0f, 0.1f),
             f32Field<ECS::SpotLightComponent>("Angle", &ECS::SpotLightComponent::angle, 1.0f, 179.0f, 1.0f),
             boolField<ECS::SpotLightComponent>("Cast shadows", &ECS::SpotLightComponent::castShadows)}),
        component<ECS::MeshRendererComponent>(7, "Mesh Renderer",
            {stringField<ECS::MeshRendererComponent>("Mesh", &ECS::MeshRendererComponent::meshPath),
             stringField<ECS::MeshRendererComponent>("Material", &ECS::MeshRendererComponent::materialPath),
             boolField<ECS::MeshRendererComponent>("Cast shadows", &ECS::MeshRendererComponent::castShadows),
             boolField<ECS::MeshRendererComponent>("Receive shadows", &ECS::MeshRendererComponent::receiveShadows)}),
        component<Physics3D::RigidBody3D>(8, "Rigid Body 3D",
            {f32Field<Physics3D::RigidBody3D>("Mass", &Physics3D::RigidBody3D::mass, 0.0f, 100000.0f, 0.1f),
             f32Field<Physics3D::RigidBody3D>("Linear damping", &Physics3D::RigidBody3D::linearDamping, 0.0f, 10.0f, 0.01f),
             f32Field<Physics3D::RigidBody3D>("Angular damping", &Physics3D::RigidBody3D::angularDamping, 0.0f, 10.0f, 0.01f),
             f32Field<Physics3D::RigidBody3D>("Friction", &Physics3D::RigidBody3D::friction, 0.0f, 10.0f, 0.01f),
             f32Field<Physics3D::RigidBody3D>("Restitution", &Physics3D::RigidBody3D::restitution, 0.0f, 10.0f, 0.01f),
             enumField<Physics3D::RigidBody3D, Physics3D::BodyType3D>("Body type", &Physics3D::RigidBody3D::bodyType, {"Dynamic", "Kinematic", "Static"}),
             boolField<Physics3D::RigidBody3D>("Lock rotation", &Physics3D::RigidBody3D::lockRotation)}),
        component<Physics3D::Collider3D>(9, "Collider 3D",
            {enumField<Physics3D::Collider3D, Physics3D::ColliderShape3D>("Shape", &Physics3D::Collider3D::shape, {"Box", "Sphere", "Capsule"}),
             vec3Field<Physics3D::Collider3D>("Half extents", &Physics3D::Collider3D::halfExtents),
             vec3Field<Physics3D::Collider3D>("Offset", &Physics3D::Collider3D::offset),
             boolField<Physics3D::Collider3D>("Is trigger", &Physics3D::Collider3D::isTrigger)}),
        component<Physics2D::RigidBody2D>(10, "Rigid Body 2D",
            {f32Field<Physics2D::RigidBody2D>("Mass", &Physics2D::RigidBody2D::mass, 0.0f, 100000.0f, 0.1f),
             f32Field<Physics2D::RigidBody2D>("Restitution", &Physics2D::RigidBody2D::restitution, 0.0f, 10.0f, 0.01f),
             f32Field<Physics2D::RigidBody2D>("Friction", &Physics2D::RigidBody2D::friction, 0.0f, 10.0f, 0.01f),
             f32Field<Physics2D::RigidBody2D>("Linear damping", &Physics2D::RigidBody2D::linearDamping, 0.0f, 10.0f, 0.01f),
             boolField<Physics2D::RigidBody2D>("Kinematic", &Physics2D::RigidBody2D::isKinematic),
             boolField<Physics2D::RigidBody2D>("Lock rotation", &Physics2D::RigidBody2D::lockRotation),
             boolField<Physics2D::RigidBody2D>("Sleeping", &Physics2D::RigidBody2D::isSleeping),
             f32Field<Physics2D::RigidBody2D>("Velocity X", &Physics2D::RigidBody2D::velocityX, -10000.0f, 10000.0f, 0.1f),
             f32Field<Physics2D::RigidBody2D>("Velocity Y", &Physics2D::RigidBody2D::velocityY, -10000.0f, 10000.0f, 0.1f)}),
        component<Physics2D::Collider2D>(11, "Collider 2D",
            {vec2Field<Physics2D::Collider2D>("Size", &Physics2D::Collider2D::size),
             vec2Field<Physics2D::Collider2D>("Offset", &Physics2D::Collider2D::offset),
             f32Field<Physics2D::Collider2D>("Radius", &Physics2D::Collider2D::radius, 0.0f, 10000.0f, 0.05f),
             enumField<Physics2D::Collider2D, Physics2D::ColliderShape>("Shape", &Physics2D::Collider2D::shape, {"AABB", "Circle"}),
             u32Field<Physics2D::Collider2D>("Layer", &Physics2D::Collider2D::layer),
             u32Field<Physics2D::Collider2D>("Layer mask", &Physics2D::Collider2D::layerMask),
             boolField<Physics2D::Collider2D>("Static", &Physics2D::Collider2D::isStatic),
             boolField<Physics2D::Collider2D>("Is trigger", &Physics2D::Collider2D::isTrigger),
             boolField<Physics2D::Collider2D>("One way", &Physics2D::Collider2D::isOneWay)}),
        component<ECS::Position3D>(13, "Position 3D",
            {vec3Field<ECS::Position3D>("Position", &ECS::Position3D::position)}),
        component<ECS::Rotation3D>(14, "Rotation 3D",
            {vec4Field<ECS::Rotation3D>("Quaternion", &ECS::Rotation3D::quaternion, -1.0f, 1.0f, 0.01f)}),
        component<ECS::Scale3D>(15, "Scale 3D",
            {vec3Field<ECS::Scale3D>("Scale", &ECS::Scale3D::scale)}),
        component<ECS::MeshFilterComponent>(16, "Mesh Filter",
            {enumField<ECS::MeshFilterComponent, ECS::MeshPrimitive>("Primitive", &ECS::MeshFilterComponent::primitive, {"Custom", "Cube", "Sphere", "Capsule", "Cylinder", "Plane", "Cone", "Pyramid", "Torus"}),
             stringField<ECS::MeshFilterComponent>("Mesh", &ECS::MeshFilterComponent::customMeshPath),
             stringField<ECS::MeshFilterComponent>("Texture", &ECS::MeshFilterComponent::customTexturePath),
             stringField<ECS::MeshFilterComponent>("Normal map", &ECS::MeshFilterComponent::customNormalPath),
             stringField<ECS::MeshFilterComponent>("Material", &ECS::MeshFilterComponent::customMaterialPath),
             f32Field<ECS::MeshFilterComponent>("Shininess", &ECS::MeshFilterComponent::shininess, 0.0f, 1024.0f, 1.0f)}),
        component<Caffeine::Script::ScriptComponent>(12, "Script",
            {stringField<Caffeine::Script::ScriptComponent>("Script", &Caffeine::Script::ScriptComponent::scriptPath)}),
    };
    return registry;
}

const ComponentDesc* componentTypeById(uint32_t id) {
    for (const ComponentDesc& desc : componentRegistry()) {
        if (desc.id == id) return &desc;
    }
    return nullptr;
}

std::vector<const ComponentDesc*> presentComponents(CaffeineSession& session,
                                                    Caffeine::ECS::Entity entity) {
    std::vector<const ComponentDesc*> present;
    for (const ComponentDesc& desc : componentRegistry()) {
        if (desc.has(session, entity)) present.push_back(&desc);
    }
    return present;
}

void copyText(char* destination, size_t size, const char* text) {
    if (!destination || size == 0) return;
    std::strncpy(destination, text ? text : "", size - 1);
    destination[size - 1] = '\0';
}

std::string joinOptions(const std::vector<const char*>& options) {
    std::string joined;
    for (size_t i = 0; i < options.size(); ++i) {
        if (i) joined += '|';
        joined += options[i];
    }
    return joined;
}

void writeMaterial(const Caffeine::Assets::MaterialSurface& source, CaffeineMaterial& out) {
    std::memset(&out, 0, sizeof(out));
    copyText(out.name, sizeof(out.name), source.name.c_str());
    out.albedo[0] = source.albedo.x;
    out.albedo[1] = source.albedo.y;
    out.albedo[2] = source.albedo.z;
    out.albedo[3] = source.albedo.w;
    out.metallic = source.metallic;
    out.roughness = source.roughness;
    out.reflectance = source.reflectance;
    out.emission[0] = source.emission.x;
    out.emission[1] = source.emission.y;
    out.emission[2] = source.emission.z;
    out.emission_strength = source.emissionStrength;
    copyText(out.albedo_map, sizeof(out.albedo_map), source.albedoMap.c_str());
    copyText(out.normal_map, sizeof(out.normal_map), source.normalMap.c_str());
    copyText(out.orm_map, sizeof(out.orm_map), source.ormMap.c_str());
    copyText(out.emission_map, sizeof(out.emission_map), source.emissionMap.c_str());
    out.uv_tiling[0] = source.uvTiling.x;
    out.uv_tiling[1] = source.uvTiling.y;
    out.uv_offset[0] = source.uvOffset.x;
    out.uv_offset[1] = source.uvOffset.y;
    out.normal_strength = source.normalStrength;
    out.normal_directx = source.normalDirectX ? 1 : 0;
    out.ao_strength = source.aoStrength;
    out.alpha_mode = static_cast<int>(source.alphaMode);
    out.alpha_cutoff = source.alphaCutoff;
    out.transmission = source.transmission;
    out.ior = source.ior;
    out.clearcoat = source.clearcoat;
    out.clearcoat_roughness = source.clearcoatRoughness;
    out.sheen_color[0] = source.sheenColor.x;
    out.sheen_color[1] = source.sheenColor.y;
    out.sheen_color[2] = source.sheenColor.z;
    out.sheen_roughness = source.sheenRoughness;
    out.iridescence = source.iridescence;
    out.iridescence_thickness = source.iridescenceThickness;
    out.iridescence_ior = source.iridescenceIor;
    out.reflection = source.reflection;
    out.reflection_budget = source.reflectionBudget;
    out.reflection_performance = source.reflectionPerformance;
    out.reflection_quality = source.reflectionQuality;
    out.reflection_planar = source.reflectionPlanar;
    out.ssr_resolution = source.ssrResolution;
    out.ssr_max_steps = source.ssrMaxSteps;
    out.ssr_temporal = source.ssrTemporalFrames;
    out.ssr_distance = source.ssrDistance;
    out.ssr_samples = source.ssrSamples;
    out.ssr_denoise = source.ssrDenoise;
    out.ssr_probe_blend = source.ssrProbeBlend;
    out.ssr_bounces = source.ssrBounces;
    out.height_scale = source.heightScale;
    out.parallax_steps = source.parallaxSteps;
    out.displacement_mode = source.displacementMode;
    out.displacement_scale = source.displacementScale;
    out.mesh_subdivisions = source.meshSubdivisions;
    out.uv_object_scale = source.uvObjectScale ? 1 : 0;
}

Caffeine::Assets::MaterialSurface readMaterial(const CaffeineMaterial& source) {
    Caffeine::Assets::MaterialSurface out;
    out.name = source.name;
    out.albedo = Caffeine::Vec4(source.albedo[0], source.albedo[1], source.albedo[2],
                                source.albedo[3]);
    out.metallic = source.metallic;
    out.roughness = source.roughness;
    out.reflectance = source.reflectance;
    out.emission = Caffeine::Vec3(source.emission[0], source.emission[1], source.emission[2]);
    out.emissionStrength = source.emission_strength;
    out.albedoMap = source.albedo_map;
    out.normalMap = source.normal_map;
    out.ormMap = source.orm_map;
    out.emissionMap = source.emission_map;
    out.uvTiling = Caffeine::Vec2(source.uv_tiling[0], source.uv_tiling[1]);
    out.uvOffset = Caffeine::Vec2(source.uv_offset[0], source.uv_offset[1]);
    out.normalStrength = source.normal_strength;
    out.normalDirectX = source.normal_directx != 0;
    out.aoStrength = source.ao_strength;
    out.alphaMode = static_cast<Caffeine::Assets::MaterialAlphaMode>(
        std::clamp(source.alpha_mode, 0, 2));
    out.alphaCutoff = source.alpha_cutoff;
    out.transmission = source.transmission;
    out.ior = source.ior;
    out.clearcoat = source.clearcoat;
    out.clearcoatRoughness = source.clearcoat_roughness;
    out.sheenColor = Caffeine::Vec3(source.sheen_color[0], source.sheen_color[1],
                                    source.sheen_color[2]);
    out.sheenRoughness = source.sheen_roughness;
    out.iridescence = source.iridescence;
    out.iridescenceThickness = source.iridescence_thickness;
    out.iridescenceIor = source.iridescence_ior;
    out.reflection = source.reflection;
    out.reflectionBudget = static_cast<Caffeine::u8>(std::clamp(source.reflection_budget, 0, 3));
    out.reflectionPerformance = source.reflection_performance ? 1 : 0;
    out.reflectionQuality = source.reflection_quality ? 1 : 0;
    out.reflectionPlanar = source.reflection_planar ? 1 : 0;
    out.ssrResolution = source.ssr_resolution;
    out.ssrMaxSteps = source.ssr_max_steps;
    out.ssrTemporalFrames = source.ssr_temporal;
    out.ssrDistance = source.ssr_distance;
    out.ssrSamples = source.ssr_samples;
    out.ssrDenoise = source.ssr_denoise;
    out.ssrProbeBlend = source.ssr_probe_blend;
    out.ssrBounces = source.ssr_bounces;
    out.heightScale = source.height_scale;
    out.parallaxSteps = source.parallax_steps;
    out.displacementMode = static_cast<Caffeine::u8>(source.displacement_mode ? 1 : 0);
    out.displacementScale = source.displacement_scale;
    out.meshSubdivisions = static_cast<Caffeine::u8>(std::clamp(source.mesh_subdivisions, 1, 9));
    out.uvObjectScale = source.uv_object_scale ? 1 : 0;
    Caffeine::Assets::sanitizeMaterialSurface(out);
    out.valid = true;
    return out;
}

/// Converts an asset-root-relative path to the project-relative form stored in
/// `.mat` files and scene components. Absolute paths are returned unchanged.
std::string assetToProjectPath(const CaffeineSession& session, const std::string& path) {
    if (path.empty()) return {};
    std::filesystem::path input(path);
    if (input.is_absolute()) return input.generic_string();
    if (!session.assetRoot.empty() && !session.context.projectRootPath.empty()) {
        std::error_code ec;
        const std::filesystem::path root(session.context.projectRootPath);
        const std::filesystem::path assets(session.assetRoot);
        const std::filesystem::path relativeRoot = std::filesystem::relative(assets, root, ec);
        if (!ec && !relativeRoot.empty()) {
            return (relativeRoot / input).lexically_normal().generic_string();
        }
    }
    return input.lexically_normal().generic_string();
}

}  // namespace

namespace {

CaffeineSession* asSession(CaffeineSession* session) {
    return session;
}

const CaffeineSession* asSession(const CaffeineSession* session) {
    return session;
}

Caffeine::ECS::Entity entityOf(CaffeineSession& session, CaffeineEntity id) {
    return Caffeine::ECS::Entity(id, &session.world);
}

bool entityAlive(CaffeineSession& session, CaffeineEntity id) {
    const Caffeine::ECS::Entity entity = entityOf(session, id);
    return entity.isValid() && session.world.isEntityAlive(entity);
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;

// An entity can carry the legacy `Transform` and/or the split `Position3D` / `Rotation3D` /
// `Scale3D` components (scenes saved by older editors have both). The renderer and the
// hierarchy system build the world matrix from the split components whenever `Position3D`
// exists (see Scene::localMatrix), so reads must report those values and writes must reach
// every component that is present, or an edit would be invisible: the Inspector and gizmos
// would show the new number while the object keeps rendering with the old one.
bool prefersSplitComponents(CaffeineSession& session, Caffeine::ECS::Entity entity) {
    return session.world.has<Caffeine::ECS::Position3D>(entity);
}

bool readRotation(CaffeineSession& session, Caffeine::ECS::Entity entity,
                  CaffeineVec3* out) {
    const bool split = prefersSplitComponents(session, entity);
    if (split) {
        if (const auto* rotation = session.world.get<Caffeine::ECS::Rotation3D>(entity)) {
            const Caffeine::Vec3 euler =
                Caffeine::Quat(rotation->quaternion.x, rotation->quaternion.y,
                               rotation->quaternion.z, rotation->quaternion.w)
                    .normalized()
                    .toEuler();
            out->x = euler.x * kRadToDeg;
            out->y = euler.y * kRadToDeg;
            out->z = euler.z * kRadToDeg;
            return true;
        }
    }
    if (const auto* transform = session.world.get<Caffeine::ECS::Transform>(entity)) {
        out->x = transform->rotation.x;
        out->y = transform->rotation.y;
        out->z = transform->rotation.z;
        return true;
    }
    if (const auto* rotation = session.world.get<Caffeine::ECS::Rotation3D>(entity)) {
        const Caffeine::Vec3 euler =
            Caffeine::Quat(rotation->quaternion.x, rotation->quaternion.y,
                           rotation->quaternion.z, rotation->quaternion.w)
                .normalized()
                .toEuler();
        out->x = euler.x * kRadToDeg;
        out->y = euler.y * kRadToDeg;
        out->z = euler.z * kRadToDeg;
        return true;
    }
    return false;
}

bool readScale(CaffeineSession& session, Caffeine::ECS::Entity entity,
               CaffeineVec3* out) {
    if (prefersSplitComponents(session, entity)) {
        if (const auto* scale = session.world.get<Caffeine::ECS::Scale3D>(entity)) {
            out->x = scale->scale.x;
            out->y = scale->scale.y;
            out->z = scale->scale.z;
            return true;
        }
    }
    if (const auto* transform = session.world.get<Caffeine::ECS::Transform>(entity)) {
        out->x = transform->scale.x;
        out->y = transform->scale.y;
        out->z = transform->scale.z;
        return true;
    }
    if (const auto* scale = session.world.get<Caffeine::ECS::Scale3D>(entity)) {
        out->x = scale->scale.x;
        out->y = scale->scale.y;
        out->z = scale->scale.z;
        return true;
    }
    return false;
}

void writeRotation(CaffeineSession& session, Caffeine::ECS::Entity entity,
                   CaffeineVec3 euler) {
    bool written = false;
    if (auto* transform = session.world.get<Caffeine::ECS::Transform>(entity)) {
        transform->rotation = Caffeine::Vec3(euler.x, euler.y, euler.z);
        written = true;
    }
    const Caffeine::Quat quat =
        Caffeine::Quat::fromEuler(euler.x * kDegToRad, euler.y * kDegToRad,
                                  euler.z * kDegToRad)
            .normalized();
    const Caffeine::Vec4 packed(quat.x, quat.y, quat.z, quat.w);
    if (auto* rotation = session.world.get<Caffeine::ECS::Rotation3D>(entity)) {
        rotation->quaternion = packed;
        written = true;
    } else if (session.world.has<Caffeine::ECS::Position3D>(entity)) {
        // The renderer ignores Transform.rotation for split entities: give it a Rotation3D.
        session.world.add<Caffeine::ECS::Rotation3D>(entity).quaternion = packed;
        written = true;
    }
    if (!written) {
        auto& transform = session.world.add<Caffeine::ECS::Transform>(entity);
        transform.rotation = Caffeine::Vec3(euler.x, euler.y, euler.z);
    }
}

void writeScale(CaffeineSession& session, Caffeine::ECS::Entity entity,
                CaffeineVec3 scale) {
    const Caffeine::Vec3 value(scale.x, scale.y, scale.z);
    bool written = false;
    if (auto* transform = session.world.get<Caffeine::ECS::Transform>(entity)) {
        transform->scale = value;
        written = true;
    }
    if (auto* scale3d = session.world.get<Caffeine::ECS::Scale3D>(entity)) {
        scale3d->scale = value;
        written = true;
    } else if (session.world.has<Caffeine::ECS::Position3D>(entity) ||
               session.world.has<Caffeine::ECS::Rotation3D>(entity)) {
        // The renderer ignores Transform.scale for split entities: give it a Scale3D.
        session.world.add<Caffeine::ECS::Scale3D>(entity).scale = value;
        written = true;
    }
    if (!written) {
        auto& transform = session.world.add<Caffeine::ECS::Transform>(entity);
        transform.scale = value;
    }
}

CaffeineResult copyName(const char* name, char* buffer, std::size_t bufferSize) {
    if (!buffer || bufferSize == 0) return CAFFEINE_INVALID_ARGUMENT;
    if (!name) name = "";
    std::strncpy(buffer, name, bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return CAFFEINE_OK;
}

}  // namespace

extern "C" {

CaffeineSession* caffeine_session_create(void) {
    return new (std::nothrow) CaffeineSession();
}

void caffeine_session_destroy(CaffeineSession* session) {
    delete session;
}

int caffeine_capi_version(void) {
    return CAFFEINE_CAPI_VERSION;
}

CaffeineResult caffeine_session_load_scene(CaffeineSession* session, const char* path) {
    if (!session || !path || path[0] == '\0') return CAFFEINE_INVALID_ARGUMENT;

    Caffeine::Editor::SceneSerializer serializer(session->world);
    if (!serializer.deserialize(path)) return CAFFEINE_ERROR;

    session->scenePath = path;
    session->context.currentScenePath = path;
    const std::filesystem::path sceneFile(path);
    const std::filesystem::path parent = sceneFile.parent_path();
    const std::filesystem::path root =
        (parent.filename() == "scenes" && parent.has_parent_path())
            ? parent.parent_path()
            : parent;
    session->context.projectRootPath = root.string();
    session->context.isDirty = false;
    session->cameraInitialized = false;

    std::error_code assetEc;
    const std::filesystem::path assetsDir = root / "assets";
    const std::filesystem::path rawDir = assetsDir / "raw";
    if (std::filesystem::exists(rawDir, assetEc)) {
        session->assetRoot = rawDir.string();
    } else if (std::filesystem::exists(assetsDir, assetEc)) {
        session->assetRoot = assetsDir.string();
    } else {
        session->assetRoot.clear();
    }
    session->scanAssets();
    return CAFFEINE_OK;
}

int caffeine_dryad_available(void) {
#ifdef CF_HAS_DRYAD
    return 1;
#else
    return 0;
#endif
}

CaffeineResult caffeine_session_run_dryad(CaffeineSession* session, CaffeineEntity entityId,
                                          const char* scriptPath, float dt) {
#ifdef CF_HAS_DRYAD
    if (!session || !scriptPath || scriptPath[0] == '\0') return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    Caffeine::Script::DryadHost host;
    host.setWorld(&session->world);
    if (!session->context.projectRootPath.empty()) {
        host.setSearchRoot(session->context.projectRootPath);
    }
    std::string error;
    if (!host.load(scriptPath, &error)) return CAFFEINE_ERROR;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    if (!host.call(scriptPath, "onCreate", entity, 0.0f, &error)) return CAFFEINE_ERROR;
    if (!host.call(scriptPath, "onUpdate", entity, dt, &error)) return CAFFEINE_ERROR;
    session->context.isDirty = true;
    return CAFFEINE_OK;
#else
    (void)session;
    (void)entityId;
    (void)scriptPath;
    (void)dt;
    return CAFFEINE_ERROR;
#endif
}

const char* caffeine_session_scene_path(const CaffeineSession* session) {
    return session ? session->scenePath.c_str() : "";
}

int caffeine_session_is_dirty(const CaffeineSession* session) {
    return session && session->context.isDirty ? 1 : 0;
}

CaffeineResult caffeine_session_snapshot(const CaffeineSession* session, uint8_t** out_data,
                                          size_t* out_size, int* out_dirty) {
    if (!session || !out_data || !out_size) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Editor::SceneSerializer serializer(
        const_cast<Caffeine::ECS::World&>(session->world));
    std::vector<Caffeine::u8> buffer;
    if (!serializer.serializeToMemory(buffer, session->scenePath)) return CAFFEINE_ERROR;

    uint8_t* copy = nullptr;
    if (!buffer.empty()) {
        copy = new (std::nothrow) uint8_t[buffer.size()];
        if (!copy) return CAFFEINE_ERROR;
        std::memcpy(copy, buffer.data(), buffer.size());
    }
    *out_data = copy;
    *out_size = buffer.size();
    if (out_dirty) *out_dirty = session->context.isDirty ? 1 : 0;
    return CAFFEINE_OK;
}

void caffeine_session_free(void* data) {
    delete[] static_cast<uint8_t*>(data);
}

CaffeineResult caffeine_session_restore(CaffeineSession* session, const uint8_t* data,
                                         size_t size, int dirty) {
    if (!session || (!data && size > 0)) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Editor::SceneSerializer serializer(session->world);
    std::vector<Caffeine::u8> buffer;
    if (size > 0) buffer.assign(data, data + size);
    if (!serializer.deserializeFromMemory(buffer, session->scenePath)) return CAFFEINE_ERROR;
    session->context.isDirty = dirty != 0;
    session->context.selectEntity(Caffeine::ECS::Entity::INVALID);
    session->cameraInitialized = false;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_save_scene(CaffeineSession* session, const char* path) {
    if (!session || !path || path[0] == '\0') return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Editor::SceneSerializer serializer(session->world);
    if (!serializer.serialize(path)) return CAFFEINE_ERROR;
    session->scenePath = path;
    session->context.currentScenePath = path;
    session->context.isDirty = false;
    return CAFFEINE_OK;
}

uint32_t caffeine_session_entity_count(const CaffeineSession* session) {
    return session ? session->world.entityCount() : 0u;
}

CaffeineResult caffeine_session_entity_at(const CaffeineSession* session, uint32_t index,
                                          CaffeineEntityInfo* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;

    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    std::vector<CaffeineEntity> ids;
    Caffeine::ECS::ComponentQuery query;
    mutableSession.world.forEach(query, [&](Caffeine::ECS::Entity entity) {
        if (entity.isValid()) ids.push_back(entity.id());
    });
    std::sort(ids.begin(), ids.end());

    if (index >= ids.size()) return CAFFEINE_NOT_FOUND;
    out->id = ids[index];
    const Caffeine::ECS::Entity entity = entityOf(mutableSession, ids[index]);
    return copyName(
        Caffeine::Editor::getEntityName(mutableSession.world, entity),
        out->name, sizeof(out->name));
}

CaffeineResult caffeine_session_entity_name(const CaffeineSession* session,
                                            CaffeineEntity entityId, char* buffer,
                                            size_t buffer_size) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    return copyName(Caffeine::Editor::getEntityName(
                        mutableSession.world, entityOf(mutableSession, entityId)),
                    buffer, buffer_size);
}

CaffeineResult caffeine_session_entity_parent(const CaffeineSession* session,
                                              CaffeineEntity entityId,
                                              CaffeineEntity* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const auto* parent =
        mutableSession.world.get<Caffeine::Scene::Parent>(entityOf(mutableSession, entityId));
    *out = (parent && parent->parent.isValid()) ? parent->parent.id() : CAFFEINE_ENTITY_NONE;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_entity_parent(CaffeineSession* session,
                                                  CaffeineEntity entityId,
                                                  CaffeineEntity parentId) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    if (parentId == CAFFEINE_ENTITY_NONE) {
        session->world.remove<Caffeine::Scene::Parent>(entity);
        session->context.markDirty();
        return CAFFEINE_OK;
    }
    if (parentId == entityId || !entityAlive(*session, parentId)) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const Caffeine::ECS::Entity parent = entityOf(*session, parentId);
    Caffeine::ECS::Entity cursor = parent;
    for (int guard = 0; guard < 4096 && cursor.isValid(); ++guard) {
        if (cursor.id() == entityId) return CAFFEINE_INVALID_ARGUMENT;
        const auto* link = session->world.get<Caffeine::Scene::Parent>(cursor);
        if (!link) break;
        cursor = link->parent;
    }

    auto& link = session->world.add<Caffeine::Scene::Parent>(entity);
    link.parent = parent;
    link.dirty = true;
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineEntity caffeine_session_create_entity(CaffeineSession* session, const char* name) {
    if (!session) return CAFFEINE_ENTITY_NONE;
    const Caffeine::ECS::Entity entity = session->world.create(name);
    if (name && name[0] != '\0') {
        Caffeine::Editor::setEntityName(session->world, entity, name);
    }
    session->context.markDirty();
    return entity.id();
}

CaffeineResult caffeine_session_destroy_entity(CaffeineSession* session, CaffeineEntity entityId) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;

    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    session->world.destroy(entity);
    if (session->context.selectedEntity.id() == entityId) {
        session->context.selectEntity(Caffeine::ECS::Entity::INVALID);
    }
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_entity_name(CaffeineSession* session, CaffeineEntity entityId,
                                                const char* name) {
    if (!session || !name) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    Caffeine::Editor::setEntityName(session->world, entityOf(*session, entityId), name);
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_select_entity(CaffeineSession* session, CaffeineEntity entityId) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (entityId == CAFFEINE_ENTITY_NONE) {
        session->context.selectEntity(Caffeine::ECS::Entity::INVALID);
        return CAFFEINE_OK;
    }
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    session->context.selectEntity(entityOf(*session, entityId));
    return CAFFEINE_OK;
}

CaffeineEntity caffeine_session_selected_entity(const CaffeineSession* session) {
    if (!session || !session->context.selectedEntity.isValid()) return CAFFEINE_ENTITY_NONE;
    return session->context.selectedEntity.id();
}

CaffeineResult caffeine_session_entity_position(const CaffeineSession* session,
                                                CaffeineEntity entityId, CaffeineVec3* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;

    const Caffeine::ECS::Entity entity = entityOf(mutableSession, entityId);
    if (const auto* position =
            mutableSession.world.get<Caffeine::ECS::Position3D>(entity)) {
        // Split entities render from Position3D (see prefersSplitComponents).
        out->x = position->position.x;
        out->y = position->position.y;
        out->z = position->position.z;
        return CAFFEINE_OK;
    }
    if (const auto* transform = mutableSession.world.get<Caffeine::ECS::Transform>(entity)) {
        out->x = transform->position.x;
        out->y = transform->position.y;
        out->z = transform->position.z;
        return CAFFEINE_OK;
    }
    if (const auto* position =
            mutableSession.world.get<Caffeine::ECS::Position3D>(entity)) {
        out->x = position->position.x;
        out->y = position->position.y;
        out->z = position->position.z;
        return CAFFEINE_OK;
    }
    return CAFFEINE_NOT_FOUND;
}

CaffeineResult caffeine_session_entity_rotation(const CaffeineSession* session,
                                                CaffeineEntity entityId,
                                                CaffeineVec3* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    return readRotation(mutableSession, entityOf(mutableSession, entityId), out)
               ? CAFFEINE_OK
               : CAFFEINE_NOT_FOUND;
}

CaffeineResult caffeine_session_set_entity_rotation(CaffeineSession* session,
                                                    CaffeineEntity entityId,
                                                    CaffeineVec3 euler_degrees) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    writeRotation(*session, entityOf(*session, entityId), euler_degrees);
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_entity_scale(const CaffeineSession* session,
                                             CaffeineEntity entityId,
                                             CaffeineVec3* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    return readScale(mutableSession, entityOf(mutableSession, entityId), out)
               ? CAFFEINE_OK
               : CAFFEINE_NOT_FOUND;
}

CaffeineResult caffeine_session_set_entity_scale(CaffeineSession* session,
                                                 CaffeineEntity entityId,
                                                 CaffeineVec3 scale) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    writeScale(*session, entityOf(*session, entityId), scale);
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_entity_world_position(const CaffeineSession* session,
                                                      CaffeineEntity entityId,
                                                      CaffeineVec3* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::Mat4 matrix = Caffeine::Scene::computeWorldMatrix(
        mutableSession.world, entityOf(mutableSession, entityId));
    const Caffeine::Vec3 point =
        matrix.transformPoint(Caffeine::Vec3(0.0f, 0.0f, 0.0f));
    out->x = point.x;
    out->y = point.y;
    out->z = point.z;
    return CAFFEINE_OK;
}

uint32_t caffeine_component_type_count(void) {
    return static_cast<uint32_t>(componentRegistry().size());
}

CaffeineResult caffeine_component_type_at(uint32_t index, CaffeineComponentTypeInfo* out) {
    if (!out) return CAFFEINE_INVALID_ARGUMENT;
    const auto& registry = componentRegistry();
    if (index >= registry.size()) return CAFFEINE_NOT_FOUND;
    const ComponentDesc& desc = registry[index];
    out->id = desc.id;
    copyText(out->name, sizeof(out->name), desc.name);
    out->field_count = static_cast<uint32_t>(desc.fields.size());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_component_type_field_at(uint32_t typeId, uint32_t field,
                                                CaffeineFieldInfo* out) {
    if (!out) return CAFFEINE_INVALID_ARGUMENT;
    const ComponentDesc* desc = componentTypeById(typeId);
    if (!desc || field >= desc->fields.size()) return CAFFEINE_NOT_FOUND;
    const FieldDesc& source = desc->fields[field];
    std::memset(out, 0, sizeof(*out));
    copyText(out->name, sizeof(out->name), source.name);
    out->kind = source.kind;
    out->min_value = source.minValue;
    out->max_value = source.maxValue;
    out->step = source.step;
    const std::string options = joinOptions(source.options);
    copyText(out->enum_options, sizeof(out->enum_options), options.c_str());
    return CAFFEINE_OK;
}

uint32_t caffeine_session_entity_component_count(const CaffeineSession* session,
                                                CaffeineEntity entityId) {
    if (!session) return 0;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return 0;
    return static_cast<uint32_t>(
        presentComponents(mutableSession, entityOf(mutableSession, entityId)).size());
}

CaffeineResult caffeine_session_entity_component_at(const CaffeineSession* session,
                                                    CaffeineEntity entityId,
                                                    uint32_t index,
                                                    CaffeineComponentInfo* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const auto present = presentComponents(mutableSession, entityOf(mutableSession, entityId));
    if (index >= present.size()) return CAFFEINE_NOT_FOUND;
    const ComponentDesc& desc = *present[index];
    out->type_id = desc.id;
    copyText(out->name, sizeof(out->name), desc.name);
    out->field_count = static_cast<uint32_t>(desc.fields.size());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_entity_component_field_get(
    const CaffeineSession* session, CaffeineEntity entityId, uint32_t component,
    uint32_t field, CaffeineFieldValue* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const auto present = presentComponents(mutableSession, entityOf(mutableSession, entityId));
    if (component >= present.size()) return CAFFEINE_NOT_FOUND;
    const ComponentDesc& desc = *present[component];
    if (field >= desc.fields.size()) return CAFFEINE_NOT_FOUND;
    std::memset(out, 0, sizeof(*out));
    return desc.fields[field].get(mutableSession, entityOf(mutableSession, entityId), *out)
               ? CAFFEINE_OK
               : CAFFEINE_ERROR;
}

CaffeineResult caffeine_session_entity_component_field_set(
    CaffeineSession* session, CaffeineEntity entityId, uint32_t component,
    uint32_t field, const CaffeineFieldValue* value) {
    if (!session || !value) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const auto present = presentComponents(*session, entityOf(*session, entityId));
    if (component >= present.size()) return CAFFEINE_NOT_FOUND;
    const ComponentDesc& desc = *present[component];
    if (field >= desc.fields.size()) return CAFFEINE_NOT_FOUND;
    if (!desc.fields[field].set(*session, entityOf(*session, entityId), *value)) {
        return CAFFEINE_ERROR;
    }
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_add_component(CaffeineSession* session,
                                              CaffeineEntity entityId,
                                              uint32_t typeId) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const ComponentDesc* desc = componentTypeById(typeId);
    if (!desc) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    if (!desc->has(*session, entity)) {
        desc->add(*session, entity);
        session->context.markDirty();
    }
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_remove_component(CaffeineSession* session,
                                                 CaffeineEntity entityId,
                                                 uint32_t typeId) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const ComponentDesc* desc = componentTypeById(typeId);
    if (!desc) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    if (!desc->has(*session, entity)) return CAFFEINE_NOT_FOUND;
    desc->remove(*session, entity);
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_entity_position(CaffeineSession* session,
                                                    CaffeineEntity entityId,
                                                    CaffeineVec3 position) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;

    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    bool touched = false;
    if (auto* transform = session->world.get<Caffeine::ECS::Transform>(entity)) {
        transform->position = Caffeine::Vec3(position.x, position.y, position.z);
        touched = true;
    }
    if (auto* position3d = session->world.get<Caffeine::ECS::Position3D>(entity)) {
        position3d->position = Caffeine::Vec3(position.x, position.y, position.z);
        touched = true;
    }
    if (!touched) {
        Caffeine::ECS::Transform transform;
        transform.position = Caffeine::Vec3(position.x, position.y, position.z);
        session->world.add<Caffeine::ECS::Transform>(entity, transform);
    }
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_entity_world_position(CaffeineSession* session,
                                                          CaffeineEntity entityId,
                                                          CaffeineVec3 position) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;

    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    Caffeine::Mat4 local = Caffeine::Mat4::identity();
    if (const auto* link = session->world.get<Caffeine::Scene::Parent>(entity);
        link && link->parent.isValid() && link->parent.id() != entityId) {
        local = Caffeine::Scene::computeWorldMatrix(session->world, link->parent).inverted();
    }
    const Caffeine::Vec3 converted = local.transformPoint(
        Caffeine::Vec3(position.x, position.y, position.z));
    return caffeine_session_set_entity_position(
        session, entityId, {converted.x, converted.y, converted.z});
}

void caffeine_session_tick(CaffeineSession* session, float delta_time) {
    if (!session) return;
    session->lastDelta = delta_time;
    ++session->frameIndex;
    // Water surfaces animate in the editor too (previewInEditor).
    Caffeine::Scene::tickNaturalFluids(session->world, delta_time, true);
}

static bool renderFrame(CaffeineSession& session, uint32_t width, uint32_t height) {
    if (!session.ensureRender(width, height)) return false;

    // Resolve parent links before anything reads Scene::WorldTransform: the
    // renderer (and the bounds below) consume the cached world matrix, so a
    // child must inherit its group's transform every frame.
    Caffeine::Scene::propagateTransforms(session.world);
    Caffeine::Scene::syncTerrainMeshes(session.world);

    Caffeine::Vec3 min(std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max());
    Caffeine::Vec3 max(-std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max());
    bool any = false;

    Caffeine::ECS::ComponentQuery query;
    query.with<Caffeine::Scene::WorldTransform>();
    session.world.forEach<Caffeine::Scene::WorldTransform>(
        query, [&](Caffeine::ECS::Entity, Caffeine::Scene::WorldTransform& transform) {
            const Caffeine::Vec3 point =
                transform.matrix.transformPoint(Caffeine::Vec3(0.0f, 0.0f, 0.0f));
            min = Caffeine::Vec3(std::min(min.x, point.x), std::min(min.y, point.y),
                                 std::min(min.z, point.z));
            max = Caffeine::Vec3(std::max(max.x, point.x), std::max(max.y, point.y),
                                 std::max(max.z, point.z));
            any = true;
        });

    Caffeine::Vec3 center(0.0f, 0.0f, 0.0f);
    float radius = 10.0f;
    if (any) {
        center = Caffeine::Vec3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f,
                                (min.z + max.z) * 0.5f);
        radius = std::max((max - min).length() * 0.5f, 2.0f);
    }

    if (!session.cameraInitialized) {
        session.camera.yaw = 0.65f;
        session.camera.pitch = 0.35f;
        session.camera.distance = std::max(radius * 2.5f, 5.0f);
        session.camera.focus = {center.x, center.y, center.z};
        session.cameraInitialized = true;
    }
    const Caffeine::Vec3 focus(session.camera.focus.x, session.camera.focus.y,
                               session.camera.focus.z);
    const float distance = std::max(session.camera.distance, 1.0f);
    const Caffeine::Vec3 cameraPosition = Caffeine::Editor::editorCameraPosition(
        session.camera.yaw, session.camera.pitch, distance, focus);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const float visibleRange =
        std::max(std::abs(distance), std::abs(cameraPosition.y));
    const float farClip = std::min(
        50000.0f, std::max(5000.0f, (distance + std::abs(cameraPosition.y)) * 20.0f));
    // Keep the depth range useful when the editor is zoomed far out. A fixed
    // 0.1 near plane with a 50k far plane turns the ground grid and terrain
    // into z-fighting confetti.
    const float nearClip = std::clamp(distance * 0.002f, 0.1f, 50.0f);

    Caffeine::Render::GpuSceneCamera camera;
    camera.position = cameraPosition;
    camera.focus = focus;
    camera.view = Caffeine::Mat4::lookAt(cameraPosition, focus,
                                         Caffeine::Vec3(0.0f, 1.0f, 0.0f));
    camera.proj = Caffeine::Mat4::perspective(1.0472f, aspect, nearClip, farClip);
    camera.fovRad = 1.0472f;
    camera.nearClip = nearClip;
    camera.farClip = farClip;

    Caffeine::Render::GpuSceneRenderOptions options;
    options.enableShadows = false;
    options.renderScale =
        std::clamp(session.viewportSettings.render_scale, 0.5f, 2.0f);
    options.deltaTime = 1.0f / 60.0f;
    options.viewId = 1;
    options.resolveFeaturesFromScene = true;
    options.resolveEnvironmentFromScene = true;
    options.resolvePostProcessFromScene = false;
    options.postProcess = Caffeine::Render::PostProcessStack::defaults();
    options.postProcess.antiAliasing.enabled = session.viewportSettings.anti_aliasing > 0;
    options.postProcess.antiAliasing.mode =
        session.viewportSettings.anti_aliasing == 2 ? 1u : 0u;
    options.postProcess.antiAliasing.sharpness = 0.5f;

    if (session.viewportSettings.show_grid) {
        float spacing = 1.0f;
        while (visibleRange / spacing > 40.0f) spacing *= 2.0f;
        while (visibleRange / spacing < 8.0f && spacing > 0.25f) spacing *= 0.5f;
        options.grid.enabled = true;
        options.grid.cellSize = std::max(spacing, 0.25f);
        options.grid.majorEvery = 10.0f;
        options.grid.fadeDistance = std::max(visibleRange * 8.0f, 60.0f);
        // At extreme zoom the grid sits on an almost coplanar terrain and its
        // screen-space derivatives alias into dashed lines. Fade it out before
        // that happens instead of showing broken reference lines.
        const float fade = std::clamp((visibleRange - 1500.0f) / 3500.0f, 0.0f, 1.0f);
        options.grid.opacity = 1.0f - fade * fade * (3.0f - 2.0f * fade);
    }

    Caffeine::RHI::CommandBuffer* command = session.renderDevice->beginOffscreen();
    if (!command) return false;
    session.sceneRenderer->renderWithCamera(
        command, session.world, camera, session.colorTarget, nullptr, width, height,
        session.context.projectRootPath, options);
    session.renderDevice->endOffscreen(command);
    return true;
}

CaffeineResult caffeine_session_render(CaffeineSession* session, uint32_t width,
                                       uint32_t height, uint8_t* pixels,
                                       size_t pixel_bytes) {
    if (!session || !pixels || width == 0 || height == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const size_t required = static_cast<size_t>(width) * height * 4u;
    if (pixel_bytes < required) return CAFFEINE_INVALID_ARGUMENT;
    if (!renderFrame(*session, width, height)) return CAFFEINE_ERROR;

    std::vector<uint8_t> readback;
    if (!session->renderDevice->readTexture(session->colorTarget, readback)) {
        return CAFFEINE_ERROR;
    }
    const size_t copySize = std::min(readback.size(), required);
    std::memcpy(pixels, readback.data(), copySize);
    session->renderDevice->releaseOffscreenResources();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_render_begin(CaffeineSession* session, uint32_t width,
                                             uint32_t height) {
    if (!session || width == 0 || height == 0) return CAFFEINE_INVALID_ARGUMENT;
    if (session->readbackPending) return CAFFEINE_ERROR;
    if (!renderFrame(*session, width, height)) return CAFFEINE_ERROR;

    SDL_GPUDevice* device = session->renderDevice->nativeDevice();
    if (!device || !session->colorTarget) return CAFFEINE_ERROR;

    // The previous fence has already been polled (readbackPending is false);
    // release it and keep the transfer buffer for the next frame instead of
    // allocating/freeing several megabytes every 33 ms.
    session->releaseReadbackFence();
    const Uint32 requiredBytes = static_cast<Uint32>(width) * height * 4u;
    if (!session->readbackTransfer || session->readbackCapacity < requiredBytes) {
        if (session->readbackTransfer) {
            SDL_ReleaseGPUTransferBuffer(device, session->readbackTransfer);
        }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = requiredBytes;
        session->readbackTransfer = SDL_CreateGPUTransferBuffer(device, &info);
        session->readbackCapacity = session->readbackTransfer ? info.size : 0;
        if (!session->readbackTransfer) return CAFFEINE_ERROR;
    }

    SDL_GPUTransferBuffer* transfer = session->readbackTransfer;
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
    if (!command) {
        session->releaseReadback();
        return CAFFEINE_ERROR;
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
    SDL_GPUTextureRegion source{};
    source.texture = session->colorTarget->handle;
    source.w = width;
    source.h = height;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = transfer;
    destination.pixels_per_row = width;
    destination.rows_per_layer = height;
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
    if (!fence) {
        session->releaseReadback();
        SDL_WaitForGPUIdle(device);
        session->renderDevice->releaseOffscreenResources();
        return CAFFEINE_ERROR;
    }

    session->readbackFence = fence;
    session->readbackWidth = width;
    session->readbackHeight = height;
    session->readbackPending = true;
    return CAFFEINE_OK;
}

CaffeineRenderState caffeine_session_render_poll(CaffeineSession* session, uint8_t* pixels,
                                                size_t pixel_bytes) {
    if (!session || !session->readbackPending) return CAFFEINE_RENDER_IDLE;
    if (!pixels) return CAFFEINE_RENDER_FAILED;

    const size_t required =
        static_cast<size_t>(session->readbackWidth) * session->readbackHeight * 4u;
    if (pixel_bytes < required) return CAFFEINE_RENDER_FAILED;

    SDL_GPUDevice* device = session->renderDevice->nativeDevice();
    if (!device || !SDL_QueryGPUFence(device, session->readbackFence)) {
        return CAFFEINE_RENDER_PENDING;
    }

    const void* mapped = SDL_MapGPUTransferBuffer(device, session->readbackTransfer, false);
    if (!mapped) {
        session->releaseReadback();
        session->renderDevice->releaseOffscreenResources();
        return CAFFEINE_RENDER_FAILED;
    }
    std::memcpy(pixels, mapped, required);
    SDL_UnmapGPUTransferBuffer(device, session->readbackTransfer);
    session->releaseReadbackFence();
    session->readbackPending = false;
    session->readbackWidth = 0;
    session->readbackHeight = 0;
    session->renderDevice->releaseOffscreenResources();
    return CAFFEINE_RENDER_READY;
}

CaffeineResult caffeine_session_set_camera(CaffeineSession* session, CaffeineCamera camera) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    session->camera = camera;
    session->cameraInitialized = true;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_get_camera(const CaffeineSession* session,
                                           CaffeineCamera* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    *out = session->camera;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_viewport_settings(
    CaffeineSession* session, CaffeineViewportSettings settings) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    settings.show_grid = settings.show_grid ? 1 : 0;
    settings.anti_aliasing = std::clamp(settings.anti_aliasing, 0, 2);
    settings.render_scale = std::clamp(settings.render_scale, 0.5f, 2.0f);
    session->viewportSettings = settings;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_get_viewport_settings(
    const CaffeineSession* session, CaffeineViewportSettings* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    *out = session->viewportSettings;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_asset_root(CaffeineSession* session, const char* path) {
    if (!session || !path) return CAFFEINE_INVALID_ARGUMENT;
    session->assetRoot = path;
    session->scanAssets();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_refresh_assets(CaffeineSession* session) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    session->scanAssets();
    return CAFFEINE_OK;
}

uint32_t caffeine_session_asset_count(const CaffeineSession* session) {
    return session ? static_cast<uint32_t>(session->assets.size()) : 0u;
}

CaffeineResult caffeine_session_asset_at(const CaffeineSession* session, uint32_t index,
                                         CaffeineAssetInfo* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    if (index >= session->assets.size()) return CAFFEINE_NOT_FOUND;
    const AssetEntry& entry = session->assets[index];
    std::strncpy(out->name, entry.name.c_str(), sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    std::strncpy(out->relative_path, entry.relativePath.c_str(),
                 sizeof(out->relative_path) - 1);
    out->relative_path[sizeof(out->relative_path) - 1] = '\0';
    std::strncpy(out->kind, entry.kind.c_str(), sizeof(out->kind) - 1);
    out->kind[sizeof(out->kind) - 1] = '\0';
    out->size_bytes = entry.sizeBytes;
    out->is_directory = entry.isDirectory ? 1 : 0;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_asset_path(const CaffeineSession* session,
                                             const char* relative_path, char* buffer,
                                             size_t buffer_size) {
    if (!session || !relative_path || !buffer || buffer_size == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    std::filesystem::path resolved(relative_path);
    if (!resolved.is_absolute() && !session->assetRoot.empty()) {
        resolved = std::filesystem::path(session->assetRoot) / resolved;
    }
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolved, ec);
    copyText(buffer, buffer_size, (ec ? resolved : canonical).generic_string().c_str());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_asset_project_path(const CaffeineSession* session,
                                                     const char* relative_path, char* buffer,
                                                     size_t buffer_size) {
    if (!session || !relative_path || !buffer || buffer_size == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    copyText(buffer, buffer_size, assetToProjectPath(*session, relative_path).c_str());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_resolve_path(const CaffeineSession* session, const char* path,
                                                char* buffer, size_t buffer_size) {
    if (!session || !path || !buffer || buffer_size == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const std::string resolved = Caffeine::Assets::MeshCache::resolveTexturePath(
        path, session->context.projectRootPath);
    if (!resolved.empty()) {
        copyText(buffer, buffer_size, resolved.c_str());
        return CAFFEINE_OK;
    }
    std::filesystem::path fallback(path);
    if (!fallback.is_absolute()) {
        if (!session->context.projectRootPath.empty()) {
            fallback = std::filesystem::path(session->context.projectRootPath) / fallback;
        } else if (!session->assetRoot.empty()) {
            fallback = std::filesystem::path(session->assetRoot) / fallback;
        }
    }
    copyText(buffer, buffer_size, fallback.lexically_normal().generic_string().c_str());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_material_default(CaffeineMaterial* out) {
    if (!out) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Assets::MaterialSurface surface;
    surface.name = "New Material";
    surface.valid = true;
    Caffeine::Assets::sanitizeMaterialSurface(surface);
    writeMaterial(surface, *out);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_material_sanitize(CaffeineMaterial* material) {
    if (!material) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Assets::MaterialSurface surface = readMaterial(*material);
    writeMaterial(surface, *material);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_material_load(const char* path, CaffeineMaterial* out) {
    if (!path || path[0] == '\0' || !out) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Assets::MaterialSurface surface;
    if (!Caffeine::Assets::loadMaterialFile(path, surface)) return CAFFEINE_ERROR;
    writeMaterial(surface, *out);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_material_save(const char* path, const CaffeineMaterial* material) {
    if (!path || path[0] == '\0' || !material) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Assets::MaterialSurface surface = readMaterial(*material);
    if (surface.name.empty()) surface.name = "Material";
    if (!Caffeine::Assets::saveMaterialFile(path, surface)) return CAFFEINE_ERROR;
    Caffeine::Assets::MaterialCache::instance().invalidate(path);
    return CAFFEINE_OK;
}

uint32_t caffeine_material_preset_count(void) {
    return static_cast<uint32_t>(Caffeine::Assets::materialPresets().size());
}

CaffeineResult caffeine_material_preset_at(uint32_t index, CaffeineMaterialPresetInfo* info,
                                               CaffeineMaterial* out) {
    if (!info || !out) return CAFFEINE_INVALID_ARGUMENT;
    const auto& presets = Caffeine::Assets::materialPresets();
    if (index >= presets.size()) return CAFFEINE_NOT_FOUND;
    const Caffeine::Assets::MaterialPreset& preset = presets[index];
    std::memset(info, 0, sizeof(*info));
    copyText(info->category, sizeof(info->category), std::string(preset.category).c_str());
    copyText(info->name, sizeof(info->name), std::string(preset.name).c_str());
    writeMaterial(preset.surface, *out);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_apply_material(CaffeineSession* session, CaffeineEntity entityId,
                                                   const char* path) {
    if (!session || !path) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    auto* filter = session->world.get<Caffeine::ECS::MeshFilterComponent>(entity);
    if (!filter) {
        // Applying a material to an empty entity gives it a visible preview
        // mesh instead of failing with no feedback in the editor.
        filter = &session->world.add<Caffeine::ECS::MeshFilterComponent>(entity);
        filter->primitive = Caffeine::ECS::MeshPrimitive::Cube;
    }
    const std::string projectPath = assetToProjectPath(*session, path);
    filter->customMaterialPath = projectPath;
    auto* renderer = session->world.get<Caffeine::ECS::MeshRendererComponent>(entity);
    if (!renderer) {
        renderer = &session->world.add<Caffeine::ECS::MeshRendererComponent>(entity);
    }
    renderer->materialPath = projectPath;
    Caffeine::Assets::MaterialCache::instance().invalidate(projectPath);
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_bake_ao(CaffeineSession* session, CaffeineEntity entityId,
                                         uint32_t resolution, uint32_t rays, float maxDistance,
                                         const char* outputDirectory, CaffeineAOBakeResult* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;

    Caffeine::Photon::BakeSettings settings{};
    settings.resolution = resolution == 0 ? 512 : resolution;
    settings.rays = rays == 0 ? 16 : rays;
    settings.maxDistance = maxDistance > 0.0f ? maxDistance : 2.5f;
    settings.useGpu = true;
    if (outputDirectory && outputDirectory[0]) {
        settings.outputDirectory = outputDirectory;
    }

    const auto baked = Caffeine::Photon::bakeAmbientOcclusion(
        session->world, entityOf(*session, entityId), settings,
        session->context.projectRootPath, session->renderDevice.get());

    CaffeineAOBakeResult result{};
    result.ok = baked.ok ? 1 : 0;
    result.used_gpu = baked.usedGpu ? 1 : 0;
    result.covered_texels = baked.coveredTexels;
    result.triangles = baked.triangles;
    copyText(result.orm_path, sizeof(result.orm_path), baked.ormPath.c_str());
    copyText(result.material_path, sizeof(result.material_path), baked.materialPath.c_str());
    copyText(result.error, sizeof(result.error), baked.error.c_str());
    *out = result;
    if (baked.ok) session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_bake_mesh_map(CaffeineSession* session, CaffeineEntity entityId,
                                                CaffeineMeshMapKind kind, uint32_t resolution,
                                                const char* outputPath) {
    if (!session || !outputPath || outputPath[0] == '\0' || resolution < 16 ||
        resolution > 4096) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    auto* filter = session->world.get<Caffeine::ECS::MeshFilterComponent>(entity);
    if (!filter) return CAFFEINE_NOT_FOUND;
    Caffeine::Assets::Mesh3D* mesh = nullptr;
    if (filter->primitive == Caffeine::ECS::MeshPrimitive::Custom) {
        if (!filter->customMeshPath.empty()) {
            mesh = Caffeine::Assets::MeshCache::getInstance().getMesh(
                filter->customMeshPath, session->context.projectRootPath);
        }
    } else {
        mesh = Caffeine::Render::GpuProceduralMeshes::get(filter->primitive);
    }
    if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3) {
        return CAFFEINE_NOT_FOUND;
    }

    const Caffeine::Mat4 world = Caffeine::Scene::computeWorldMatrix(session->world, entity);
    const Caffeine::Mat4 normalMatrix = world.inverted().transposed();
    Caffeine::Vec3 boundsMin(1e30f, 1e30f, 1e30f);
    Caffeine::Vec3 boundsMax(-1e30f, -1e30f, -1e30f);
    for (const auto& vertex : mesh->vertices) {
        const Caffeine::Vec3 position = world.transformPoint(vertex.position);
        boundsMin.x = std::min(boundsMin.x, position.x);
        boundsMin.y = std::min(boundsMin.y, position.y);
        boundsMin.z = std::min(boundsMin.z, position.z);
        boundsMax.x = std::max(boundsMax.x, position.x);
        boundsMax.y = std::max(boundsMax.y, position.y);
        boundsMax.z = std::max(boundsMax.z, position.z);
    }
    const Caffeine::Vec3 center = (boundsMin + boundsMax) * 0.5f;
    const Caffeine::Vec3 extent = (boundsMax - boundsMin) * 0.5f;
    const auto encode = [](float value) {
        return static_cast<uint8_t>(
            std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const uint32_t idColor =
        (static_cast<uint32_t>(entityId) * 2654435761u) | 0xff000000u;

    Caffeine::Assets::TextureImage image;
    image.width = resolution;
    image.height = resolution;
    image.rgba.assign(static_cast<size_t>(resolution) * resolution * 4, 0);
    image.valid = true;

    for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const auto& va = mesh->vertices[mesh->indices[i + 0]];
        const auto& vb = mesh->vertices[mesh->indices[i + 1]];
        const auto& vc = mesh->vertices[mesh->indices[i + 2]];
        const Caffeine::Vec3 a = world.transformPoint(va.position);
        const Caffeine::Vec3 b = world.transformPoint(vb.position);
        const Caffeine::Vec3 c = world.transformPoint(vc.position);
        const Caffeine::Vec2 uv0 = va.texcoord;
        const Caffeine::Vec2 uv1 = vb.texcoord;
        const Caffeine::Vec2 uv2 = vc.texcoord;
        const float minU = std::min({uv0.x, uv1.x, uv2.x});
        const float maxU = std::max({uv0.x, uv1.x, uv2.x});
        const float minV = std::min({uv0.y, uv1.y, uv2.y});
        const float maxV = std::max({uv0.y, uv1.y, uv2.y});
        if (maxU < 0.0f || minU > 1.0f || maxV < 0.0f || minV > 1.0f) continue;
        if (maxU - minU < 1e-6f || maxV - minV < 1e-6f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(minU * resolution)) - 1);
        const int x1 = std::min(static_cast<int>(resolution) - 1,
                                static_cast<int>(std::ceil(maxU * resolution)) + 1);
        const int y0 = std::max(0, static_cast<int>(std::floor(minV * resolution)) - 1);
        const int y1 = std::min(static_cast<int>(resolution) - 1,
                                static_cast<int>(std::ceil(maxV * resolution)) + 1);
        const float area = (uv1.x - uv0.x) * (uv2.y - uv0.y) -
                           (uv2.x - uv0.x) * (uv1.y - uv0.y);
        if (std::abs(area) < 1e-9f) continue;
        const float invArea = 1.0f / area;
        for (int y = y0; y <= y1; ++y) {
            const float py = (static_cast<float>(y) + 0.5f) / resolution;
            for (int x = x0; x <= x1; ++x) {
                const float px = (static_cast<float>(x) + 0.5f) / resolution;
                const float w1 = ((px - uv0.x) * (uv2.y - uv0.y) -
                                  (uv2.x - uv0.x) * (py - uv0.y)) *
                                 invArea;
                const float w2 = ((uv1.x - uv0.x) * (py - uv0.y) -
                                  (px - uv0.x) * (uv1.y - uv0.y)) *
                                 invArea;
                const float w0 = 1.0f - w1 - w2;
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                const size_t index = (static_cast<size_t>(y) * resolution + x) * 4;
                if (kind == CAFFEINE_MESH_MAP_NORMAL) {
                    Caffeine::Vec3 normal = normalMatrix.transformVector(
                        va.normal * w0 + vb.normal * w1 + vc.normal * w2);
                    if (normal.lengthSquared() < 1e-8f) continue;
                    normal = normal.normalized();
                    image.rgba[index + 0] = encode(normal.x);
                    image.rgba[index + 1] = encode(normal.y);
                    image.rgba[index + 2] = encode(normal.z);
                    image.rgba[index + 3] = 255;
                } else if (kind == CAFFEINE_MESH_MAP_POSITION) {
                    const Caffeine::Vec3 position = a * w0 + b * w1 + c * w2;
                    image.rgba[index + 0] = encode((position.x - center.x) / std::max(extent.x, 1e-4f));
                    image.rgba[index + 1] = encode((position.y - center.y) / std::max(extent.y, 1e-4f));
                    image.rgba[index + 2] = encode((position.z - center.z) / std::max(extent.z, 1e-4f));
                    image.rgba[index + 3] = 255;
                } else {
                    image.rgba[index + 0] = static_cast<uint8_t>(idColor & 0xff);
                    image.rgba[index + 1] = static_cast<uint8_t>((idColor >> 8) & 0xff);
                    image.rgba[index + 2] = static_cast<uint8_t>((idColor >> 16) & 0xff);
                    image.rgba[index + 3] = 255;
                }
            }
        }
    }
    return Caffeine::Assets::saveTextureImage(outputPath, image) ? CAFFEINE_OK : CAFFEINE_ERROR;
}

CaffeineResult caffeine_session_pick_uv(CaffeineSession* session, CaffeineEntity entityId,
                                         float x, float y, float width, float height, float* u,
                                         float* v) {
    if (!session || !u || !v || width <= 0.0f || height <= 0.0f) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    auto* filter = session->world.get<Caffeine::ECS::MeshFilterComponent>(entity);
    if (!filter) return CAFFEINE_NOT_FOUND;
    Caffeine::Assets::Mesh3D* mesh = nullptr;
    if (filter->primitive == Caffeine::ECS::MeshPrimitive::Custom) {
        if (!filter->customMeshPath.empty()) {
            mesh = Caffeine::Assets::MeshCache::getInstance().getMesh(
                filter->customMeshPath, session->context.projectRootPath);
        }
    } else {
        mesh = Caffeine::Render::GpuProceduralMeshes::get(filter->primitive);
    }
    if (!mesh || mesh->vertices.empty() || mesh->indices.size() < 3) {
        return CAFFEINE_NOT_FOUND;
    }

    const Caffeine::Vec3 focus(session->camera.focus.x, session->camera.focus.y,
                               session->camera.focus.z);
    const float distance = std::max(session->camera.distance, 1.0f);
    const Caffeine::Vec3 eye = Caffeine::Editor::editorCameraPosition(
        session->camera.yaw, session->camera.pitch, distance, focus);
    const Caffeine::Vec3 forward = (focus - eye).normalized();
    Caffeine::Vec3 right = forward.cross(Caffeine::Vec3(0.0f, 1.0f, 0.0f));
    if (right.lengthSquared() < 1e-8f) right = Caffeine::Vec3(1.0f, 0.0f, 0.0f);
    right = right.normalized();
    const Caffeine::Vec3 up = right.cross(forward).normalized();
    const float aspect = width / height;
    const float tangent = std::tan(1.0472f * 0.5f);
    const float nx = (2.0f * x / width - 1.0f) * tangent * aspect;
    const float ny = (1.0f - 2.0f * y / height) * tangent;
    const Caffeine::Vec3 direction = (forward + right * nx + up * ny).normalized();

    const Caffeine::Mat4 world = Caffeine::Scene::computeWorldMatrix(session->world, entity);
    float nearest = std::numeric_limits<float>::max();
    Caffeine::Vec2 hitUv{};
    bool hit = false;
    for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const auto& va = mesh->vertices[mesh->indices[i + 0]];
        const auto& vb = mesh->vertices[mesh->indices[i + 1]];
        const auto& vc = mesh->vertices[mesh->indices[i + 2]];
        const Caffeine::Vec3 a = world.transformPoint(va.position);
        const Caffeine::Vec3 b = world.transformPoint(vb.position);
        const Caffeine::Vec3 c = world.transformPoint(vc.position);
        const Caffeine::Vec3 e1 = b - a;
        const Caffeine::Vec3 e2 = c - a;
        const Caffeine::Vec3 pvec = direction.cross(e2);
        const float det = e1.dot(pvec);
        if (std::abs(det) < 1e-8f) continue;
        const float inv = 1.0f / det;
        const Caffeine::Vec3 tvec = eye - a;
        const float bu = tvec.dot(pvec) * inv;
        if (bu < 0.0f || bu > 1.0f) continue;
        const Caffeine::Vec3 qvec = tvec.cross(e1);
        const float bv = direction.dot(qvec) * inv;
        if (bv < 0.0f || bu + bv > 1.0f) continue;
        const float t = e2.dot(qvec) * inv;
        if (t <= 1e-4f || t >= nearest) continue;
        const float bw = 1.0f - bu - bv;
        nearest = t;
        hitUv = va.texcoord * bw + vb.texcoord * bu + vc.texcoord * bv;
        hit = true;
    }
    if (!hit) return CAFFEINE_NOT_FOUND;
    *u = hitUv.x;
    *v = hitUv.y;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_entity_material(const CaffeineSession* session,
                                                    CaffeineEntity entityId, char* buffer,
                                                    size_t buffer_size) {
    if (!session || !buffer || buffer_size == 0) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = *const_cast<CaffeineSession*>(session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const auto* filter = mutableSession.world.get<Caffeine::ECS::MeshFilterComponent>(
        entityOf(mutableSession, entityId));
    if (!filter) return CAFFEINE_NOT_FOUND;
    copyText(buffer, buffer_size, filter->customMaterialPath.c_str());
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_render_material_preview(
    CaffeineSession* session, const CaffeineMaterial* material, uint32_t size, float yaw_degrees,
    float pitch_degrees, uint8_t* pixels, size_t pixel_bytes) {
    return caffeine_session_render_material_preview_ex(
        session, material, size, yaw_degrees, pitch_degrees, CAFFEINE_PREVIEW_SPHERE, nullptr,
        1.0f, pixels, pixel_bytes);
}

CaffeineResult caffeine_session_render_material_preview_ex(
    CaffeineSession* session, const CaffeineMaterial* material, uint32_t size, float yaw_degrees,
    float pitch_degrees, CaffeinePreviewShape shape, const char* environmentPath, float exposure,
    uint8_t* pixels, size_t pixel_bytes) {
    if (!session || !material || !pixels) return CAFFEINE_INVALID_ARGUMENT;
    const uint32_t previewSize = std::clamp(size, 32u, 1024u);
    const size_t required = static_cast<size_t>(previewSize) * previewSize * 4u;
    if (pixel_bytes < required) return CAFFEINE_INVALID_ARGUMENT;
    const uint32_t sceneWidth = session->renderWidth > 0 ? session->renderWidth : previewSize;
    const uint32_t sceneHeight = session->renderHeight > 0 ? session->renderHeight : previewSize;
    if (!session->ensureRender(sceneWidth, sceneHeight)) return CAFFEINE_ERROR;
    if (!session->materialPreview) {
        session->materialPreview = std::make_unique<Caffeine::Render::MaterialPreviewRenderer>();
        if (!session->materialPreview->init(session->renderDevice.get())) {
            session->materialPreview.reset();
            return CAFFEINE_ERROR;
        }
    }

    Caffeine::Render::MaterialPreviewSettings settings = session->previewLight;
    settings.yawDegrees = yaw_degrees;
    settings.pitchDegrees = pitch_degrees;
    settings.showFloor = true;
    settings.probeResolution = 128;
    settings.environmentExposure = std::max(exposure, 0.0f);
    settings.subject = shape == CAFFEINE_PREVIEW_CUBE
                           ? Caffeine::Render::MaterialPreviewSubject::Cube
                           : shape == CAFFEINE_PREVIEW_PLANE
                                 ? Caffeine::Render::MaterialPreviewSubject::Plane
                                 : Caffeine::Render::MaterialPreviewSubject::Sphere;
    if (environmentPath) settings.environmentPath = environmentPath;

    Caffeine::RHI::CommandBuffer* command = session->renderDevice->beginOffscreen();
    if (!command) return CAFFEINE_ERROR;
    const Caffeine::Assets::MaterialSurface surface = readMaterial(*material);
    const bool rendered = session->materialPreview->render(
        command, surface, previewSize, settings, session->context.projectRootPath);
    session->renderDevice->endOffscreen(command);
    if (!rendered || !session->materialPreview->colorTexture()) return CAFFEINE_ERROR;
    std::vector<Caffeine::u8> readback;
    if (!session->renderDevice->readTexture(session->materialPreview->colorTexture(), readback) ||
        readback.size() < required) {
        return CAFFEINE_ERROR;
    }
    std::memcpy(pixels, readback.data(), required);
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_set_material_preview_light(
    CaffeineSession* session, float sun_yaw_degrees, float sun_elevation_degrees,
    float sun_intensity, int shadows) {
    if (!session) return CAFFEINE_INVALID_ARGUMENT;
    session->previewLight.sunYawDegrees = sun_yaw_degrees;
    session->previewLight.sunElevationDegrees = std::clamp(sun_elevation_degrees, 2.0f, 89.0f);
    session->previewLight.sunIntensity = std::clamp(sun_intensity, 0.0f, 8.0f);
    session->previewLight.shadows = shadows != 0;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_render_material_preview_begin(
    CaffeineSession* session, const CaffeineMaterial* material, uint32_t size, float yaw_degrees,
    float pitch_degrees, CaffeinePreviewShape shape, const char* environmentPath, float exposure) {
    if (!session || !material) return CAFFEINE_INVALID_ARGUMENT;
    if (session->previewPending) return CAFFEINE_ERROR;
    const uint32_t previewSize = std::clamp(size, 32u, 1024u);
    const uint32_t sceneWidth = session->renderWidth > 0 ? session->renderWidth : previewSize;
    const uint32_t sceneHeight = session->renderHeight > 0 ? session->renderHeight : previewSize;
    if (!session->ensureRender(sceneWidth, sceneHeight)) return CAFFEINE_ERROR;
    if (!session->materialPreview) {
        session->materialPreview = std::make_unique<Caffeine::Render::MaterialPreviewRenderer>();
        if (!session->materialPreview->init(session->renderDevice.get())) {
            session->materialPreview.reset();
            return CAFFEINE_ERROR;
        }
    }
    Caffeine::Render::MaterialPreviewSettings settings = session->previewLight;
    settings.yawDegrees = yaw_degrees;
    settings.pitchDegrees = pitch_degrees;
    settings.showFloor = true;
    settings.probeResolution = 128;
    settings.environmentExposure = std::max(exposure, 0.0f);
    settings.subject = shape == CAFFEINE_PREVIEW_CUBE
                           ? Caffeine::Render::MaterialPreviewSubject::Cube
                           : shape == CAFFEINE_PREVIEW_PLANE
                                 ? Caffeine::Render::MaterialPreviewSubject::Plane
                                 : Caffeine::Render::MaterialPreviewSubject::Sphere;
    if (environmentPath) settings.environmentPath = environmentPath;
    Caffeine::RHI::CommandBuffer* command = session->renderDevice->beginOffscreen();
    if (!command) return CAFFEINE_ERROR;
    const Caffeine::Assets::MaterialSurface surface = readMaterial(*material);
    const bool rendered = session->materialPreview->render(
        command, surface, previewSize, settings, session->context.projectRootPath);
    session->renderDevice->endOffscreen(command);
    if (!rendered || !session->materialPreview->colorTexture()) return CAFFEINE_ERROR;

    SDL_GPUDevice* device = session->renderDevice->nativeDevice();
    if (!device) return CAFFEINE_ERROR;
    session->releasePreviewReadback();
    const Uint32 requiredBytes = previewSize * previewSize * 4u;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = requiredBytes;
    session->previewTransfer = SDL_CreateGPUTransferBuffer(device, &info);
    if (!session->previewTransfer) return CAFFEINE_ERROR;
    SDL_GPUCommandBuffer* readbackCommand = SDL_AcquireGPUCommandBuffer(device);
    if (!readbackCommand) {
        session->releasePreviewReadback();
        return CAFFEINE_ERROR;
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(readbackCommand);
    SDL_GPUTextureRegion source{};
    source.texture = session->materialPreview->colorTexture()->handle;
    source.w = previewSize;
    source.h = previewSize;
    source.d = 1;
    SDL_GPUTextureTransferInfo destination{};
    destination.transfer_buffer = session->previewTransfer;
    destination.pixels_per_row = previewSize;
    destination.rows_per_layer = previewSize;
    SDL_DownloadFromGPUTexture(copy, &source, &destination);
    SDL_EndGPUCopyPass(copy);
    session->previewFence = SDL_SubmitGPUCommandBufferAndAcquireFence(readbackCommand);
    if (!session->previewFence) {
        SDL_WaitForGPUIdle(device);
        session->releasePreviewReadback();
        return CAFFEINE_ERROR;
    }
    session->previewSize = previewSize;
    session->previewPending = true;
    return CAFFEINE_OK;
}

CaffeineRenderState caffeine_session_render_material_preview_poll(CaffeineSession* session,
                                                                 uint8_t* pixels,
                                                                 size_t pixelBytes) {
    if (!session || !session->previewPending) return CAFFEINE_RENDER_IDLE;
    if (!pixels) return CAFFEINE_RENDER_FAILED;
    const size_t required = static_cast<size_t>(session->previewSize) * session->previewSize * 4u;
    if (pixelBytes < required) return CAFFEINE_RENDER_FAILED;
    SDL_GPUDevice* device = session->renderDevice->nativeDevice();
    if (!device || !SDL_QueryGPUFence(device, session->previewFence)) {
        return CAFFEINE_RENDER_PENDING;
    }
    const void* mapped = SDL_MapGPUTransferBuffer(device, session->previewTransfer, false);
    if (!mapped) {
        session->releasePreviewReadback();
        return CAFFEINE_RENDER_FAILED;
    }
    std::memcpy(pixels, mapped, required);
    SDL_UnmapGPUTransferBuffer(device, session->previewTransfer);
    session->releasePreviewReadback();
    return CAFFEINE_RENDER_READY;
}

CaffeineResult caffeine_texture_generate_map(const char* source_path, const char* output_path,
                                                 CaffeineTextureMapKind kind, float strength,
                                                 int directx, int wrap, float min_value,
                                                 float max_value, int invert, uint32_t radius) {
    if (!source_path || source_path[0] == '\0' || !output_path || output_path[0] == '\0') {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    Caffeine::Assets::TextureImage source;
    if (!Caffeine::Assets::loadTextureImage(source_path, source)) return CAFFEINE_ERROR;

    Caffeine::Assets::TextureImage result;
    switch (kind) {
        case CAFFEINE_TEXTURE_NORMAL:
            result = Caffeine::Assets::generateNormalFromHeight(source, strength, directx != 0,
                                                                wrap != 0);
            break;
        case CAFFEINE_TEXTURE_AO:
            result = Caffeine::Assets::generateAmbientOcclusion(source, strength, radius,
                                                                wrap != 0);
            break;
        case CAFFEINE_TEXTURE_ROUGHNESS:
        case CAFFEINE_TEXTURE_METALLIC:
        case CAFFEINE_TEXTURE_HEIGHT:
            result = Caffeine::Assets::generateScalarFromAlbedo(
                source, static_cast<Caffeine::Assets::TextureMapKind>(kind), min_value, max_value,
                invert != 0);
            break;
        default:
            return CAFFEINE_INVALID_ARGUMENT;
    }
    if (!result.valid || !Caffeine::Assets::saveTextureImage(output_path, result)) {
        return CAFFEINE_ERROR;
    }
    return CAFFEINE_OK;
}

CaffeineResult caffeine_texture_pack_maps(const char* ao_path, const char* roughness_path,
                                              const char* metallic_path, const char* output_path,
                                              int kind) {    if (!output_path || output_path[0] == '\0') return CAFFEINE_INVALID_ARGUMENT;
    if (kind < 0 || kind > 2) return CAFFEINE_INVALID_ARGUMENT;
    if (!ao_path && !roughness_path && !metallic_path) return CAFFEINE_INVALID_ARGUMENT;

    Caffeine::Assets::TextureImage ao;
    Caffeine::Assets::TextureImage roughness;
    Caffeine::Assets::TextureImage metallic;
    if (ao_path && ao_path[0] && !Caffeine::Assets::loadTextureImage(ao_path, ao)) {
        return CAFFEINE_ERROR;
    }
    if (roughness_path && roughness_path[0] &&
        !Caffeine::Assets::loadTextureImage(roughness_path, roughness)) {
        return CAFFEINE_ERROR;
    }
    if (metallic_path && metallic_path[0] &&
        !Caffeine::Assets::loadTextureImage(metallic_path, metallic)) {
        return CAFFEINE_ERROR;
    }
    const Caffeine::Assets::TextureImage* aoPtr = ao.valid ? &ao : nullptr;
    const Caffeine::Assets::TextureImage* roughnessPtr = roughness.valid ? &roughness : nullptr;
    const Caffeine::Assets::TextureImage* metallicPtr = metallic.valid ? &metallic : nullptr;
    Caffeine::Assets::TextureImage packed = Caffeine::Assets::packMaterialMap(
        aoPtr, roughnessPtr, metallicPtr, static_cast<Caffeine::Assets::TexturePackKind>(kind));
    if (!packed.valid || !Caffeine::Assets::saveTextureImage(output_path, packed)) {
        return CAFFEINE_ERROR;
    }
    return CAFFEINE_OK;
}

CaffeineResult caffeine_texture_save_rgba(const char* output_path, uint32_t width,
                                              uint32_t height, const uint8_t* pixels,
                                              size_t pixel_bytes) {
    if (!output_path || output_path[0] == '\0' || !pixels || width == 0 || height == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const size_t required = static_cast<size_t>(width) * height * 4u;
    if (pixel_bytes < required) return CAFFEINE_INVALID_ARGUMENT;
    Caffeine::Assets::TextureImage image;
    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + required);
    image.valid = true;
    return Caffeine::Assets::saveTextureImage(output_path, image) ? CAFFEINE_OK
                                                                   : CAFFEINE_ERROR;
}

CaffeineResult caffeine_texture_save_rgba16(const char* output_path, uint32_t width,
                                                uint32_t height, const uint16_t* pixels,
                                                size_t pixel_bytes) {
    if (!output_path || output_path[0] == '\0' || !pixels || width == 0 || height == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const size_t required = static_cast<size_t>(width) * height * 4u * sizeof(uint16_t);
    if (pixel_bytes < required) return CAFFEINE_INVALID_ARGUMENT;
    return Caffeine::Assets::saveTextureImage16(
               output_path, width, height, reinterpret_cast<const Caffeine::u16*>(pixels))
               ? CAFFEINE_OK
               : CAFFEINE_ERROR;
}

CaffeineResult caffeine_texture_load_thumbnail(const char* input_path, uint32_t maxSize,
                                                uint32_t* width, uint32_t* height,
                                                uint8_t** pixels, size_t* pixelBytes) {
    if (!input_path || input_path[0] == '\0' || !width || !height || !pixels || !pixelBytes) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const uint32_t maxSizeClamped = std::clamp(maxSize, 16u, 512u);
    int sourceWidth = 0;
    int sourceHeight = 0;
    int channels = 0;
    stbi_uc* source = stbi_load(input_path, &sourceWidth, &sourceHeight, &channels, 4);
    bool hdr = false;
    float* sourceHdr = nullptr;
    if (!source) {
        sourceHdr = stbi_loadf(input_path, &sourceWidth, &sourceHeight, &channels, 4);
        hdr = sourceHdr != nullptr;
    }
    if ((!source && !sourceHdr) || sourceWidth <= 0 || sourceHeight <= 0) {
        if (source) stbi_image_free(source);
        if (sourceHdr) stbi_image_free(sourceHdr);
        return CAFFEINE_ERROR;
    }
    const uint32_t longest = static_cast<uint32_t>(std::max(sourceWidth, sourceHeight));
    const uint32_t targetWidth = std::max(
        1u, static_cast<uint32_t>(sourceWidth) * maxSizeClamped / longest);
    const uint32_t targetHeight = std::max(
        1u, static_cast<uint32_t>(sourceHeight) * maxSizeClamped / longest);
    const size_t bytes = static_cast<size_t>(targetWidth) * targetHeight * 4u;
    auto* output = new (std::nothrow) uint8_t[bytes];
    if (!output) {
        if (source) stbi_image_free(source);
        if (sourceHdr) stbi_image_free(sourceHdr);
        return CAFFEINE_ERROR;
    }
    for (uint32_t y = 0; y < targetHeight; ++y) {
        const uint32_t sy = y * static_cast<uint32_t>(sourceHeight) / targetHeight;
        for (uint32_t x = 0; x < targetWidth; ++x) {
            const uint32_t sx = x * static_cast<uint32_t>(sourceWidth) / targetWidth;
            const size_t sourceIndex = (static_cast<size_t>(sy) * sourceWidth + sx) * 4;
            const size_t targetIndex = (static_cast<size_t>(y) * targetWidth + x) * 4;
            if (hdr) {
                for (int channel = 0; channel < 3; ++channel) {
                    const float linear = std::max(sourceHdr[sourceIndex + channel], 0.0f);
                    output[targetIndex + channel] = static_cast<uint8_t>(
                        std::pow(linear, 1.0f / 2.2f) * 255.0f + 0.5f);
                }
                output[targetIndex + 3] = 255;
            } else {
                for (int channel = 0; channel < 4; ++channel) {
                    output[targetIndex + channel] = source[sourceIndex + channel];
                }
            }
        }
    }
    if (source) stbi_image_free(source);
    if (sourceHdr) stbi_image_free(sourceHdr);
    *width = targetWidth;
    *height = targetHeight;
    *pixels = output;
    *pixelBytes = bytes;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_texture_load_rgba(const char* input_path, uint32_t* width,
                                              uint32_t* height, uint8_t** pixels,
                                              size_t* pixel_bytes) {
    if (!input_path || input_path[0] == '\0' || !width || !height || !pixels || !pixel_bytes) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    Caffeine::Assets::TextureImage image;
    if (!Caffeine::Assets::loadTextureImage(input_path, image)) return CAFFEINE_ERROR;
    const size_t bytes = image.rgba.size();
    auto* copy = new (std::nothrow) uint8_t[bytes];
    if (!copy) return CAFFEINE_ERROR;
    std::memcpy(copy, image.rgba.data(), bytes);
    *width = image.width;
    *height = image.height;
    *pixels = copy;
    *pixel_bytes = bytes;
    return CAFFEINE_OK;
}

void caffeine_session_frame_stats(const CaffeineSession* session, CaffeineFrameStats* out) {
    if (!out) return;
    out->delta_time = session ? session->lastDelta : 0.0f;
    out->frame_index = session ? session->frameIndex : 0u;
    out->entity_count = session ? session->world.entityCount() : 0u;
}

}  // extern "C"

// ---------------------------------------------------------------------------------- water

CaffeineResult caffeine_session_set_water(CaffeineSession* session, CaffeineEntity entityId,
                                          const CaffeineWater* water) {
    if (!session || !water) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(*session, entityId);
    auto* fluid = session->world.get<Caffeine::ECS::NaturalFluidComponent>(entity);
    if (!fluid) fluid = &session->world.add<Caffeine::ECS::NaturalFluidComponent>(entity);
    fluid->enabled = water->enabled ? 1 : 0;
    fluid->mode = static_cast<uint8_t>(Caffeine::ECS::NaturalFluidMode::RealisticWater);
    fluid->preset = static_cast<uint8_t>(Caffeine::ECS::NaturalFluidPreset::Water);
    fluid->sizeX = std::clamp(water->size_x, 1.0f, 2000.0f);
    fluid->sizeZ = std::clamp(water->size_z, 1.0f, 2000.0f);
    fluid->waterLevel = water->water_level;
    fluid->depth = std::clamp(water->depth, 0.05f, 4000.0f);
    fluid->surfaceResolution = std::clamp(water->resolution, 8u, 512u);
    fluid->timeScale = std::clamp(water->time_scale, 0.0f, 8.0f);
    fluid->albedoR = water->albedo[0];
    fluid->albedoG = water->albedo[1];
    fluid->albedoB = water->albedo[2];
    fluid->albedoA = std::clamp(water->albedo[3], 0.0f, 1.0f);
    fluid->roughness = std::clamp(water->roughness, 0.02f, 1.0f);
    fluid->foamThreshold = water->foam_threshold;
    fluid->seed = water->seed;
    const uint32_t count = std::min<uint32_t>(water->wave_count, CAFFEINE_WATER_MAX_WAVES);
    fluid->customWaveCount = count;
    for (uint32_t i = 0; i < CAFFEINE_WATER_MAX_WAVES; ++i) {
        float* slot = fluid->customWaves[i];
        if (i < count) {
            const CaffeineWaterWave& w = water->waves[i];
            slot[0] = w.dir_x;
            slot[1] = w.dir_z;
            slot[2] = w.wavelength;
            slot[3] = w.amplitude;
            slot[4] = w.steepness;
            slot[5] = w.phase;
        } else {
            for (int k = 0; k < 6; ++k) slot[k] = 0.0f;
        }
    }
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_water(const CaffeineSession* session, CaffeineEntity entityId,
                                      CaffeineWater* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = const_cast<CaffeineSession&>(*session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(mutableSession, entityId);
    const auto* fluid = session->world.get<Caffeine::ECS::NaturalFluidComponent>(entity);
    if (!fluid) return CAFFEINE_NOT_FOUND;
    *out = CaffeineWater{};
    out->enabled = fluid->enabled;
    out->size_x = fluid->sizeX;
    out->size_z = fluid->sizeZ;
    out->water_level = fluid->waterLevel;
    out->depth = fluid->depth;
    out->resolution = fluid->surfaceResolution;
    out->time_scale = fluid->timeScale;
    out->albedo[0] = fluid->albedoR;
    out->albedo[1] = fluid->albedoG;
    out->albedo[2] = fluid->albedoB;
    out->albedo[3] = fluid->albedoA;
    out->roughness = fluid->roughness;
    out->foam_threshold = fluid->foamThreshold;
    out->seed = fluid->seed;
    out->wave_count = std::min<uint32_t>(fluid->customWaveCount, CAFFEINE_WATER_MAX_WAVES);
    for (uint32_t i = 0; i < out->wave_count; ++i) {
        const float* slot = fluid->customWaves[i];
        out->waves[i] = {slot[0], slot[1], slot[2], slot[3], slot[4], slot[5]};
    }
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_water_height(const CaffeineSession* session,
                                             CaffeineEntity entityId, float x, float z,
                                             float* out_height) {
    if (!session || !out_height) return CAFFEINE_INVALID_ARGUMENT;
    if (!entityAlive(const_cast<CaffeineSession&>(*session), entityId)) return CAFFEINE_NOT_FOUND;
    const auto* world = Caffeine::Scene::naturalFluidWorldFor(
        const_cast<Caffeine::ECS::World&>(session->world), entityId);
    if (!world) return CAFFEINE_NOT_FOUND;
    *out_height = world->surfaceHeight(x, z);
    return CAFFEINE_OK;
}

// -------------------------------------------------------------------------------- terrain

CaffeineResult caffeine_session_set_terrain(CaffeineSession* session, CaffeineEntity entityId,
                                            const CaffeineTerrain* terrain, const float* heights,
                                            const uint8_t* splat_rgba) {
    using namespace Caffeine;
    if (!session || !terrain || !heights) return CAFFEINE_INVALID_ARGUMENT;
    if (terrain->resolution_x < 8 || terrain->resolution_z < 8 || terrain->resolution_x > 4097 ||
        terrain->resolution_z > 4097) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    if (!entityAlive(*session, entityId)) return CAFFEINE_NOT_FOUND;
    const ECS::Entity entity = entityOf(*session, entityId);
    auto* component = session->world.get<ECS::TerrainComponent>(entity);
    if (!component) component = &session->world.add<ECS::TerrainComponent>(entity);
    component->resolutionX = terrain->resolution_x;
    component->resolutionZ = terrain->resolution_z;
    component->worldSizeX = std::clamp(terrain->size_x, 1.0f, 20000.0f);
    component->worldSizeZ = std::clamp(terrain->size_z, 1.0f, 20000.0f);
    component->maxHeight = std::clamp(terrain->max_height, 0.1f, 8000.0f);
    component->castShadows = terrain->cast_shadows != 0;
    // The renderer draws terrains from the mesh-filter pass: give the entity the components it
    // looks for (the terrain mesh itself comes from the terrain cache).
    if (!session->world.get<ECS::MeshFilterComponent>(entity)) {
        session->world.add<ECS::MeshFilterComponent>(entity);
    }
    if (!session->world.get<ECS::MeshRendererComponent>(entity)) {
        session->world.add<ECS::MeshRendererComponent>(entity);
    }
    // Adding components can move the entity's data: fetch the terrain component again.
    component = session->world.get<ECS::TerrainComponent>(entity);
    if (!component) return CAFFEINE_ERROR;

    auto& cache = Terrain::TerrainCache::instance();
    cache.syncEntity(session->world, entity);
    Terrain::TerrainHeightmap* map = cache.heightmapFor(entity);
    if (!map) return CAFFEINE_ERROR;
    const size_t count = static_cast<size_t>(terrain->resolution_x) * terrain->resolution_z;
    if (map->heights().size() != count) return CAFFEINE_ERROR;
    for (size_t i = 0; i < count; ++i) map->heights()[i] = std::clamp(heights[i], 0.0f, 1.0f);

    if (splat_rgba) {
        if (Terrain::TerrainSplatmap* splat = cache.splatmapFor(entity)) {
            const u32 sx = splat->resolutionX();
            const u32 sz = splat->resolutionZ();
            const u32 rx = terrain->resolution_x;
            const u32 rz = terrain->resolution_z;
            for (u32 z = 0; z < sz; ++z) {
                for (u32 x = 0; x < sx; ++x) {
                    const u32 gx = std::min(rx - 1, static_cast<u32>(
                        (static_cast<f32>(x) + 0.5f) / static_cast<f32>(sx) * static_cast<f32>(rx)));
                    const u32 gz = std::min(rz - 1, static_cast<u32>(
                        (static_cast<f32>(z) + 0.5f) / static_cast<f32>(sz) * static_cast<f32>(rz)));
                    const uint8_t* p = splat_rgba + (static_cast<size_t>(gz) * rx + gx) * 4;
                    Vec4 w(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f);
                    const f32 sum = w.x + w.y + w.z + w.w;
                    if (sum > 1e-5f) {
                        w = Vec4(w.x / sum, w.y / sum, w.z / sum, w.w / sum);
                    } else {
                        w = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
                    }
                    splat->set(x, z, w);
                }
            }
        }
        component->splatRevision++;
    }
    component->dataRevision++;
    cache.syncEntity(session->world, entity);
    session->context.markDirty();
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_terrain(const CaffeineSession* session, CaffeineEntity entityId,
                                        CaffeineTerrain* out) {
    if (!session || !out) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = const_cast<CaffeineSession&>(*session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const auto* component = mutableSession.world.get<Caffeine::ECS::TerrainComponent>(
        entityOf(mutableSession, entityId));
    if (!component) return CAFFEINE_NOT_FOUND;
    out->resolution_x = component->resolutionX;
    out->resolution_z = component->resolutionZ;
    out->size_x = component->worldSizeX;
    out->size_z = component->worldSizeZ;
    out->max_height = component->maxHeight;
    out->cast_shadows = component->castShadows ? 1 : 0;
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_terrain_height(const CaffeineSession* session,
                                               CaffeineEntity entityId, float x, float z,
                                               float* out_height) {
    if (!session || !out_height) return CAFFEINE_INVALID_ARGUMENT;
    CaffeineSession& mutableSession = const_cast<CaffeineSession&>(*session);
    if (!entityAlive(mutableSession, entityId)) return CAFFEINE_NOT_FOUND;
    const Caffeine::ECS::Entity entity = entityOf(mutableSession, entityId);
    const auto* component = mutableSession.world.get<Caffeine::ECS::TerrainComponent>(entity);
    const auto* map = Caffeine::Terrain::TerrainCache::instance().heightmapFor(entity);
    if (!component || !map || map->empty()) return CAFFEINE_NOT_FOUND;
    const float u = std::clamp(x / component->worldSizeX, 0.0f, 1.0f);
    const float v = std::clamp(z / component->worldSizeZ, 0.0f, 1.0f);
    *out_height = map->sampleBilinear(u, v) * component->maxHeight;
    return CAFFEINE_OK;
}
