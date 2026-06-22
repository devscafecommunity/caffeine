#pragma once

#include "script/CppScript.hpp"
#include "ecs/World.hpp"
#include "ecs/Components.hpp"

class ExampleScript : public Caffeine::Script::CppScript {
public:
    void onCreate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {
        (void)entity; (void)world;
    }

    void onUpdate(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world, Caffeine::f32 dt) override {
        if (auto* tf = world.get<Caffeine::ECS::Transform>(entity)) {
            tf->position.x += 100.0f * dt;
        }
    }

    void onDestroy(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {
        (void)entity; (void)world;
    }
};

REGISTER_CPP_SCRIPT(ExampleScript)
