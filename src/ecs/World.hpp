/**
 * @file World.hpp
 * @brief ECS World container managing entities and archetypes
 * @copyright Copyright (c) 2025 Caffeine Engine
 */

#pragma once

#include "core/Types.hpp"
#include "core/Assertions.hpp"
#include "containers/Vector.hpp"
#include "containers/HashMap.hpp"
#include "ecs/Entity.hpp"
#include "ecs/Archetype.hpp"
#include "ecs/ComponentSet.hpp"
#include "ecs/ComponentID.hpp"
#include "ecs/ComponentQuery.hpp"
#include "threading/JobSystem.hpp"
#include <memory>

namespace Caffeine::ECS {

class World {
public:
    World(IAllocator* allocator = nullptr);
    ~World();
    
    Entity create(const char* name = nullptr);
    void destroy(Entity e);
    void destroyAll();
    
    u32 entityCount() const { return m_entityCount; }
    u32 archetypeCount() const { return static_cast<u32>(m_archetypes.size()); }
    
    template<typename T, typename... Args>
    T& add(Entity e, Args&&... args);
    
    template<typename T>
    void remove(Entity e);
    
    template<typename T>
    bool has(Entity e) const;
    
    template<typename T>
    T* get(Entity e);
    
    template<typename T>
    const T* get(Entity e) const;
    
    template<typename... Ts, typename Callback>
    void forEach(const ComponentQuery& query, Callback callback);
    
    template<typename... Ts, typename Callback>
    void forEachParallel(const ComponentQuery& query, Threading::JobSystem& jobSystem, Callback callback);
    
private:
    struct EntityData {
        u32 archetypeIndex;
        u32 indexInArchetype;
    };
    
    IAllocator* m_allocator;
    Vector<EntityData> m_entities;
    u32 m_entityCount = 0;
    u32 m_nextEntityID = 0;
    
    Vector<std::unique_ptr<Archetype>> m_archetypes;
    HashMap<ComponentSet, u32> m_archetypeIndex;
    
    Archetype* getOrCreateArchetype(const ComponentSet& set);
    Archetype* getArchetype(u32 index);
    const Archetype* getArchetype(u32 index) const;
};

template<typename T, typename... Args>
T& World::add(Entity e, Args&&... args) {
    CF_ASSERT(e.isValid(), "Cannot add component to invalid entity");
    CF_ASSERT(e.id() < m_entities.size(), "Entity ID out of range");
    
    u32 entityID = e.id();
    EntityData& entityData = m_entities[entityID];
    
    Archetype* oldArchetype = getArchetype(entityData.archetypeIndex);
    CF_ASSERT(oldArchetype, "Entity archetype is null");
    
    u32 componentID = ComponentID::get<T>();
    
    if (oldArchetype->getComponentSet().has(componentID)) {
        return *oldArchetype->getComponent<T>(entityData.indexInArchetype);
    }
    
    ComponentSet newSet = oldArchetype->getComponentSet();
    newSet.add(componentID);
    
    Archetype* newArchetype = getOrCreateArchetype(newSet);
    
    u32 newIndexInArchetype = newArchetype->addEntity(entityID);
    
    const ComponentSet& oldSet = oldArchetype->getComponentSet();
    for (u32 cid = 0; cid < 64; ++cid) {
        if (oldSet.has(cid)) {
            newArchetype->copyComponent(cid, entityData.indexInArchetype, oldArchetype);
        }
    }
    
    T component(std::forward<Args>(args)...);
    newArchetype->addComponent<T>(newIndexInArchetype, component);
    
    u32 swappedEntityID = oldArchetype->removeEntity(entityData.indexInArchetype);
    if (swappedEntityID != u32_max && swappedEntityID != entityID) {
        m_entities[swappedEntityID].indexInArchetype = entityData.indexInArchetype;
    }
    
    m_entities[entityID].archetypeIndex = newArchetype->index();
    m_entities[entityID].indexInArchetype = newIndexInArchetype;
    
    return *newArchetype->getComponent<T>(newIndexInArchetype);
}

template<typename T>
void World::remove(Entity e) {
    CF_ASSERT(e.isValid(), "Cannot remove component from invalid entity");
    CF_ASSERT(e.id() < m_entities.size(), "Entity ID out of range");
    
    u32 entityID = e.id();
    EntityData& entityData = m_entities[entityID];
    
    Archetype* oldArchetype = getArchetype(entityData.archetypeIndex);
    CF_ASSERT(oldArchetype, "Entity archetype is null");
    
    u32 componentID = ComponentID::get<T>();
    
    if (!oldArchetype->getComponentSet().has(componentID)) {
        return;
    }
    
    ComponentSet newSet = oldArchetype->getComponentSet();
    newSet.remove(componentID);
    
    Archetype* newArchetype = getOrCreateArchetype(newSet);
    
    u32 newIndexInArchetype = newArchetype->addEntity(entityID);
    
    const ComponentSet& oldSet = oldArchetype->getComponentSet();
    for (u32 cid = 0; cid < 64; ++cid) {
        if (oldSet.has(cid) && cid != componentID) {
            newArchetype->copyComponent(cid, entityData.indexInArchetype, oldArchetype);
        }
    }
    
    u32 swappedEntityID = oldArchetype->removeEntity(entityData.indexInArchetype);
    if (swappedEntityID != u32_max && swappedEntityID != entityID) {
        m_entities[swappedEntityID].indexInArchetype = entityData.indexInArchetype;
    }
    
    m_entities[entityID].archetypeIndex = newArchetype->index();
    m_entities[entityID].indexInArchetype = newIndexInArchetype;
}

template<typename T>
bool World::has(Entity e) const {
    if (!e.isValid() || e.id() >= m_entities.size()) {
        return false;
    }
    
    const EntityData& data = m_entities[e.id()];
    const Archetype* arch = getArchetype(data.archetypeIndex);
    if (!arch) {
        return false;
    }
    
    u32 componentID = ComponentID::get<T>();
    return arch->getComponentSet().has(componentID);
}

template<typename T>
T* World::get(Entity e) {
    if (!e.isValid() || e.id() >= m_entities.size()) {
        return nullptr;
    }
    
    EntityData& data = m_entities[e.id()];
    Archetype* arch = getArchetype(data.archetypeIndex);
    if (!arch) {
        return nullptr;
    }
    
    return arch->getComponent<T>(data.indexInArchetype);
}

template<typename T>
const T* World::get(Entity e) const {
    if (!e.isValid() || e.id() >= m_entities.size()) {
        return nullptr;
    }
    
    const EntityData& data = m_entities[e.id()];
    const Archetype* arch = getArchetype(data.archetypeIndex);
    if (!arch) {
        return nullptr;
    }
    
    return arch->getComponent<T>(data.indexInArchetype);
}

template<typename... Ts, typename Callback>
void World::forEach(const ComponentQuery& query, Callback callback) {
    for (usize i = 0; i < m_archetypes.size(); ++i) {
        Archetype* archetype = m_archetypes[i].get();
        
        if (!archetype || !query.matches(archetype)) {
            continue;
        }
        
        u32 entityCount = archetype->entityCount();
        for (u32 j = 0; j < entityCount; ++j) {
            u32 entityID = archetype->getEntityID(j);
            Entity entity(entityID, this);
            
            callback(entity, *archetype->getComponent<Ts>(j)...);
        }
    }
}

template<typename... Ts, typename Callback>
void World::forEachParallel(const ComponentQuery& query, Threading::JobSystem& jobSystem, Callback callback) {
    struct ArchetypeWork {
        Archetype* archetype;
        u32 startIndex;
        u32 count;
    };
    
    Vector<ArchetypeWork> workItems(m_allocator);
    
    for (usize i = 0; i < m_archetypes.size(); ++i) {
        Archetype* archetype = m_archetypes[i].get();
        
        if (!archetype || !query.matches(archetype)) {
            continue;
        }
        
        u32 entityCount = archetype->entityCount();
        if (entityCount == 0) {
            continue;
        }
        
        constexpr u32 BATCH_SIZE = 1000;
        
        if (entityCount <= BATCH_SIZE) {
            workItems.pushBack(ArchetypeWork{archetype, 0, entityCount});
        } else {
            for (u32 start = 0; start < entityCount; start += BATCH_SIZE) {
                u32 count = (start + BATCH_SIZE <= entityCount) ? BATCH_SIZE : (entityCount - start);
                workItems.pushBack(ArchetypeWork{archetype, start, count});
            }
        }
    }
    
    if (workItems.empty()) {
        return;
    }
    
    Threading::JobBarrier barrier(static_cast<u32>(workItems.size()));
    
    for (usize i = 0; i < workItems.size(); ++i) {
        const ArchetypeWork& work = workItems[i];
        
        jobSystem.scheduleData(
            work,
            [this, callback](ArchetypeWork& w) {
                for (u32 j = w.startIndex; j < w.startIndex + w.count; ++j) {
                    u32 entityID = w.archetype->getEntityID(j);
                    Entity entity(entityID, const_cast<World*>(this));
                    
                    callback(entity, *w.archetype->template getComponent<Ts>(j)...);
                }
            },
            &barrier,
            Threading::JobPriority::Normal
        );
    }
    
    barrier.wait();
}

}
