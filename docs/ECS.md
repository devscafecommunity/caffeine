# Caffeine ECS Core Documentation

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [Components](#components)
4. [Entities](#entities)
5. [Queries](#queries)
6. [Systems](#systems)
7. [Command Buffer](#command-buffer)
8. [Performance](#performance)
9. [API Reference](#api-reference)
10. [Common Patterns](#common-patterns)
11. [Troubleshooting](#troubleshooting)

---

## Introduction

The Caffeine ECS (Entity Component System) is a high-performance, thread-safe architecture for managing game entities and their behaviors. It solves the fundamental game development challenge: organizing entities (players, enemies, objects) and their behaviors (movement, rendering, physics) in a way that's efficient, maintainable, and scalable.

### Why ECS?

Traditional object-oriented game programming uses deep hierarchies and inheritance, which leads to:
- **Fragile hierarchies**: Adding new behavior types requires modifying base classes
- **Poor cache locality**: Game logic scatters across objects in memory
- **Difficult parallelization**: Entity interdependencies prevent safe multi-threaded execution

The Caffeine ECS approach uses **composition over inheritance**:
- **Components** are small data containers (e.g., Position, Velocity, Health)
- **Entities** are just lightweight IDs that aggregate components
- **Systems** are pure functions that operate on entities matching a query
- **Queries** efficiently find entities with specific component combinations

Result: Better performance, easier to parallelize, and more flexible architecture.

### Performance Targets (Achieved)

- ✅ **100K entities** with 3 components each
- ✅ **Query execution < 1.2ms** (frame budget at 60fps = 16.67ms)
- ✅ **Parallel scaling 2.94x** on multi-core
- ✅ **Zero-copy component access** with dense arrays
- ✅ **Thread-safe by design** without locks or mutex contention

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                          World                              │
│  (Manages entities, archetypes, and component storage)      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │ Archetype A  │  │ Archetype B  │  │ Archetype C  │     │
│  │ [Pos, Vel]   │  │ [Pos, Vel,   │  │ [Pos]        │     │
│  │              │  │  Health]     │  │              │     │
│  │ Entity 0 ────┼─→ pos[0]      │  │ Entity 5 ────┼─→ pos[0] │
│  │ Entity 1 ────┼─→ pos[1]      │  │              │     │
│  │ Entity 2 ────┼─→ pos[2]      │  │              │     │
│  │              │  │ vel[0]      │  │              │     │
│  │              │  │ vel[1]      │  │              │     │
│  │              │  │ vel[2]      │  │              │     │
│  │              │  │ health[0]   │  │              │     │
│  │              │  │ health[1]   │  │              │     │
│  │              │  │ health[2]   │  │              │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
│                                                             │
│  Queries efficiently find matching archetypes              │
│  Systems operate on query results                          │
└─────────────────────────────────────────────────────────────┘
```

### Key Concepts

**World**: Container for all entities and component data. Central to the ECS, handles:
- Entity creation and destruction
- Component addition/removal (with archetype migration)
- Query execution (forEach, forEachParallel)

**Archetype**: Groups entities with identical component signatures. For example:
- Archetype [Position, Velocity]: Moving entities
- Archetype [Position, Velocity, Health]: Moving entities with health
- Archetype [Position]: Static entities

All entities in an archetype store components in dense arrays for cache efficiency.

**ComponentID**: Type-safe identifier for component types. Automatically assigned at compile-time via template specialization.

**ComponentSet**: Bitmask representing which components an entity has. Used for:
- Fast archetype matching (O(1))
- Query filtering
- Archetype lookups

**Entity**: Lightweight handle (32-bit ID + World pointer). Provides methods to:
- Query components: `entity.has<Position>()`
- Get components: `entity.get<Position>()`
- Defer operations: queue changes via CommandBuffer

**ComponentQuery**: Builder for specifying query patterns:
- `with<A, B>()`: Require components A and B
- `without<C>()`: Exclude component C
- `any<D, E>()`: Require at least one of D or E

**System**: Implements ISystem interface, updates entities each frame. Example:
- MovementSystem updates positions based on velocities
- RenderSystem draws sprites to screen
- PhysicsSystem resolves collisions

---

## Components

Components are small POD (Plain Old Data) structures containing only state, no behavior.

### Predefined Components

The ECS provides 8 predefined components for rapid game development:

```cpp
// Position in 2D space
struct Position2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

// Velocity (rate of change)
struct Velocity2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

// Acceleration
struct Acceleration2D {
    f32 x = 0.0f;
    f32 y = 0.0f;
};

// Rotation angle (radians)
struct Rotation {
    f32 angle = 0.0f;
};

// Scale factors
struct Scale2D {
    f32 x = 1.0f;
    f32 y = 1.0f;
};

// Visual representation
struct Sprite {
    std::string name;      // Texture/sprite name
    u32 frameIndex = 0;    // For sprite sheets
};

// Health system
struct Health {
    u32 current = 100;
    u32 max = 100;
};

// Zero-cost marker component for tagging
struct Tag { };
```

### Custom Components

Define your own components for game-specific data:

```cpp
// Damage-dealing weapon
struct DamageDealer {
    u32 damagePerHit = 10;
    f32 cooldownTime = 0.2f;
    f32 timeSinceLastHit = 0.0f;
};

// Inventory system
struct Inventory {
    std::vector<Item> items;
    u32 maxCapacity = 20;
};

// AI state machine
struct AIController {
    enum State { Idle, Chasing, Attacking } state = Idle;
    Entity target = Entity();
    f32 visionRange = 100.0f;
};
```

### Component Guidelines

- **Keep it simple**: Components should contain only data (POD structures)
- **No behavior**: Avoid methods; use systems instead
- **Copyable**: Components must be trivially copyable
- **Small**: Cache locality matters; aim for < 64 bytes per component
- **Default-constructible**: Components need default values

---

## Entities

Entities are the basic unit in the ECS. An entity is just a unique ID that aggregates components.

### Entity Lifecycle

```cpp
// Create entity with initial components
Entity player = world.create<Position2D, Velocity2D>(
    Position2D{10.0f, 20.0f},
    Velocity2D{5.0f, 0.0f}
);

// Add component later
world.add<Health>(player, Health{100, 100});

// Check if entity has component
if (world.has<Position2D>(player)) {
    // ...
}

// Get component reference
Position2D* pos = world.get<Position2D>(player);
if (pos) {
    pos->x += 5.0f;  // Modify in-place
}

// Remove component (entity moves to new archetype)
world.remove<Velocity2D>(player);

// Destroy entity
world.destroy(player);
```

### Important Notes

- **Entity IDs are lightweight** (32-bit), copy them freely
- **Component access is O(1)** after archetype lookup
- **Archetype migration is transparent** when adding/removing components
- **Entities are always valid** after creation (checked with assertions)

---

## Queries

Queries select entities matching specific component patterns, enabling efficient system updates.

### Query Builder

```cpp
// Require Position and Velocity
ComponentQuery query1 = ComponentQuery()
    .with<Position2D, Velocity2D>();

// Require Position, exclude Disabled
ComponentQuery query2 = ComponentQuery()
    .with<Position2D>()
    .without<Disabled>();

// Require one of several components
ComponentQuery query3 = ComponentQuery()
    .with<Position2D>()
    .any<Health, Shield>();  // Require Health OR Shield
```

### Single-Threaded Iteration

```cpp
world.forEach<Position2D, Velocity2D>(
    ComponentQuery().with<Position2D, Velocity2D>(),
    [deltaTime](Entity e, Position2D& pos, Velocity2D& vel) {
        pos.x += vel.x * deltaTime;
        pos.y += vel.y * deltaTime;
    }
);
```

### Parallel Iteration

```cpp
world.forEachParallel<Position2D, Velocity2D>(
    ComponentQuery().with<Position2D, Velocity2D>(),
    [deltaTime](Entity e, Position2D& pos, Velocity2D& vel) {
        pos.x += vel.x * deltaTime;  // Thread-safe: each entity processed by one thread
        pos.y += vel.y * deltaTime;
    },
    &jobSystem  // Use JobSystem for parallelization
);
```

### Query Performance

Query execution is O(N) where N = number of matching entities:
- **Archetype filtering** is O(1) per archetype
- **Entity iteration** within archetype is dense array access (cache-efficient)
- **No sorting or searching** required
- **Parallel scaling**: Nearly linear on multi-core CPUs

---

## Systems

Systems encapsulate game logic by operating on entities matching queries.

### Basic System

```cpp
class MovementSystem : public ISystem {
public:
    void onUpdate(World& world, f32 deltaTime) override {
        world.forEach<Position2D, Velocity2D>(
            ComponentQuery().with<Position2D, Velocity2D>(),
            [deltaTime](Entity e, Position2D& pos, Velocity2D& vel) {
                pos.x += vel.x * deltaTime;
                pos.y += vel.y * deltaTime;
            }
        );
    }
};
```

### System Registry

```cpp
SystemRegistry systems;
systems.add(std::make_unique<MovementSystem>());
systems.add(std::make_unique<PhysicsSystem>());
systems.add(std::make_unique<RenderSystem>());

// Game loop
while (running) {
    systems.updateAll(world, deltaTime);
    render();
}
```

### Deferred Operations in Systems

Avoid modifying the world directly in forEach callbacks (breaks iteration). Use CommandBuffer instead:

```cpp
class HealthSystem : public ISystem {
public:
    void onUpdate(World& world, f32 deltaTime) override {
        CommandBuffer commands;
        
        world.forEach<Health>(
            ComponentQuery().with<Health>(),
            [&commands](Entity e, Health& health) {
                health.current--;
                if (health.current == 0) {
                    commands.destroy(e);  // Safe: deferred until after iteration
                }
            }
        );
        
        commands.execute(world);  // Apply all changes
    }
};
```

---

## Command Buffer

CommandBuffer defers entity modifications, enabling safe concurrent updates during query execution.

### Operations

```cpp
CommandBuffer commands;

// Create entity with components
Entity newEntity = commands.create<Position2D>(
    Position2D{0.0f, 0.0f}
);

// Add component to existing entity
commands.addComponent<Velocity2D>(myEntity, Velocity2D{10.0f, 0.0f});

// Remove component
commands.removeComponent<Velocity2D>(myEntity);

// Destroy entity
commands.destroy(myEntity);

// Execute all queued operations (in order)
commands.execute(world);
```

### Why CommandBuffer?

Without deferred operations:
```cpp
// DANGEROUS! Breaks forEach iteration
world.forEach<Position>([&](Entity e, Position& pos) {
    if (shouldDestroy(e)) {
        world.destroy(e);  // Iteration undefined behavior!
    }
});
```

With CommandBuffer:
```cpp
// SAFE! Defers changes until after iteration
CommandBuffer commands;
world.forEach<Position>([&](Entity e, Position& pos) {
    if (shouldDestroy(e)) {
        commands.destroy(e);  // Queued, not executed yet
    }
});
commands.execute(world);  // All changes applied after iteration completes
```

---

## Performance

### Benchmarks (100K entities with 3 components each)

| Operation | Time | Frame Budget (60fps) |
|-----------|------|---------------------|
| Query forEach (100K) | 1.15 ms | 93% remaining |
| Query forEach (50K filtered) | 0.29 ms | 98% remaining |
| System update (3 systems) | 0.34 ms | 98% remaining |
| Component add/remove (10K ops) | 5.29 ms | 68% remaining |
| Entity destruction (10K) | 63.4 ms | (batch operation) |
| Parallel speedup | 2.94x | - |

### Performance Tips

1. **Use forEachParallel** for large datasets (1000+ entities)
2. **Batch operations** to reduce archetype migrations
3. **Keep components small** (< 64 bytes for cache efficiency)
4. **Use ComponentQuery filters** to reduce iteration size
5. **Profile with benchmarks** to identify bottlenecks

### Parallel Scaling

The ECS automatically scales to multi-core CPUs:
- Work partitioned by archetype (no shared state between threads)
- Each archetype processed by independent thread
- No locks or mutex contention
- Linear scaling up to CPU core count

---

## API Reference

### World Class

```cpp
class World {
public:
    // Entity lifecycle
    Entity create();  // Create empty entity
    template<typename... Ts>
    Entity create(const Ts&... components);  // Create with components
    
    bool destroy(Entity e);  // Destroy entity
    
    // Component access
    template<typename T>
    bool has(Entity e) const;  // Check if entity has component
    
    template<typename T>
    T* get(Entity e);  // Get component reference (nullptr if not present)
    
    template<typename T>
    T* add(Entity e, const T& component);  // Add component
    
    template<typename T>
    bool remove(Entity e);  // Remove component
    
    // Query execution
    template<typename... Ts, typename Callback>
    void forEach(const ComponentQuery& query, Callback cb);
    
    template<typename... Ts, typename Callback>
    void forEachParallel(const ComponentQuery& query, Callback cb,
                         Caffeine::Threading::JobSystem* jobSystem);
};
```

### ComponentQuery Class

```cpp
class ComponentQuery {
public:
    // Builder methods
    template<typename... Ts>
    ComponentQuery& with();  // Require components
    
    template<typename... Ts>
    ComponentQuery& without();  // Exclude components
    
    template<typename... Ts>
    ComponentQuery& any();  // Require at least one
    
    bool matches(const Archetype* archetype) const;
};
```

### ISystem Interface

```cpp
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void onUpdate(World& world, f32 deltaTime) = 0;
};
```

### SystemRegistry Class

```cpp
class SystemRegistry {
public:
    void add(std::unique_ptr<ISystem> system);
    void updateAll(World& world, f32 deltaTime);
    usize count() const;
};
```

### CommandBuffer Class

```cpp
class CommandBuffer {
public:
    Entity create();  // Queue entity creation
    template<typename T>
    void addComponent(Entity e, const T& component);  // Queue add
    
    template<typename T>
    void removeComponent(Entity e);  // Queue remove
    
    void destroy(Entity e);  // Queue destroy
    void execute(World& world);  // Execute all queued operations
};
```

---

## Common Patterns

### Pattern 1: Movement System

```cpp
class MovementSystem : public ISystem {
    void onUpdate(World& world, f32 deltaTime) override {
        world.forEach<Position2D, Velocity2D>(
            ComponentQuery().with<Position2D, Velocity2D>(),
            [deltaTime](Entity e, Position2D& pos, Velocity2D& vel) {
                pos.x += vel.x * deltaTime;
                pos.y += vel.y * deltaTime;
            }
        );
    }
};
```

### Pattern 2: Damage and Destruction

```cpp
class HealthSystem : public ISystem {
    void onUpdate(World& world, f32 deltaTime) override {
        CommandBuffer commands;
        
        world.forEach<Health>(
            ComponentQuery().with<Health>(),
            [&commands](Entity e, Health& health) {
                if (health.current <= 0) {
                    commands.destroy(e);
                }
            }
        );
        
        commands.execute(world);
    }
};
```

### Pattern 3: Conditional Processing

```cpp
// Only process entities with Health but optionally Shield
world.forEach<Health>(
    ComponentQuery().with<Health>().any<Shield, Armor>(),
    [](Entity e, Health& health) {
        // This entity has Health and at least one of Shield/Armor
    }
);
```

### Pattern 4: Filtering with Without

```cpp
// Update only active entities (no Disabled tag)
world.forEach<Position2D, Velocity2D>(
    ComponentQuery()
        .with<Position2D, Velocity2D>()
        .without<Disabled>(),
    [deltaTime](Entity e, Position2D& pos, Velocity2D& vel) {
        pos.x += vel.x * deltaTime;
        pos.y += vel.y * deltaTime;
    }
);
```

---

## Troubleshooting

### Issue: "Entity is not valid"

**Cause**: Accessing an entity after it was destroyed.

**Solution**: Check entity validity before access. Consider using CommandBuffer to defer destruction.

```cpp
if (entity.isValid()) {
    // Safe to access
}
```

### Issue: "Component not found"

**Cause**: Trying to access a component the entity doesn't have.

**Solution**: Check with `has<T>()` first or use `get<T>()` which returns nullptr.

```cpp
if (world.has<Position2D>(entity)) {
    auto pos = world.get<Position2D>(entity);
}
```

### Issue: "Iteration broken" / "Undefined behavior"

**Cause**: Modifying the world directly inside forEach.

**Solution**: Use CommandBuffer to queue changes.

```cpp
// WRONG:
world.forEach<Position>([&](Entity e, Position& pos) {
    if (condition) world.destroy(e);  // Breaks iteration!
});

// CORRECT:
CommandBuffer commands;
world.forEach<Position>([&](Entity e, Position& pos) {
    if (condition) commands.destroy(e);  // Safe, deferred
});
commands.execute(world);
```

### Issue: "Performance degradation"

**Cause**: Too many small queries or sequential iteration on large datasets.

**Solution**: 
- Batch operations (reduce archetype migrations)
- Use forEachParallel for 1000+ entities
- Profile with benchmarks to find bottlenecks

---

## Next Steps

- Read [ECS_EXAMPLES.md](ECS_EXAMPLES.md) for practical code examples
- Check the benchmark suite (`tests/ecs/test_benchmarks.cpp`) for performance characteristics
- Explore existing systems in your game project for inspiration
- Join the Caffeine community for support and discussion

Happy coding! 🚀
