#pragma once
#include "core/Types.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "editor/EditorContext.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caffeine::Editor {

class SceneSerializer {
public:
    explicit SceneSerializer(ECS::World& world) : m_world(world) {}

    // ── Binary .caf ──────────────────────────────────────────────────────────

    bool serialize(const std::string& filepath);
    bool deserialize(const std::string& filepath);
    bool serializeToMemory(std::vector<u8>& out, const std::string& scenePath = {});
    bool deserializeFromMemory(const std::vector<u8>& data, const std::string& scenePath = {});

private:
    ECS::World& m_world;

    // Editor-specific component type IDs for the binary format
    static constexpr u32 kTypeName          = 0;
    static constexpr u32 kTypeTransform     = 1;
    static constexpr u32 kTypeAcceleration2D = 3;
    static constexpr u32 kTypeSprite        = 6;
    static constexpr u32 kTypeTag           = 8;
    static constexpr u32 kTypeAudioEmitter  = 9;
    static constexpr u32 kTypeParent        = 10;
    static constexpr u32 kTypeLight         = 11;
    static constexpr u32 kTypeDirLight      = 12;
    static constexpr u32 kTypePointLight    = 13;
    static constexpr u32 kTypeSpotLight     = 14;
    static constexpr u32 kTypePosition3D    = 15;
    static constexpr u32 kTypeRotation3D    = 16;
    static constexpr u32 kTypeScale3D       = 17;
    static constexpr u32 kTypeMeshFilter    = 18;
    static constexpr u32 kTypeMeshRenderer  = 19;
    static constexpr u32 kTypePrefabInstance = 20;
    static constexpr u32 kTypeCamera3D       = 21;
    static constexpr u32 kTypeCameraActive   = 22;
    static constexpr u32 kTypeCamera2D       = 23;
    static constexpr u32 kTypeRigidBody2D    = 24;
    static constexpr u32 kTypeCollider2D     = 25;
    static constexpr u32 kTypeScript         = 26;
    static constexpr u32 kTypeCppScript      = 27;
    static constexpr u32 kTypePersistent     = 28;
    static constexpr u32 kTypeDisabledTag    = 29;
    static constexpr u32 kTypeParticleEmitter = 30;
    static constexpr u32 kTypeWorldTransform   = 31;
    static constexpr u32 kTypeEntityLayer      = 32;
    static constexpr u32 kTypeSkinnedMeshRenderer = 33;
    static constexpr u32 kTypeUIWidget       = 34;
    static constexpr u32 kTypeUIButton       = 35;
    static constexpr u32 kTypeUILabel        = 36;
    static constexpr u32 kTypeUIProgressBar  = 37;
    static constexpr u32 kTypeUISlider       = 38;
    static constexpr u32 kTypeUICheckbox     = 39;
    static constexpr u32 kTypeAnimator       = 40;
    static constexpr u32 kTypeSkybox         = 41;
    static constexpr u32 kTypeTerrain        = 42;
    static constexpr u32 kTypeRigidBody3D    = 43;
    static constexpr u32 kTypeCollider3D     = 44;
    static constexpr u32 kTypeForwardRenderFeatures = 45;
    static constexpr u32 kTypePostProcess    = 46;
    static constexpr u32 kTypeAnimationPlayer = 47;
    static constexpr u32 kTypeNavVolume      = 48;
    static constexpr u32 kTypeNavAgent       = 49;
    static constexpr u32 kTypeSkinnedPose    = 50;
    static constexpr u32 kTypeSpriteSheet    = 51;
    static constexpr u32 kTypeEffect         = 52;
    static constexpr u32 kTypeMeshGeometry   = 53;
    static constexpr u32 kTypeEnvironmentEffects = 54;
    static constexpr u32 kTypeCount          = 55;
    static constexpr u32 kPoseCurveMarker    = 0x56525543u;
    static constexpr u32 kPoseStripMarker    = 0x50525453u;
    static constexpr u32 kAnimatorEditorMarker = 0x524F5441u;
    static constexpr u32 kAnimatorGraphMarker  = 0x48505247u;

    static constexpr u32 kFormatVersion    = 7;
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

    void collectMeshFilterComponents(
        std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectMeshGeometryComponents(
        std::vector<std::pair<u32, std::vector<u8>>>& entries);

    void collectMeshRendererComponents(
        std::vector<std::pair<u32, std::vector<u8>>>& entries);

    void collectPrefabInstanceComponents(
        std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectScriptComponents(std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectCppScriptComponents(std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectParticleEmitterComponents(std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectSkinnedMeshRendererComponents(std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectUIWidgetComponents(std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectAnimatorComponents(std::vector<std::pair<u32, std::vector<u8>>>& entries);
    void collectTerrainComponents(const std::string& scenePath,
                                  std::vector<std::pair<u32, std::vector<u8>>>& entries);

    template<typename T>
    void emitPodComponents(u32 typeId,
                           std::unordered_map<u32, std::vector<std::pair<u32, std::vector<u8>>>>& entityMap) {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<T>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(typeId, std::move(data));
        }
    }

    bool applyNameComponent(ECS::Entity e, const u8* data, u32 size);
    bool applySpriteComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyMeshFilterComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyMeshGeometryComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyMeshRendererComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyPrefabInstanceComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyScriptComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyCppScriptComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyParticleEmitterComponent(ECS::Entity e, const u8* data, u32 size);
    bool applySkinnedMeshRendererComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyUIWidgetComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyAnimatorComponent(ECS::Entity e, const u8* data, u32 size);
    bool applyTerrainComponent(ECS::Entity e, const u8* data, u32 size,
                               const std::string& scenePath);

    static std::vector<u8> serializeTerrainComponent(const ECS::TerrainComponent& terrain);
    static bool deserializeTerrainComponent(const u8* data, u32 size, ECS::TerrainComponent& terrain);

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
