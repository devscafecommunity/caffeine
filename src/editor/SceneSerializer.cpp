#include "editor/SceneSerializer.hpp"
#include "ecs/Components.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/PrefabComponents.hpp"
#include "audio/AudioComponents.hpp"
#include "editor/EditorContext.hpp"
#include "scene/SceneComponents.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <cstring>

namespace Caffeine::Editor {

// ── Helpers ──────────────────────────────────────────────────────

void SceneSerializer::collectNameComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    ECS::ComponentQuery q;
    q.with<NameComponent>();
    m_world.forEach<NameComponent>(q, [&](ECS::Entity e, NameComponent& nc) {
        std::vector<u8> data(64);
        memcpy(data.data(), nc.name, 64);
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectSpriteComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    ECS::ComponentQuery q;
    q.with<ECS::Sprite>();
    m_world.forEach<ECS::Sprite>(q, [&](ECS::Entity e, ECS::Sprite& s) {
        // Format: frameIndex (4 bytes) + nameLength (4 bytes) + nameData
        u32 nameLen = static_cast<u32>(s.name.size());
        std::vector<u8> data(8 + nameLen);
        memcpy(data.data(), &s.frameIndex, 4);
        memcpy(data.data() + 4, &nameLen, 4);
        if (nameLen > 0) {
            memcpy(data.data() + 8, s.name.data(), nameLen);
        }
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectMeshFilterComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    ECS::ComponentQuery q;
    q.with<ECS::MeshFilterComponent>();
    m_world.forEach<ECS::MeshFilterComponent>(q, [&](ECS::Entity e, ECS::MeshFilterComponent& mf) {
        // Format: primitive (1) + meshPathLen (4) + meshPath + texturePathLen (4) + texturePath
        u8 prim = static_cast<u8>(mf.primitive);
        u32 meshPathLen = static_cast<u32>(mf.customMeshPath.size());
        u32 texturePathLen = static_cast<u32>(mf.customTexturePath.size());
        std::vector<u8> data(9 + meshPathLen + texturePathLen);
        u32 offset = 0;
        memcpy(data.data() + offset, &prim, 1); offset += 1;
        memcpy(data.data() + offset, &meshPathLen, 4); offset += 4;
        if (meshPathLen > 0) {
            memcpy(data.data() + offset, mf.customMeshPath.data(), meshPathLen);
            offset += meshPathLen;
        }
        memcpy(data.data() + offset, &texturePathLen, 4); offset += 4;
        if (texturePathLen > 0) {
            memcpy(data.data() + offset, mf.customTexturePath.data(), texturePathLen);
        }
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectMeshRendererComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    ECS::ComponentQuery q;
    q.with<ECS::MeshRendererComponent>();
    m_world.forEach<ECS::MeshRendererComponent>(q, [&](ECS::Entity e, ECS::MeshRendererComponent& mr) {
        // Format: meshPathLen (4) + meshPath + materialPathLen (4) + materialPath + castShadows (1) + receiveShadows (1)
        u32 meshLen = static_cast<u32>(mr.meshPath.size());
        u32 matLen = static_cast<u32>(mr.materialPath.size());
        u8 castShadows = mr.castShadows ? 1 : 0;
        u8 receiveShadows = mr.receiveShadows ? 1 : 0;
        
        std::vector<u8> data(4 + meshLen + 4 + matLen + 2);
        u32 offset = 0;
        memcpy(data.data() + offset, &meshLen, 4); offset += 4;
        if (meshLen > 0) {
            memcpy(data.data() + offset, mr.meshPath.data(), meshLen);
            offset += meshLen;
        }
        memcpy(data.data() + offset, &matLen, 4); offset += 4;
        if (matLen > 0) {
            memcpy(data.data() + offset, mr.materialPath.data(), matLen);
            offset += matLen;
        }
        memcpy(data.data() + offset, &castShadows, 1); offset += 1;
        memcpy(data.data() + offset, &receiveShadows, 1);
        
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectPrefabInstanceComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    ECS::ComponentQuery q;
    q.with<ECS::PrefabInstance>();
    m_world.forEach<ECS::PrefabInstance>(q, [&](ECS::Entity e, ECS::PrefabInstance& pi) {
        // Format: pathLen + path + rootEntityId + mapCount + [index,eid]* +
        //         overrideCount + [index, compLen, comp, propLen, prop, valLen, val]*
        u32 pathLen = static_cast<u32>(pi.prefabPath.size());
        const u32 mapCount = static_cast<u32>(pi.entityIndexMap.size());
        const u32 overrideCount = static_cast<u32>(pi.overrides.size());

        usize size = 4 + pathLen + 4 + 4 + mapCount * 8 + 4;
        for (const auto& o : pi.overrides) {
            size += 4 + 4 + o.componentName.size() + 4 + o.propertyName.size() + 4 + o.value.size();
        }

        std::vector<u8> data(size);
        u32 offset = 0;
        memcpy(data.data() + offset, &pathLen, 4);
        offset += 4;
        if (pathLen > 0) {
            memcpy(data.data() + offset, pi.prefabPath.data(), pathLen);
            offset += pathLen;
        }
        memcpy(data.data() + offset, &pi.rootEntityId, 4);
        offset += 4;
        memcpy(data.data() + offset, &mapCount, 4);
        offset += 4;
        for (const auto& [index, eid] : pi.entityIndexMap) {
            memcpy(data.data() + offset, &index, 4);
            offset += 4;
            memcpy(data.data() + offset, &eid, 4);
            offset += 4;
        }
        memcpy(data.data() + offset, &overrideCount, 4);
        offset += 4;
        for (const auto& o : pi.overrides) {
            memcpy(data.data() + offset, &o.entityIndex, 4);
            offset += 4;
            const u32 compLen = static_cast<u32>(o.componentName.size());
            memcpy(data.data() + offset, &compLen, 4);
            offset += 4;
            if (compLen > 0) {
                memcpy(data.data() + offset, o.componentName.data(), compLen);
                offset += compLen;
            }
            const u32 propLen = static_cast<u32>(o.propertyName.size());
            memcpy(data.data() + offset, &propLen, 4);
            offset += 4;
            if (propLen > 0) {
                memcpy(data.data() + offset, o.propertyName.data(), propLen);
                offset += propLen;
            }
            const u32 valLen = static_cast<u32>(o.value.size());
            memcpy(data.data() + offset, &valLen, 4);
            offset += 4;
            if (valLen > 0) {
                memcpy(data.data() + offset, o.value.data(), valLen);
                offset += valLen;
            }
        }
        entries.push_back({e.id(), std::move(data)});
    });
}

// ── Serialize ────────────────────────────────────────────────────

bool SceneSerializer::serialize(const std::string& filepath) {
    // Collect all components grouped by entity
    std::unordered_map<u32, std::vector<std::pair<u32, std::vector<u8>>>> entityMap;

    auto addToMap = [&](const std::vector<std::pair<u32, std::vector<u8>>>& entries) {
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(0, std::move(data)); // typeId filled below
        }
    };
    (void)addToMap;

    // Collect each component type
    // Type 0: NameComponent (needs special handling for char[64])
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectNameComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeName, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::Transform>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeTransform, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::Acceleration2D>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeAcceleration2D, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectSpriteComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeSprite, std::move(data));
        }
    }

    // Type 8: Tag (no data, just presence)
    {
        ECS::ComponentQuery q;
        q.with<ECS::Tag>();
        m_world.forEach<ECS::Tag>(q, [&](ECS::Entity e, ECS::Tag&) {
            entityMap[e.id()].emplace_back(kTypeTag, std::vector<u8>{});
        });
    }

    // Type 9: AudioEmitter
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<Audio::AudioEmitter>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeAudioEmitter, std::move(data));
        }
    }

    // Type 10: Scene::Parent — serialize parent entity ID (u32)
    {
        ECS::ComponentQuery q;
        q.with<Scene::Parent>();
        m_world.forEach<Scene::Parent>(q, [&](ECS::Entity e, Scene::Parent& pc) {
            if (!pc.parent.isValid()) return;
            std::vector<u8> data(4);
            u32 parentId = pc.parent.id();
            memcpy(data.data(), &parentId, 4);
            entityMap[e.id()].emplace_back(kTypeParent, std::move(data));
        });
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::LightComponent>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeLight, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::DirectionalLightComponent>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeDirLight, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::PointLightComponent>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypePointLight, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::SpotLightComponent>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeSpotLight, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::Position3D>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypePosition3D, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::Rotation3D>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeRotation3D, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::Scale3D>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeScale3D, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectMeshFilterComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeMeshFilter, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectMeshRendererComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeMeshRenderer, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectPrefabInstanceComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypePrefabInstance, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::Camera3DComponent>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeCamera3D, std::move(data));
        }
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectComponent<ECS::CameraActiveComponent>(m_world, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeCameraActive, std::move(data));
        }
    }

    // Write binary file
    std::ofstream fout(filepath, std::ios::binary);
    if (!fout.is_open()) return false;

    // Header
    u32 signature = kSignature;
    u32 version   = kFormatVersion;
    u32 count     = static_cast<u32>(entityMap.size());
    fout.write(reinterpret_cast<const char*>(&signature), 4);
    fout.write(reinterpret_cast<const char*>(&version), 4);
    fout.write(reinterpret_cast<const char*>(&count), 4);

    // Entity data
    for (auto& [eid, components] : entityMap) {
        u32 compCount = static_cast<u32>(components.size());
        fout.write(reinterpret_cast<const char*>(&eid), 4);
        fout.write(reinterpret_cast<const char*>(&compCount), 4);

        for (auto& [typeId, data] : components) {
            u32 dataSize = static_cast<u32>(data.size());
            fout.write(reinterpret_cast<const char*>(&typeId), 4);
            fout.write(reinterpret_cast<const char*>(&dataSize), 4);
            if (dataSize > 0) {
                fout.write(reinterpret_cast<const char*>(data.data()), dataSize);
            }
        }
    }

    fout.close();
    return true;
}

// ── Deserialize ──────────────────────────────────────────────────

bool SceneSerializer::deserialize(const std::string& filepath) {
    std::ifstream fin(filepath, std::ios::binary | std::ios::ate);
    if (!fin.is_open()) return false;

    // Read entire file into memory
    std::streampos fileSize = fin.tellg();
    fin.seekg(0, std::ios::beg);

    std::vector<u8> buffer(static_cast<usize>(fileSize));
    if (!fin.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        fin.close();
        return false;
    }
    fin.close();

    // Parse header
    if (buffer.size() < 12) return false;
    u32 signature, version, entityCount;
    memcpy(&signature,   buffer.data(),      4);
    memcpy(&version,     buffer.data() + 4,  4);
    memcpy(&entityCount, buffer.data() + 8,  4);

    if (signature != kSignature) return false;
    if (version != 4 && version != kFormatVersion) return false;

    // ── Pass 1: collect all entity IDs ────────────────────────────
    struct Entry {
        u32 eid;
        u32 typeId;
        std::vector<u8> data;
    };
    std::vector<Entry> allEntries;
    std::unordered_set<u32> uniqueIds;

    usize cursor = 12;
    for (u32 i = 0; i < entityCount; ++i) {
        if (cursor + 8 > buffer.size()) return false;
        u32 eid, compCount;
        memcpy(&eid,       buffer.data() + cursor,      4); cursor += 4;
        memcpy(&compCount, buffer.data() + cursor,      4); cursor += 4;

        uniqueIds.insert(eid);

        for (u32 j = 0; j < compCount; ++j) {
            if (cursor + 8 > buffer.size()) return false;
            u32 typeId, dataSize;
            memcpy(&typeId,   buffer.data() + cursor, 4); cursor += 4;
            memcpy(&dataSize, buffer.data() + cursor, 4); cursor += 4;

            if (cursor + dataSize > buffer.size()) return false;
            std::vector<u8> compData(dataSize);
            if (dataSize > 0) {
                memcpy(compData.data(), buffer.data() + cursor, dataSize);
                cursor += dataSize;
            }
            allEntries.push_back({eid, typeId, std::move(compData)});
        }
    }

    // ── Pass 2: create entities with remap ────────────────────
    m_world.destroyAll();
    std::unordered_map<u32, ECS::Entity> remap;
    remap.reserve(uniqueIds.size());
    for (u32 oldId : uniqueIds) {
        remap[oldId] = m_world.create();
    }

    // ── Pass 3: apply components ──────────────────────────────
    for (auto& entry : allEntries) {
        auto it = remap.find(entry.eid);
        if (it == remap.end()) continue;
        ECS::Entity e = it->second;

        switch (entry.typeId) {
            case kTypeName:
                applyNameComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeTransform:
                applyPODComponent<ECS::Transform>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeAcceleration2D:
                applyPODComponent<ECS::Acceleration2D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeSprite:
                applySpriteComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeTag:
                m_world.add<ECS::Tag>(e);
                break;
            case kTypeAudioEmitter:
                applyPODComponent<Audio::AudioEmitter>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeParent: {
                if (entry.data.size() < 4) break;
                u32 oldParentId;
                memcpy(&oldParentId, entry.data.data(), 4);
                auto pit = remap.find(oldParentId);
                if (pit != remap.end()) {
                    auto& pc = m_world.add<Scene::Parent>(e);
                    pc.parent = pit->second;
                    pc.dirty  = true;
                }
                break;
            }
            case kTypeLight:
                applyPODComponent<ECS::LightComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeDirLight:
                applyPODComponent<ECS::DirectionalLightComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypePointLight:
                applyPODComponent<ECS::PointLightComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeSpotLight:
                applyPODComponent<ECS::SpotLightComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypePosition3D:
                applyPODComponent<ECS::Position3D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeRotation3D:
                applyPODComponent<ECS::Rotation3D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeScale3D:
                applyPODComponent<ECS::Scale3D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeMeshFilter:
                applyMeshFilterComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeMeshRenderer:
                applyMeshRendererComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypePrefabInstance:
                applyPrefabInstanceComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeCamera3D:
                applyPODComponent<ECS::Camera3DComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeCameraActive:
                applyPODComponent<ECS::CameraActiveComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            default:
                break;
        }
    }

    return true;
}

// ── Component apply helpers ──────────────────────────────────────

bool SceneSerializer::applyNameComponent(ECS::Entity e, const u8* data, u32 size) {
    if (size != 64) return false;
    auto& nc = m_world.add<NameComponent>(e);
    memcpy(nc.name, data, 64);
    nc.name[63] = '\0';
    return true;
}

bool SceneSerializer::applySpriteComponent(ECS::Entity e, const u8* data, u32 size) {
    // Format: frameIndex (4) + nameLength (4) + nameData (nameLength)
    if (size < 8) return false;
    u32 frameIndex, nameLen;
    memcpy(&frameIndex, data, 4);
    memcpy(&nameLen, data + 4, 4);

    if (8 + nameLen > size) return false;
    std::string spriteName(reinterpret_cast<const char*>(data + 8), nameLen);
    m_world.add<ECS::Sprite>(e, std::move(spriteName), frameIndex);
    return true;
}

bool SceneSerializer::applyMeshFilterComponent(ECS::Entity e, const u8* data, u32 size) {
    if (size < 5) return false;
    u8 prim;
    u32 pathLen;
    memcpy(&prim, data, 1);
    memcpy(&pathLen, data + 1, 4);
    
    if (5 + pathLen > size) return false;
    auto& mf = m_world.add<ECS::MeshFilterComponent>(e);
    mf.primitive = static_cast<ECS::MeshPrimitive>(prim);
    if (pathLen > 0) {
        mf.customMeshPath.assign(reinterpret_cast<const char*>(data + 5), pathLen);
    }

    const u32 textureOffset = 5 + pathLen;
    if (textureOffset + 4 <= size) {
        u32 texturePathLen = 0;
        memcpy(&texturePathLen, data + textureOffset, 4);
        if (textureOffset + 4 + texturePathLen <= size && texturePathLen > 0) {
            mf.customTexturePath.assign(reinterpret_cast<const char*>(data + textureOffset + 4), texturePathLen);
        }
    }
    return true;
}

bool SceneSerializer::applyMeshRendererComponent(ECS::Entity e, const u8* data, u32 size) {
    if (size < 10) return false;
    u32 offset = 0;
    u32 meshLen, matLen;
    memcpy(&meshLen, data + offset, 4); offset += 4;
    
    if (4 + meshLen + 4 + 2 > size) return false;
    std::string meshPath;
    if (meshLen > 0) {
        meshPath.assign(reinterpret_cast<const char*>(data + offset), meshLen);
        offset += meshLen;
    }
    
    memcpy(&matLen, data + offset, 4); offset += 4;
    std::string matPath;
    if (matLen > 0) {
        matPath.assign(reinterpret_cast<const char*>(data + offset), matLen);
        offset += matLen;
    }
    
    u8 castShadows, receiveShadows;
    memcpy(&castShadows, data + offset, 1); offset += 1;
    memcpy(&receiveShadows, data + offset, 1);
    
    auto& mr = m_world.add<ECS::MeshRendererComponent>(e);
    mr.meshPath = std::move(meshPath);
    mr.materialPath = std::move(matPath);
    mr.castShadows = (castShadows != 0);
    mr.receiveShadows = (receiveShadows != 0);
    return true;
}

bool SceneSerializer::applyPrefabInstanceComponent(ECS::Entity e, const u8* data, u32 size) {
    if (size < 12) return false;
    u32 pathLen = 0;
    memcpy(&pathLen, data, 4);

    if (4 + pathLen + 4 > size) return false;
    std::string prefabPath;
    if (pathLen > 0) {
        prefabPath.assign(reinterpret_cast<const char*>(data + 4), pathLen);
    }

    u32 offset = 4 + pathLen;
    u32 rootEntityId = 0;
    memcpy(&rootEntityId, data + offset, 4);
    offset += 4;

    auto& pi = m_world.add<ECS::PrefabInstance>(e);
    pi.prefabPath = std::move(prefabPath);
    pi.rootEntityId = rootEntityId;

    if (offset + 4 > size) return true;
    u32 mapCount = 0;
    memcpy(&mapCount, data + offset, 4);
    offset += 4;

    for (u32 i = 0; i < mapCount; ++i) {
        if (offset + 8 > size) break;
        u32 index = 0;
        u32 eid = 0;
        memcpy(&index, data + offset, 4);
        offset += 4;
        memcpy(&eid, data + offset, 4);
        offset += 4;
        pi.entityIndexMap[index] = eid;
    }

    if (offset + 4 > size) return true;
    u32 overrideCount = 0;
    memcpy(&overrideCount, data + offset, 4);
    offset += 4;

    for (u32 i = 0; i < overrideCount; ++i) {
        if (offset + 4 > size) break;
        ECS::PrefabOverride o;
        memcpy(&o.entityIndex, data + offset, 4);
        offset += 4;

        if (offset + 4 > size) break;
        u32 compLen = 0;
        memcpy(&compLen, data + offset, 4);
        offset += 4;
        if (offset + compLen > size) break;
        if (compLen > 0) {
            o.componentName.assign(reinterpret_cast<const char*>(data + offset), compLen);
            offset += compLen;
        }

        if (offset + 4 > size) break;
        u32 propLen = 0;
        memcpy(&propLen, data + offset, 4);
        offset += 4;
        if (offset + propLen > size) break;
        if (propLen > 0) {
            o.propertyName.assign(reinterpret_cast<const char*>(data + offset), propLen);
            offset += propLen;
        }

        if (offset + 4 > size) break;
        u32 valLen = 0;
        memcpy(&valLen, data + offset, 4);
        offset += 4;
        if (offset + valLen > size) break;
        if (valLen > 0) {
            o.value.assign(data + offset, data + offset + valLen);
            offset += valLen;
        }
        pi.overrides.push_back(std::move(o));
    }

    return true;
}

} // namespace Caffeine::Editor
