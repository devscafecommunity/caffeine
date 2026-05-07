/**
 * @file Archetype.hpp
 * @brief Dense component storage and archetype grouping for cache-friendly iteration
 */

#pragma once

#include "core/Types.hpp"
#include "containers/Vector.hpp"
#include "memory/Allocator.hpp"
#include "ecs/ComponentSet.hpp"
#include "ecs/ComponentID.hpp"

namespace Caffeine {
namespace ECS {

template<typename T>
class ComponentPool {
public:
    explicit ComponentPool(IAllocator* allocator = nullptr)
        : m_data(allocator) {}
    
    u32 add(const T& component) {
        m_data.pushBack(component);
        return static_cast<u32>(m_data.size() - 1);
    }
    
    void remove(u32 index) {
        if (index >= m_data.size()) {
            return;
        }
        
        if (index != m_data.size() - 1) {
            m_data[index] = m_data[m_data.size() - 1];
        }
        m_data.popBack();
    }
    
    T* get(u32 index) {
        if (index >= m_data.size()) {
            return nullptr;
        }
        return &m_data[index];
    }
    
    const T* get(u32 index) const {
        if (index >= m_data.size()) {
            return nullptr;
        }
        return &m_data[index];
    }
    
    usize size() const {
        return m_data.size();
    }
    
    usize capacity() const {
        return m_data.capacity();
    }
    
    void clear() {
        m_data.clear();
    }
    
    T* data() {
        return m_data.data();
    }
    
    const T* data() const {
        return m_data.data();
    }

private:
    Vector<T> m_data;
};

class Archetype {
public:
    explicit Archetype(const ComponentSet& componentSet, IAllocator* allocator = nullptr, u32 archetypeIndex = 0)
        : m_componentSet(componentSet)
        , m_allocator(allocator)
        , m_archetypeIndex(archetypeIndex)
        , m_entities(allocator)
        , m_poolRegistry() {}
    
    const ComponentSet& getComponentSet() const {
        return m_componentSet;
    }
    
    u32 entityCount() const {
        return static_cast<u32>(m_entities.size());
    }
    
    u32 index() const {
        return m_archetypeIndex;
    }
    
    u32 getEntityID(u32 indexInArchetype) const {
        if (indexInArchetype >= m_entities.size()) {
            return u32_max;
        }
        return m_entities[indexInArchetype];
    }
    
    u32 addEntity(u32 entityID) {
        m_entities.pushBack(entityID);
        return static_cast<u32>(m_entities.size() - 1);
    }
    
    u32 removeEntity(u32 indexInArchetype) {
        if (indexInArchetype >= m_entities.size()) {
            return u32_max;
        }
        
        m_poolRegistry.forEach([indexInArchetype](u32 cid, void* pool, CopyFunc copyFunc, CreatePoolFunc createFunc, RemoveFunc removeFunc) {
            if (pool && removeFunc) {
                removeFunc(pool, indexInArchetype);
            }
        });
        
        u32 swappedEntity = u32_max;
        if (indexInArchetype != m_entities.size() - 1) {
            swappedEntity = m_entities[m_entities.size() - 1];
            m_entities[indexInArchetype] = swappedEntity;
        }
        m_entities.popBack();
        return swappedEntity;
    }
    
    template<typename T>
    T* getComponent(u32 indexInArchetype) {
        u32 componentID = ComponentID::get<T>();
        void* pool = m_poolRegistry.getPool(componentID);
        
        if (pool == nullptr) {
            return nullptr;
        }
        
        ComponentPool<T>* typedPool = static_cast<ComponentPool<T>*>(pool);
        return typedPool->get(indexInArchetype);
    }
    
    template<typename T>
    const T* getComponent(u32 indexInArchetype) const {
        u32 componentID = ComponentID::get<T>();
        void* pool = m_poolRegistry.getPool(componentID);
        
        if (pool == nullptr) {
            return nullptr;
        }
        
        const ComponentPool<T>* typedPool = static_cast<const ComponentPool<T>*>(pool);
        return typedPool->get(indexInArchetype);
    }
    
    template<typename T>
    u32 addComponent(u32 indexInArchetype, const T& component) {
        u32 componentID = ComponentID::get<T>();
        void* pool = m_poolRegistry.getPool(componentID);
        
        if (pool != nullptr) {
            ComponentPool<T>* typedPool = static_cast<ComponentPool<T>*>(pool);
            return typedPool->add(component);
        }
        
        ComponentPool<T>* newPool = new ComponentPool<T>(m_allocator);
        m_poolRegistry.set(componentID, newPool, &copyComponentImpl<T>, &createPoolImpl<T>, &removeComponentImpl<T>);
        return newPool->add(component);
    }
    
    void* copyComponent(u32 componentID, u32 srcIndex, const Archetype* srcArchetype) {
        if (!srcArchetype->m_poolRegistry.has(componentID)) {
            return nullptr;
        }
        
        void* srcPool = srcArchetype->m_poolRegistry.getPool(componentID);
        CopyFunc srcCopyFunc = srcArchetype->m_poolRegistry.getCopyFunc(componentID);
        CreatePoolFunc srcCreatePoolFunc = srcArchetype->m_poolRegistry.getCreatePoolFunc(componentID);
        RemoveFunc srcRemoveFunc = srcArchetype->m_poolRegistry.getRemoveFunc(componentID);
        
        void* dstPool = m_poolRegistry.getPool(componentID);
        
        if (dstPool != nullptr) {
            srcCopyFunc(dstPool, srcPool, srcIndex);
            return dstPool;
        }
        
        void* newPool = srcCreatePoolFunc(m_allocator);
        m_poolRegistry.set(componentID, newPool, srcCopyFunc, srcCreatePoolFunc, srcRemoveFunc);
        srcCopyFunc(newPool, srcPool, srcIndex);
        return newPool;
    }

private:
    using CopyFunc = void (*)(void* dstPool, const void* srcPool, u32 srcIndex);
    using CreatePoolFunc = void* (*)(IAllocator* allocator);
    using RemoveFunc = void (*)(void* pool, u32 index);
    
    template<typename T>
    static void copyComponentImpl(void* dstPool, const void* srcPool, u32 srcIndex) {
        ComponentPool<T>* dst = static_cast<ComponentPool<T>*>(dstPool);
        const ComponentPool<T>* src = static_cast<const ComponentPool<T>*>(srcPool);
        const T* srcComponent = src->get(srcIndex);
        if (srcComponent) {
            dst->add(*srcComponent);
        }
    }
    
    template<typename T>
    static void* createPoolImpl(IAllocator* allocator) {
        return new ComponentPool<T>(allocator);
    }
    
    template<typename T>
    static void removeComponentImpl(void* pool, u32 index) {
        ComponentPool<T>* typedPool = static_cast<ComponentPool<T>*>(pool);
        typedPool->remove(index);
    }
    
    struct PoolEntry {
        void* pool;
        CopyFunc copyFunc;
        CreatePoolFunc createPoolFunc;
        RemoveFunc removeFunc;
    };
    
    class PoolRegistry {
    public:
        static constexpr u32 MAX_COMPONENTS = 64;
        
        PoolRegistry()
            : m_validIDs(0) {
            for (u32 i = 0; i < MAX_COMPONENTS; ++i) {
                m_pools[i].pool = nullptr;
                m_pools[i].copyFunc = nullptr;
                m_pools[i].createPoolFunc = nullptr;
                m_pools[i].removeFunc = nullptr;
            }
        }
        
        void set(u32 componentID, void* pool, CopyFunc copyFunc, CreatePoolFunc createPoolFunc, RemoveFunc removeFunc) {
            if (componentID >= MAX_COMPONENTS) {
                return;
            }
            m_pools[componentID].pool = pool;
            m_pools[componentID].copyFunc = copyFunc;
            m_pools[componentID].createPoolFunc = createPoolFunc;
            m_pools[componentID].removeFunc = removeFunc;
            m_validIDs = m_validIDs | (1ULL << componentID);
        }
        
        void* getPool(u32 componentID) const {
            if (componentID >= MAX_COMPONENTS) {
                return nullptr;
            }
            return m_pools[componentID].pool;
        }
        
        CopyFunc getCopyFunc(u32 componentID) const {
            if (componentID >= MAX_COMPONENTS) {
                return nullptr;
            }
            return m_pools[componentID].copyFunc;
        }
        
        CreatePoolFunc getCreatePoolFunc(u32 componentID) const {
            if (componentID >= MAX_COMPONENTS) {
                return nullptr;
            }
            return m_pools[componentID].createPoolFunc;
        }
        
        RemoveFunc getRemoveFunc(u32 componentID) const {
            if (componentID >= MAX_COMPONENTS) {
                return nullptr;
            }
            return m_pools[componentID].removeFunc;
        }
        
        template<typename Fn>
        void forEach(Fn&& fn) const {
            for (u32 i = 0; i < MAX_COMPONENTS; ++i) {
                if ((m_validIDs & (1ULL << i)) != 0) {
                    fn(i, m_pools[i].pool, m_pools[i].copyFunc, m_pools[i].createPoolFunc, m_pools[i].removeFunc);
                }
            }
        }
        
        bool has(u32 componentID) const {
            if (componentID >= MAX_COMPONENTS) {
                return false;
            }
            return (m_validIDs & (1ULL << componentID)) != 0;
        }
        
    private:
        PoolEntry m_pools[MAX_COMPONENTS];
        u64 m_validIDs;
    };
    
    ComponentSet m_componentSet;
    IAllocator* m_allocator;
    u32 m_archetypeIndex;
    Vector<u32> m_entities;
    PoolRegistry m_poolRegistry;
};

} // namespace ECS
} // namespace Caffeine
