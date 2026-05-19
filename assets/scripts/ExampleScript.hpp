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
        if (auto* pos = world.get<Caffeine::ECS::Position2D>(entity)) {
            pos->x += 100.0f * dt;
        }
        if (auto* tf = world.get<Caffeine::ECS::Transform>(entity)) {
            tf->position.x += 100.0f * dt;
            if (auto* pos = world.get<Caffeine::ECS::Position2D>(entity)) {
                pos->x = tf->position.x;
                pos->y = tf->position.y;
            }
        }
    }

    void onDestroy(Caffeine::ECS::Entity entity, Caffeine::ECS::World& world) override {
        (void)entity; (void)world;
    }
};

REGISTER_CPP_SCRIPT(ExampleScript)
