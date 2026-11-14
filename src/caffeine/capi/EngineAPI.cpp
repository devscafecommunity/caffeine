#include "caffeine/capi/EngineAPI.h"

#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/World.hpp"
#include "math/Mat4.hpp"
#include "math/Quat.hpp"
#include "render/GpuSceneRenderer.hpp"
#include "rhi/RenderDevice.hpp"
#include "scene/EditorCameraMath.hpp"
#include "scene/SceneComponents.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <vector>
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
        shutdownRender();
    }

    void shutdownRender() {
        if (sceneRenderer) sceneRenderer->shutdown();
        if (renderDevice && colorTarget) renderDevice->destroyTexture(colorTarget);
        colorTarget = nullptr;
        renderWidth = 0;
        renderHeight = 0;
        if (renderDevice) renderDevice->shutdown();
        sceneRenderer.reset();
        renderDevice.reset();
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

CaffeineResult caffeine_session_render(CaffeineSession* session, uint32_t width,
                                       uint32_t height, uint8_t* pixels,
                                       size_t pixel_bytes) {
    if (!session || !pixels || width == 0 || height == 0) {
        return CAFFEINE_INVALID_ARGUMENT;
    }
    const size_t required = static_cast<size_t>(width) * height * 4u;
    if (pixel_bytes < required) return CAFFEINE_INVALID_ARGUMENT;
    if (!session->ensureRender(width, height)) return CAFFEINE_ERROR;

    Caffeine::Vec3 min(std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max(),
                       std::numeric_limits<float>::max());
    Caffeine::Vec3 max(-std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max(),
                       -std::numeric_limits<float>::max());
    bool any = false;

    Caffeine::ECS::ComponentQuery query;
    query.with<Caffeine::Scene::WorldTransform>();
    session->world.forEach<Caffeine::Scene::WorldTransform>(
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

    if (!session->cameraInitialized) {
        session->camera.yaw = 0.65f;
        session->camera.pitch = 0.35f;
        session->camera.distance = std::max(radius * 2.5f, 5.0f);
        session->camera.focus = {center.x, center.y, center.z};
        session->cameraInitialized = true;
    }
    const Caffeine::Vec3 focus(session->camera.focus.x, session->camera.focus.y,
                               session->camera.focus.z);
    const float distance = std::max(session->camera.distance, 1.0f);
    const Caffeine::Vec3 cameraPosition = Caffeine::Editor::editorCameraPosition(
        session->camera.yaw, session->camera.pitch, distance, focus);
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

    Caffeine::RHI::CommandBuffer* command = session->renderDevice->beginOffscreen();
    if (!command) return CAFFEINE_ERROR;
    session->sceneRenderer->renderWithCamera(
        command, session->world, camera, session->colorTarget, nullptr, width, height,
        session->context.projectRootPath, options);
    session->renderDevice->endOffscreen(command);

    std::vector<uint8_t> readback;
    if (!session->renderDevice->readTexture(session->colorTarget, readback)) {
        return CAFFEINE_ERROR;
    }
    const size_t copySize = std::min(readback.size(), required);
    std::memcpy(pixels, readback.data(), copySize);
    return CAFFEINE_OK;
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
