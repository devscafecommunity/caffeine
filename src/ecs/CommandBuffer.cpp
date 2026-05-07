/**
 * @file CommandBuffer.cpp
 * @brief CommandBuffer implementation
 * @copyright Copyright (c) 2025 Caffeine Engine
 */

#include "ecs/CommandBuffer.hpp"
#include "ecs/World.hpp"

namespace Caffeine::ECS {

void CommandBuffer::execute(World& world) {
    for (auto& command : m_commands) {
        if (command) {
            command->execute(world);
        }
    }
    m_commands.clear();
    m_nextPendingID = 0;
}

void CreateCommand::execute(World& world) {
    world.create();
}

void DestroyCommand::execute(World& world) {
    if (entity.isValid()) {
        world.destroy(entity);
    }
}

}
