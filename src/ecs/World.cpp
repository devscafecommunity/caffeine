/**
 * @file World.cpp
 * @brief Implementation of World entity lifecycle
 * @copyright Copyright (c) 2025 Caffeine Engine
 */

#include "ecs/World.hpp"
#include "memory/Allocator.hpp"
#include <cassert>

namespace Caffeine::ECS {

World::World(IAllocator* allocator)
    : m_allocator(allocator)
    , m_entities(allocator)
    , m_archetypes(allocator)
    , m_archetypeIndex()
{
}

World::~World() {
    destroyAll();
}

Entity World::create(const char* name) {
    u32 id = m_nextEntityID++;
    
    if (id >= m_entities.size()) {
        m_entities.resize(id + 1);
    }
    
    ComponentSet emptySet;
    Archetype* arch = getOrCreateArchetype(emptySet);
    
    u32 indexInArchetype = arch->addEntity(id);
    
    m_entities[id] = EntityData{
        .archetypeIndex = arch->index(),
        .indexInArchetype = indexInArchetype
    };
    
    ++m_entityCount;
    
    return Entity(id, this);
}

void World::destroy(Entity e) {
    if (!e.isValid() || e.id() >= m_entities.size()) {
        return;
    }
    
    u32 id = e.id();
    EntityData& data = m_entities[id];
    
    Archetype* arch = getArchetype(data.archetypeIndex);
    if (!arch) {
        return;
    }
    
    u32 swappedEntityID = arch->removeEntity(data.indexInArchetype);
    
    if (swappedEntityID != u32_max && swappedEntityID != id) {
        m_entities[swappedEntityID].indexInArchetype = data.indexInArchetype;
    }
    
    --m_entityCount;
}

void World::destroyAll() {
    m_entities.clear();
    m_archetypes.clear();
    m_archetypeIndex.clear();
    m_entityCount = 0;
    m_nextEntityID = 0;
}

Archetype* World::getOrCreateArchetype(const ComponentSet& set) {
    u32* existing = m_archetypeIndex.get(set);
    if (existing) {
        return m_archetypes[*existing].get();
    }
    
    u32 index = static_cast<u32>(m_archetypes.size());
    auto arch = std::make_unique<Archetype>(set, m_allocator, index);
    Archetype* ptr = arch.get();
    
    m_archetypes.pushBack(std::move(arch));
    m_archetypeIndex.set(set, index);
    
    return ptr;
}

Archetype* World::getArchetype(u32 index) {
    if (index >= m_archetypes.size()) {
        return nullptr;
    }
    return m_archetypes[index].get();
}

const Archetype* World::getArchetype(u32 index) const {
    if (index >= m_archetypes.size()) {
        return nullptr;
    }
    return m_archetypes[index].get();
}

}
