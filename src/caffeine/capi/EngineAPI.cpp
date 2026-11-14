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
#include "rhi/RenderDevice.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "physics/PhysicsComponents3D.hpp"
#include "scene/EditorCameraMath.hpp"
#include "scene/SceneComponents.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "script/ScriptTypes.hpp"
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
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".gif") {
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
    Caffeine::RHI::Texture* colorTarget = nullptr;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    CaffeineCamera camera{0.65f, 0.35f, 10.0f, {0.0f, 0.0f, 0.0f}};
    bool cameraInitialized = false;
    std::string assetRoot;
    std::vector<AssetEntry> assets;

    // Async CPU readback: submit a download copy and poll its fence instead of
    // blocking the UI thread in SDL_WaitForGPUIdle.
    SDL_GPUTransferBuffer* readbackTransfer = nullptr;
    SDL_GPUFence* readbackFence = nullptr;
    uint32_t readbackWidth = 0;
    uint32_t readbackHeight = 0;
    bool readbackPending = false;

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
        releaseReadback();
        shutdownRender();
    }

    void releaseReadback() {
        if (!renderDevice) {
            readbackTransfer = nullptr;
            readbackFence = nullptr;
            readbackPending = false;
            return;
        }
        if (readbackFence) {
            SDL_ReleaseGPUFence(renderDevice->nativeDevice(), readbackFence);
            readbackFence = nullptr;
        }
        if (readbackTransfer) {
            SDL_ReleaseGPUTransferBuffer(renderDevice->nativeDevice(), readbackTransfer);
            readbackTransfer = nullptr;
        }
        readbackPending = false;
        readbackWidth = 0;
        readbackHeight = 0;
    }

    void shutdownRender() {
        if (sceneRenderer) sceneRenderer->shutdown();
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
            {vec4Field<ECS::LightComponent>("Color", &ECS::LightComponent::color, 0.0f, 1.0f),
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

bool readRotation(CaffeineSession& session, Caffeine::ECS::Entity entity,
                  CaffeineVec3* out) {
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
    if (auto* transform = session.world.get<Caffeine::ECS::Transform>(entity)) {
        transform->rotation =
            Caffeine::Vec3(euler.x, euler.y, euler.z);
        return;
    }
    const Caffeine::Quat quat =
        Caffeine::Quat::fromEuler(euler.x * kDegToRad, euler.y * kDegToRad,
                                  euler.z * kDegToRad)
            .normalized();
    if (auto* rotation = session.world.get<Caffeine::ECS::Rotation3D>(entity)) {
        rotation->quaternion =
            Caffeine::Vec4(quat.x, quat.y, quat.z, quat.w);
        return;
    }
    if (session.world.has<Caffeine::ECS::Position3D>(entity)) {
        auto& rotation = session.world.add<Caffeine::ECS::Rotation3D>(entity);
        rotation.quaternion = Caffeine::Vec4(quat.x, quat.y, quat.z, quat.w);
        return;
    }
    auto& transform = session.world.add<Caffeine::ECS::Transform>(entity);
    transform.rotation = Caffeine::Vec3(euler.x, euler.y, euler.z);
}

void writeScale(CaffeineSession& session, Caffeine::ECS::Entity entity,
                CaffeineVec3 scale) {
    if (auto* transform = session.world.get<Caffeine::ECS::Transform>(entity)) {
        transform->scale = Caffeine::Vec3(scale.x, scale.y, scale.z);
        return;
    }
    if (auto* scale3d = session.world.get<Caffeine::ECS::Scale3D>(entity)) {
        scale3d->scale = Caffeine::Vec3(scale.x, scale.y, scale.z);
        return;
    }
    if (session.world.has<Caffeine::ECS::Position3D>(entity) ||
        session.world.has<Caffeine::ECS::Rotation3D>(entity)) {
        auto& scale3d = session.world.add<Caffeine::ECS::Scale3D>(entity);
        scale3d.scale = Caffeine::Vec3(scale.x, scale.y, scale.z);
        return;
    }
    auto& transform = session.world.add<Caffeine::ECS::Transform>(entity);
    transform.scale = Caffeine::Vec3(scale.x, scale.y, scale.z);
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

void caffeine_session_tick(CaffeineSession* session, float delta_time) {
    if (!session) return;
    session->lastDelta = delta_time;
    ++session->frameIndex;
}

static bool renderFrame(CaffeineSession& session, uint32_t width, uint32_t height) {
    if (!session.ensureRender(width, height)) return false;

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
    const float farClip = std::max(1000.0f, distance * 20.0f);

    Caffeine::Render::GpuSceneCamera camera;
    camera.position = cameraPosition;
    camera.focus = focus;
    camera.view = Caffeine::Mat4::lookAt(cameraPosition, focus,
                                         Caffeine::Vec3(0.0f, 1.0f, 0.0f));
    camera.proj = Caffeine::Mat4::perspective(1.0472f, aspect, 0.1f, farClip);
    camera.fovRad = 1.0472f;
    camera.nearClip = 0.1f;
    camera.farClip = farClip;

    Caffeine::Render::GpuSceneRenderOptions options;
    options.enableShadows = false;
    options.renderScale = 1.0f;
    options.deltaTime = 1.0f / 60.0f;
    options.viewId = 1;
    options.resolveFeaturesFromScene = true;
    options.resolveEnvironmentFromScene = true;
    options.resolvePostProcessFromScene = false;
    options.postProcess = Caffeine::Render::PostProcessStack::defaults();
    options.postProcess.antiAliasing.enabled = false;

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
    return CAFFEINE_OK;
}

CaffeineResult caffeine_session_render_begin(CaffeineSession* session, uint32_t width,
                                             uint32_t height) {
    if (!session || width == 0 || height == 0) return CAFFEINE_INVALID_ARGUMENT;
    if (session->readbackPending) return CAFFEINE_ERROR;
    if (!renderFrame(*session, width, height)) return CAFFEINE_ERROR;

    session->releaseReadback();
    SDL_GPUDevice* device = session->renderDevice->nativeDevice();
    if (!device || !session->colorTarget) return CAFFEINE_ERROR;

    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = static_cast<Uint32>(width) * height * 4u;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &info);
    if (!transfer) return CAFFEINE_ERROR;

    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
    if (!command) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
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
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_WaitForGPUIdle(device);
        return CAFFEINE_ERROR;
    }

    session->readbackTransfer = transfer;
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
        return CAFFEINE_RENDER_FAILED;
    }
    std::memcpy(pixels, mapped, required);
    SDL_UnmapGPUTransferBuffer(device, session->readbackTransfer);
    session->releaseReadback();
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

void caffeine_session_frame_stats(const CaffeineSession* session, CaffeineFrameStats* out) {
    if (!out) return;
    out->delta_time = session ? session->lastDelta : 0.0f;
    out->frame_index = session ? session->frameIndex : 0u;
    out->entity_count = session ? session->world.entityCount() : 0u;
}

}  // extern "C"
