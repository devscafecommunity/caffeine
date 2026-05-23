#include "assets/PrefabSerializer.hpp"
#include "ecs/ComponentQuery.hpp"
#include "ecs/Components3D.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"
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
    
    std::ofstream file(filePath, std::ios::binary);
    if (!file) return false;
    
    u32 magic = PrefabAsset::MAGIC;
    u32 version = PrefabAsset::VERSION;
    u32 entityCount = static_cast<u32>(prefab.entities.size());
    
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&entityCount), sizeof(entityCount));
    
    for (const auto& entityData : prefab.entities) {
        u32 nameLen = static_cast<u32>(entityData.name.size());
        file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        file.write(entityData.name.c_str(), nameLen);
        
        u32 componentCount = static_cast<u32>(entityData.components.size());
        file.write(reinterpret_cast<const char*>(&componentCount), sizeof(componentCount));
        
        for (const auto& comp : entityData.components) {
            file.write(reinterpret_cast<const char*>(&comp.typeId), sizeof(comp.typeId));
            u32 dataSize = static_cast<u32>(comp.data.size());
            file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
            file.write(reinterpret_cast<const char*>(comp.data.data()), dataSize);
        }
        
        u32 childCount = static_cast<u32>(entityData.childEntityIds.size());
        file.write(reinterpret_cast<const char*>(&childCount), sizeof(childCount));
        for (u32 childId : entityData.childEntityIds) {
            file.write(reinterpret_cast<const char*>(&childId), sizeof(childId));
        }
    }
    
    return file.good();
}

ECS::Entity PrefabSerializer::load(const std::string& filePath, const Vec3& positionOffset) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return ECS::Entity{};
    
    u32 magic, version, entityCount;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&entityCount), sizeof(entityCount));
    
    if (magic != PrefabAsset::MAGIC || version != PrefabAsset::VERSION) {
        return ECS::Entity{};
    }
    
    std::vector<ECS::Entity> createdEntities;
    std::vector<PrefabAsset::EntityData> loadedEntities;
    
    for (u32 i = 0; i < entityCount; ++i) {
        PrefabAsset::EntityData data;
        
        u32 nameLen;
        file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        data.name.resize(nameLen);
        file.read(&data.name[0], nameLen);
        
        u32 componentCount;
        file.read(reinterpret_cast<char*>(&componentCount), sizeof(componentCount));
        
        for (u32 j = 0; j < componentCount; ++j) {
            PrefabAsset::ComponentEntry comp;
            file.read(reinterpret_cast<char*>(&comp.typeId), sizeof(comp.typeId));
            
            u32 dataSize;
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
            comp.data.resize(dataSize);
            file.read(reinterpret_cast<char*>(comp.data.data()), dataSize);
            
            data.components.push_back(comp);
        }
        
        u32 childCount;
        file.read(reinterpret_cast<char*>(&childCount), sizeof(childCount));
        for (u32 j = 0; j < childCount; ++j) {
            u32 childId;
            file.read(reinterpret_cast<char*>(&childId), sizeof(childId));
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
