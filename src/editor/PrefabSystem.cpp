#include "editor/PrefabSystem.hpp"
#include "assets/PrefabSerializer.hpp"
#include "ecs/Components.hpp"
#include "ecs/Components3D.hpp"
#include "editor/EditorContext.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Caffeine::Editor {

std::vector<u8> PrefabSystem::SerializeVec3(const Vec3& v) {
    std::vector<u8> data(sizeof(Vec3));
    memcpy(data.data(), &v, sizeof(Vec3));
    return data;
}

Vec3 PrefabSystem::DeserializeVec3(const std::vector<u8>& data) {
    Vec3 v{};
    if (data.size() >= sizeof(Vec3)) {
        memcpy(&v, data.data(), sizeof(Vec3));
    }
    return v;
}

bool PrefabSystem::CreateFromEntity(ECS::World& world, ECS::Entity entity,
                                    const std::filesystem::path& path) {
    if (!entity.isValid()) return false;

    std::filesystem::path outPath = path;
    if (outPath.extension() != ".caf" && outPath.extension() != ".prefab") {
        outPath += ".prefab.caf";
    }

    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    Assets::PrefabSerializer serializer(world);
    return serializer.save(outPath.string(), entity);
}

ECS::Entity PrefabSystem::Instantiate(ECS::World& world, const std::string& prefabPath,
                                      const Vec3& positionOffset) {
    Assets::PrefabSerializer serializer(world);
    const Assets::PrefabLoadResult loaded = serializer.loadWithMap(prefabPath, positionOffset);
    if (!loaded.success) return ECS::Entity{};

    auto& instance = world.add<ECS::PrefabInstance>(loaded.root);
    instance.prefabPath = prefabPath;
    instance.rootEntityId = loaded.root.id();
    instance.entityIndexMap = loaded.entityIndexMap;
    instance.overrides.clear();

    return loaded.root;
}

ECS::Entity PrefabSystem::FindInstanceRoot(ECS::World& world, ECS::Entity entity) {
    if (!entity.isValid()) return ECS::Entity{};

    if (world.has<ECS::PrefabInstance>(entity)) {
        return entity;
    }

    ECS::Entity found;
    ECS::ComponentQuery q;
    q.with<ECS::PrefabInstance>();
    world.forEach<ECS::PrefabInstance>(q, [&](ECS::Entity root, ECS::PrefabInstance& inst) {
        if (found.isValid()) return;
        if (inst.rootEntityId == entity.id()) {
            found = root;
            return;
        }
        for (const auto& [index, eid] : inst.entityIndexMap) {
            if (eid == entity.id()) {
                found = root;
                return;
            }
        }
    });
    return found;
}

u32 PrefabSystem::EntityIndex(ECS::World& world, ECS::Entity entity) {
    const ECS::Entity root = FindInstanceRoot(world, entity);
    if (!root.isValid()) return u32_max;

    auto* inst = world.get<ECS::PrefabInstance>(root);
    if (!inst) return u32_max;

    if (root == entity) return 0;
    return inst->indexForEntity(entity.id());
}

void PrefabSystem::RecordOverride(ECS::World& world, ECS::Entity entity,
                                  const std::string& componentName, const std::string& propertyName,
                                  const std::vector<u8>& value) {
    const ECS::Entity root = FindInstanceRoot(world, entity);
    if (!root.isValid()) return;

    auto* inst = world.get<ECS::PrefabInstance>(root);
    if (!inst) return;

    const u32 index = EntityIndex(world, entity);
    if (index == u32_max) return;

    inst->setOverride(index, componentName, propertyName, value);
}

bool PrefabSystem::IsOverridden(ECS::World& world, ECS::Entity entity,
                                const std::string& componentName, const std::string& propertyName) {
    const ECS::Entity root = FindInstanceRoot(world, entity);
    if (!root.isValid()) return false;

    const auto* inst = world.get<ECS::PrefabInstance>(root);
    if (!inst) return false;

    const u32 index = EntityIndex(world, entity);
    if (index == u32_max) return false;

    return inst->isOverridden(index, componentName, propertyName);
}

namespace {

void destroyPrefabSubtree(ECS::World& world, const ECS::PrefabInstance& inst, ECS::Entity root) {
    std::vector<u32> ids;
    ids.push_back(root.id());
    for (const auto& [index, eid] : inst.entityIndexMap) {
        if (eid != root.id()) ids.push_back(eid);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        world.destroy(ECS::Entity(*it, &world));
    }
}

void applyOverridesToEntity(ECS::World& world, ECS::Entity entity,
                            const std::vector<ECS::PrefabOverride>& overrides, u32 entityIndex) {
    for (const auto& o : overrides) {
        if (o.entityIndex != entityIndex) continue;

        if (o.componentName == "Position3D" && o.propertyName == "position" && o.value.size() >= sizeof(Vec3)) {
            auto& pos = world.add<ECS::Position3D>(entity);
            memcpy(&pos.position, o.value.data(), sizeof(Vec3));
        } else if (o.componentName == "Transform" && o.propertyName == "position" &&
                   o.value.size() >= sizeof(Vec3)) {
            auto& t = world.add<ECS::Transform>(entity);
            memcpy(&t.position, o.value.data(), sizeof(Vec3));
        } else if (o.componentName == "Scale3D" && o.propertyName == "scale" && o.value.size() >= sizeof(Vec3)) {
            auto& s = world.add<ECS::Scale3D>(entity);
            memcpy(&s.scale, o.value.data(), sizeof(Vec3));
        } else if (o.componentName == "Sprite" && o.propertyName == "name") {
            const std::string name(reinterpret_cast<const char*>(o.value.data()), o.value.size());
            if (auto* sprite = world.get<ECS::Sprite>(entity)) {
                sprite->name = name;
            } else {
                world.add<ECS::Sprite>(entity, name, 0);
            }
        }
    }
}

Vec3 readEntityPosition(ECS::World& world, ECS::Entity entity) {
    if (auto* pos = world.get<ECS::Position3D>(entity)) return pos->position;
    if (auto* t = world.get<ECS::Transform>(entity)) return t->position;
    return Vec3(0, 0, 0);
}

}  // namespace

void PrefabSystem::RevertOverrides(ECS::World& world, ECS::Entity instanceRoot) {
    auto* inst = world.get<ECS::PrefabInstance>(instanceRoot);
    if (!inst || inst->prefabPath.empty()) return;

    const Vec3 offset = readEntityPosition(world, instanceRoot);
    const std::string path = inst->prefabPath;
    destroyPrefabSubtree(world, *inst, instanceRoot);
    Instantiate(world, path, offset);
}

bool PrefabSystem::ApplyOverridesToPrefab(ECS::World& world, ECS::Entity instanceRoot) {
    auto* inst = world.get<ECS::PrefabInstance>(instanceRoot);
    if (!inst || inst->prefabPath.empty()) return false;

    Assets::PrefabSerializer serializer(world);
    if (!serializer.save(inst->prefabPath, instanceRoot)) return false;

    inst->clearAllOverrides();
    PropagateChanges(world, inst->prefabPath);
    return true;
}

void PrefabSystem::PropagateChanges(ECS::World& world, const std::string& prefabPath) {
    struct InstanceState {
        Vec3 offset;
        std::vector<ECS::PrefabOverride> overrides;
    };

    std::vector<InstanceState> states;
    std::vector<ECS::Entity> roots;

    ECS::ComponentQuery q;
    q.with<ECS::PrefabInstance>();
    world.forEach<ECS::PrefabInstance>(q, [&](ECS::Entity root, ECS::PrefabInstance& inst) {
        if (inst.prefabPath != prefabPath) return;
        states.push_back({readEntityPosition(world, root), inst.overrides});
        roots.push_back(root);
    });

    for (ECS::Entity root : roots) {
        if (auto* inst = world.get<ECS::PrefabInstance>(root)) {
            destroyPrefabSubtree(world, *inst, root);
        }
    }

    for (const auto& state : states) {
        ECS::Entity newRoot = Instantiate(world, prefabPath, state.offset);
        if (!newRoot.isValid()) continue;

        auto* inst = world.get<ECS::PrefabInstance>(newRoot);
        if (!inst) continue;

        inst->overrides = state.overrides;
        for (const auto& [index, eid] : inst->entityIndexMap) {
            applyOverridesToEntity(world, ECS::Entity(eid, &world), inst->overrides, index);
        }
    }
}

}  // namespace Caffeine::Editor
