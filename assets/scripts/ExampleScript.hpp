#pragma once

#include "script/CppScript.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"

class ExampleScript : public Caffeine::Script::CppScript {
public:
    float speed = 5.0f;

    void gatherExposedFields(std::vector<Caffeine::Script::ExposedScriptField>& fields) override {
        fields.push_back({"speed", Caffeine::Script::ExposedScriptField::Type::Float, &speed});
    }

    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {
        (void)entity; (void)world;
    }

    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {
        if (auto* p3 = world.get<Caffeine::ECS::Position3D>(entity)) {
            p3->position.x += speed * dt;
            return;
        }
        if (auto* tf = world.get<Caffeine::ECS::Transform>(entity)) {
            tf->position.x += speed * dt;
        }
    }

    void onDestroy(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {
        (void)entity; (void)world;
    }
};

REGISTER_CPP_SCRIPT(ExampleScript)
