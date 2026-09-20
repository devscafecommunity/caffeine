#pragma once

#include "editor/EditorContext.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "scene/SceneComponents.hpp"
#include "script/ScriptTypes.hpp"

#include <sstream>
#include <string>

namespace Caffeine::Editor {

inline std::string exportEntitySnapshot(ECS::World& world, ECS::Entity e) {
    std::ostringstream out;
    out << "# Entity snapshot: " << getEntityName(world, e) << " (id=" << e.id() << ")\n";

    if (auto* t = world.get<ECS::Transform>(e)) {
        out << "transform.position = {" << t->position.x << ", " << t->position.y << ", "
            << t->position.z << "}\n";
        out << "transform.rotation = {" << t->rotation.x << ", " << t->rotation.y << ", "
            << t->rotation.z << "}\n";
        out << "transform.scale = {" << t->scale.x << ", " << t->scale.y << ", " << t->scale.z
            << "}\n";
    }
    if (auto* p = world.get<ECS::Position3D>(e)) {
        out << "position3d = {" << p->position.x << ", " << p->position.y << ", " << p->position.z
            << "}\n";
    }
    if (auto* r = world.get<ECS::Rotation3D>(e)) {
        out << "rotation3d = {" << r->quaternion.x << ", " << r->quaternion.y << ", "
            << r->quaternion.z << ", " << r->quaternion.w << "}\n";
    }
    if (auto* s = world.get<ECS::Scale3D>(e)) {
        out << "scale3d = {" << s->scale.x << ", " << s->scale.y << ", " << s->scale.z << "}\n";
    }
    if (auto* wt = world.get<Scene::WorldTransform>(e)) {
        const Vec3 wp = wt->matrix.transformPoint(Vec3(0, 0, 0));
        out << "world_position = {" << wp.x << ", " << wp.y << ", " << wp.z << "}\n";
    }
    if (auto* pc = world.get<Scene::Parent>(e)) {
        out << "parent_id = " << pc->parent.id() << "\n";
    }
    if (auto* cam2 = world.get<ECS::Camera2DComponent>(e)) {
        out << "camera2d.zoom = " << cam2->zoom << "\n";
    }
    if (auto* cam3 = world.get<ECS::Camera3DComponent>(e)) {
        out << "camera3d.fov = " << cam3->fov << "\n";
        out << "camera3d.near = " << cam3->nearClip << "\n";
        out << "camera3d.far = " << cam3->farClip << "\n";
    }
    if (auto* mf = world.get<ECS::MeshFilterComponent>(e)) {
        out << "mesh.primitive = " << static_cast<int>(mf->primitive) << "\n";
        if (!mf->customMeshPath.empty()) out << "mesh.path = \"" << mf->customMeshPath << "\"\n";
    }
#ifdef CF_HAS_SCRIPTING
    if (auto* sc = world.get<Script::ScriptComponent>(e)) {
        if (!sc->scriptPath.empty()) out << "script = \"" << sc->scriptPath << "\"\n";
    }
#endif

    return out.str();
}

}  // namespace Caffeine::Editor
