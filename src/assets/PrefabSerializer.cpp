#include "assets/PrefabSerializer.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components3D.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/BlobLoader.hpp"
#include "core/io/Crc32.hpp"
#include "memory/LinearAllocator.hpp"
#include "assets/MeshTypes.hpp"
#include "editor/EditorContext.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace Caffeine::Assets {

using namespace Caffeine;

bool PrefabSerializer::save(const std::string& filePath, ECS::Entity rootEntity) {
    PrefabAsset prefab;
    std::unordered_map<u32, u32> entityIdMap;
    
    if (!rootEntity.isValid()) {
        return false;
    }
    
    prefab.entities.push_back(serializeEntity(rootEntity, entityIdMap));
    
    // Serialize to binary payload
    std::vector<u8> payload;
    payload.reserve(4096);
    
    // Write entity count
    u32 entityCount = static_cast<u32>(prefab.entities.size());
    payload.insert(payload.end(), 
                   reinterpret_cast<const u8*>(&entityCount),
                   reinterpret_cast<const u8*>(&entityCount) + sizeof(entityCount));
    
    // Write each entity
    for (const auto& entityData : prefab.entities) {
        // Entity name
        u32 nameLen = static_cast<u32>(entityData.name.size());
        payload.insert(payload.end(),
                       reinterpret_cast<const u8*>(&nameLen),
                       reinterpret_cast<const u8*>(&nameLen) + sizeof(nameLen));
        payload.insert(payload.end(),
                       reinterpret_cast<const u8*>(entityData.name.c_str()),
                       reinterpret_cast<const u8*>(entityData.name.c_str()) + nameLen);
        
        // Components
        u32 componentCount = static_cast<u32>(entityData.components.size());
        payload.insert(payload.end(),
                       reinterpret_cast<const u8*>(&componentCount),
                       reinterpret_cast<const u8*>(&componentCount) + sizeof(componentCount));
        
        for (const auto& comp : entityData.components) {
            payload.insert(payload.end(),
                           reinterpret_cast<const u8*>(&comp.typeId),
                           reinterpret_cast<const u8*>(&comp.typeId) + sizeof(comp.typeId));
            u32 dataSize = static_cast<u32>(comp.data.size());
            payload.insert(payload.end(),
                           reinterpret_cast<const u8*>(&dataSize),
                           reinterpret_cast<const u8*>(&dataSize) + sizeof(dataSize));
            payload.insert(payload.end(), comp.data.begin(), comp.data.end());
        }
        
        // Children
        u32 childCount = static_cast<u32>(entityData.childEntityIds.size());
        payload.insert(payload.end(),
                       reinterpret_cast<const u8*>(&childCount),
                       reinterpret_cast<const u8*>(&childCount) + sizeof(childCount));
        for (u32 childId : entityData.childEntityIds) {
            payload.insert(payload.end(),
                           reinterpret_cast<const u8*>(&childId),
                           reinterpret_cast<const u8*>(&childId) + sizeof(childId));
        }
    }
    
    // Metadata is minimal for prefabs (empty)
    IO::CafWriter::WriteResult result = IO::CafWriter::write(
        filePath.c_str(),
        AssetType::Prefab,
        CAF_FLAG_NONE,
        nullptr,
        0,
        payload.data(),
        payload.size()
    );
    
    return result.success;
}

ECS::Entity PrefabSerializer::load(const std::string& filePath, const Vec3& positionOffset) {
    auto alloc = std::make_unique<LinearAllocator>(16 * 1024 * 1024);
    IO::BlobLoader::LoadResult result = IO::BlobLoader::load(filePath.c_str(), alloc.get());
    
    if (!result.valid || result.header->type != AssetType::Prefab) {
        return ECS::Entity{};
    }
    
    const u8* payload = static_cast<const u8*>(result.payload);
    u64 payloadSize = result.header->dataSize;
    
    if (payloadSize < sizeof(u32)) {
        return ECS::Entity{};
    }
    
    u64 offset = 0;
    
    u32 entityCount = *reinterpret_cast<const u32*>(payload + offset);
    offset += sizeof(entityCount);
    
    std::vector<ECS::Entity> createdEntities;
    std::vector<PrefabAsset::EntityData> loadedEntities;
    
    for (u32 i = 0; i < entityCount && offset < payloadSize; ++i) {
        PrefabAsset::EntityData data;
        
        if (offset + sizeof(u32) > payloadSize) break;
        u32 nameLen = *reinterpret_cast<const u32*>(payload + offset);
        offset += sizeof(nameLen);
        
        if (offset + nameLen > payloadSize) break;
        data.name.assign(reinterpret_cast<const char*>(payload + offset), nameLen);
        offset += nameLen;
        
        if (offset + sizeof(u32) > payloadSize) break;
        u32 componentCount = *reinterpret_cast<const u32*>(payload + offset);
        offset += sizeof(componentCount);
        
        for (u32 j = 0; j < componentCount && offset < payloadSize; ++j) {
            if (offset + sizeof(u32) * 2 > payloadSize) break;
            
            PrefabAsset::ComponentEntry comp;
            comp.typeId = *reinterpret_cast<const u32*>(payload + offset);
            offset += sizeof(comp.typeId);
            
            u32 dataSize = *reinterpret_cast<const u32*>(payload + offset);
            offset += sizeof(dataSize);
            
            if (offset + dataSize > payloadSize) break;
            comp.data.assign(payload + offset, payload + offset + dataSize);
            offset += dataSize;
            
            data.components.push_back(comp);
        }
        
        if (offset + sizeof(u32) > payloadSize) break;
        u32 childCount = *reinterpret_cast<const u32*>(payload + offset);
        offset += sizeof(childCount);
        
        for (u32 j = 0; j < childCount && offset < payloadSize; ++j) {
            if (offset + sizeof(u32) > payloadSize) break;
            u32 childId = *reinterpret_cast<const u32*>(payload + offset);
            offset += sizeof(childId);
            data.childEntityIds.push_back(childId);
        }
        
        loadedEntities.push_back(data);
    }
    
    if (loadedEntities.empty()) {
        return ECS::Entity{};
    }
    
    ECS::Entity rootEntity = deserializeEntity(loadedEntities[0], createdEntities);
    
    if (rootEntity.isValid() && !(positionOffset.x == 0 && positionOffset.y == 0 && positionOffset.z == 0)) {
        if (auto* pos = m_world.get<ECS::Position3D>(rootEntity)) {
            pos->position.x += positionOffset.x;
            pos->position.y += positionOffset.y;
            pos->position.z += positionOffset.z;
        }
    }
    
    return rootEntity;
}

PrefabAsset::EntityData PrefabSerializer::serializeEntity(ECS::Entity entity, std::unordered_map<u32, u32>& entityIdMap) {
    PrefabAsset::EntityData data;
    
    if (auto* nameComp = m_world.get<Editor::NameComponent>(entity)) {
        data.name = nameComp->name;
    }
    
    if (auto* pos3d = m_world.get<ECS::Position3D>(entity)) {
        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypePosition3D;
        comp.data.resize(sizeof(ECS::Position3D));
        memcpy(comp.data.data(), pos3d, sizeof(ECS::Position3D));
        data.components.push_back(comp);
    }
    
    if (auto* rot3d = m_world.get<ECS::Rotation3D>(entity)) {
        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeRotation3D;
        comp.data.resize(sizeof(ECS::Rotation3D));
        memcpy(comp.data.data(), rot3d, sizeof(ECS::Rotation3D));
        data.components.push_back(comp);
    }
    
    if (auto* scale3d = m_world.get<ECS::Scale3D>(entity)) {
        PrefabAsset::ComponentEntry comp;
        comp.typeId = kTypeScale3D;
        comp.data.resize(sizeof(ECS::Scale3D));
        memcpy(comp.data.data(), scale3d, sizeof(ECS::Scale3D));
        data.components.push_back(comp);
    }
    
    return data;
}

ECS::Entity PrefabSerializer::deserializeEntity(const PrefabAsset::EntityData& data, std::vector<ECS::Entity>& outCreatedEntities) {
    ECS::Entity entity = m_world.create();
    outCreatedEntities.push_back(entity);
    
    if (!data.name.empty()) {
        auto& nameComp = m_world.add<Editor::NameComponent>(entity);
        std::strncpy(nameComp.name, data.name.c_str(), sizeof(nameComp.name) - 1);
        nameComp.name[sizeof(nameComp.name) - 1] = '\0';
    }
    
    for (const auto& comp : data.components) {
        applyComponentData(entity, comp.typeId, comp.data.data(), static_cast<u32>(comp.data.size()));
    }
    
    return entity;
}

void PrefabSerializer::applyComponentData(ECS::Entity entity, u32 typeId, const u8* data, u32 size) {
    switch (typeId) {
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
        default:
            break;
    }
}

}
