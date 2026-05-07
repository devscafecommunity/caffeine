/**
 * @file CommandBuffer.hpp
 * @brief Deferred entity operation queue for safe iteration
 * @copyright Copyright (c) 2025 Caffeine Engine
 */

#pragma once

#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "ecs/ComponentID.hpp"
#include "containers/Vector.hpp"
#include <memory>
#include <utility>

namespace Caffeine::ECS {

class World;

struct ICommand {
    virtual ~ICommand() = default;
    virtual void execute(World& world) = 0;
};

class CommandBuffer {
public:
    CommandBuffer(IAllocator* allocator = nullptr);
    ~CommandBuffer();
    
    Entity create();
    
    template<typename T>
    Entity create(const T& component);
    
    template<typename T1, typename T2>
    Entity create(const T1& c1, const T2& c2);
    
    template<typename T>
    void addComponent(Entity e, const T& component);
    
    template<typename T>
    void removeComponent(Entity e);
    
    void destroy(Entity e);
    
    void execute(World& world);
    
private:
    IAllocator* m_allocator;
    Vector<std::unique_ptr<ICommand>> m_commands;
    u32 m_nextPendingID = 0;
};

struct CreateCommand : ICommand {
    u32 pendingID;
    
    explicit CreateCommand(u32 id) : pendingID(id) {}
    
    void execute(World& world) override;
};

template<typename T>
struct CreateWithComponentCommand : ICommand {
    u32 pendingID;
    T component;
    
    CreateWithComponentCommand(u32 id, const T& comp) 
        : pendingID(id), component(comp) {}
    
    void execute(World& world) override;
};

template<typename T1, typename T2>
struct CreateWithTwoComponentsCommand : ICommand {
    u32 pendingID;
    T1 component1;
    T2 component2;
    
    CreateWithTwoComponentsCommand(u32 id, const T1& c1, const T2& c2)
        : pendingID(id), component1(c1), component2(c2) {}
    
    void execute(World& world) override;
};

template<typename T>
struct AddComponentCommand : ICommand {
    Entity entity;
    T component;
    
    AddComponentCommand(Entity e, const T& comp)
        : entity(e), component(comp) {}
    
    void execute(World& world) override;
};

template<typename T>
struct RemoveComponentCommand : ICommand {
    Entity entity;
    
    explicit RemoveComponentCommand(Entity e) : entity(e) {}
    
    void execute(World& world) override;
};

struct DestroyCommand : ICommand {
    Entity entity;
    
    explicit DestroyCommand(Entity e) : entity(e) {}
    
    void execute(World& world) override;
};

inline CommandBuffer::CommandBuffer(IAllocator* allocator)
    : m_allocator(allocator)
    , m_commands(allocator) {
}

inline CommandBuffer::~CommandBuffer() = default;

inline Entity CommandBuffer::create() {
    u32 pendingID = m_nextPendingID++;
    m_commands.pushBack(std::make_unique<CreateCommand>(pendingID));
    return Entity(pendingID, nullptr);
}

template<typename T>
inline Entity CommandBuffer::create(const T& component) {
    u32 pendingID = m_nextPendingID++;
    m_commands.pushBack(std::make_unique<CreateWithComponentCommand<T>>(pendingID, component));
    return Entity(pendingID, nullptr);
}

template<typename T1, typename T2>
inline Entity CommandBuffer::create(const T1& c1, const T2& c2) {
    u32 pendingID = m_nextPendingID++;
    m_commands.pushBack(std::make_unique<CreateWithTwoComponentsCommand<T1, T2>>(pendingID, c1, c2));
    return Entity(pendingID, nullptr);
}

template<typename T>
inline void CommandBuffer::addComponent(Entity e, const T& component) {
    m_commands.pushBack(std::make_unique<AddComponentCommand<T>>(e, component));
}

template<typename T>
inline void CommandBuffer::removeComponent(Entity e) {
    m_commands.pushBack(std::make_unique<RemoveComponentCommand<T>>(e));
}

inline void CommandBuffer::destroy(Entity e) {
    m_commands.pushBack(std::make_unique<DestroyCommand>(e));
}

}

#include "ecs/World.hpp"

namespace Caffeine::ECS {

template<typename T>
void CreateWithComponentCommand<T>::execute(World& world) {
    Entity e = world.create();
    world.add<T>(e, component);
}

template<typename T1, typename T2>
void CreateWithTwoComponentsCommand<T1, T2>::execute(World& world) {
    Entity e = world.create();
    world.add<T1>(e, component1);
    world.add<T2>(e, component2);
}

template<typename T>
void AddComponentCommand<T>::execute(World& world) {
    if (entity.isValid()) {
        world.add<T>(entity, component);
    }
}

template<typename T>
void RemoveComponentCommand<T>::execute(World& world) {
    if (entity.isValid()) {
        world.remove<T>(entity);
    }
}

}
