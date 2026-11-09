#include "scene/Camera3DSystem.hpp"

#include "ecs/CameraComponents.hpp"
#include "ecs/Components3D.hpp"
#include "math/Math.hpp"
#include "math/Quat.hpp"
#include "render/Camera3D.hpp"

namespace Caffeine::Scene {

void updateCamera3DControllers(ECS::World& world, f64 dt, const Camera3DInput& input) {
    const f32 delta = static_cast<f32>(dt);

    ECS::ComponentQuery query;
    query.with<ECS::Camera3DControllerComponent>().with<ECS::Position3D>().with<ECS::Rotation3D>();

    world.forEach<ECS::Camera3DControllerComponent, ECS::Position3D, ECS::Rotation3D>(
        query, [&](ECS::Entity entity, ECS::Camera3DControllerComponent& ctrl, ECS::Position3D& pos,
                   ECS::Rotation3D& rot) {
            if (ctrl.mode == ECS::Camera3DControlMode::None) return;

            const auto* active = world.get<ECS::CameraActiveComponent>(entity);
            if (active && active->is2D) return;

            Render::Camera3D camera;
            if (const auto* cam3 = world.get<ECS::Camera3DComponent>(entity)) {
                camera.setFOV(cam3->fov);
                camera.setAspect(cam3->aspectRatio);
                camera.setNearFar(cam3->nearClip, cam3->farClip);
            }
            camera.setPosition(pos.position);
            camera.setRotation(Quat(rot.quaternion.x, rot.quaternion.y, rot.quaternion.z,
                                    rot.quaternion.w));

            switch (ctrl.mode) {
            case ECS::Camera3DControlMode::Orbit:
                camera.setOrbitTarget(ctrl.orbitTarget);
                camera.setOrbitDistance(ctrl.orbitDistance);
                if (input.mouseDeltaX != 0.0f || input.mouseDeltaY != 0.0f) {
                    camera.orbit(input.mouseDeltaX * ctrl.mouseSensitivity,
                                 -input.mouseDeltaY * ctrl.mouseSensitivity);
                }
                if (input.zoomDelta != 0.0f) {
                    camera.zoom(input.zoomDelta);
                }
                ctrl.orbitDistance = (camera.position() - ctrl.orbitTarget).length();
                break;

            case ECS::Camera3DControlMode::FPS:
                if (input.mouseDeltaX != 0.0f || input.mouseDeltaY != 0.0f) {
                    camera.rotateFPS(-input.mouseDeltaY * ctrl.mouseSensitivity,
                                     input.mouseDeltaX * ctrl.mouseSensitivity);
                }
                if (input.moveX != 0.0f || input.moveY != 0.0f || input.moveZ != 0.0f) {
                    camera.moveFPS({input.moveX * ctrl.moveSpeed * delta,
                                    input.moveY * ctrl.moveSpeed * delta,
                                    input.moveZ * ctrl.moveSpeed * delta});
                }
                break;

            case ECS::Camera3DControlMode::Follow:
                if (ctrl.followTarget.isValid()) {
                    camera.follow(ctrl.followTarget, ctrl.followOffset, ctrl.followSmoothing);
                    camera.update(dt, world);
                }
                break;

            default:
                break;
            }

            pos.position = camera.position();
            const Quat q = camera.rotation();
            rot.quaternion = Vec4(q.x, q.y, q.z, q.w);
        });
}


}  // namespace Caffeine::Scene
