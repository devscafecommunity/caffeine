/**
 * @file Entity.hpp
 * @brief Lightweight entity handle for ECS
 * 
 * Entity is just a reference (u32 ID + World*). Component data lives in archetypes.
 */

#pragma once

#include "core/Types.hpp"

namespace Caffeine::ECS {

class World;

class Entity {
public:
    static const Entity INVALID;
    
    Entity() : m_id(u32_max), m_world(nullptr) {}
    explicit Entity(u32 id, World* world) : m_id(id), m_world(world) {}
    
    u32 id() const { return m_id; }
    bool isValid() const { return m_id != u32_max; }
    
    template<typename T, typename... Args>
    T& add(Args&&... args);
    
    template<typename T>
    void remove();
    
    template<typename T>
    bool has() const;
    
    template<typename T>
    T* get();
    
    template<typename T>
    T& getOrAdd();
    
    void destroy();
    
    explicit operator bool() const { return isValid(); }
    bool operator==(const Entity& o) const { return m_id == o.m_id; }
    
private:
    u32 m_id;
    World* m_world;
    
    friend class World;
};

inline const Entity Entity::INVALID = Entity();

}
