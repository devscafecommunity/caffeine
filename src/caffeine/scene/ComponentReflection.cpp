#include "scene/ComponentReflection.hpp"

#include "ecs/CameraComponents.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/LightComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/World.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "physics/PhysicsComponents3D.hpp"
#include "script/ScriptTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Caffeine::Scene::Reflection {
namespace {

template <typename T>
ComponentDesc component(uint32_t id, const char* name, std::vector<FieldDesc> fields) {
    return ComponentDesc{
        id,
        name,
        [](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity) {
            return world.has<T>(entity);
        },
        [](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity) {
            world.add<T>(entity, T{});
        },
        [](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity) {
            world.remove<T>(entity);
        },
        std::move(fields)};
}

FieldValue baseValue(FieldKind kind) {
    FieldValue value{};
    value.kind = kind;
    return value;
}

template <typename T>
FieldDesc f32Field(const char* name, float T::*member, float minValue = 0.0f,
                   float maxValue = 0.0f, float step = 0.0f) {
    return FieldDesc{
        name, FieldKind::F32, minValue, maxValue, step, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::F32);
            value.vec[0] = (*component).*member;
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
            if (!component) return false;
            (*component).*member = value.vec[0];
            return true;
        }};
}

template <typename T>
FieldDesc boolField(const char* name, bool T::*member) {
    return FieldDesc{
        name, FieldKind::Bool, 0.0f, 0.0f, 0.0f, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::Bool);
            value.boolean = (*component).*member ? 1 : 0;
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
            if (!component) return false;
            (*component).*member = value.boolean != 0;
            return true;
        }};
}

template <typename T>
FieldDesc i32Field(const char* name, int32_t T::*member, float minValue = 0.0f,
                   float maxValue = 0.0f, float step = 1.0f) {
    return FieldDesc{
        name, FieldKind::I32, minValue, maxValue, step, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::I32);
            value.integer = (*component).*member;
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
            if (!component) return false;
            (*component).*member = value.integer;
            return true;
        }};
}

template <typename T>
FieldDesc u32Field(const char* name, uint32_t T::*member) {
    return FieldDesc{
        name, FieldKind::U32, 0.0f, 0.0f, 1.0f, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::U32);
            value.integer = static_cast<int32_t>((*component).*member);
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
            if (!component) return false;
            (*component).*member = static_cast<uint32_t>(value.integer);
            return true;
        }};
}

template <typename T>
FieldDesc vec2Field(const char* name, Caffeine::Vec2 T::*member, float step = 0.1f) {
    return FieldDesc{
        name, FieldKind::Vec2, 0.0f, 0.0f, step, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::Vec2);
            const Caffeine::Vec2& vec = (*component).*member;
            value.vec[0] = vec.x;
            value.vec[1] = vec.y;
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
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
        name, FieldKind::Vec3, 0.0f, 0.0f, step, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::Vec3);
            const Caffeine::Vec3& vec = (*component).*member;
            value.vec[0] = vec.x;
            value.vec[1] = vec.y;
            value.vec[2] = vec.z;
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
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
        name, FieldKind::Vec4, minValue, maxValue, step, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::Vec4);
            const Caffeine::Vec4& vec = (*component).*member;
            value.vec[0] = vec.x;
            value.vec[1] = vec.y;
            value.vec[2] = vec.z;
            value.vec[3] = vec.w;
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
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
    field.kind = FieldKind::Color;
    auto original = field.get;
    field.get = [original](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                           FieldValue& value) {
        if (!original(world, entity, value)) return false;
        value.kind = FieldKind::Color;
        return true;
    };
    return field;
}

template <typename T>
FieldDesc stringField(const char* name, std::string T::*member) {
    return FieldDesc{
        name, FieldKind::String, 0.0f, 0.0f, 0.0f, {},
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::String);
            std::strncpy(value.text, ((*component).*member).c_str(),
                         sizeof(value.text) - 1);
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
            if (!component) return false;
            (*component).*member = std::string(value.text);
            return true;
        }};
}

template <typename T, typename E>
FieldDesc enumField(const char* name, E T::*member, std::vector<const char*> options) {
    return FieldDesc{
        name, FieldKind::Enum, 0.0f,
        static_cast<float>(options.size() - 1), 1.0f, std::move(options),
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 FieldValue& value) {
            const auto* component = world.get<T>(entity);
            if (!component) return false;
            value = baseValue(FieldKind::Enum);
            value.integer = static_cast<int32_t>((*component).*member);
            return true;
        },
        [member](Caffeine::ECS::World& world, Caffeine::ECS::Entity entity,
                 const FieldValue& value) {
            auto* component = world.get<T>(entity);
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

}  // namespace

const ComponentDesc* findById(std::uint32_t id) {
    for (const ComponentDesc& desc : componentRegistry()) {
        if (desc.id == id) return &desc;
    }
    return nullptr;
}

const std::vector<ComponentDesc>& registry() {
    return componentRegistry();
}

namespace {
std::string normalised(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c != ' ' && c != '_' && c != '-') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}
}  // namespace

const ComponentDesc* findByName(const std::string& name) {
    const std::string wanted = normalised(name);
    for (const ComponentDesc& desc : componentRegistry()) {
        if (normalised(desc.name) == wanted) return &desc;
    }
    return nullptr;
}

const FieldDesc* findField(const ComponentDesc& component, const std::string& name) {
    const std::string wanted = normalised(name);
    for (const FieldDesc& field : component.fields) {
        if (normalised(field.name) == wanted) return &field;
    }
    return nullptr;
}

std::vector<const ComponentDesc*> presentComponents(ECS::World& world, ECS::Entity entity) {
    std::vector<const ComponentDesc*> present;
    for (const ComponentDesc& desc : componentRegistry()) {
        if (desc.has(world, entity)) present.push_back(&desc);
    }
    return present;
}

}  // namespace Caffeine::Scene::Reflection
