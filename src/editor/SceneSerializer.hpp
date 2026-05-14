#pragma once
#include "core/Types.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include <string>

namespace Caffeine::Editor {
using namespace Caffeine;

class SceneSerializer {
public:
    explicit SceneSerializer(ECS::World& world) : m_world(world) {}

    // ── Binary .caf ──────────────────────────────────────────────────────────

    bool serialize(const std::string& filepath);
    bool deserialize(const std::string& filepath);

private:
    ECS::World& m_world;

    // Editor-specific component type IDs for the binary format
    static constexpr u32 kTypeName         = 0;
    static constexpr u32 kTypePosition2D   = 1;
    static constexpr u32 kTypeVelocity2D   = 2;
    static constexpr u32 kTypeAcceleration2D = 3;
    static constexpr u32 kTypeRotation     = 4;
    static constexpr u32 kTypeScale2D      = 5;
    static constexpr u32 kTypeSprite       = 6;
    static constexpr u32 kTypeHealth       = 7;
    static constexpr u32 kTypeTag          = 8;
    static constexpr u32 kTypeCount        = 9;

    // File format constants
    static constexpr u32 kFormatVersion    = 1;
    static constexpr u32 kSignature        = 0x46464143; // "CAFF" little-endian

    // ── Per-component serialization helpers ──────────────────────────────────

    template<typename T>
    static void collectComponent(ECS::World& world,
                                  std::vector<std::pair<u32, std::vector<u8>>>& entries) {
        ECS::ComponentQuery q;
        q.with<T>();
        world.forEach<T>(q, [&](ECS::Entity e, T& comp) {
            std::vector<u8> data(sizeof(T));
            memcpy(data.data(), &comp, sizeof(T));
            entries.push_back({e.id(), std::move(data)});
        });
    }

    void collectNameComponents(
        std::vector<std::pair<u32, std::vector<u8>>>& entries);

    void collectSpriteComponents(
        std::vector<std::pair<u32, std::vector<u8>>>& entries);

    bool applyNameComponent(ECS::Entity e, const u8* data, u32 size);
    bool applySpriteComponent(ECS::Entity e, const u8* data, u32 size);

    template<typename T>
    static bool applyPODComponent(ECS::Entity e, const u8* data, u32 size,
                                   ECS::World& world) {
        if (size != sizeof(T)) return false;
        auto& comp = world.add<T>(e);
        memcpy(&comp, data, sizeof(T));
        return true;
    }
};

} // namespace Caffeine::Editor
