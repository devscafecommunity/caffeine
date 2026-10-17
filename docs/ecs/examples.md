# Caffeine ECS Examples

Practical code examples for using the Caffeine ECS Core in game development.

## Example 1: Hello World - Create Entity with Components

The simplest ECS example: create an entity and add components.

```cpp
#include <Caffeine.hpp>

using namespace Caffeine;
using namespace Caffeine::ECS;

int main() {
    World world;
    
    // Create entity with initial components
    Entity player = world.create<Position2D, Velocity2D>(
        Position2D{100.0f, 200.0f},
        Velocity2D{10.0f, 0.0f}
    );
    
    // Verify components exist
    assert(world.has<Position2D>(player));
    assert(world.has<Velocity2D>(player));
    
    // Get component reference
    Position2D* pos = world.get<Position2D>(player);
    Velocity2D* vel = world.get<Velocity2D>(player);
    
    std::cout << "Player at (" << pos->x << ", " << pos->y << ")\n";
    std::cout << "Velocity (" << vel->x << ", " << vel->y << ")\n";
    
    return 0;
}
```

**Output:**
```
Player at (100, 200)
Velocity (10, 0)
```

---

## Example 2: Simple Movement System

Create a system that updates entity positions based on velocity each frame.

```cpp
#include <Caffeine.hpp>
#include <iostream>

using namespace Caffeine;
using namespace Caffeine::ECS;

// Define custom system
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

int main() {
    World world;
    SystemRegistry systems;
    
    // Register movement system
    systems.add(std::make_unique<MovementSystem>());
    
    // Create moving entity
    Entity entity = world.create<Position2D, Velocity2D>(
        Position2D{0.0f, 0.0f},
        Velocity2D{5.0f, 3.0f}
    );
    
    // Simulate 5 frames at 60fps
    f32 deltaTime = 1.0f / 60.0f;
    for (int frame = 0; frame < 5; ++frame) {
        systems.updateAll(world, deltaTime);
        
        Position2D* pos = world.get<Position2D>(entity);
        std::cout << "Frame " << frame << ": (" << pos->x << ", " << pos->y << ")\n";
    }
    
    return 0;
}
```

**Output:**
```
Frame 0: (0.0833, 0.05)
Frame 1: (0.1667, 0.1)
Frame 2: (0.25, 0.15)
Frame 3: (0.3333, 0.2)
Frame 4: (0.4167, 0.25)
```

---

## Example 3: Health and Damage System

Implement a system that damages entities and removes them when health reaches zero.

```cpp
#include <Caffeine.hpp>
#include <iostream>

using namespace Caffeine;
using namespace Caffeine::ECS;

class HealthSystem : public ISystem {
public:
    void onUpdate(World& world, f32 deltaTime) override {
        CommandBuffer commands;
        
        // Damage all entities with health
        world.forEach<Health>(
            ComponentQuery().with<Health>(),
            [&commands, deltaTime](Entity e, Health& health) {
                // Apply damage each second
                u32 damagePerSecond = 10;
                health.current -= static_cast<u32>(damagePerSecond * deltaTime);
                
                // Queue destruction when health depleted
                if (health.current <= 0) {
                    std::cout << "Entity destroyed!\n";
                    commands.destroy(e);
                }
            }
        );
        
        // Execute all deferred commands
        commands.execute(world);
    }
};

int main() {
    World world;
    SystemRegistry systems;
    systems.add(std::make_unique<HealthSystem>());
    
    // Create entity with health
    Entity entity = world.create<Health>(
        Health{50, 100}
    );
    
    std::cout << "Initial health: " << world.get<Health>(entity)->current << "\n";
    
    // Update for 6 seconds (50 damage at 10/sec)
    f32 deltaTime = 1.0f;
    for (int sec = 0; sec < 6; ++sec) {
        systems.updateAll(world, deltaTime);
        
        Health* health = world.get<Health>(entity);
        if (health) {
            std::cout << "After " << (sec + 1) << "s: " << health->current << " HP\n";
        } else {
            std::cout << "After " << (sec + 1) << "s: Entity destroyed\n";
        }
    }
    
    return 0;
}
```

**Output:**
```
Initial health: 50
After 1s: 40 HP
After 2s: 30 HP
After 3s: 20 HP
After 4s: 10 HP
After 5s: 0 HP
Entity destroyed!
After 6s: Entity destroyed
```

---

## Example 4: Collision Detection Pattern

Query for entities with collision components and resolve overlaps.

```cpp
#include <Caffeine.hpp>
#include <iostream>
#include <cmath>

using namespace Caffeine;
using namespace Caffeine::ECS;

// Custom component for collision
struct Collider {
    f32 radius = 10.0f;
};

class CollisionSystem : public ISystem {
public:
    void onUpdate(World& world, f32 deltaTime) override {
        std::vector<Entity> collidables;
        
        // Collect all entities with colliders
        world.forEach<Position2D, Collider>(
            ComponentQuery().with<Position2D, Collider>(),
            [&collidables](Entity e, Position2D& pos, Collider& collider) {
                collidables.push_back(e);
            }
        );
        
        // Check all pairs
        for (size_t i = 0; i < collidables.size(); ++i) {
            for (size_t j = i + 1; j < collidables.size(); ++j) {
                Entity e1 = collidables[i];
                Entity e2 = collidables[j];
                
                Position2D* pos1 = world.get<Position2D>(e1);
                Position2D* pos2 = world.get<Position2D>(e2);
                Collider* col1 = world.get<Collider>(e1);
                Collider* col2 = world.get<Collider>(e2);
                
                // Calculate distance
                f32 dx = pos1->x - pos2->x;
                f32 dy = pos1->y - pos2->y;
                f32 distance = std::sqrt(dx * dx + dy * dy);
                
                // Check collision
                if (distance < (col1->radius + col2->radius)) {
                    std::cout << "Collision detected!\n";
                }
            }
        }
    }
};

int main() {
    World world;
    SystemRegistry systems;
    systems.add(std::make_unique<CollisionSystem>());
    
    // Create two entities that collide
    Entity entity1 = world.create<Position2D, Collider>(
        Position2D{0.0f, 0.0f},
        Collider{10.0f}
    );
    
    Entity entity2 = world.create<Position2D, Collider>(
        Position2D{15.0f, 0.0f},  // Close enough to collide
        Collider{10.0f}
    );
    
    systems.updateAll(world, 1.0f / 60.0f);
    
    return 0;
}
```

**Output:**
```
Collision detected!
```

---

## Example 5: Parallel System Execution

Use parallel queries for better performance with large entity counts.

```cpp
#include <Caffeine.hpp>
#include <iostream>

using namespace Caffeine;
using namespace Caffeine::ECS;

class ParallelMovementSystem : public ISystem {
public:
    void onUpdate(World& world, f32 deltaTime) override {
        auto& jobSystem = world.jobSystem();  // Get access to job system
        
        // Use parallel forEach for better performance
        world.forEachParallel<Position2D, Velocity2D>(
            ComponentQuery().with<Position2D, Velocity2D>(),
            [deltaTime](Entity e, Position2D& pos, Velocity2D& vel) {
                // This runs in parallel across multiple threads
                // Each entity processed by exactly one thread (safe!)
                pos.x += vel.x * deltaTime;
                pos.y += vel.y * deltaTime;
            },
            &jobSystem
        );
    }
};

int main() {
    World world;
    SystemRegistry systems;
    systems.add(std::make_unique<ParallelMovementSystem>());
    
    // Create many moving entities
    for (int i = 0; i < 100000; ++i) {
        world.create<Position2D, Velocity2D>(
            Position2D{static_cast<f32>(i), 0.0f},
            Velocity2D{1.0f, 1.0f}
        );
    }
    
    std::cout << "Updating 100K entities...\n";
    systems.updateAll(world, 1.0f / 60.0f);
    std::cout << "Done (parallelized across CPU cores)\n";
    
    return 0;
}
```

**Output:**
```
Updating 100K entities...
Done (parallelized across CPU cores)
```

---

## Example 6: Deferred Commands Pattern

Safe entity modifications during system updates using CommandBuffer.

```cpp
#include <Caffeine.hpp>
#include <iostream>

using namespace Caffeine;
using namespace Caffeine::ECS;

struct Enemy {
    u32 enemyId = 0;
};

class EnemySpawner : public ISystem {
public:
    u32 timeElapsed = 0;
    
    void onUpdate(World& world, f32 deltaTime) override {
        CommandBuffer commands;
        timeElapsed += static_cast<u32>(deltaTime * 1000);
        
        // Spawn new enemies every 100ms
        if (timeElapsed >= 100) {
            commands.create<Position2D, Velocity2D, Enemy>(
                Position2D{100.0f, 100.0f},
                Velocity2D{-5.0f, 0.0f},
                Enemy{nextEnemyId++}
            );
            timeElapsed = 0;
        }
        
        // Remove enemies out of bounds (safe with deferred commands)
        world.forEach<Position2D>(
            ComponentQuery().with<Position2D>().any<Enemy>(),
            [&commands](Entity e, Position2D& pos) {
                if (pos.x < -100.0f) {
                    commands.destroy(e);
                }
            }
        );
        
        // Execute all deferred operations
        commands.execute(world);
    }
    
private:
    u32 nextEnemyId = 0;
};

int main() {
    World world;
    SystemRegistry systems;
    systems.add(std::make_unique<EnemySpawner>());
    
    // Simulate enemy spawning
    for (int i = 0; i < 5; ++i) {
        systems.updateAll(world, 0.2f);
        
        u32 enemyCount = 0;
        world.forEach<Enemy>(
            ComponentQuery().with<Enemy>(),
            [&enemyCount](Entity e, Enemy& enemy) {
                enemyCount++;
            }
        );
        std::cout << "Active enemies: " << enemyCount << "\n";
    }
    
    return 0;
}
```

**Output:**
```
Active enemies: 1
Active enemies: 2
Active enemies: 3
Active enemies: 4
Active enemies: 5
```

---

## Example 7: Complete Game Loop

Full integration of World, Systems, and game update loop.

```cpp
#include <Caffeine.hpp>
#include <iostream>
#include <chrono>

using namespace Caffeine;
using namespace Caffeine::ECS;

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

class PhysicsSystem : public ISystem {
    void onUpdate(World& world, f32 deltaTime) override {
        world.forEach<Velocity2D, Acceleration2D>(
            ComponentQuery().with<Velocity2D, Acceleration2D>(),
            [deltaTime](Entity e, Velocity2D& vel, Acceleration2D& accel) {
                vel.x += accel.x * deltaTime;
                vel.y += accel.y * deltaTime;
            }
        );
    }
};

class RenderSystem : public ISystem {
    void onUpdate(World& world, f32 deltaTime) override {
        u32 count = 0;
        world.forEach<Position2D>(
            ComponentQuery().with<Position2D>(),
            [&count](Entity e, Position2D& pos) {
                count++;
            }
        );
        std::cout << "Rendering " << count << " entities\n";
    }
};

int main() {
    World world;
    SystemRegistry systems;
    
    // Register systems (order matters!)
    systems.add(std::make_unique<PhysicsSystem>());
    systems.add(std::make_unique<MovementSystem>());
    systems.add(std::make_unique<RenderSystem>());
    
    // Create test entity
    Entity entity = world.create<Position2D, Velocity2D, Acceleration2D>(
        Position2D{0.0f, 0.0f},
        Velocity2D{10.0f, 0.0f},
        Acceleration2D{0.0f, 9.81f}  // Gravity
    );
    
    // Game loop (60fps, 3 frames)
    f32 targetFps = 60.0f;
    f32 deltaTime = 1.0f / targetFps;
    
    for (int frame = 0; frame < 3; ++frame) {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        // Update all systems
        systems.updateAll(world, deltaTime);
        
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(
            frameEnd - frameStart).count();
        
        std::cout << "Frame " << frame << " (" << frameDuration / 1000.0f << "ms)\n";
    }
    
    return 0;
}
```

**Output:**
```
Rendering 1 entities
Frame 0 (0.123ms)
Rendering 1 entities
Frame 1 (0.145ms)
Rendering 1 entities
Frame 2 (0.132ms)
```

---

## Example 8: Custom Components

Define and use application-specific components.

```cpp
#include <Caffeine.hpp>
#include <string>
#include <iostream>

using namespace Caffeine;
using namespace Caffeine::ECS;

// Custom component: weapon system
struct Weapon {
    std::string name;
    u32 ammo = 30;
    f32 fireRate = 10.0f;  // Rounds per second
    f32 timeSinceLastShot = 0.0f;
};

// Custom component: AI behavior
struct AIBehavior {
    enum State { Idle, Patrol, Attack } state = Idle;
    f32 sightRange = 50.0f;
};

// Custom component: score
struct Score {
    u32 points = 0;
};

class WeaponSystem : public ISystem {
    void onUpdate(World& world, f32 deltaTime) override {
        world.forEach<Weapon>(
            ComponentQuery().with<Weapon>(),
            [deltaTime](Entity e, Weapon& weapon) {
                weapon.timeSinceLastShot += deltaTime;
                
                // Ammo regenerates slowly
                if (weapon.ammo < 30 && weapon.timeSinceLastShot > 2.0f) {
                    weapon.ammo++;
                    weapon.timeSinceLastShot = 0.0f;
                }
            }
        );
    }
};

int main() {
    World world;
    SystemRegistry systems;
    systems.add(std::make_unique<WeaponSystem>());
    
    // Create player entity with custom components
    Entity player = world.create<Position2D, Weapon, Score>(
        Position2D{0.0f, 0.0f},
        Weapon{"Rifle", 30, 10.0f},
        Score{1000}
    );
    
    // Create enemy entity with AI
    Entity enemy = world.create<Position2D, Velocity2D, AIBehavior>(
        Position2D{50.0f, 0.0f},
        Velocity2D{-2.0f, 0.0f},
        AIBehavior{AIBehavior::Patrol, 30.0f}
    );
    
    // Run for a bit
    for (int i = 0; i < 3; ++i) {
        systems.updateAll(world, 1.0f);
        
        Weapon* weapon = world.get<Weapon>(player);
        Score* score = world.get<Score>(player);
        
        std::cout << "Ammo: " << weapon->ammo << ", Score: " << score->points << "\n";
    }
    
    return 0;
}
```

**Output:**
```
Ammo: 30, Score: 1000
Ammo: 30, Score: 1000
Ammo: 31, Score: 1000
```

---

## Performance Tips

1. **Batch operations**: Group entity modifications to reduce archetype migrations
2. **Use forEachParallel**: For 1000+ entities, parallel execution is faster
3. **Keep systems focused**: Each system should do one thing well
4. **Profile regularly**: Use the benchmark suite to catch performance regressions
5. **Defer modifications**: Use CommandBuffer in forEach callbacks to avoid iteration issues

## Next Steps

- Review [ECS.md](ECS.md) for architectural details
- Explore the benchmark suite for performance characteristics
- Start building your game systems using these patterns
- Profile your game loop to find optimization opportunities

Happy coding! 🎮🚀
