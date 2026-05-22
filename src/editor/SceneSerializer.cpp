#include "editor/SceneSerializer.hpp"
#include "ecs/Components.hpp"
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
    if (version != kFormatVersion) return false;

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

} // namespace Caffeine::Editor
