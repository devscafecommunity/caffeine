#include "assets/PrefabSerializer.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/LightComponents.hpp"
#include "audio/AudioComponents.hpp"
#include "scene/SceneComponents.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/BlobLoader.hpp"
#include "editor/EditorContext.hpp"
#include "memory/LinearAllocator.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace Caffeine::Assets {

namespace {

bool parsePayload(const u8* payload, u64 payloadSize, std::vector<PrefabAsset::EntityData>& outEntities) {
    if (!payload || payloadSize < sizeof(u32)) return false;

    u64 offset = 0;
    const u32 entityCount = *reinterpret_cast<const u32*>(payload + offset);
    offset += sizeof(entityCount);

    for (u32 i = 0; i < entityCount && offset < payloadSize; ++i) {
        PrefabAsset::EntityData data;

        if (offset + sizeof(u32) > payloadSize) return false;
        const u32 nameLen = *reinterpret_cast<const u32*>(payload + offset);
        offset += sizeof(nameLen);

        if (offset + nameLen > payloadSize) return false;
        data.name.assign(reinterpret_cast<const char*>(payload + offset), nameLen);
        offset += nameLen;

        if (offset + sizeof(u32) > payloadSize) return false;
        const u32 componentCount = *reinterpret_cast<const u32*>(payload + offset);
        offset += sizeof(componentCount);

        for (u32 j = 0; j < componentCount && offset < payloadSize; ++j) {
            if (offset + sizeof(u32) * 2 > payloadSize) return false;

            PrefabAsset::ComponentEntry comp;
            comp.typeId = *reinterpret_cast<const u32*>(payload + offset);
            offset += sizeof(comp.typeId);

            const u32 dataSize = *reinterpret_cast<const u32*>(payload + offset);
            offset += sizeof(dataSize);

            if (offset + dataSize > payloadSize) return false;
            comp.data.assign(payload + offset, payload + offset + dataSize);
            offset += dataSize;

            data.components.push_back(std::move(comp));
        }

        if (offset + sizeof(u32) > payloadSize) return false;
        const u32 childCount = *reinterpret_cast<const u32*>(payload + offset);
        offset += sizeof(childCount);

        for (u32 j = 0; j < childCount && offset < payloadSize; ++j) {
            if (offset + sizeof(u32) > payloadSize) return false;
            const u32 childId = *reinterpret_cast<const u32*>(payload + offset);
            offset += sizeof(childId);
            data.childEntityIds.push_back(childId);
        }

        outEntities.push_back(std::move(data));
    }

    return !outEntities.empty();
}

std::vector<u8> buildPayload(const std::vector<PrefabAsset::EntityData>& entities) {
    std::vector<u8> payload;
    payload.reserve(4096);

    const u32 entityCount = static_cast<u32>(entities.size());
    payload.insert(payload.end(), reinterpret_cast<const u8*>(&entityCount),
                   reinterpret_cast<const u8*>(&entityCount) + sizeof(entityCount));

    for (const auto& entityData : entities) {
        const u32 nameLen = static_cast<u32>(entityData.name.size());
        payload.insert(payload.end(), reinterpret_cast<const u8*>(&nameLen),
                       reinterpret_cast<const u8*>(&nameLen) + sizeof(nameLen));
        if (nameLen > 0) {
            payload.insert(payload.end(), reinterpret_cast<const u8*>(entityData.name.c_str()),
                           reinterpret_cast<const u8*>(entityData.name.c_str()) + nameLen);
        }

        const u32 componentCount = static_cast<u32>(entityData.components.size());
        payload.insert(payload.end(), reinterpret_cast<const u8*>(&componentCount),
                       reinterpret_cast<const u8*>(&componentCount) + sizeof(componentCount));

        for (const auto& comp : entityData.components) {
            payload.insert(payload.end(), reinterpret_cast<const u8*>(&comp.typeId),
                           reinterpret_cast<const u8*>(&comp.typeId) + sizeof(comp.typeId));
            const u32 dataSize = static_cast<u32>(comp.data.size());
            payload.insert(payload.end(), reinterpret_cast<const u8*>(&dataSize),
                           reinterpret_cast<const u8*>(&dataSize) + sizeof(dataSize));
            payload.insert(payload.end(), comp.data.begin(), comp.data.end());
        }

        const u32 childCount = static_cast<u32>(entityData.childEntityIds.size());
        payload.insert(payload.end(), reinterpret_cast<const u8*>(&childCount),
                       reinterpret_cast<const u8*>(&childCount) + sizeof(childCount));
        for (const u32 childId : entityData.childEntityIds) {
            payload.insert(payload.end(), reinterpret_cast<const u8*>(&childId),
                           reinterpret_cast<const u8*>(&childId) + sizeof(childId));
        }
    }

    return payload;
}

}  // namespace

void PrefabSerializer::gatherSubtree(ECS::Entity root, std::vector<ECS::Entity>& out) const {
    out.push_back(root);

    std::vector<ECS::Entity> children;
    ECS::ComponentQuery q;
    q.with<Scene::Parent>();
    m_world.forEach<Scene::Parent>(q, [&](ECS::Entity child, Scene::Parent& parentComp) {
        if (parentComp.parent == root) {
            children.push_back(child);
        }
    });

    std::sort(children.begin(), children.end(),
              [](ECS::Entity a, ECS::Entity b) { return a.id() < b.id(); });

    for (ECS::Entity child : children) {
        gatherSubtree(child, out);
    }
}

void PrefabSerializer::collectName(ECS::Entity entity, std::vector<PrefabAsset::ComponentEntry>& out) const {
    if (auto* nameComp = m_world.get<Editor::NameComponent>(entity)) {
        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeName;
        comp.data.resize(64);
        memcpy(comp.data.data(), nameComp->name, 64);
        out.push_back(std::move(comp));
    }
}

void PrefabSerializer::collectSprite(ECS::Entity entity, std::vector<PrefabAsset::ComponentEntry>& out) const {
    if (auto* sprite = m_world.get<ECS::Sprite>(entity)) {
        const u32 nameLen = static_cast<u32>(sprite->name.size());
        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeSprite;
        comp.data.resize(8 + nameLen);
        memcpy(comp.data.data(), &sprite->frameIndex, 4);
        memcpy(comp.data.data() + 4, &nameLen, 4);
        if (nameLen > 0) {
            memcpy(comp.data.data() + 8, sprite->name.data(), nameLen);
        }
        out.push_back(std::move(comp));
    }
}

void PrefabSerializer::collectMeshFilter(ECS::Entity entity, std::vector<PrefabAsset::ComponentEntry>& out) const {
    if (auto* mf = m_world.get<ECS::MeshFilterComponent>(entity)) {
        const u8 prim = static_cast<u8>(mf->primitive);
        const u32 meshPathLen = static_cast<u32>(mf->customMeshPath.size());
        const u32 texturePathLen = static_cast<u32>(mf->customTexturePath.size());
        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeMeshFilter;
        comp.data.resize(9 + meshPathLen + texturePathLen);
        u32 offset = 0;
        memcpy(comp.data.data() + offset, &prim, 1);
        offset += 1;
        memcpy(comp.data.data() + offset, &meshPathLen, 4);
        offset += 4;
        if (meshPathLen > 0) {
            memcpy(comp.data.data() + offset, mf->customMeshPath.data(), meshPathLen);
            offset += meshPathLen;
        }
        memcpy(comp.data.data() + offset, &texturePathLen, 4);
        offset += 4;
        if (texturePathLen > 0) {
            memcpy(comp.data.data() + offset, mf->customTexturePath.data(), texturePathLen);
        }
        out.push_back(std::move(comp));
    }
}

void PrefabSerializer::collectMeshRenderer(ECS::Entity entity, std::vector<PrefabAsset::ComponentEntry>& out) const {
    if (auto* mr = m_world.get<ECS::MeshRendererComponent>(entity)) {
        const u32 meshLen = static_cast<u32>(mr->meshPath.size());
        const u32 matLen = static_cast<u32>(mr->materialPath.size());
        const u8 castShadows = mr->castShadows ? 1 : 0;
        const u8 receiveShadows = mr->receiveShadows ? 1 : 0;

        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeMeshRenderer;
        comp.data.resize(4 + meshLen + 4 + matLen + 2);
        u32 offset = 0;
        memcpy(comp.data.data() + offset, &meshLen, 4);
        offset += 4;
        if (meshLen > 0) {
            memcpy(comp.data.data() + offset, mr->meshPath.data(), meshLen);
            offset += meshLen;
        }
        memcpy(comp.data.data() + offset, &matLen, 4);
        offset += 4;
        if (matLen > 0) {
            memcpy(comp.data.data() + offset, mr->materialPath.data(), matLen);
            offset += matLen;
        }
        memcpy(comp.data.data() + offset, &castShadows, 1);
        offset += 1;
        memcpy(comp.data.data() + offset, &receiveShadows, 1);
        out.push_back(std::move(comp));
    }
}

void PrefabSerializer::collectParent(ECS::Entity entity, const std::unordered_map<u32, u32>& runtimeToIndex,
                                     std::vector<PrefabAsset::ComponentEntry>& out) const {
    if (auto* parent = m_world.get<Scene::Parent>(entity)) {
        if (!parent->parent.isValid()) return;
        const auto it = runtimeToIndex.find(parent->parent.id());
        if (it == runtimeToIndex.end()) return;

        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeParent;
        comp.data.resize(4);
        const u32 parentIndex = it->second;
        memcpy(comp.data.data(), &parentIndex, 4);
        out.push_back(std::move(comp));
    }
}

PrefabAsset::EntityData PrefabSerializer::serializeEntity(
    ECS::Entity entity, const std::unordered_map<u32, u32>& runtimeToIndex) const {
    PrefabAsset::EntityData data;

    if (auto* nameComp = m_world.get<Editor::NameComponent>(entity)) {
        data.name = nameComp->name;
    }

    collectName(entity, data.components);
    collectPOD<ECS::Transform>(entity, kTypeTransform, data.components);
    collectPOD<ECS::Acceleration2D>(entity, kTypeAcceleration2D, data.components);
    collectSprite(entity, data.components);
    if (m_world.has<ECS::Tag>(entity)) {
        data.components.push_back({kTypeTag, {}});
    }
    collectPOD<Audio::AudioEmitter>(entity, kTypeAudioEmitter, data.components);
    collectParent(entity, runtimeToIndex, data.components);
    collectPOD<ECS::LightComponent>(entity, kTypeLight, data.components);
    collectPOD<ECS::DirectionalLightComponent>(entity, kTypeDirLight, data.components);
    collectPOD<ECS::PointLightComponent>(entity, kTypePointLight, data.components);
    collectPOD<ECS::SpotLightComponent>(entity, kTypeSpotLight, data.components);
    collectPOD<ECS::Position3D>(entity, kTypePosition3D, data.components);
    collectPOD<ECS::Rotation3D>(entity, kTypeRotation3D, data.components);
    collectPOD<ECS::Scale3D>(entity, kTypeScale3D, data.components);
    collectMeshFilter(entity, data.components);
    collectMeshRenderer(entity, data.components);

    ECS::ComponentQuery q;
    q.with<Scene::Parent>();
    m_world.forEach<Scene::Parent>(q, [&](ECS::Entity child, Scene::Parent& parentComp) {
        if (parentComp.parent != entity) return;
        const auto it = runtimeToIndex.find(child.id());
        if (it != runtimeToIndex.end()) {
            data.childEntityIds.push_back(it->second);
        }
    });
    std::sort(data.childEntityIds.begin(), data.childEntityIds.end());

    return data;
}

bool PrefabSerializer::save(const std::string& filePath, ECS::Entity rootEntity) {
    if (!rootEntity.isValid()) return false;

    std::vector<ECS::Entity> subtree;
    gatherSubtree(rootEntity, subtree);

    std::unordered_map<u32, u32> runtimeToIndex;
    for (u32 i = 0; i < subtree.size(); ++i) {
        runtimeToIndex[subtree[i].id()] = i;
    }

    std::vector<PrefabAsset::EntityData> entities;
    entities.reserve(subtree.size());
    for (ECS::Entity entity : subtree) {
        entities.push_back(serializeEntity(entity, runtimeToIndex));
    }

    const std::vector<u8> payload = buildPayload(entities);
    const IO::CafWriter::WriteResult result = IO::CafWriter::write(
        filePath.c_str(), AssetType::Prefab, CAF_FLAG_NONE, nullptr, 0, payload.data(), payload.size());
    return result.success;
}

void PrefabSerializer::applyComponentData(ECS::Entity entity, u32 typeId, const u8* data, u32 size) {
    switch (typeId) {
        case kTypeName: {
            if (size != 64) break;
            auto& nc = m_world.add<Editor::NameComponent>(entity);
            memcpy(nc.name, data, 64);
            nc.name[63] = '\0';
            break;
        }
        case kTypeTransform: {
            if (size == sizeof(ECS::Transform)) {
                auto& comp = m_world.add<ECS::Transform>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeAcceleration2D: {
            if (size == sizeof(ECS::Acceleration2D)) {
                auto& comp = m_world.add<ECS::Acceleration2D>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeSprite: {
            if (size < 8) break;
            u32 frameIndex = 0;
            u32 nameLen = 0;
            memcpy(&frameIndex, data, 4);
            memcpy(&nameLen, data + 4, 4);
            if (8 + nameLen > size) break;
            std::string spriteName(reinterpret_cast<const char*>(data + 8), nameLen);
            m_world.add<ECS::Sprite>(entity, std::move(spriteName), frameIndex);
            break;
        }
        case kTypeTag:
            m_world.add<ECS::Tag>(entity);
            break;
        case kTypeAudioEmitter: {
            if (size == sizeof(Audio::AudioEmitter)) {
                auto& comp = m_world.add<Audio::AudioEmitter>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeLight: {
            if (size == sizeof(ECS::LightComponent)) {
                auto& comp = m_world.add<ECS::LightComponent>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeDirLight: {
            if (size == sizeof(ECS::DirectionalLightComponent)) {
                auto& comp = m_world.add<ECS::DirectionalLightComponent>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypePointLight: {
            if (size == sizeof(ECS::PointLightComponent)) {
                auto& comp = m_world.add<ECS::PointLightComponent>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeSpotLight: {
            if (size == sizeof(ECS::SpotLightComponent)) {
                auto& comp = m_world.add<ECS::SpotLightComponent>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypePosition3D: {
            if (size == sizeof(ECS::Position3D)) {
                auto& comp = m_world.add<ECS::Position3D>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeRotation3D: {
            if (size == sizeof(ECS::Rotation3D)) {
                auto& comp = m_world.add<ECS::Rotation3D>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeScale3D: {
            if (size == sizeof(ECS::Scale3D)) {
                auto& comp = m_world.add<ECS::Scale3D>(entity);
                memcpy(&comp, data, size);
            }
            break;
        }
        case kTypeMeshFilter: {
            if (size < 5) break;
            u8 prim = 0;
            u32 pathLen = 0;
            memcpy(&prim, data, 1);
            memcpy(&pathLen, data + 1, 4);
            if (5 + pathLen > size) break;
            auto& mf = m_world.add<ECS::MeshFilterComponent>(entity);
            mf.primitive = static_cast<ECS::MeshPrimitive>(prim);
            if (pathLen > 0) {
                mf.customMeshPath.assign(reinterpret_cast<const char*>(data + 5), pathLen);
            }
            const u32 textureOffset = 5 + pathLen;
            if (textureOffset + 4 <= size) {
                u32 texturePathLen = 0;
                memcpy(&texturePathLen, data + textureOffset, 4);
                if (textureOffset + 4 + texturePathLen <= size && texturePathLen > 0) {
                    mf.customTexturePath.assign(reinterpret_cast<const char*>(data + textureOffset + 4),
                                                texturePathLen);
                }
            }
            break;
        }
        case kTypeMeshRenderer: {
            if (size < 10) break;
            u32 offset = 0;
            u32 meshLen = 0;
            u32 matLen = 0;
            memcpy(&meshLen, data + offset, 4);
            offset += 4;
            if (4 + meshLen + 4 + 2 > size) break;
            std::string meshPath;
            if (meshLen > 0) {
                meshPath.assign(reinterpret_cast<const char*>(data + offset), meshLen);
                offset += meshLen;
            }
            memcpy(&matLen, data + offset, 4);
            offset += 4;
            std::string matPath;
            if (matLen > 0) {
                matPath.assign(reinterpret_cast<const char*>(data + offset), matLen);
                offset += matLen;
            }
            u8 castShadows = 0;
            u8 receiveShadows = 0;
            memcpy(&castShadows, data + offset, 1);
            offset += 1;
            memcpy(&receiveShadows, data + offset, 1);

            auto& mr = m_world.add<ECS::MeshRendererComponent>(entity);
            mr.meshPath = std::move(meshPath);
            mr.materialPath = std::move(matPath);
            mr.castShadows = castShadows != 0;
            mr.receiveShadows = receiveShadows != 0;
            break;
        }
        default:
            break;
    }
}

ECS::Entity PrefabSerializer::deserializeEntity(const PrefabAsset::EntityData& data,
                                                std::vector<ECS::Entity>& outCreatedEntities) {
    ECS::Entity entity = m_world.create();
    outCreatedEntities.push_back(entity);

    if (!data.name.empty()) {
        auto& nameComp = m_world.add<Editor::NameComponent>(entity);
        std::strncpy(nameComp.name, data.name.c_str(), sizeof(nameComp.name) - 1);
        nameComp.name[sizeof(nameComp.name) - 1] = '\0';
    }

    for (const auto& comp : data.components) {
        if (comp.typeId == kTypeParent) continue;
        applyComponentData(entity, comp.typeId, comp.data.data(), static_cast<u32>(comp.data.size()));
    }

    return entity;
}

void PrefabSerializer::applyParentLinks(const std::vector<PrefabAsset::EntityData>& entities,
                                        const std::vector<ECS::Entity>& runtimeEntities) const {
    for (u32 i = 0; i < entities.size() && i < runtimeEntities.size(); ++i) {
        for (const auto& comp : entities[i].components) {
            if (comp.typeId != kTypeParent || comp.data.size() < 4) continue;
            u32 parentIndex = 0;
            memcpy(&parentIndex, comp.data.data(), 4);
            if (parentIndex >= runtimeEntities.size()) continue;
            auto& parentComp = m_world.add<Scene::Parent>(runtimeEntities[i]);
            parentComp.parent = runtimeEntities[parentIndex];
            parentComp.dirty = true;
        }
    }
}

PrefabLoadResult PrefabSerializer::loadWithMap(const std::string& filePath, const Vec3& positionOffset) {
    PrefabLoadResult result;

    auto alloc = std::make_unique<LinearAllocator>(16 * 1024 * 1024);
    const IO::BlobLoader::LoadResult blob = IO::BlobLoader::load(filePath.c_str(), alloc.get());
    if (!blob.valid || blob.header->type != AssetType::Prefab) {
        return result;
    }

    std::vector<PrefabAsset::EntityData> loadedEntities;
    if (!parsePayload(static_cast<const u8*>(blob.payload), blob.header->dataSize, loadedEntities)) {
        return result;
    }

    std::vector<ECS::Entity> runtimeEntities;
    runtimeEntities.reserve(loadedEntities.size());
    for (const auto& entityData : loadedEntities) {
        deserializeEntity(entityData, runtimeEntities);
    }

    applyParentLinks(loadedEntities, runtimeEntities);

    if (runtimeEntities.empty()) {
        return result;
    }

    result.root = runtimeEntities[0];
    for (u32 i = 0; i < runtimeEntities.size(); ++i) {
        result.entityIndexMap[i] = runtimeEntities[i].id();
    }

    if (!(positionOffset.x == 0 && positionOffset.y == 0 && positionOffset.z == 0)) {
        if (auto* pos = m_world.get<ECS::Position3D>(result.root)) {
            pos->position.x += positionOffset.x;
            pos->position.y += positionOffset.y;
            pos->position.z += positionOffset.z;
        }
        if (auto* transform = m_world.get<ECS::Transform>(result.root)) {
            transform->position.x += positionOffset.x;
            transform->position.y += positionOffset.y;
            transform->position.z += positionOffset.z;
        }
    }

    result.success = result.root.isValid();
    return result;
}

ECS::Entity PrefabSerializer::load(const std::string& filePath, const Vec3& positionOffset) {
    return loadWithMap(filePath, positionOffset).root;
}

}  // namespace Caffeine::Assets
