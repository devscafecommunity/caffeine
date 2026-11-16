#include "editor/EntityPresetFactories.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/EntityPresetUtils.hpp"
#include "editor/PrefabSystem.hpp"
#include "ecs/Components.hpp"
#include "ecs/LightComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "ui/UIComponents.hpp"

namespace Caffeine::Editor {
namespace {

EntityPresetSpawnResult spawn2DCharacter(ECS::World& world, EditorContext& ctx,
                                         const std::filesystem::path& projectRoot,
                                         const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float speed = wizard.getFloat("move_speed", 220.0f);
    const float jump = wizard.getFloat("jump_force", 420.0f);
    const float gravity = wizard.getFloat("gravity", 1600.0f);
    const int extraJumps = wizard.getInt("air_jumps", 1);
    const bool withCamera = wizard.getBool("include_camera", true);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Transform>(root);
    world.add<ECS::Sprite>(root);
    auto& rb = world.add<Physics2D::RigidBody2D>(root);
    rb.isKinematic = false;
    rb.lockRotation = true;
    Physics2D::Collider2D col;
    col.shape = Physics2D::ColliderShape::Circle;
    col.radius = 24.0f;
    col.size = {48.0f, 96.0f};
    world.add<Physics2D::Collider2D>(root, col);

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/player_2d.lua";
    const std::string script = R"(-- 2D platformer: coyote, jump buffer, air jumps, sprint
speed = )" + std::to_string(speed) + R"(
airControl = 0.65
sprintMultiplier = 1.45
jumpForce = )" + std::to_string(jump) + R"(
gravity = )" + std::to_string(gravity) + R"(
fallGravity = 1.55
maxFall = -1400.0
airJumps = )" + std::to_string(extraJumps) + R"(
coyoteTime = 0.12
jumpBuffer = 0.12
groundY = 0.0
vy = 0.0
onGround = true
jumpsLeft = 0
coyote = 0.0
buffer = 0.0
facing = 1.0
health = 100.0
invuln = 0.0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    groundY = t.y
    jumpsLeft = airJumps
    caffeine.debug.log("2D player: WASD/arrows, Space jump, Shift sprint")
end

function onUpdate(entity, dt)
    local moveX = caffeine.input.getAxis("Horizontal")
    local sprint = caffeine.input.isKeyDown("LShift") or caffeine.input.isKeyDown("RShift")
    local wantJump = caffeine.input.isKeyDown("Space") or caffeine.input.isActionDown("Jump")
    if wantJump then buffer = jumpBuffer else buffer = math.max(0.0, buffer - dt) end

    local t = caffeine.world.getTransform(entity)
    local control = onGround and 1.0 or airControl
    local sp = speed * (sprint and sprintMultiplier or 1.0) * control
    t.x = t.x + moveX * sp * dt
    if math.abs(moveX) > 0.1 then facing = (moveX > 0) and 1.0 or -1.0 end
    t.scaleX = math.abs(t.scaleX) * facing

    if onGround then
        coyote = coyoteTime
        jumpsLeft = airJumps
    else
        coyote = math.max(0.0, coyote - dt)
    end

    local canJump = onGround or coyote > 0.0 or jumpsLeft > 0
    if buffer > 0.0 and canJump then
        local fromGround = onGround or coyote > 0.0
        vy = jumpForce
        onGround = false
        coyote = 0.0
        buffer = 0.0
        if not fromGround then jumpsLeft = math.max(0, jumpsLeft - 1) end
    end

    local g = gravity
    if vy < 0.0 then g = gravity * fallGravity end
    if not wantJump and vy > 0.0 then g = gravity * 2.2 end
    vy = vy - g * dt
    if vy < maxFall then vy = maxFall end
    t.y = t.y + vy * dt
    if t.y <= groundY then
        t.y = groundY
        vy = 0.0
        onGround = true
    else
        onGround = false
    end

    invuln = math.max(0.0, invuln - dt)
    caffeine.world.setTransform(entity, t)
    caffeine.events.emit("player_pose", t.x, t.y, t.z, entity)
    caffeine.events.emit("hud_stats", health, 0)
end
)";
    const std::string cppBody =
        "    float m_speed = " + std::to_string(speed) + "f;\n"
        "    float m_jump = " + std::to_string(jump) + "f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        if (auto* tf = world.get<Caffeine::ECS::Transform>(entity)) {\n"
        "            // TODO: read input and apply m_speed / jump using physics.\n"
        "            (void)dt;\n"
        "            (void)tf;\n"
        "        }\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource("Player2DScript", "", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/Player2DScript.hpp", "Player2DScript", cppSrc);
#endif

    if (withCamera) {
        ECS::Entity cam = EntityPresetUtils::makeCamera2D(world, "Camera", true);
        if (auto* camTransform = world.get<ECS::Transform>(cam)) {
            camTransform->position = Vec3(0.0f, 48.0f, 0.0f);
        }
        EntityPresetUtils::parentEntity(world, cam, root);
    }

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("2D player base created. Assign a sprite and customize the generated script.");
    return result;
}

EntityPresetSpawnResult spawnFppPlayer(ECS::World& world, EditorContext& ctx,
                                       const std::filesystem::path& projectRoot,
                                       const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float speed = wizard.getFloat("move_speed", 6.0f);
    const float sensitivity = wizard.getFloat("mouse_sensitivity", 0.12f);
    const float eyeHeight = wizard.getFloat("eye_height", 1.65f);
    const float jump = wizard.getFloat("jump_force", 7.5f);
    const float gravity = wizard.getFloat("gravity", 22.0f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Position3D>(root);
    world.add<ECS::Rotation3D>(root);
    world.add<ECS::Scale3D>(root);

    ECS::Entity body = EntityPresetUtils::make3DPrimitive(world, "Body", ECS::MeshPrimitive::Capsule,
                                                            Vec3(0, 1.0f, 0), Vec3(0.8f, 1.0f, 0.8f));
    EntityPresetUtils::parentEntity(world, body, root);

    ECS::Entity camera = EntityPresetUtils::makeCamera3D(world, "Camera", true);
    if (auto* p = world.get<ECS::Position3D>(camera)) p->position = Vec3(0, eyeHeight, 0);
    if (auto* active = world.get<ECS::CameraActiveComponent>(camera)) active->is2D = false;
    EntityPresetUtils::parentEntity(world, camera, root);

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/player_fpp.lua";
    const std::string script = R"(-- First-person: look, sprint stamina, jump, crouch
moveSpeed = )" + std::to_string(speed) + R"(
sprintMultiplier = 1.85
crouchMultiplier = 0.45
mouseSensitivity = )" + std::to_string(sensitivity) + R"(
jumpForce = )" + std::to_string(jump) + R"(
gravity = )" + std::to_string(gravity) + R"(
stamina = 1.0
staminaDrain = 0.35
staminaRegen = 0.22
yaw = 0.0
pitch = 0.0
vy = 0.0
groundY = 0.0
onGround = true
crouching = false
health = 100.0
bob = 0.0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    groundY = t.y
    caffeine.debug.log("FPP: WASD, mouse look, Shift sprint, Ctrl crouch, Space jump")
end

function onUpdate(entity, dt)
    local mx = caffeine.input.getAxis("Horizontal")
    local mz = caffeine.input.getAxis("Vertical")
    yaw = yaw + caffeine.input.getAxis("LookX") * mouseSensitivity
    pitch = math.max(-1.35, math.min(1.35, pitch - caffeine.input.getAxis("LookY") * mouseSensitivity))

    crouching = caffeine.input.isKeyDown("LCtrl") or caffeine.input.isKeyDown("RCtrl")
    local sprinting = (caffeine.input.isKeyDown("LShift") or caffeine.input.isKeyDown("RShift")) and stamina > 0.05 and not crouching
    if sprinting and (math.abs(mx) + math.abs(mz)) > 0.1 then
        stamina = math.max(0.0, stamina - staminaDrain * dt)
    else
        stamina = math.min(1.0, stamina + staminaRegen * dt)
    end

    local speed = moveSpeed
    if sprinting then speed = speed * sprintMultiplier end
    if crouching then speed = speed * crouchMultiplier end

    local cs = math.cos(yaw)
    local sn = math.sin(yaw)
    local fx, fz = -sn, -cs
    local rx, rz = cs, -sn
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + (rx * mx + fx * mz) * speed * dt
    t.z = t.z + (rz * mx + fz * mz) * speed * dt

    if (caffeine.input.isKeyDown("Space") or caffeine.input.isActionDown("Jump")) and onGround then
        vy = jumpForce
        onGround = false
    end
    vy = vy - gravity * dt
    t.y = t.y + vy * dt
    if t.y <= groundY then
        t.y = groundY
        vy = 0.0
        onGround = true
    end
    if crouching then t.y = t.y - 0.35 end

    bob = bob + math.sqrt(mx * mx + mz * mz) * speed * dt * 8.0
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, yaw, pitch)
    caffeine.events.emit("player_pose", t.x, t.y, t.z, entity)
    caffeine.events.emit("hud_stats", health, stamina)
end
)";
    const std::string cppBody =
        "    float m_moveSpeed = " + std::to_string(speed) + "f;\n"
        "    float m_mouseSensitivity = " + std::to_string(sensitivity) + "f;\n"
        "    float m_yaw = 0.0f;\n"
        "    float m_pitch = 0.0f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "        // TODO: apply WASD movement and mouse look using m_moveSpeed / m_mouseSensitivity.\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource(
        "FppPlayerScript", "#include \"ecs/Components3D.hpp\"\n", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/FppPlayerScript.hpp", "FppPlayerScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("FPP rig created with body + camera child.");
    return result;
}

EntityPresetSpawnResult spawnThirdPersonPlayer(ECS::World& world, EditorContext& ctx,
                                               const std::filesystem::path& projectRoot,
                                               const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float speed = wizard.getFloat("move_speed", 5.0f);
    const float distance = wizard.getFloat("camera_distance", 6.0f);
    const bool orbital = wizard.getEnumLabel("camera_mode", "Follow") == "Orbital";

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Position3D>(root);
    world.add<ECS::Rotation3D>(root);
    world.add<ECS::Scale3D>(root);

    ECS::Entity body = EntityPresetUtils::make3DPrimitive(world, "Body", ECS::MeshPrimitive::Capsule,
                                                            Vec3(0, 1.0f, 0), Vec3(0.8f, 1.0f, 0.8f));
    EntityPresetUtils::parentEntity(world, body, root);

    ECS::Entity rig = world.create();
    setEntityName(world, rig, "CameraRig");
    world.add<ECS::Position3D>(rig, ECS::Position3D{Vec3(0, 1.6f, 0)});
    world.add<ECS::Rotation3D>(rig);
    world.add<ECS::Scale3D>(rig);
    EntityPresetUtils::parentEntity(world, rig, root);

    ECS::Entity camera = EntityPresetUtils::makeCamera3D(world, "Camera", true);
    if (auto* p = world.get<ECS::Position3D>(camera)) p->position = Vec3(0, 0.4f, distance);
    if (auto* active = world.get<ECS::CameraActiveComponent>(camera)) active->is2D = false;
    EntityPresetUtils::parentEntity(world, camera, rig);

#ifdef CF_HAS_SCRIPTING
    const std::string relPlayer = "scripts/presets/player_third_person.lua";
    const std::string playerScript = R"(-- Third-person: camera-relative move, face velocity, sprint, jump
moveSpeed = )" + std::to_string(speed) + R"(
sprintMultiplier = 1.7
jumpForce = 7.0
gravity = 22.0
yaw = 0.0
vy = 0.0
groundY = 0.0
onGround = true
health = 100.0
turnSharpness = 10.0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    groundY = t.y
    caffeine.debug.log("3rd-person: WASD, mouse orbit (rig), Space jump, Shift sprint")
end

function onUpdate(entity, dt)
    local mx = caffeine.input.getAxis("Horizontal")
    local mz = caffeine.input.getAxis("Vertical")
    yaw = yaw + caffeine.input.getAxis("LookX") * 0.08
    local sprint = caffeine.input.isKeyDown("LShift") or caffeine.input.isKeyDown("RShift")
    local speed = moveSpeed * (sprint and sprintMultiplier or 1.0)
    local cs = math.cos(yaw)
    local sn = math.sin(yaw)
    local fx, fz = -sn, -cs
    local rx, rz = cs, -sn
    local t = caffeine.world.getTransform(entity)
    local vx = (rx * mx + fx * mz)
    local vz = (rz * mx + fz * mz)
    t.x = t.x + vx * speed * dt
    t.z = t.z + vz * speed * dt
    if math.abs(vx) + math.abs(vz) > 0.05 then
        local face = math.atan(vx, vz)
        caffeine.world.setYawPitch(entity, face, 0.0)
    end
    if caffeine.input.isKeyDown("Space") and onGround then
        vy = jumpForce
        onGround = false
    end
    vy = vy - gravity * dt
    t.y = t.y + vy * dt
    if t.y <= groundY then
        t.y = groundY
        vy = 0.0
        onGround = true
    end
    caffeine.world.setTransform(entity, t)
    caffeine.events.emit("player_pose", t.x, t.y, t.z, entity)
    caffeine.events.emit("hud_stats", health, 0)
end
)";
    const std::string playerCppBody =
        "    float m_moveSpeed = " + std::to_string(speed) + "f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        if (auto* pos = world.get<Caffeine::ECS::Position3D>(entity)) {\n"
        "            (void)pos; (void)dt;\n"
        "            // TODO: read input and move on XZ plane.\n"
        "        }\n"
        "    }";
    const std::string playerCppSrc = EntityPresetUtils::makeCppScriptSource(
        "ThirdPersonPlayerScript", "#include \"ecs/Components3D.hpp\"\n", playerCppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        relPlayer, playerScript, "scripts/presets/ThirdPersonPlayerScript.hpp",
        "ThirdPersonPlayerScript", playerCppSrc);

    const std::string relCam = "scripts/presets/camera_third_person.lua";
    const std::string camScript = R"(-- Third-person camera: smooth orbit, zoom, pitch clamp
cameraDistance = )" + std::to_string(distance) + R"(
orbitalMode = )" + std::string(orbital ? "true" : "false") + R"(
yaw = 0.0
pitch = 0.28
zoom = cameraDistance
minZoom = math.max(2.0, cameraDistance * 0.4)
maxZoom = cameraDistance * 2.2
smooth = 12.0

function onCreate(entity)
    caffeine.debug.log("Camera rig: mouse orbit, Q/E zoom")
end

function onUpdate(entity, dt)
    yaw = yaw + caffeine.input.getAxis("LookX") * 0.09
    pitch = math.max(0.08, math.min(1.25, pitch + caffeine.input.getAxis("LookY") * 0.055))
    if caffeine.input.isKeyDown("Q") then zoom = math.min(maxZoom, zoom + 8.0 * dt) end
    if caffeine.input.isKeyDown("E") then zoom = math.max(minZoom, zoom - 8.0 * dt) end
    caffeine.world.setYawPitch(entity, yaw, pitch)
    local t = caffeine.world.getTransform(entity)
    if orbitalMode then
        t.rotation = yaw * 57.2958
    end
    t.y = 1.6
    caffeine.world.setTransform(entity, t)
end
)";
    const std::string camCppBody =
        "    float m_cameraDistance = " + std::to_string(distance) + "f;\n"
        "    bool m_orbitalMode = " + std::string(orbital ? "true" : "false") + ";\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "        // TODO: implement follow or orbital camera using m_orbitalMode.\n"
        "    }";
    const std::string camCppSrc = EntityPresetUtils::makeCppScriptSource(
        "ThirdPersonCameraScript", "#include \"ecs/Components3D.hpp\"\n", camCppBody);
    EntityPresetUtils::attachPresetScript(world, rig, projectRoot, wizard, result,
        relCam, camScript, "scripts/presets/ThirdPersonCameraScript.hpp",
        "ThirdPersonCameraScript", camCppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back(orbital ? "3rd-person rig with orbital camera script." :
                                           "3rd-person rig with follow camera script.");
    return result;
}

EntityPresetSpawnResult spawnVehiclePlayer(ECS::World& world, EditorContext& ctx,
                                           const std::filesystem::path& projectRoot,
                                           const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float maxSpeed = wizard.getFloat("max_speed", 24.0f);
    const float turnRate = wizard.getFloat("turn_rate", 90.0f);
    const float accel = wizard.getFloat("acceleration", 18.0f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 0.75f, 0), Vec3(2.0f, 1.0f, 4.0f));

    ECS::Entity chaseRig = world.create();
    setEntityName(world, chaseRig, "ChaseCameraRig");
    world.add<ECS::Position3D>(chaseRig, ECS::Position3D{Vec3(0, 0, 0)});
    world.add<ECS::Rotation3D>(chaseRig);
    world.add<ECS::Scale3D>(chaseRig);
    EntityPresetUtils::parentEntity(world, chaseRig, root);

    ECS::Entity camera = EntityPresetUtils::makeCamera3D(world, "ChaseCamera", true);
    if (auto* p = world.get<ECS::Position3D>(camera)) p->position = Vec3(0, 1.2f, -12.0f);
    if (auto* active = world.get<ECS::CameraActiveComponent>(camera)) active->is2D = false;
    EntityPresetUtils::parentEntity(world, camera, chaseRig);

#ifdef CF_HAS_SCRIPTING
    const std::string relCam = "scripts/presets/vehicle_chase_camera.lua";
    const std::string camScript = R"(-- Vehicle chase camera: orbit yaw/pitch, Q/E zoom
cameraDistance = 12.0
cameraHeight = 5.0
yaw = 0.0
pitch = 0.28
minZoom = 7.0
maxZoom = 18.0

function onCreate(entity)
    caffeine.debug.log("Vehicle chase cam: mouse look, Q/E zoom")
end

function onUpdate(entity, dt)
    yaw = yaw + caffeine.input.getAxis("LookX") * 0.055
    pitch = math.max(0.1, math.min(0.55, pitch + caffeine.input.getAxis("LookY") * 0.04))
    if caffeine.input.isKeyDown("Q") then cameraDistance = math.min(maxZoom, cameraDistance + 10.0 * dt) end
    if caffeine.input.isKeyDown("E") then cameraDistance = math.max(minZoom, cameraDistance - 10.0 * dt) end
    caffeine.world.setYawPitch(entity, yaw, pitch)
    local t = caffeine.world.getTransform(entity)
    t.y = cameraHeight
    caffeine.world.setTransform(entity, t)
end
)";
    const std::string chaseCamCpp = EntityPresetUtils::makeCppScriptSource(
        "VehicleChaseCameraScript", "#include \"ecs/Components3D.hpp\"\n",
        "    float m_distance = 12.0f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "    }\n");
    EntityPresetUtils::attachPresetScript(world, chaseRig, projectRoot, wizard, result,
        relCam, camScript, "scripts/presets/VehicleChaseCameraScript.hpp",
        "VehicleChaseCameraScript", chaseCamCpp);

    const std::string rel = "scripts/presets/vehicle_player.lua";
    const std::string script = R"(-- Arcade vehicle: accel, reverse, grip, handbrake
maxSpeed = )" + std::to_string(maxSpeed) + R"(
turnRate = )" + std::to_string(turnRate) + R"(
acceleration = )" + std::to_string(accel) + R"(
brake = 28.0
friction = 6.0
yaw = 0.0
speed = 0.0
health = 100.0

function onCreate(entity)
    caffeine.debug.log("Vehicle: W/S throttle, A/D steer, Space handbrake")
end

function onUpdate(entity, dt)
    local throttle = caffeine.input.getAxis("Vertical")
    local steer = caffeine.input.getAxis("Horizontal")
    local handbrake = caffeine.input.isKeyDown("Space")
    if throttle > 0.05 then
        speed = math.min(maxSpeed, speed + acceleration * throttle * dt)
    elseif throttle < -0.05 then
        speed = math.max(-maxSpeed * 0.4, speed - acceleration * 0.7 * dt)
    else
        local decel = friction * (handbrake and 3.5 or 1.0)
        if speed > 0 then speed = math.max(0, speed - decel * dt)
        elseif speed < 0 then speed = math.min(0, speed + decel * dt) end
    end
    local steerScale = math.min(1.0, math.abs(speed) / math.max(4.0, maxSpeed * 0.35))
    if handbrake then steerScale = steerScale * 1.6 end
    yaw = yaw + steer * turnRate * 0.0174533 * steerScale * dt * (speed >= 0 and 1 or -1)
    local fx, fz = -math.sin(yaw), -math.cos(yaw)
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + fx * speed * dt
    t.z = t.z + fz * speed * dt
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, yaw, 0.0)
    caffeine.events.emit("player_pose", t.x, t.y, t.z, entity)
    caffeine.events.emit("hud_stats", health, math.abs(speed) / maxSpeed)
end
)";
    const std::string cppBody =
        "    float m_maxSpeed = " + std::to_string(maxSpeed) + "f;\n"
        "    float m_turnRate = " + std::to_string(turnRate) + "f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        if (auto* pos = world.get<Caffeine::ECS::Position3D>(entity)) {\n"
        "            (void)pos; (void)dt;\n"
        "            // TODO: arcade throttle/steer using m_maxSpeed and m_turnRate.\n"
        "        }\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource(
        "VehiclePlayerScript", "#include \"ecs/Components3D.hpp\"\n", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/VehiclePlayerScript.hpp", "VehiclePlayerScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Vehicle base with chase camera created.");
    return result;
}

EntityPresetSpawnResult spawnNpc(ECS::World& world, EditorContext& ctx,
                                 const std::filesystem::path& projectRoot,
                                 const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float patrolRadius = wizard.getFloat("patrol_radius", 4.0f);
    const bool hasDialog = wizard.getBool("enable_dialog", true);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Capsule,
                                                          Vec3(0, 1.0f, 0), Vec3(0.7f, 1.0f, 0.7f));

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/npc_basic.lua";
    const std::string script = R"(-- NPC: waypoint patrol, idle pause, greet player
patrolRadius = )" + std::to_string(patrolRadius) + R"(
dialogEnabled = )" + std::string(hasDialog ? "true" : "false") + R"(
walkSpeed = 1.6
idleTime = 1.4
greetRange = 3.5
originX, originZ = 0, 0
angle = 0
pause = 0.0
state = "patrol"
saidHello = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originX, originZ = t.x, t.z
    caffeine.debug.log("NPC patrol and greet. Press E when close")
end

function onUpdate(entity, dt)
    local t = caffeine.world.getTransform(entity)
    if state == "idle" then
        pause = pause - dt
        if pause <= 0.0 then state = "patrol" end
    else
        angle = angle + dt * (walkSpeed / math.max(1.0, patrolRadius))
        t.x = originX + math.cos(angle) * patrolRadius
        t.z = originZ + math.sin(angle) * patrolRadius
        caffeine.world.setYawPitch(entity, angle + 1.5708, 0.0)
        if math.floor(angle) ~= math.floor(angle - dt * 0.5) and (math.floor(angle) % 4 == 0) then
            state = "idle"
            pause = idleTime
        end
    end
    caffeine.world.setTransform(entity, t)

    local player = caffeine.world.findNearest(entity, greetRange)
    if dialogEnabled and player ~= 0 then
        if caffeine.input.isActionPressed("Interact") or caffeine.input.isKeyDown("E") then
            caffeine.debug.log("NPC: Hello traveler.")
            caffeine.events.emit("npc_talk", entity, player)
            saidHello = true
        elseif not saidHello then
            caffeine.debug.log("NPC: Press E to talk")
            saidHello = true
        end
    elseif player == 0 then
        saidHello = false
    end
end
)";
    const std::string cppBody =
        "    float m_patrolRadius = " + std::to_string(patrolRadius) + "f;\n"
        "    bool m_dialogEnabled = " + std::string(hasDialog ? "true" : "false") + ";\n"
        "    float m_originX = 0.0f;\n"
        "    float m_originZ = 0.0f;\n"
        "    float m_angle = 0.0f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        if (auto* pos = world.get<Caffeine::ECS::Position3D>(entity)) {\n"
        "            m_originX = pos->position.x;\n"
        "            m_originZ = pos->position.z;\n"
        "        }\n"
        "        (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        m_angle += dt * 0.5f;\n"
        "        if (auto* pos = world.get<Caffeine::ECS::Position3D>(entity)) {\n"
        "            pos->position.x = m_originX + std::cos(m_angle) * m_patrolRadius;\n"
        "            pos->position.z = m_originZ + std::sin(m_angle) * m_patrolRadius;\n"
        "        }\n"
        "        (void)m_dialogEnabled;\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource(
        "NpcBasicScript", "#include \"ecs/Components3D.hpp\"\n#include <cmath>\n", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/NpcBasicScript.hpp", "NpcBasicScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("NPC with patrol script created.");
    return result;
}

EntityPresetSpawnResult spawnEnemy(ECS::World& world, EditorContext& ctx,
                                   const std::filesystem::path& projectRoot,
                                   const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float health = wizard.getFloat("health", 100.0f);
    const float damage = wizard.getFloat("damage", 10.0f);
    const float chaseRange = wizard.getFloat("chase_range", 12.0f);
    const float attackRange = wizard.getFloat("attack_range", 2.2f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Sphere,
                                                          Vec3(0, 1.0f, 0), Vec3(1.2f, 1.2f, 1.2f));

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/enemy_basic.lua";
    const std::string script = R"(-- Enemy: wander, aggro, chase, attack cooldown, leashed return
health = )" + std::to_string(health) + R"(
damage = )" + std::to_string(damage) + R"(
chaseRange = )" + std::to_string(chaseRange) + R"(
attackRange = )" + std::to_string(attackRange) + R"(
attackCooldown = 0.9
moveSpeed = 3.4
leash = chaseRange * 1.6
originX, originY, originZ = 0, 0, 0
angle = 0
state = "wander"
cool = 0.0
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originX, originY, originZ = t.x, t.y, t.z
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Enemy AI ready health=" .. tostring(health))
end

function onUpdate(entity, dt)
    cool = math.max(0.0, cool - dt)
    local t = caffeine.world.getTransform(entity)
    local dx, dz = px - t.x, pz - t.z
    local dist = math.sqrt(dx * dx + dz * dz)
    local homeDx, homeDz = t.x - originX, t.z - originZ
    local homeDist = math.sqrt(homeDx * homeDx + homeDz * homeDz)

    if homeDist > leash then
        state = "return"
    elseif hasPlayer and dist < chaseRange then
        state = (dist < attackRange) and "attack" or "chase"
    else
        state = "wander"
    end

    if state == "wander" then
        angle = angle + dt * 0.7
        t.x = originX + math.cos(angle) * math.min(chaseRange * 0.2, 2.5)
        t.z = originZ + math.sin(angle) * math.min(chaseRange * 0.2, 2.5)
        caffeine.world.setYawPitch(entity, angle + 1.5708, 0.0)
    elseif state == "chase" or state == "attack" then
        local inv = 1.0 / math.max(0.05, dist)
        t.x = t.x + dx * inv * moveSpeed * dt
        t.z = t.z + dz * inv * moveSpeed * dt
        caffeine.world.setYawPitch(entity, math.atan(dx, dz), 0.0)
        if state == "attack" and cool <= 0.0 then
            cool = attackCooldown
            caffeine.events.emit("player_hit", damage)
            caffeine.debug.log("Enemy hit for " .. tostring(damage))
        end
    else
        local inv = 1.0 / math.max(0.05, homeDist)
        t.x = t.x - homeDx * inv * moveSpeed * 1.2 * dt
        t.z = t.z - homeDz * inv * moveSpeed * 1.2 * dt
        if homeDist < 0.4 then state = "wander" end
    end
    caffeine.world.setTransform(entity, t)
    if health <= 0 then
        caffeine.debug.log("Enemy down")
        caffeine.world.destroy(entity)
    end
end
)";
    const std::string cppBody =
        "    float m_health = " + std::to_string(health) + "f;\n"
        "    float m_damage = " + std::to_string(damage) + "f;\n"
        "    float m_chaseRange = " + std::to_string(chaseRange) + "f;\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "        // TODO: detect player within m_chaseRange and apply m_damage.\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource("EnemyBasicScript", "", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/EnemyBasicScript.hpp", "EnemyBasicScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Enemy base with combat script created.");
    return result;
}

EntityPresetSpawnResult spawnItem(ECS::World& world, EditorContext& ctx,
                                  const std::filesystem::path& projectRoot,
                                  const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const int value = wizard.getInt("value", 1);
    const bool autoPickup = wizard.getBool("auto_pickup", true);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Sphere,
                                                          Vec3(0, 0.5f, 0), Vec3(0.35f, 0.35f, 0.35f));

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/item_collectible.lua";
    const std::string script = R"(-- Collectible: bob, spin, pickup radius, respawn optional
value = )" + std::to_string(value) + R"(
autoPickup = )" + std::string(autoPickup ? "true" : "false") + R"(
pickupRange = 1.6
spin = 0.0
baseY = 0.0
taken = false
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    baseY = t.y
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Collectible value=" .. tostring(value))
end

function onUpdate(entity, dt)
    if taken then return end
    spin = spin + dt * 2.4
    local t = caffeine.world.getTransform(entity)
    t.y = baseY + math.sin(spin) * 0.18
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, spin, 0.35)
    if not hasPlayer then return end
    local dx, dy, dz = t.x - px, t.y - py, t.z - pz
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    local want = autoPickup or caffeine.input.isActionPressed("Interact") or caffeine.input.isKeyDown("E")
    if dist < pickupRange and want then
        taken = true
        caffeine.events.emit("item_collected", value, entity)
        caffeine.debug.log("Picked up +" .. tostring(value))
        caffeine.world.destroy(entity)
    end
end
)";
    const std::string cppBody =
        "    int m_value = " + std::to_string(value) + ";\n"
        "    bool m_autoPickup = " + std::string(autoPickup ? "true" : "false") + ";\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "        // TODO: pickup when player is in range if m_autoPickup.\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource("ItemCollectibleScript", "", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/ItemCollectibleScript.hpp", "ItemCollectibleScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Collectible item created.");
    return result;
}

EntityPresetSpawnResult spawnDestructible(ECS::World& world, EditorContext& ctx,
                                          const std::filesystem::path& projectRoot,
                                          const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float health = wizard.getFloat("health", 50.0f);
    const bool explode = wizard.getBool("explode_on_destroy", false);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 0.5f, 0), Vec3(1.0f, 1.0f, 1.0f));

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/destructible.lua";
    const std::string script = R"(-- Destructible: proximity damage, flash scale, optional explode
health = )" + std::to_string(health) + R"(
maxHealth = health
explodeOnDestroy = )" + std::string(explode ? "true" : "false") + R"(
hurtRange = 2.4
destroyed = false
flash = 0.0
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Destructible health=" .. tostring(health) .. " — Attack when close")
end

function onUpdate(entity, dt)
    if destroyed then return end
    flash = math.max(0.0, flash - dt * 3.0)
    local t = caffeine.world.getTransform(entity)
    local s = 1.0 + flash * 0.25
    t.scaleX, t.scaleY = s, s
    caffeine.world.setTransform(entity, t)
    if hasPlayer then
        local dx, dy, dz = t.x - px, t.y - py, t.z - pz
        local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if dist < hurtRange and (caffeine.input.isActionPressed("Attack") or caffeine.input.isMouseButtonDown(1)) then
            health = health - 15.0
            flash = 1.0
            caffeine.debug.log("Crate " .. tostring(math.max(0, health)) .. "/" .. tostring(maxHealth))
        end
    end
    if health <= 0 then
        destroyed = true
        if explodeOnDestroy then
            caffeine.events.emit("explosion", t.x, t.y, t.z, 4.0)
            caffeine.debug.log("Destructible exploded")
        else
            caffeine.debug.log("Destructible destroyed")
        end
        caffeine.world.destroy(entity)
    end
end
)";
    const std::string cppBody =
        "    float m_health = " + std::to_string(health) + "f;\n"
        "    bool m_explodeOnDestroy = " + std::string(explode ? "true" : "false") + ";\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        if (m_health <= 0.0f) {\n"
        "            (void)entity; (void)world; (void)dt;\n"
        "            // TODO: spawn VFX / destroy entity when m_explodeOnDestroy.\n"
        "        }\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource("DestructibleScript", "", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/DestructibleScript.hpp", "DestructibleScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Destructible object created.");
    return result;
}

EntityPresetSpawnResult spawnGameHud(ECS::World& world, EditorContext& ctx,
                                     const std::filesystem::path& projectRoot,
                                     const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const bool showHealth = wizard.getBool("show_health_bar", true);
    const bool showScore = wizard.getBool("show_score", true);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    UI::UIWidget canvas;
    canvas.type = UI::UIWidgetType::Canvas;
    canvas.computedRect = {{0.0f, 0.0f}, {1280.0f, 720.0f}};
    world.add<UI::UIWidget>(root, canvas);

    u32 healthBarId = 0;
    u32 scoreLabelId = 0;

    if (showHealth) {
        ECS::Entity healthBar = world.create();
        setEntityName(world, healthBar, "HealthBar");
        UI::UIWidget barWidget;
        barWidget.type = UI::UIWidgetType::ProgressBar;
        barWidget.interactable = false;
        barWidget.parentId = root.id();
        barWidget.transform.anchorMin = {0.0f, 1.0f};
        barWidget.transform.anchorMax = {0.0f, 1.0f};
        barWidget.transform.offsetMin = {16.0f, -40.0f};
        barWidget.transform.offsetMax = {236.0f, -16.0f};
        world.add<UI::UIWidget>(healthBar, barWidget);
        UI::UIProgressBar pb;
        pb.minValue = 0.0f;
        pb.maxValue = 100.0f;
        pb.currentValue = 100.0f;
        pb.showText = true;
        world.add<UI::UIProgressBar>(healthBar, pb);
        EntityPresetUtils::parentUIWidget(world, healthBar, root);
        healthBarId = healthBar.id();
    }

    if (showScore) {
        ECS::Entity score = world.create();
        setEntityName(world, score, "ScoreLabel");
        UI::UIWidget labelWidget;
        labelWidget.type = UI::UIWidgetType::Label;
        labelWidget.interactable = false;
        labelWidget.parentId = root.id();
        labelWidget.transform.anchorMin = {1.0f, 1.0f};
        labelWidget.transform.anchorMax = {1.0f, 1.0f};
        labelWidget.transform.offsetMin = {-220.0f, -40.0f};
        labelWidget.transform.offsetMax = {-16.0f, -16.0f};
        world.add<UI::UIWidget>(score, labelWidget);
        UI::UILabel lbl;
        lbl.text = "Score: 0";
        world.add<UI::UILabel>(score, lbl);
        EntityPresetUtils::parentUIWidget(world, score, root);
        scoreLabelId = score.id();
    }

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/game_hud.lua";
    const std::string script = R"(-- Game HUD: listen to player stats and pickups
showHealth = )" + std::string(showHealth ? "true" : "false") + R"(
showScore = )" + std::string(showScore ? "true" : "false") + R"(
healthBarId = )" + std::to_string(healthBarId) + R"(
scoreLabelId = )" + std::to_string(scoreLabelId) + R"(
score = 0
health = 100
stamina = 1.0
pulse = 0.0

function onCreate(entity)
    caffeine.events.on("hud_stats", function(h, s)
        if h ~= nil then health = h end
        if s ~= nil then stamina = s end
    end)
    caffeine.events.on("item_collected", function(value)
        score = score + (value or 1)
        caffeine.debug.log("Score " .. tostring(score))
    end)
    caffeine.events.on("player_hit", function(dmg)
        health = math.max(0, health - (dmg or 10))
        pulse = 0.4
        caffeine.debug.log("HP " .. tostring(health))
    end)
    caffeine.events.on("player_heal", function(amt)
        health = math.min(100, health + (amt or 25))
        caffeine.debug.log("Healed to " .. tostring(health))
    end)
    caffeine.debug.log("HUD listening for hud_stats / item_collected / player_hit")
end

function onUpdate(entity, dt)
    pulse = math.max(0.0, pulse - dt)
    if showHealth and healthBarId > 0 then
        caffeine.ui.setProgress(healthBarId, health / 100.0)
    end
    if showScore and scoreLabelId > 0 then
        caffeine.ui.setLabel(scoreLabelId, "Score: " .. tostring(score))
    end
end
)";
    const std::string cppBody =
        "    bool m_showHealth = " + std::string(showHealth ? "true" : "false") + ";\n"
        "    bool m_showScore = " + std::string(showScore ? "true" : "false") + ";\n\n"
        "    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "        // TODO: update HUD widgets using m_showHealth / m_showScore.\n"
        "    }";
    const std::string cppSrc = EntityPresetUtils::makeCppScriptSource("GameHudScript", "", cppBody);
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        rel, script, "scripts/presets/GameHudScript.hpp", "GameHudScript", cppSrc);
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("HUD canvas with starter widgets created.");
    return result;
}

std::string stubCpp(const char* className, const char* extraIncludes, const std::string& members) {
    const std::string body = members +
        "\n    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {\n"
        "        (void)entity; (void)world;\n"
        "    }\n"
        "    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {\n"
        "        (void)entity; (void)world; (void)dt;\n"
        "    }\n";
    return EntityPresetUtils::makeCppScriptSource(className, extraIncludes ? extraIncludes : "", body);
}

EntityPresetSpawnResult spawnTopDownPlayer(ECS::World& world, EditorContext& ctx,
                                           const std::filesystem::path& projectRoot,
                                           const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float speed = wizard.getFloat("move_speed", 7.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Position3D>(root, ECS::Position3D{Vec3(0, 0, 0)});
    world.add<ECS::Rotation3D>(root);
    world.add<ECS::Scale3D>(root);
    ECS::Entity body = EntityPresetUtils::make3DPrimitive(world, "Body", ECS::MeshPrimitive::Capsule,
                                                          Vec3(0, 1.0f, 0), Vec3(0.8f, 1.0f, 0.8f));
    EntityPresetUtils::parentEntity(world, body, root);
    ECS::Entity camera = EntityPresetUtils::makeCamera3D(world, "OverheadCamera", true);
    if (auto* p = world.get<ECS::Position3D>(camera)) p->position = Vec3(0, 14.0f, 8.0f);
    if (auto* cam = world.get<ECS::Camera3DComponent>(camera)) cam->fov = 50.0f;
    EntityPresetUtils::parentEntity(world, camera, root);
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Top-down twin-stick: move, face look, dash
moveSpeed = )" + std::to_string(speed) + R"(
dashSpeed = 18.0
dashTime = 0.18
dashCool = 0.7
yaw = 0.0
dashT = 0.0
cool = 0.0
health = 100.0

function onCreate(entity)
    caffeine.debug.log("Top-down: WASD move, mouse look, Space dash")
end

function onUpdate(entity, dt)
    local mx = caffeine.input.getAxis("Horizontal")
    local mz = caffeine.input.getAxis("Vertical")
    yaw = yaw + caffeine.input.getAxis("LookX") * 0.1
    cool = math.max(0.0, cool - dt)
    if dashT <= 0.0 and cool <= 0.0 and caffeine.input.isKeyDown("Space") then
        dashT = dashTime
        cool = dashCool
    end
    local speed = moveSpeed
    if dashT > 0.0 then
        speed = dashSpeed
        dashT = dashT - dt
    end
    local cs, sn = math.cos(yaw), math.sin(yaw)
    local fx, fz = -sn, -cs
    local rx, rz = cs, -sn
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + (rx * mx + fx * mz) * speed * dt
    t.z = t.z + (rz * mx + fz * mz) * speed * dt
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, yaw, 0.0)
    caffeine.events.emit("player_pose", t.x, t.y, t.z, entity)
    caffeine.events.emit("hud_stats", health, dashT > 0 and 1 or (1.0 - cool / dashCool))
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/player_topdown.lua", script,
        "scripts/presets/TopDownPlayerScript.hpp", "TopDownPlayerScript",
        stubCpp("TopDownPlayerScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_speed = " + std::to_string(speed) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Top-down player with overhead camera and dash.");
    return result;
}

EntityPresetSpawnResult spawnFlyingSpectator(ECS::World& world, EditorContext& ctx,
                                             const std::filesystem::path& projectRoot,
                                             const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float speed = wizard.getFloat("fly_speed", 12.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::makeCamera3D(world, wizard.entityName.c_str(), true);
    if (auto* p = world.get<ECS::Position3D>(root)) p->position = Vec3(0, 4.0f, 8.0f);
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Free-fly camera
flySpeed = )" + std::to_string(speed) + R"(
fastMul = 3.0
yaw = 3.14159
pitch = -0.25

function onCreate(entity)
    caffeine.debug.log("Fly cam: WASD, Q/E up/down, Shift fast, mouse look")
end

function onUpdate(entity, dt)
    yaw = yaw + caffeine.input.getAxis("LookX") * 0.1
    pitch = math.max(-1.4, math.min(1.4, pitch - caffeine.input.getAxis("LookY") * 0.08))
    local speed = flySpeed
    if caffeine.input.isKeyDown("LShift") then speed = speed * fastMul end
    local cs, sn = math.cos(yaw), math.sin(yaw)
    local fx, fz = -sn, -cs
    local rx, rz = cs, -sn
    local lift = 0.0
    if caffeine.input.isKeyDown("E") or caffeine.input.isKeyDown("Space") then lift = 1.0 end
    if caffeine.input.isKeyDown("Q") or caffeine.input.isKeyDown("LCtrl") then lift = lift - 1.0 end
    local mx = caffeine.input.getAxis("Horizontal")
    local mz = caffeine.input.getAxis("Vertical")
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + (rx * mx + fx * mz) * speed * dt
    t.y = t.y + lift * speed * dt
    t.z = t.z + (rz * mx + fz * mz) * speed * dt
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, yaw, pitch)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/flying_camera.lua", script,
        "scripts/presets/FlyingCameraScript.hpp", "FlyingCameraScript",
        stubCpp("FlyingCameraScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_speed = " + std::to_string(speed) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Free-fly spectator camera created.");
    return result;
}

EntityPresetSpawnResult spawnTurret(ECS::World& world, EditorContext& ctx,
                                    const std::filesystem::path& projectRoot,
                                    const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float range = wizard.getFloat("range", 16.0f);
    const float fireRate = wizard.getFloat("fire_rate", 2.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cylinder,
                                                          Vec3(0, 0.6f, 0), Vec3(1.2f, 1.2f, 1.2f));
    ECS::Entity barrel = EntityPresetUtils::make3DPrimitive(world, "Barrel", ECS::MeshPrimitive::Cube,
                                                            Vec3(0, 1.1f, 0.8f), Vec3(0.25f, 0.25f, 1.6f));
    EntityPresetUtils::parentEntity(world, barrel, root);
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Auto-turret: track player_pose, fire cooldown
range = )" + std::to_string(range) + R"(
fireRate = )" + std::to_string(fireRate) + R"(
damage = 8.0
cool = 0.0
px, py, pz = 0, 0, 0
hasPlayer = false
yaw = 0.0

function onCreate(entity)
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Turret tracking players in range " .. tostring(range))
end

function onUpdate(entity, dt)
    cool = math.max(0.0, cool - dt)
    if not hasPlayer then return end
    local t = caffeine.world.getTransform(entity)
    local dx, dz = px - t.x, pz - t.z
    local dist = math.sqrt(dx * dx + dz * dz)
    if dist > range then return end
    yaw = math.atan(dx, dz)
    caffeine.world.setYawPitch(entity, yaw, 0.0)
    if cool <= 0.0 then
        cool = 1.0 / math.max(0.2, fireRate)
        caffeine.events.emit("player_hit", damage)
        caffeine.debug.log("Turret fired")
    end
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/turret.lua", script,
        "scripts/presets/TurretScript.hpp", "TurretScript",
        stubCpp("TurretScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_range = " + std::to_string(range) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Turret with barrel child and tracking script.");
    return result;
}

EntityPresetSpawnResult spawnMovingPlatform(ECS::World& world, EditorContext& ctx,
                                            const std::filesystem::path& projectRoot,
                                            const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float distance = wizard.getFloat("travel", 6.0f);
    const float period = wizard.getFloat("period", 4.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 0.25f, 0), Vec3(3.0f, 0.35f, 3.0f));
#ifdef CF_HAS_SCRIPTING
    const std::string axis = wizard.getEnumLabel("axis", "X");
    const std::string script = R"(-- Ping-pong platform
travel = )" + std::to_string(distance) + R"(
period = )" + std::to_string(period) + R"(
axis = ")" + axis + R"("
ox, oy, oz = 0, 0, 0
time = 0.0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    ox, oy, oz = t.x, t.y, t.z
    caffeine.debug.log("Moving platform axis=" .. axis)
end

function onUpdate(entity, dt)
    time = time + dt
    local u = (math.sin(time * 6.28318 / math.max(0.2, period)) + 1.0) * 0.5
    local t = caffeine.world.getTransform(entity)
    t.x, t.y, t.z = ox, oy, oz
    if axis == "Y" then t.y = oy + u * travel
    elseif axis == "Z" then t.z = oz + u * travel
    else t.x = ox + u * travel end
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/moving_platform.lua", script,
        "scripts/presets/MovingPlatformScript.hpp", "MovingPlatformScript",
        stubCpp("MovingPlatformScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_travel = " + std::to_string(distance) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Moving platform with ping-pong motion.");
    return result;
}

EntityPresetSpawnResult spawnTriggerVolume(ECS::World& world, EditorContext& ctx,
                                           const std::filesystem::path& projectRoot,
                                           const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float radius = wizard.getFloat("radius", 3.0f);
    const std::string eventName = wizard.getEnumLabel("event", "checkpoint");
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 1.0f, 0), Vec3(radius, 2.0f, radius));
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Trigger volume
radius = )" + std::to_string(radius) + R"(
eventName = ")" + eventName + R"("
inside = false
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Trigger '" .. eventName .. "' radius=" .. tostring(radius))
end

function onUpdate(entity, dt)
    if not hasPlayer then return end
    local t = caffeine.world.getTransform(entity)
    local dx, dy, dz = t.x - px, t.y - py, t.z - pz
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    local now = dist < radius
    if now and not inside then
        caffeine.events.emit(eventName, entity, px, py, pz)
        caffeine.debug.log("Trigger entered: " .. eventName)
    elseif (not now) and inside then
        caffeine.events.emit(eventName .. "_exit", entity)
    end
    inside = now
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/trigger_volume.lua", script,
        "scripts/presets/TriggerVolumeScript.hpp", "TriggerVolumeScript",
        stubCpp("TriggerVolumeScript", "", "    float m_radius = " + std::to_string(radius) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Trigger volume emits a named event when the player enters.");
    return result;
}

EntityPresetSpawnResult spawnDoor(ECS::World& world, EditorContext& ctx,
                                  const std::filesystem::path& projectRoot,
                                  const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float openHeight = wizard.getFloat("open_height", 3.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 1.5f, 0), Vec3(2.4f, 3.0f, 0.25f));
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Door: Interact nearby or listen to 'checkpoint'
openHeight = )" + std::to_string(openHeight) + R"(
openSpeed = 2.2
range = 4.0
open = false
amount = 0.0
baseY = 0.0
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    baseY = t.y
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.events.on("checkpoint", function()
        open = true
    end)
    caffeine.debug.log("Door: E to toggle when close, or checkpoint event")
end

function onUpdate(entity, dt)
    local t = caffeine.world.getTransform(entity)
    if hasPlayer then
        local dx, dz = t.x - px, t.z - pz
        local dist = math.sqrt(dx * dx + dz * dz)
        if dist < range and (caffeine.input.isActionPressed("Interact") or caffeine.input.isKeyDown("E")) then
            open = not open
        end
    end
    local target = open and 1.0 or 0.0
    if amount < target then amount = math.min(target, amount + openSpeed * dt)
    elseif amount > target then amount = math.max(target, amount - openSpeed * dt) end
    t.y = baseY + amount * openHeight
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/door.lua", script,
        "scripts/presets/DoorScript.hpp", "DoorScript",
        stubCpp("DoorScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_openHeight = " + std::to_string(openHeight) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Animated door (Interact / checkpoint event).");
    return result;
}

EntityPresetSpawnResult spawnHealthPack(ECS::World& world, EditorContext& ctx,
                                        const std::filesystem::path& projectRoot,
                                        const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float heal = wizard.getFloat("heal_amount", 25.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 0.4f, 0), Vec3(0.5f, 0.5f, 0.5f));
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Health pack
healAmount = )" + std::to_string(heal) + R"(
spin = 0.0
baseY = 0.0
taken = false
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    baseY = t.y
    caffeine.events.on("player_pose", function(x, y, z)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Health pack +" .. tostring(healAmount))
end

function onUpdate(entity, dt)
    if taken then return end
    spin = spin + dt * 3.0
    local t = caffeine.world.getTransform(entity)
    t.y = baseY + math.sin(spin) * 0.12
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, spin, spin * 0.4)
    if not hasPlayer then return end
    local dx, dy, dz = t.x - px, t.y - py, t.z - pz
    if math.sqrt(dx * dx + dy * dy + dz * dz) < 1.5 then
        taken = true
        caffeine.events.emit("player_heal", healAmount)
        caffeine.world.destroy(entity)
    end
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/health_pack.lua", script,
        "scripts/presets/HealthPackScript.hpp", "HealthPackScript",
        stubCpp("HealthPackScript", "", "    float m_heal = " + std::to_string(heal) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Health pack emits player_heal on pickup.");
    return result;
}

EntityPresetSpawnResult spawnHoverDrone(ECS::World& world, EditorContext& ctx,
                                        const std::filesystem::path& projectRoot,
                                        const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float range = wizard.getFloat("chase_range", 14.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Sphere,
                                                          Vec3(0, 2.2f, 0), Vec3(0.7f, 0.7f, 0.7f));
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Hover drone: bob, strafe, chase from above
chaseRange = )" + std::to_string(range) + R"(
hoverAmp = 0.45
strafe = 2.2
speed = 4.0
originY = 0.0
time = 0.0
px, py, pz = 0, 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originY = t.y
    caffeine.events.on("player_pose", function(x, y, z)
        px, py, pz = x, y, z
        hasPlayer = true
    end)
    caffeine.debug.log("Hover drone online")
end

function onUpdate(entity, dt)
    time = time + dt
    local t = caffeine.world.getTransform(entity)
    t.y = originY + math.sin(time * 2.2) * hoverAmp
    if hasPlayer then
        local dx, dz = px - t.x, pz - t.z
        local dist = math.sqrt(dx * dx + dz * dz)
        if dist < chaseRange and dist > 0.2 then
            t.x = t.x + dx / dist * speed * dt
            t.z = t.z + dz / dist * speed * dt
        else
            t.x = t.x + math.cos(time) * strafe * dt
            t.z = t.z + math.sin(time) * strafe * dt
        end
        caffeine.world.setYawPitch(entity, math.atan(dx, dz), 0.2)
    end
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/hover_drone.lua", script,
        "scripts/presets/HoverDroneScript.hpp", "HoverDroneScript",
        stubCpp("HoverDroneScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_range = " + std::to_string(range) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Hover drone enemy with bobbing chase.");
    return result;
}

EntityPresetSpawnResult spawnOrbitLight(ECS::World& world, EditorContext& ctx,
                                        const std::filesystem::path& projectRoot,
                                        const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float radius = wizard.getFloat("orbit_radius", 5.0f);
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Sphere,
                                                          Vec3(radius, 3.0f, 0), Vec3(0.25f, 0.25f, 0.25f));
    world.add<ECS::LightComponent>(root);
    world.add<ECS::PointLightComponent>(root);
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Orbiting point light
orbitRadius = )" + std::to_string(radius) + R"(
height = 3.0
speed = 0.45
angle = 0.0
ox, oz = 0, 0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    ox, oz = t.x - orbitRadius, t.z
    caffeine.debug.log("Orbit light")
end

function onUpdate(entity, dt)
    angle = angle + speed * dt
    local t = caffeine.world.getTransform(entity)
    t.x = ox + math.cos(angle) * orbitRadius
    t.y = height
    t.z = oz + math.sin(angle) * orbitRadius
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/orbit_light.lua", script,
        "scripts/presets/OrbitLightScript.hpp", "OrbitLightScript",
        stubCpp("OrbitLightScript", "#include \"ecs/Components3D.hpp\"\n",
                "    float m_radius = " + std::to_string(radius) + "f;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Orbiting point light for scene mood.");
    return result;
}

EntityPresetSpawnResult spawnPauseMenu(ECS::World& world, EditorContext& ctx,
                                       const std::filesystem::path& projectRoot,
                                       const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    UI::UIWidget canvas;
    canvas.type = UI::UIWidgetType::Canvas;
    canvas.computedRect = {{0.0f, 0.0f}, {1280.0f, 720.0f}};
    world.add<UI::UIWidget>(root, canvas);
    ECS::Entity label = world.create();
    setEntityName(world, label, "PauseLabel");
    UI::UIWidget w;
    w.type = UI::UIWidgetType::Label;
    world.add<UI::UIWidget>(label, w);
    UI::UILabel lbl;
    lbl.text = "Paused";
    world.add<UI::UILabel>(label, lbl);
    EntityPresetUtils::parentEntity(world, label, root);
#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Pause overlay
paused = false
shown = false

function onCreate(entity)
    caffeine.debug.log("Pause menu: P to toggle")
end

function onUpdate(entity, dt)
    if caffeine.input.isKeyDown("P") or caffeine.input.isActionPressed("Pause") then
        if not shown then
            paused = not paused
            shown = true
            caffeine.events.emit(paused and "game_paused" or "game_resumed")
            caffeine.debug.log(paused and "PAUSED" or "RESUMED")
        end
    else
        shown = false
    end
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/pause_menu.lua", script,
        "scripts/presets/PauseMenuScript.hpp", "PauseMenuScript",
        stubCpp("PauseMenuScript", "", "    bool m_paused = false;\n"));
#endif
    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Pause overlay toggled with Escape.");
    return result;
}

EntityPresetSpawnResult spawn2DTopDownShooter(ECS::World& world, EditorContext& ctx,
                                              const std::filesystem::path& projectRoot,
                                              const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float speed = wizard.getFloat("move_speed", 280.0f);
    const bool withCamera = wizard.getBool("include_camera", true);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Transform>(root);
    world.add<ECS::Sprite>(root);
    auto& rb = world.add<Physics2D::RigidBody2D>(root);
    rb.isKinematic = true;
    rb.lockRotation = true;
    Physics2D::Collider2D col;
    col.shape = Physics2D::ColliderShape::Circle;
    col.radius = 20.0f;
    world.add<Physics2D::Collider2D>(root, col);

#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Top-down 2D shooter: 8-dir move, face aim, shoot cooldown
moveSpeed = )" + std::to_string(speed) + R"(
fireRate = 0.18
cooldown = 0.0
aimX, aimY = 0, 1
health = 100.0

function onCreate(entity)
    caffeine.debug.log("2D top-down: WASD move, mouse aim, Space shoot")
end

function onUpdate(entity, dt)
    local mx = caffeine.input.getAxis("Horizontal")
    local my = caffeine.input.getAxis("Vertical")
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + mx * moveSpeed * dt
    t.y = t.y + my * moveSpeed * dt
    if math.abs(mx) + math.abs(my) > 0.05 then
        aimX, aimY = mx, my
        t.rotationZ = math.deg(math.atan(aimY, aimX))
    end
    cooldown = math.max(0.0, cooldown - dt)
    if caffeine.input.isKeyDown("Space") and cooldown <= 0.0 then
        cooldown = fireRate
        caffeine.events.emit("player_shot", t.x, t.y, aimX, aimY, entity)
    end
    caffeine.world.setTransform(entity, t)
    caffeine.events.emit("player_pose", t.x, t.y, t.z, entity)
    caffeine.events.emit("hud_stats", health, cooldown / fireRate)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/player_2d_topdown.lua", script,
        "scripts/presets/Player2DTopDownScript.hpp", "Player2DTopDownScript",
        stubCpp("Player2DTopDownScript", "", "    float m_speed = " + std::to_string(speed) + "f;\n"));
#endif

    if (withCamera) {
        ECS::Entity cam = EntityPresetUtils::makeCamera2D(world, "Camera", true);
        EntityPresetUtils::parentEntity(world, cam, root);
    }

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Top-down 2D player with optional child camera.");
    return result;
}

EntityPresetSpawnResult spawn2DEnemyPatrol(ECS::World& world, EditorContext& ctx,
                                           const std::filesystem::path& projectRoot,
                                           const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float patrol = wizard.getFloat("patrol_distance", 160.0f);
    const float speed = wizard.getFloat("move_speed", 90.0f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Transform>(root);
    world.add<ECS::Sprite>(root);

#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- 2D patrol enemy: ping-pong along X
patrolDistance = )" + std::to_string(patrol) + R"(
moveSpeed = )" + std::to_string(speed) + R"(
originX = 0.0
dir = 1.0
health = 30.0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originX = t.x
end

function onUpdate(entity, dt)
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + dir * moveSpeed * dt
    if t.x > originX + patrolDistance then dir = -1.0 end
    if t.x < originX - patrolDistance then dir = 1.0 end
    t.scaleX = math.abs(t.scaleX) * dir
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/enemy_2d_patrol.lua", script,
        "scripts/presets/Enemy2DPatrolScript.hpp", "Enemy2DPatrolScript",
        stubCpp("Enemy2DPatrolScript", "", "    float m_patrol = " + std::to_string(patrol) + "f;\n"));
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("2D enemy that patrols horizontally.");
    return result;
}

EntityPresetSpawnResult spawn2DCoin(ECS::World& world, EditorContext& ctx,
                                    const std::filesystem::path& projectRoot,
                                    const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const int value = wizard.getInt("value", 1);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Transform>(root);
    world.add<ECS::Sprite>(root);
    if (auto* t = world.get<ECS::Transform>(root)) {
        t->scale = Vec3(0.5f, 0.5f, 1.0f);
    }

#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- 2D coin: bob, spin, pickup on overlap
value = )" + std::to_string(value) + R"(
pickupRange = 36.0
spin = 0.0
baseY = 0.0
px, py = 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    baseY = t.y
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py = x, y
        hasPlayer = true
    end)
end

function onUpdate(entity, dt)
    spin = spin + dt * 3.0
    local t = caffeine.world.getTransform(entity)
    t.y = baseY + math.sin(spin) * 6.0
    t.rotationZ = spin * 57.2958
    caffeine.world.setTransform(entity, t)
    if not hasPlayer then return end
    local dx, dy = t.x - px, t.y - py
    if dx * dx + dy * dy < pickupRange * pickupRange then
        caffeine.events.emit("item_collected", value, entity)
        caffeine.world.destroy(entity)
    end
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/coin_2d.lua", script,
        "scripts/presets/Coin2DScript.hpp", "Coin2DScript",
        stubCpp("Coin2DScript", "", "    int m_value = " + std::to_string(value) + ";\n"));
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("2D collectible coin.");
    return result;
}

EntityPresetSpawnResult spawn2DMovingPlatform(ECS::World& world, EditorContext& ctx,
                                              const std::filesystem::path& projectRoot,
                                              const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float travel = wizard.getFloat("travel_distance", 200.0f);
    const float period = wizard.getFloat("period", 3.0f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Transform>(root);
    world.add<ECS::Sprite>(root);
    if (auto* t = world.get<ECS::Transform>(root)) {
        t->scale = Vec3(3.0f, 0.5f, 1.0f);
    }
    auto& rb = world.add<Physics2D::RigidBody2D>(root);
    rb.isKinematic = true;

#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- 2D moving platform: ping-pong along X
travelDistance = )" + std::to_string(travel) + R"(
period = )" + std::to_string(period) + R"(
originX = 0.0
time = 0.0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originX = t.x
end

function onUpdate(entity, dt)
    time = time + dt
    local t = caffeine.world.getTransform(entity)
    local phase = (time % period) / period
    local ping = phase
    if phase > 0.5 then ping = 1.0 - phase end
    ping = ping * 2.0
    t.x = originX + (ping * 2.0 - 1.0) * travelDistance
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/platform_2d.lua", script,
        "scripts/presets/Platform2DScript.hpp", "Platform2DScript",
        stubCpp("Platform2DScript", "", "    float m_travel = " + std::to_string(travel) + "f;\n"));
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("2D kinematic moving platform.");
    return result;
}

EntityPresetSpawnResult spawn2DCameraFollow(ECS::World& world, EditorContext& ctx,
                                            const std::filesystem::path& projectRoot,
                                            const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float smooth = wizard.getFloat("smooth", 0.15f);
    const float zoom = wizard.getFloat("zoom", 1.0f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity cam = EntityPresetUtils::makeCamera2D(world, wizard.entityName.c_str(), true);
    if (auto* camComp = world.get<ECS::Camera2DComponent>(cam)) {
        camComp->zoom = zoom;
    }

#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- 2D follow camera: tracks player_pose with smoothing
smooth = )" + std::to_string(smooth) + R"(
px, py = 0, 0
hasTarget = false

function onCreate(entity)
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py = x, y
        hasTarget = true
    end)
    caffeine.debug.log("2D follow camera listening for player_pose")
end

function onUpdate(entity, dt)
    if not hasTarget then return end
    local t = caffeine.world.getTransform(entity)
    local alpha = math.min(1.0, smooth * 60.0 * dt)
    t.x = t.x + (px - t.x) * alpha
    t.y = t.y + (py - t.y) * alpha
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, cam, projectRoot, wizard, result,
        "scripts/presets/camera_2d_follow.lua", script,
        "scripts/presets/Camera2DFollowScript.hpp", "Camera2DFollowScript",
        stubCpp("Camera2DFollowScript", "", "    float m_smooth = " + std::to_string(smooth) + "f;\n"));
#endif

    ctx.selectEntity(cam);
    ctx.endUndo(world);
    result.root = cam;
    result.infoMessages.push_back("Standalone 2D camera that follows player_pose events.");
    return result;
}

EntityPresetSpawnResult spawn2DParallaxBackground(ECS::World& world, EditorContext& ctx,
                                                const std::filesystem::path& projectRoot,
                                                const EntityPresetWizardState& wizard) {
    EntityPresetSpawnResult result;
    const float factorX = wizard.getFloat("parallax_x", 0.35f);
    const float factorY = wizard.getFloat("parallax_y", 0.08f);
    const float width = wizard.getFloat("width", 2400.0f);

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = world.create();
    setEntityName(world, root, wizard.entityName.c_str());
    world.add<ECS::Transform>(root);
    world.add<ECS::Sprite>(root);
    if (auto* t = world.get<ECS::Transform>(root)) {
        t->position = Vec3(0.0f, 120.0f, 0.0f);
        t->scale = Vec3(width / 100.0f, 6.0f, 1.0f);
    }

#ifdef CF_HAS_SCRIPTING
    const std::string script = R"(-- Parallax background: follows player_pose at reduced rate
parallaxX = )" + std::to_string(factorX) + R"(
parallaxY = )" + std::to_string(factorY) + R"(
originX, originY = 0, 0
px, py = 0, 0
hasPlayer = false

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originX, originY = t.x, t.y
    caffeine.events.on("player_pose", function(x, y, z, id)
        px, py = x, y
        hasPlayer = true
    end)
    caffeine.debug.log("Parallax background: assign a wide sprite texture")
end

function onUpdate(entity, dt)
    if not hasPlayer then return end
    local t = caffeine.world.getTransform(entity)
    t.x = originX + (px - originX) * parallaxX
    t.y = originY + (py - originY) * parallaxY
    caffeine.world.setTransform(entity, t)
end
)";
    EntityPresetUtils::attachPresetScript(world, root, projectRoot, wizard, result,
        "scripts/presets/parallax_2d.lua", script,
        "scripts/presets/Parallax2DScript.hpp", "Parallax2DScript",
        stubCpp("Parallax2DScript", "", "    float m_factorX = " + std::to_string(factorX) + "f;\n"));
#endif

    ctx.selectEntity(root);
    ctx.endUndo(world);
    result.root = root;
    result.infoMessages.push_back("Parallax background layer — assign a wide sprite and place behind the player.");
    return result;
}

EntityPresetDescriptor makeDescriptor(const char* id, const char* name, const char* category,
                                      const char* description, const char* defaultEntityName,
                                      std::vector<EntityPresetWizardField> fields,
                                      EntityPresetSpawnFn spawn) {
    EntityPresetDescriptor d;
    d.id = id;
    d.displayName = name;
    d.category = category;
    d.description = description;
    d.builtIn = true;
    d.source = "builtin";
    d.defaultEntityName = defaultEntityName;
    d.defaultFields = std::move(fields);
    d.spawn = std::move(spawn);
    return d;
}

}  // namespace

void registerBuiltInEntityPresets(EntityPresetRegistry& registry) {
    registry.registerPreset(makeDescriptor(
        "player_2d", "2D Character", "playable_2d",
        "Sprite + physics base for side/top-down games, optional follow camera.",
        "Player 2D",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 220.0f, 50.0f, 800.0f),
            EntityPresetUtils::makeFloatField("jump_force", "Jump Force", 420.0f, 0.0f, 1200.0f),
            EntityPresetUtils::makeFloatField("gravity", "Gravity", 1600.0f, 200.0f, 4000.0f),
            EntityPresetUtils::makeIntField("air_jumps", "Air Jumps", 1, 0, 3),
            EntityPresetUtils::makeBoolField("include_camera", "Create Camera", true),
        },
        spawn2DCharacter));

    registry.registerPreset(makeDescriptor(
        "player_fpp", "First Person Player", "playable",
        "Capsule body, mouse look, sprint stamina, jump and crouch.",
        "Player FPP",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 6.0f, 1.0f, 20.0f),
            EntityPresetUtils::makeFloatField("mouse_sensitivity", "Mouse Sensitivity", 0.12f, 0.01f, 1.0f),
            EntityPresetUtils::makeFloatField("eye_height", "Eye Height", 1.65f, 1.0f, 2.5f),
            EntityPresetUtils::makeFloatField("jump_force", "Jump Force", 7.5f, 0.0f, 20.0f),
            EntityPresetUtils::makeFloatField("gravity", "Gravity", 22.0f, 0.0f, 60.0f),
        },
        spawnFppPlayer));

    registry.registerPreset(makeDescriptor(
        "player_third_person", "Third Person Player", "playable",
        "Character with camera rig. Choose follow or orbital camera behavior.",
        "Player 3rd Person",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 5.0f, 1.0f, 20.0f),
            EntityPresetUtils::makeFloatField("camera_distance", "Camera Distance", 6.0f, 2.0f, 20.0f),
            EntityPresetUtils::makeEnumField("camera_mode", "Camera Mode", {"Follow", "Orbital"}, 1),
        },
        spawnThirdPersonPlayer));

    registry.registerPreset(makeDescriptor(
        "player_vehicle", "Vehicle Player", "playable",
        "Arcade vehicle body with chase camera and driving script.",
        "Vehicle",
        {
            EntityPresetUtils::makeFloatField("max_speed", "Max Speed", 24.0f, 4.0f, 80.0f),
            EntityPresetUtils::makeFloatField("turn_rate", "Turn Rate", 90.0f, 10.0f, 240.0f),
            EntityPresetUtils::makeFloatField("acceleration", "Acceleration", 18.0f, 4.0f, 60.0f),
        },
        spawnVehiclePlayer));

    registry.registerPreset(makeDescriptor(
        "npc_basic", "NPC", "npcs",
        "Patrolling NPC with optional dialog hooks.",
        "NPC",
        {
            EntityPresetUtils::makeFloatField("patrol_radius", "Patrol Radius", 4.0f, 1.0f, 30.0f),
            EntityPresetUtils::makeBoolField("enable_dialog", "Enable Dialog", true),
        },
        spawnNpc));

    registry.registerPreset(makeDescriptor(
        "enemy_basic", "Enemy", "enemies",
        "Simple enemy with health, damage and chase range variables.",
        "Enemy",
        {
            EntityPresetUtils::makeFloatField("health", "Health", 100.0f, 1.0f, 1000.0f),
            EntityPresetUtils::makeFloatField("damage", "Damage", 10.0f, 1.0f, 200.0f),
            EntityPresetUtils::makeFloatField("chase_range", "Chase Range", 12.0f, 2.0f, 50.0f),
            EntityPresetUtils::makeFloatField("attack_range", "Attack Range", 2.2f, 0.5f, 10.0f),
        },
        spawnEnemy));

    registry.registerPreset(makeDescriptor(
        "item_collectible", "Collectible Item", "items",
        "Pickup item with value and auto-pickup option.",
        "Item",
        {
            EntityPresetUtils::makeIntField("value", "Value", 1, 1, 9999),
            EntityPresetUtils::makeBoolField("auto_pickup", "Auto Pickup", true),
        },
        spawnItem));

    registry.registerPreset(makeDescriptor(
        "destructible", "Destructible Object", "objects",
        "Object that can take damage and optionally explode on destroy.",
        "Destructible",
        {
            EntityPresetUtils::makeFloatField("health", "Health", 50.0f, 1.0f, 500.0f),
            EntityPresetUtils::makeBoolField("explode_on_destroy", "Explode On Destroy", false),
        },
        spawnDestructible));

    registry.registerPreset(makeDescriptor(
        "game_hud", "Game HUD", "ui",
        "Canvas with health bar and score label plus HUD controller script.",
        "HUD",
        {
            EntityPresetUtils::makeBoolField("show_health_bar", "Health Bar", true),
            EntityPresetUtils::makeBoolField("show_score", "Score Label", true),
        },
        spawnGameHud));

    registry.registerPreset(makeDescriptor(
        "player_topdown", "Top-Down Player", "playable",
        "Overhead camera, WASD movement, mouse yaw and Space dash.",
        "Player TopDown",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 7.0f, 2.0f, 20.0f),
        },
        spawnTopDownPlayer));

    registry.registerPreset(makeDescriptor(
        "flying_camera", "Flying Camera", "playable",
        "Noclip spectator: WASD, Q/E altitude, Shift boost, mouse look.",
        "Fly Camera",
        {
            EntityPresetUtils::makeFloatField("fly_speed", "Fly Speed", 12.0f, 2.0f, 40.0f),
        },
        spawnFlyingSpectator));

    registry.registerPreset(makeDescriptor(
        "turret", "Auto Turret", "enemies",
        "Tracks player_pose and fires on a cooldown.",
        "Turret",
        {
            EntityPresetUtils::makeFloatField("range", "Range", 16.0f, 4.0f, 60.0f),
            EntityPresetUtils::makeFloatField("fire_rate", "Fire Rate", 2.0f, 0.2f, 10.0f),
        },
        spawnTurret));

    registry.registerPreset(makeDescriptor(
        "hover_drone", "Hover Drone", "enemies",
        "Bobbing airborne enemy that chases from above.",
        "Hover Drone",
        {
            EntityPresetUtils::makeFloatField("chase_range", "Chase Range", 14.0f, 4.0f, 40.0f),
        },
        spawnHoverDrone));

    registry.registerPreset(makeDescriptor(
        "moving_platform", "Moving Platform", "gameplay",
        "Ping-pong platform along X, Y or Z.",
        "Platform",
        {
            EntityPresetUtils::makeFloatField("travel", "Travel Distance", 6.0f, 1.0f, 30.0f),
            EntityPresetUtils::makeFloatField("period", "Period (s)", 4.0f, 0.5f, 20.0f),
            EntityPresetUtils::makeEnumField("axis", "Axis", {"X", "Y", "Z"}, 0),
        },
        spawnMovingPlatform));

    registry.registerPreset(makeDescriptor(
        "trigger_volume", "Trigger Volume", "gameplay",
        "Fires a named event when the player enters the radius.",
        "Trigger",
        {
            EntityPresetUtils::makeFloatField("radius", "Radius", 3.0f, 0.5f, 20.0f),
            EntityPresetUtils::makeEnumField("event", "Event", {"checkpoint", "door_open", "zone"}, 0),
        },
        spawnTriggerVolume));

    registry.registerPreset(makeDescriptor(
        "door", "Animated Door", "gameplay",
        "Slides open on Interact or checkpoint event.",
        "Door",
        {
            EntityPresetUtils::makeFloatField("open_height", "Open Height", 3.0f, 0.5f, 10.0f),
        },
        spawnDoor));

    registry.registerPreset(makeDescriptor(
        "health_pack", "Health Pack", "items",
        "Heals the player on contact (player_heal).",
        "Health Pack",
        {
            EntityPresetUtils::makeFloatField("heal_amount", "Heal", 25.0f, 1.0f, 100.0f),
        },
        spawnHealthPack));

    registry.registerPreset(makeDescriptor(
        "orbit_light", "Orbit Light", "objects",
        "Point light that orbits the spawn origin.",
        "Orbit Light",
        {
            EntityPresetUtils::makeFloatField("orbit_radius", "Orbit Radius", 5.0f, 1.0f, 30.0f),
        },
        spawnOrbitLight));

    registry.registerPreset(makeDescriptor(
        "pause_menu", "Pause Overlay", "ui",
        "Canvas + script. Press P to toggle pause events.",
        "Pause Menu",
        {},
        spawnPauseMenu));

    registry.registerPreset(makeDescriptor(
        "player_2d_topdown", "2D Top-Down Shooter", "playable_2d",
        "8-direction movement, mouse aim and Space to shoot.",
        "Player 2D TopDown",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 280.0f, 80.0f, 600.0f),
            EntityPresetUtils::makeBoolField("include_camera", "Create Camera", true),
        },
        spawn2DTopDownShooter));

    registry.registerPreset(makeDescriptor(
        "enemy_2d_patrol", "2D Patrol Enemy", "playable_2d",
        "Sprite enemy that patrols horizontally.",
        "Enemy 2D",
        {
            EntityPresetUtils::makeFloatField("patrol_distance", "Patrol Distance", 160.0f, 40.0f, 600.0f),
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 90.0f, 20.0f, 300.0f),
        },
        spawn2DEnemyPatrol));

    registry.registerPreset(makeDescriptor(
        "coin_2d", "2D Coin", "playable_2d",
        "Bobbing collectible that emits item_collected.",
        "Coin 2D",
        {
            EntityPresetUtils::makeIntField("value", "Value", 1, 1, 999),
        },
        spawn2DCoin));

    registry.registerPreset(makeDescriptor(
        "platform_2d", "2D Moving Platform", "playable_2d",
        "Kinematic platform that ping-pongs along X.",
        "Platform 2D",
        {
            EntityPresetUtils::makeFloatField("travel_distance", "Travel Distance", 200.0f, 40.0f, 800.0f),
            EntityPresetUtils::makeFloatField("period", "Period (s)", 3.0f, 0.5f, 12.0f),
        },
        spawn2DMovingPlatform));

    registry.registerPreset(makeDescriptor(
        "camera_2d_follow", "2D Follow Camera", "playable_2d",
        "Standalone camera that smoothly tracks player_pose.",
        "Camera 2D Follow",
        {
            EntityPresetUtils::makeFloatField("smooth", "Smoothing", 0.15f, 0.02f, 1.0f),
            EntityPresetUtils::makeFloatField("zoom", "Zoom", 1.0f, 0.25f, 3.0f),
        },
        spawn2DCameraFollow));

    registry.registerPreset(makeDescriptor(
        "parallax_2d", "2D Parallax Background", "playable_2d",
        "Wide sprite layer that scrolls slower than the player for depth.",
        "Parallax BG",
        {
            EntityPresetUtils::makeFloatField("parallax_x", "Parallax X", 0.35f, 0.0f, 1.0f),
            EntityPresetUtils::makeFloatField("parallax_y", "Parallax Y", 0.08f, 0.0f, 1.0f),
            EntityPresetUtils::makeFloatField("width", "Width (units)", 2400.0f, 400.0f, 8000.0f),
        },
        spawn2DParallaxBackground));
}

}  // namespace Caffeine::Editor
