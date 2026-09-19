#include "editor/EntityPresetFactories.hpp"
#include "editor/EntityPresetRegistry.hpp"
#include "editor/EntityPresetUtils.hpp"
#include "editor/PrefabSystem.hpp"
#include "ecs/Components.hpp"
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
    const std::string script = R"(-- 2D character controller starter
local speed = )" + std::to_string(speed) + R"(
local jumpForce = )" + std::to_string(jump) + R"(

function onCreate(entity)
    caffeine.debug.log("2D player ready. Tune speed/jump in this script.")
end

function onUpdate(entity, dt)
    local input = caffeine.input
    local moveX = input.getAxis("Horizontal")
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + moveX * speed * dt
    caffeine.world.setTransform(entity, t)

    if input.isKeyDown("Space") then
        -- Hook your jump / physics impulse here.
    end
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
    const std::string script = R"(-- First-person controller
moveSpeed = )" + std::to_string(speed) + R"(
mouseSensitivity = )" + std::to_string(sensitivity) + R"(
yaw = 0.0
pitch = 0.0

function onCreate(entity)
    caffeine.debug.log("FPP player ready")
end

function onUpdate(entity, dt)
    local mx = caffeine.input.getAxis("Horizontal")
    local mz = caffeine.input.getAxis("Vertical")
    yaw = yaw + caffeine.input.getAxis("LookX") * mouseSensitivity
    pitch = math.max(-1.2, math.min(1.2, pitch - caffeine.input.getAxis("LookY") * mouseSensitivity))

    local cs = math.cos(yaw)
    local sn = math.sin(yaw)
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + (mx * cs + mz * sn) * moveSpeed * dt
    t.z = t.z + (mz * cs - mx * sn) * moveSpeed * dt
    caffeine.world.setTransform(entity, t)
    caffeine.world.setYawPitch(entity, yaw, pitch)
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
    const std::string playerScript = R"(-- Third-person movement starter
local moveSpeed = )" + std::to_string(speed) + R"(

function onCreate(entity)
    caffeine.debug.log("3rd-person player ready.")
end

function onUpdate(entity, dt)
    local input = caffeine.input
    local moveX = input.getAxis("Horizontal")
    local moveY = input.getAxis("Vertical")
    local t = caffeine.world.getTransform(entity)
    t.x = t.x + moveX * moveSpeed * dt
    t.z = t.z + moveY * moveSpeed * dt
    caffeine.world.setTransform(entity, t)
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
    const std::string camScript = R"(-- Third-person camera rig starter
local cameraDistance = )" + std::to_string(distance) + R"(
local orbitalMode = )" + std::string(orbital ? "true" : "false") + R"(

function onCreate(entity)
    caffeine.debug.log("Camera rig ready. Implement follow/orbit here.")
end

function onUpdate(entity, dt)
    if orbitalMode then
        local input = caffeine.input
        local lookX = input.getAxis("LookX")
        local t = caffeine.world.getTransform(entity)
        t.rotation = t.rotation + lookX * 90.0 * dt
        caffeine.world.setTransform(entity, t)
    end
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

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Cube,
                                                          Vec3(0, 0.75f, 0), Vec3(2.0f, 1.0f, 4.0f));

    ECS::Entity camera = EntityPresetUtils::makeCamera3D(world, "ChaseCamera", true);
    if (auto* p = world.get<ECS::Position3D>(camera)) p->position = Vec3(0, 3.0f, -8.0f);
    if (auto* active = world.get<ECS::CameraActiveComponent>(camera)) active->is2D = false;
    EntityPresetUtils::parentEntity(world, camera, root);

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/vehicle_player.lua";
    const std::string script = R"(-- Arcade vehicle controller starter
local maxSpeed = )" + std::to_string(maxSpeed) + R"(
local turnRate = )" + std::to_string(turnRate) + R"(

function onCreate(entity)
    caffeine.debug.log("Vehicle player ready.")
end

function onUpdate(entity, dt)
    local input = caffeine.input
    local throttle = input.getAxis("Vertical")
    local steer = input.getAxis("Horizontal")
    local t = caffeine.world.getTransform(entity)
    t.z = t.z + throttle * maxSpeed * dt
    t.rotation = t.rotation + steer * turnRate * dt
    caffeine.world.setTransform(entity, t)
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
    const std::string script = R"(-- Basic NPC patrol + dialog starter
local patrolRadius = )" + std::to_string(patrolRadius) + R"(
local dialogEnabled = )" + std::string(hasDialog ? "true" : "false") + R"(
local originX, originZ = 0, 0
local angle = 0

function onCreate(entity)
    local t = caffeine.world.getTransform(entity)
    originX, originZ = t.x, t.z
    caffeine.debug.log("NPC ready. Customize patrol/dialog behavior.")
end

function onUpdate(entity, dt)
    angle = angle + dt * 0.5
    local t = caffeine.world.getTransform(entity)
    t.x = originX + math.cos(angle) * patrolRadius
    t.z = originZ + math.sin(angle) * patrolRadius
    caffeine.world.setTransform(entity, t)
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

    ctx.beginUndo(EditorCommand::AddEntity, u32_max, world);
    ECS::Entity root = EntityPresetUtils::make3DPrimitive(world, wizard.entityName.c_str(),
                                                          ECS::MeshPrimitive::Sphere,
                                                          Vec3(0, 1.0f, 0), Vec3(1.2f, 1.2f, 1.2f));

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/enemy_basic.lua";
    const std::string script = R"(-- Basic enemy AI starter
local health = )" + std::to_string(health) + R"(
local damage = )" + std::to_string(damage) + R"(
local chaseRange = )" + std::to_string(chaseRange) + R"(

function onCreate(entity)
    caffeine.debug.log("Enemy spawned with health=" .. health)
end

function onUpdate(entity, dt)
    -- Replace with player detection / chase logic.
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
    const std::string script = R"(-- Collectible item starter
local value = )" + std::to_string(value) + R"(
local autoPickup = )" + std::string(autoPickup ? "true" : "false") + R"(

function onCreate(entity)
    caffeine.debug.log("Collectible item ready")
end

function onUpdate(entity, dt)
    if autoPickup then
        -- Hook trigger / distance check to player here.
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
    const std::string script = R"(-- Destructible object starter
local health = )" + std::to_string(health) + R"(
local explodeOnDestroy = )" + std::string(explode ? "true" : "false") + R"(

function onCreate(entity)
    caffeine.debug.log("Destructible ready.")
end

function onUpdate(entity, dt)
    if health <= 0 then
        if explodeOnDestroy then
            caffeine.debug.log("Boom! Spawn VFX here.")
        end
        -- caffeine.world.destroy(entity) when API is available
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

    if (showHealth) {
        ECS::Entity healthBar = world.create();
        setEntityName(world, healthBar, "HealthBar");
        UI::UIWidget barWidget;
        barWidget.type = UI::UIWidgetType::ProgressBar;
        barWidget.interactable = false;
        barWidget.transform.offsetMax = {220.0f, 20.0f};
        world.add<UI::UIWidget>(healthBar, barWidget);
        world.add<UI::UIProgressBar>(healthBar);
        EntityPresetUtils::parentEntity(world, healthBar, root);
    }

    if (showScore) {
        ECS::Entity score = world.create();
        setEntityName(world, score, "ScoreLabel");
        UI::UIWidget labelWidget;
        labelWidget.type = UI::UIWidgetType::Label;
        labelWidget.interactable = false;
        labelWidget.transform.offsetMax = {180.0f, 30.0f};
        world.add<UI::UIWidget>(score, labelWidget);
        UI::UILabel lbl;
        lbl.text = "Score: 0";
        world.add<UI::UILabel>(score, lbl);
        EntityPresetUtils::parentEntity(world, score, root);
    }

#ifdef CF_HAS_SCRIPTING
    const std::string rel = "scripts/presets/game_hud.lua";
    const std::string script = R"(-- Game HUD controller starter
local showHealth = )" + std::string(showHealth ? "true" : "false") + R"(
local showScore = )" + std::string(showScore ? "true" : "false") + R"(

function onCreate(entity)
    caffeine.debug.log("HUD ready. Bind health/score updates here.")
end

function onUpdate(entity, dt)
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
        "player_2d", "2D Character", "playable",
        "Sprite + physics base for side/top-down games, optional follow camera.",
        "Player 2D",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 220.0f, 50.0f, 800.0f),
            EntityPresetUtils::makeFloatField("jump_force", "Jump Force", 420.0f, 0.0f, 1200.0f),
            EntityPresetUtils::makeBoolField("include_camera", "Create Camera", true),
        },
        spawn2DCharacter));

    registry.registerPreset(makeDescriptor(
        "player_fpp", "First Person Player", "playable",
        "Capsule body with eye-level camera and FPP movement script.",
        "Player FPP",
        {
            EntityPresetUtils::makeFloatField("move_speed", "Move Speed", 6.0f, 1.0f, 20.0f),
            EntityPresetUtils::makeFloatField("mouse_sensitivity", "Mouse Sensitivity", 0.12f, 0.01f, 1.0f),
            EntityPresetUtils::makeFloatField("eye_height", "Eye Height", 1.65f, 1.0f, 2.5f),
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
}

}  // namespace Caffeine::Editor
