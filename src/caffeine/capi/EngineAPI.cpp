#include "caffeine/capi/EngineAPI.h"

#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/World.hpp"
#include "math/Quat.hpp"
#include "scene/EditorContext.hpp"
#include "scene/EditorSceneSerializer.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

struct CaffeineSession {
    Caffeine::ECS::World world;
    Caffeine::Editor::EditorContext context;
    std::string scenePath;
    float lastDelta = 0.0f;
    uint64_t frameIndex = 0;
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

void caffeine_session_frame_stats(const CaffeineSession* session, CaffeineFrameStats* out) {
    if (!out) return;
    out->delta_time = session ? session->lastDelta : 0.0f;
    out->frame_index = session ? session->frameIndex : 0u;
    out->entity_count = session ? session->world.entityCount() : 0u;
}

}  // extern "C"
