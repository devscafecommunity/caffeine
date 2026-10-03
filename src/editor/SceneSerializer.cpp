#include "editor/SceneSerializer.hpp"
#include "editor/SceneSerializerIO.hpp"
#include "ecs/Components.hpp"
#include "ecs/CameraComponents.hpp"
#include "ecs/ForwardRenderComponents.hpp"
#include "ecs/MeshComponents.hpp"
#include "ecs/MeshGeometry.hpp"
#include "ecs/PrefabComponents.hpp"
#include "ecs/TerrainComponents.hpp"
#include "audio/AudioComponents.hpp"
#include "animation/AnimationComponents.hpp"
#include "animation/AnimationPlayer.hpp"
#include "animation/SkinLibrary.hpp"
#include "effects/EffectTypes.hpp"
#include "navigation/NavVolume.hpp"
#include "ecs/PostProcessComponents.hpp"
#include "ecs/EnvironmentEffectsComponents.hpp"
#include "physics/PhysicsComponents2D.hpp"
#include "physics/PhysicsComponents3D.hpp"
#include "script/ScriptTypes.hpp"
#include "ui/UIComponents.hpp"
#include "editor/EditorContext.hpp"
#include "scene/SceneComponents.hpp"
#include "editor/TerrainGenerationBlobLegacy.hpp"
#include "terrain/TerrainCache.hpp"
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <cstring>
#include <cctype>
#include <filesystem>

namespace Caffeine::Editor {

namespace IO = SceneSerializerIO;

namespace {

constexpr u32 kTerrainBlobVersion = 10;

std::filesystem::path projectRootFromScenePath(const std::string& scenePath) {
    if (scenePath.empty()) return {};
    const auto sceneDir = std::filesystem::path(scenePath).parent_path();
    std::filesystem::path root = sceneDir.parent_path();
    return root.empty() ? sceneDir : root;
}

std::string sanitizeTerrainFileStem(const std::string& name) {
    std::string stem = name.empty() ? "Terrain" : name;
    for (char& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            c = '_';
        }
    }
    if (stem.empty()) stem = "Terrain";
    return stem;
}

void appendU32(std::vector<u8>& out, u32 value) {
    const usize offset = out.size();
    out.resize(offset + sizeof(u32));
    std::memcpy(out.data() + offset, &value, sizeof(u32));
}

void appendF32(std::vector<u8>& out, f32 value) {
    const usize offset = out.size();
    out.resize(offset + sizeof(f32));
    std::memcpy(out.data() + offset, &value, sizeof(f32));
}

void appendU8(std::vector<u8>& out, u8 value) {
    out.push_back(value);
}

void appendString(std::vector<u8>& out, const char* str) {
    const u32 len = static_cast<u32>(std::strlen(str));
    appendU32(out, len);
    if (len == 0) return;
    const usize offset = out.size();
    out.resize(offset + len);
    std::memcpy(out.data() + offset, str, len);
}

bool readU32(const u8*& cursor, const u8* end, u32& value) {
    if (cursor + sizeof(u32) > end) return false;
    std::memcpy(&value, cursor, sizeof(u32));
    cursor += sizeof(u32);
    return true;
}

bool readF32(const u8*& cursor, const u8* end, f32& value) {
    if (cursor + sizeof(f32) > end) return false;
    std::memcpy(&value, cursor, sizeof(f32));
    cursor += sizeof(f32);
    return true;
}

bool readU8(const u8*& cursor, const u8* end, u8& value) {
    if (cursor >= end) return false;
    value = *cursor++;
    return true;
}

bool readString(const u8*& cursor, const u8* end, char* dst, usize dstSize) {
    u32 len = 0;
    if (!readU32(cursor, end, len)) return false;
    if (cursor + len > end || len >= dstSize) return false;
    if (len > 0) {
        std::memcpy(dst, cursor, len);
        cursor += len;
    }
    dst[len] = '\0';
    return true;
}

}  // namespace

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

void SceneSerializer::collectMeshGeometryComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    ECS::ComponentQuery q;
    q.with<ECS::MeshGeometryComponent>();
    m_world.forEach<ECS::MeshGeometryComponent>(q, [&](ECS::Entity e, ECS::MeshGeometryComponent& geometry) {
        const u32 positionCount = static_cast<u32>(geometry.positions.size());
        const u32 indexCount = static_cast<u32>(geometry.indices.size());
        std::vector<u8> data(12 + positionCount * sizeof(Vec3) + indexCount * sizeof(u32));
        u32 offset = 0;
        memcpy(data.data() + offset, &positionCount, 4); offset += 4;
        memcpy(data.data() + offset, &indexCount, 4); offset += 4;
        memcpy(data.data() + offset, &geometry.revision, 4); offset += 4;
        if (positionCount > 0) {
            memcpy(data.data() + offset, geometry.positions.data(), positionCount * sizeof(Vec3));
            offset += positionCount * sizeof(Vec3);
        }
        if (indexCount > 0) {
            memcpy(data.data() + offset, geometry.indices.data(), indexCount * sizeof(u32));
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
        u32 materialPathLen = static_cast<u32>(mf.customMaterialPath.size());
        u32 normalPathLen = static_cast<u32>(mf.customNormalPath.size());
        std::vector<u8> data(17 + meshPathLen + texturePathLen + materialPathLen + normalPathLen +
                              sizeof(f32));
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
            offset += texturePathLen;
        }
        memcpy(data.data() + offset, &materialPathLen, 4); offset += 4;
        if (materialPathLen > 0) {
            memcpy(data.data() + offset, mf.customMaterialPath.data(), materialPathLen);
            offset += materialPathLen;
        }
        memcpy(data.data() + offset, &normalPathLen, 4);
        offset += 4;
        if (normalPathLen > 0) {
            memcpy(data.data() + offset, mf.customNormalPath.data(), normalPathLen);
            offset += normalPathLen;
        }
        memcpy(data.data() + offset, &mf.shininess, sizeof(f32));
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

void SceneSerializer::collectScriptComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries) {
    ECS::ComponentQuery q;
    q.with<Script::ScriptComponent>();
    m_world.forEach<Script::ScriptComponent>(q, [&](ECS::Entity e, Script::ScriptComponent& sc) {
        std::vector<u8> data;
        IO::appendString(data, sc.scriptPath);
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectCppScriptComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries) {
    ECS::ComponentQuery q;
    q.with<Script::CppScriptComponent>();
    m_world.forEach<Script::CppScriptComponent>(q, [&](ECS::Entity e, Script::CppScriptComponent& sc) {
        std::vector<u8> data;
        IO::appendString(data, sc.className);
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectParticleEmitterComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries) {
    ECS::ComponentQuery q;
    q.with<ECS::ParticleEmitterComponent>();
    m_world.forEach<ECS::ParticleEmitterComponent>(q, [&](ECS::Entity e, ECS::ParticleEmitterComponent& pe) {
        std::vector<u8> data;
        IO::appendI32(data, pe.maxParticles);
        IO::appendF32(data, pe.emissionRate);
        IO::appendF32(data, pe.lifetime);
        IO::appendPOD(data, pe.velocityMin);
        IO::appendPOD(data, pe.velocityMax);
        IO::appendU32(data, pe.startColor);
        IO::appendU32(data, pe.endColor);
        IO::appendF32(data, pe.startSize);
        IO::appendF32(data, pe.endSize);
        const u32 count = static_cast<u32>(pe.activeParticles.size());
        IO::appendU32(data, count);
        for (const auto& p : pe.activeParticles) {
            IO::appendPOD(data, p);
        }
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectSkinnedMeshRendererComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries) {
    ECS::ComponentQuery q;
    q.with<ECS::SkinnedMeshRendererComponent>();
    m_world.forEach<ECS::SkinnedMeshRendererComponent>(q, [&](ECS::Entity e, ECS::SkinnedMeshRendererComponent& mr) {
        std::vector<u8> data;
        IO::appendString(data, mr.meshPath);
        IO::appendString(data, mr.materialPath);
        IO::appendString(data, mr.skeletonPath);
        IO::appendU8(data, mr.castShadows ? 1 : 0);
        IO::appendU8(data, mr.receiveShadows ? 1 : 0);
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectUIWidgetComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries) {
    ECS::ComponentQuery q;
    q.with<UI::UIWidget>();
    m_world.forEach<UI::UIWidget>(q, [&](ECS::Entity e, UI::UIWidget& w) {
        std::vector<u8> data;
        IO::appendU8(data, static_cast<u8>(w.type));
        IO::appendU32(data, w.parentId);
        IO::appendU8(data, w.visible ? 1 : 0);
        IO::appendU8(data, w.interactable ? 1 : 0);
        IO::appendI32(data, w.siblingOrder);
        IO::appendPOD(data, w.style);
        IO::appendPOD(data, w.transform);
        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectAnimatorComponents(
    std::vector<std::pair<u32, std::vector<u8>>>& entries) {
    ECS::ComponentQuery q;
    q.with<Animation::Animator>();
    m_world.forEach<Animation::Animator>(q, [&](ECS::Entity e, Animation::Animator& anim) {
        std::vector<const Animation::AnimationClip*> clipPtrs;
        auto clipIndex = [&](const Animation::AnimationClip* clip) -> u32 {
            if (!clip) return 0xFFFFFFFFu;
            for (u32 i = 0; i < clipPtrs.size(); ++i) {
                if (clipPtrs[i] == clip) return i;
            }
            clipPtrs.push_back(clip);
            return static_cast<u32>(clipPtrs.size() - 1);
        };

        for (auto& [_, state] : anim.states) {
            clipIndex(state.clip);
        }
        for (const auto& clip : anim.embeddedClips) {
            clipIndex(&clip);
        }

        std::vector<u8> data;
        IO::appendU32(data, static_cast<u32>(clipPtrs.size()));
        for (const auto* clip : clipPtrs) {
            IO::appendString(data, clip->name.cStr());
            IO::appendU32(data, clip->fps);
            IO::appendU8(data, clip->loop ? 1 : 0);
            IO::appendU32(data, static_cast<u32>(clip->frames.size()));
            for (const auto& frame : clip->frames) {
                IO::appendPOD(data, frame);
            }
        }

        IO::appendU32(data, static_cast<u32>(anim.parameters.size()));
        for (const auto& p : anim.parameters) {
            IO::appendString(data, p.name.cStr());
            IO::appendU8(data, static_cast<u8>(p.type));
            IO::appendU8(data, p.triggered ? 1 : 0);
            IO::appendU8(data, p.boolValue ? 1 : 0);
            IO::appendF32(data, p.floatValue);
            IO::appendI32(data, p.intValue);
        }

        IO::appendU32(data, static_cast<u32>(anim.states.size()));
        for (auto& [stateName, state] : anim.states) {
            IO::appendString(data, stateName.cStr());
            IO::appendU32(data, clipIndex(state.clip));
            IO::appendF32(data, state.speed);
            IO::appendU32(data, static_cast<u32>(state.transitions.size()));
            for (const auto& tr : state.transitions) {
                IO::appendString(data, tr.toState.cStr());
                IO::appendF32(data, tr.blendTime);
                IO::appendU8(data, tr.hasExitTime ? 1 : 0);
                IO::appendU32(data, static_cast<u32>(tr.conditions.size()));
                for (const auto& cond : tr.conditions) {
                    IO::appendString(data, cond.parameterName.cStr());
                    IO::appendU8(data, static_cast<u8>(cond.op));
                    IO::appendU8(data, cond.boolValue ? 1 : 0);
                    IO::appendF32(data, cond.floatValue);
                    IO::appendI32(data, cond.intValue);
                }
            }
        }

        IO::appendString(data, anim.currentState.cStr());
        IO::appendString(data, anim.previousState.cStr());
        IO::appendF32(data, anim.timeInState);
        IO::appendF32(data, anim.blendWeight);
        IO::appendF32(data, anim.playbackScale);
        IO::appendU8(data, anim.paused ? 1 : 0);

        IO::appendU32(data, static_cast<u32>(anim.frameEvents.size()));
        for (const auto& [frame, evt] : anim.frameEvents) {
            IO::appendU32(data, frame);
            IO::appendString(data, evt.cStr());
        }

        IO::appendU32(data, kAnimatorEditorMarker);
        IO::appendString(data, anim.defaultState.cStr());
        IO::appendU32(data, static_cast<u32>(anim.states.size()));
        for (auto& [stateName, state] : anim.states) {
            IO::appendString(data, stateName.cStr());
            IO::appendString(data, state.motion.cStr());
            IO::appendU8(data, state.hasEditorPosition ? 1 : 0);
            IO::appendF32(data, state.editorX);
            IO::appendF32(data, state.editorY);
        }

        auto writeCurve = [&](const Animation::KeyCurve& curve) {
            IO::appendU8(data, static_cast<u8>(curve.mode));
            IO::appendF32(data, curve.x1);
            IO::appendF32(data, curve.y1);
            IO::appendF32(data, curve.x2);
            IO::appendF32(data, curve.y2);
        };
        auto writeTransition = [&](const Animation::AnimationTransition& tr) {
            IO::appendString(data, tr.toState.cStr());
            IO::appendF32(data, tr.blendTime);
            IO::appendU8(data, tr.hasExitTime ? 1 : 0);
            IO::appendF32(data, tr.exitTime);
            IO::appendF32(data, tr.offset);
            writeCurve(tr.blendCurve);
            IO::appendU32(data, static_cast<u32>(tr.conditions.size()));
            for (const auto& cond : tr.conditions) {
                IO::appendString(data, cond.parameterName.cStr());
                IO::appendU8(data, static_cast<u8>(cond.op));
                IO::appendU8(data, cond.boolValue ? 1 : 0);
                IO::appendF32(data, cond.floatValue);
                IO::appendI32(data, cond.intValue);
            }
        };
        auto writeMachine = [&](const Animation::AnimatorStateMachine& sm) {
            IO::appendString(data, sm.defaultState.cStr());
            IO::appendF32(data, sm.entryX);
            IO::appendF32(data, sm.entryY);
            IO::appendF32(data, sm.anyX);
            IO::appendF32(data, sm.anyY);
            IO::appendF32(data, sm.exitX);
            IO::appendF32(data, sm.exitY);
            IO::appendU32(data, static_cast<u32>(sm.anyStateTransitions.size()));
            for (const auto& tr : sm.anyStateTransitions) writeTransition(tr);
            IO::appendU32(data, static_cast<u32>(sm.states.size()));
            for (auto& [stateName, state] : sm.states) {
                IO::appendString(data, stateName.cStr());
                IO::appendString(data, state.motion.cStr());
                IO::appendF32(data, state.speed);
                IO::appendU8(data, state.hasEditorPosition ? 1 : 0);
                IO::appendF32(data, state.editorX);
                IO::appendF32(data, state.editorY);
                IO::appendU32(data, static_cast<u32>(state.transitions.size()));
                for (const auto& tr : state.transitions) writeTransition(tr);
            }
        };
        IO::appendU32(data, kAnimatorGraphMarker);
        writeMachine(anim);
        IO::appendU32(data, static_cast<u32>(anim.layers.size()));
        for (const Animation::AnimatorLayer& layer : anim.layers) {
            IO::appendString(data, layer.name.cStr());
            IO::appendString(data, layer.parentGroup.cStr());
            IO::appendU8(data, layer.isGroup ? 1 : 0);
            IO::appendU8(data, layer.muted ? 1 : 0);
            IO::appendU8(data, layer.expanded ? 1 : 0);
            IO::appendF32(data, layer.weight);
            IO::appendU8(data, static_cast<u8>(layer.blend));
            IO::appendU32(data, layer.mask);
            if (!layer.isGroup) writeMachine(layer);
        }

        entries.push_back({e.id(), std::move(data)});
    });
}

void SceneSerializer::collectTerrainComponents(
    const std::string& scenePath,
    std::vector<std::pair<u32, std::vector<u8>>>& entries)
{
    const std::filesystem::path projectRoot = projectRootFromScenePath(scenePath);
    auto& cache = Terrain::TerrainCache::instance();

    ECS::ComponentQuery q;
    q.with<ECS::TerrainComponent>();
    m_world.forEach<ECS::TerrainComponent>(q, [&](ECS::Entity e, ECS::TerrainComponent& terrain) {
        cache.syncEntity(m_world, e);

        if (terrain.terrainDataPath[0] == '\0') {
            std::string entityName = "Terrain";
            if (auto* name = m_world.get<NameComponent>(e)) {
                if (name->name[0] != '\0') entityName = name->name;
            }
            const std::string relPath = "terrain/" + sanitizeTerrainFileStem(entityName) + "_" +
                                        std::to_string(e.id()) + ".cterrain";
            std::strncpy(terrain.terrainDataPath, relPath.c_str(), sizeof(terrain.terrainDataPath) - 1);
            terrain.terrainDataPath[sizeof(terrain.terrainDataPath) - 1] = '\0';
        }

        if (!projectRoot.empty()) {
            cache.saveTerrainFile(e, projectRoot / terrain.terrainDataPath, terrain.useSplatmap);
        }

        entries.push_back({e.id(), serializeTerrainComponent(terrain)});
    });
}

std::vector<u8> SceneSerializer::serializeTerrainComponent(const ECS::TerrainComponent& terrain) {
    std::vector<u8> data;
    appendU32(data, kTerrainBlobVersion);
    appendU32(data, terrain.resolutionX);
    appendU32(data, terrain.resolutionZ);
    appendU32(data, terrain.splatResolutionScale);
    appendF32(data, terrain.worldSizeX);
    appendF32(data, terrain.worldSizeZ);
    appendF32(data, terrain.maxHeight);
    appendU32(data, terrain.dataRevision);
    appendU32(data, terrain.meshRevision);
    appendU32(data, terrain.splatRevision);
    appendU8(data, terrain.castShadows ? 1 : 0);
    appendU8(data, terrain.receiveShadows ? 1 : 0);
    appendU8(data, terrain.useSplatmap ? 1 : 0);
    appendU8(data, terrain.useChunks ? 1 : 0);
    appendU8(data, terrain.frustumCull ? 1 : 0);
    appendU32(data, terrain.chunkVertexCount);
    appendU32(data, terrain.maxLodLevels);
    appendF32(data, terrain.textureTileSize);
    appendF32(data, terrain.splatTileSize);
    appendF32(data, terrain.lodDistanceScale);
    appendF32(data, terrain.lodHysteresis);
    appendString(data, terrain.texturePath);
    appendString(data, terrain.terrainDataPath);
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        appendString(data, terrain.splatLayerPaths[i]);
    }
    appendU32(data, terrain.collisionSampleStep);
    appendU8(data, terrain.buildCollisionMesh ? 1 : 0);
    appendString(data, terrain.normalMapPath);
    appendF32(data, terrain.shininess);
    return data;
}

bool SceneSerializer::deserializeTerrainComponent(const u8* data, u32 size,
                                                  ECS::TerrainComponent& terrain) {
    const u8* cursor = data;
    const u8* end = data + size;

    u32 blobVersion = 0;
    if (!readU32(cursor, end, blobVersion) || blobVersion < 1 || blobVersion > kTerrainBlobVersion) {
        return false;
    }

    u8 castShadows = 1;
    u8 receiveShadows = 1;
    u8 useSplatmap = 1;
    u8 useChunks = 1;
    u8 frustumCull = 1;

    if (!readU32(cursor, end, terrain.resolutionX)) return false;
    if (!readU32(cursor, end, terrain.resolutionZ)) return false;
    if (blobVersion >= 3) {
        if (!readU32(cursor, end, terrain.splatResolutionScale)) return false;
    } else {
        terrain.splatResolutionScale = 4;
    }
    if (!readF32(cursor, end, terrain.worldSizeX)) return false;
    if (!readF32(cursor, end, terrain.worldSizeZ)) return false;
    if (!readF32(cursor, end, terrain.maxHeight)) return false;
    if (!readU32(cursor, end, terrain.dataRevision)) return false;
    if (!readU32(cursor, end, terrain.meshRevision)) return false;
    if (!readU32(cursor, end, terrain.splatRevision)) return false;
    if (!readU8(cursor, end, castShadows)) return false;
    if (!readU8(cursor, end, receiveShadows)) return false;
    if (!readU8(cursor, end, useSplatmap)) return false;
    if (!readU8(cursor, end, useChunks)) return false;
    if (!readU8(cursor, end, frustumCull)) return false;
    if (!readU32(cursor, end, terrain.chunkVertexCount)) return false;
    if (!readU32(cursor, end, terrain.maxLodLevels)) return false;
    if (!readF32(cursor, end, terrain.textureTileSize)) return false;
    if (!readF32(cursor, end, terrain.splatTileSize)) return false;
    if (!readF32(cursor, end, terrain.lodDistanceScale)) return false;
    if (!readF32(cursor, end, terrain.lodHysteresis)) return false;

    terrain.castShadows = castShadows != 0;
    terrain.receiveShadows = receiveShadows != 0;
    terrain.useSplatmap = useSplatmap != 0;
    terrain.useChunks = useChunks != 0;
    terrain.frustumCull = frustumCull != 0;

    if (!readString(cursor, end, terrain.texturePath, sizeof(terrain.texturePath))) return false;
    if (!readString(cursor, end, terrain.terrainDataPath, sizeof(terrain.terrainDataPath))) {
        return false;
    }
    for (u32 i = 0; i < ECS::kTerrainSplatLayerCount; ++i) {
        if (!readString(cursor, end, terrain.splatLayerPaths[i], sizeof(terrain.splatLayerPaths[i]))) {
            return false;
        }
    }

    if (blobVersion >= 2 && blobVersion <= 8) {
        LegacyTerrainGenerationSettings gen;
        u8 noiseAlgorithm = 0;
        u8 domainWarp = 0;
        u8 thermalErosion = 0;
        u8 hydraulicErosion = 0;
        u8 smoothPass = 0;
        u8 autoSplat = 1;
        u8 style = 0;

        if (!readU32(cursor, end, gen.seed)) return false;
        if (!readF32(cursor, end, gen.noiseScale)) return false;
        if (!readU32(cursor, end, gen.octaves)) return false;
        if (!readF32(cursor, end, gen.persistence)) return false;
        if (!readF32(cursor, end, gen.lacunarity)) return false;
        if (!readF32(cursor, end, gen.amplitude)) return false;
        if (!readF32(cursor, end, gen.baseHeight)) return false;
        if (!readU8(cursor, end, noiseAlgorithm)) return false;
        if (!readU8(cursor, end, domainWarp)) return false;
        if (!readF32(cursor, end, gen.domainWarpStrength)) return false;
        if (!readU8(cursor, end, thermalErosion)) return false;
        if (!readU32(cursor, end, gen.thermalIterations)) return false;
        if (!readF32(cursor, end, gen.thermalTalus)) return false;
        if (!readU8(cursor, end, hydraulicErosion)) return false;
        if (!readU32(cursor, end, gen.hydraulicIterations)) return false;
        if (!readF32(cursor, end, gen.hydraulicRain)) return false;
        if (!readF32(cursor, end, gen.hydraulicErode)) return false;
        if (!readF32(cursor, end, gen.hydraulicDeposit)) return false;
        if (!readU8(cursor, end, smoothPass)) return false;
        if (!readU32(cursor, end, gen.smoothIterations)) return false;
        if (!readF32(cursor, end, gen.heightQuantize)) return false;
        if (!readU8(cursor, end, autoSplat)) return false;
        if (!readU8(cursor, end, style)) return false;

        gen.noiseAlgorithm = static_cast<LegacyTerrainNoiseAlgorithm>(noiseAlgorithm);
        gen.domainWarp = domainWarp != 0;
        gen.thermalErosion = thermalErosion != 0;
        gen.hydraulicErosion = hydraulicErosion != 0;
        gen.smoothPass = smoothPass != 0;
        gen.autoSplat = autoSplat != 0;
        gen.style = static_cast<LegacyTerrainGenStyle>(style);

        if (blobVersion >= 4) {
            u8 useRidgedNoise = 1;
            u8 simEnabled = 1;
            u8 simEnvironment = 0;
            u8 simAutoConvergence = 1;
            if (!readU8(cursor, end, useRidgedNoise)) return false;
            if (!readU32(cursor, end, gen.domainWarpPasses)) return false;
            if (!readU32(cursor, end, gen.hydraulicMaxSteps)) return false;
            if (!readF32(cursor, end, gen.hydraulicInertia)) return false;
            if (!readF32(cursor, end, gen.hydraulicEvaporation)) return false;
            if (!readF32(cursor, end, gen.splatBlendRange)) return false;
            LegacyTerrainSimulationSettings& sim = gen.simulation;
            if (!readU8(cursor, end, simEnabled)) return false;
            if (!readU8(cursor, end, simEnvironment)) return false;
            if (!readU32(cursor, end, sim.totalIterations)) return false;
            if (!readU32(cursor, end, sim.dropletsPerIteration)) return false;
            if (!readU32(cursor, end, sim.maxDropletSteps)) return false;
            if (!readF32(cursor, end, sim.convergenceThreshold)) return false;
            if (!readU8(cursor, end, simAutoConvergence)) return false;
            if (!readF32(cursor, end, sim.tectonicActivity)) return false;
            if (!readF32(cursor, end, sim.erosionWater)) return false;
            if (!readF32(cursor, end, sim.erosionThermal)) return false;
            if (!readF32(cursor, end, sim.erosionGlacial)) return false;
            if (!readF32(cursor, end, sim.erosionWind)) return false;
            if (!readF32(cursor, end, sim.erosionBiological)) return false;
            if (!readF32(cursor, end, sim.temperature)) return false;
            if (!readF32(cursor, end, sim.humidity)) return false;
            if (!readF32(cursor, end, sim.windDirection)) return false;
            if (!readF32(cursor, end, sim.rainfallBase)) return false;
            gen.useRidgedNoise = useRidgedNoise != 0;
            sim.enabled = simEnabled != 0;
            sim.environment = static_cast<LegacyTerrainEnvironment>(simEnvironment);
            sim.autoConvergence = simAutoConvergence != 0;
        } else {
            gen.useRidgedNoise = false;
            gen.domainWarpPasses = 1;
            gen.hydraulicMaxSteps = 64;
            gen.hydraulicInertia = 0.85f;
            gen.hydraulicEvaporation = 0.05f;
            gen.splatBlendRange = 0.15f;
        }

        if (blobVersion >= 5) {
            if (!readF32(cursor, end, gen.ridgedBlend)) return false;
            if (!readU32(cursor, end, gen.postSimSmoothIterations)) return false;
            if (!readU32(cursor, end, gen.splatBlurPasses)) return false;
        } else {
            gen.ridgedBlend = gen.useRidgedNoise ? 0.45f : 0.0f;
            gen.postSimSmoothIterations = 4;
            gen.splatBlurPasses = 2;
        }

        if (blobVersion >= 6) {
            u8 heightModel = 0;
            u8 fractalDomainWarp = 1;
            u8 slopeWeighting = 1;
            if (!readU8(cursor, end, heightModel)) return false;
            if (!readF32(cursor, end, gen.ridgeOffset)) return false;
            if (!readF32(cursor, end, gen.ridgeGain)) return false;
            if (!readF32(cursor, end, gen.multiplicativeContrast)) return false;
            if (!readU8(cursor, end, fractalDomainWarp)) return false;
            if (!readF32(cursor, end, gen.domainWarpScale)) return false;
            if (!readU8(cursor, end, slopeWeighting)) return false;
            if (!readF32(cursor, end, gen.slopeWeightAlpha)) return false;
            gen.heightModel = static_cast<LegacyTerrainHeightModel>(heightModel);
            gen.fractalDomainWarp = fractalDomainWarp != 0;
            gen.slopeWeighting = slopeWeighting != 0;
        } else {
            gen.heightModel = gen.useRidgedNoise ? LegacyTerrainHeightModel::Hybrid
                                                 : LegacyTerrainHeightModel::RollingHills;
            gen.ridgeOffset = 1.0f;
            gen.ridgeGain = 1.5f;
            gen.multiplicativeContrast = 1.2f;
            gen.fractalDomainWarp = true;
            gen.domainWarpScale = 48.0f;
            gen.slopeWeighting = true;
            gen.slopeWeightAlpha = 0.18f;
        }

        if (blobVersion >= 7) {
            u8 useClimateBiomes = 1;
            u8 traceRivers = 1;
            if (!readU8(cursor, end, useClimateBiomes)) return false;
            if (!readF32(cursor, end, gen.climate.prevailingWindAngle)) return false;
            if (!readF32(cursor, end, gen.climate.baseHumidity)) return false;
            if (!readF32(cursor, end, gen.climate.temperature)) return false;
            if (!readF32(cursor, end, gen.climate.seaLevel)) return false;
            if (!readU8(cursor, end, traceRivers)) return false;
            if (!readU32(cursor, end, gen.hydrology.maxRiverSources)) return false;
            if (!readF32(cursor, end, gen.hydrology.riverSourceMinHeight)) return false;
            if (!readF32(cursor, end, gen.hydrology.riverSourceMaxHeight)) return false;
            if (!readF32(cursor, end, gen.hydrology.riverCarveStrength)) return false;
            if (!readU32(cursor, end, gen.hydrology.rainShadowSteps)) return false;
            if (!readF32(cursor, end, gen.hydrology.waterMoistureRadius)) return false;
            if (!readF32(cursor, end, gen.hydrology.sedimentDepositStrength)) return false;
            gen.useClimateBiomes = useClimateBiomes != 0;
            gen.hydrology.traceRivers = traceRivers != 0;
        } else {
            gen.useClimateBiomes = true;
            gen.hydrology.traceRivers = true;
        }

        if (blobVersion >= 8) {
            if (!readF32(cursor, end, gen.fractalRoughness)) return false;
            if (!readF32(cursor, end, gen.spectralExponent)) return false;
            u8 multiplyLayerA = 0;
            u8 multiplyLayerB = 2;
            if (!readU8(cursor, end, multiplyLayerA)) return false;
            if (!readU8(cursor, end, multiplyLayerB)) return false;
            gen.multiplyLayerA = static_cast<LegacyTerrainHeightModel>(multiplyLayerA);
            gen.multiplyLayerB = static_cast<LegacyTerrainHeightModel>(multiplyLayerB);
        } else {
            gen.fractalRoughness = 0.55f;
            gen.spectralExponent = 2.0f;
            gen.multiplyLayerA = LegacyTerrainHeightModel::RollingHills;
            gen.multiplyLayerB = LegacyTerrainHeightModel::RidgedMountains;
        }
        (void)gen;
    }

    if (blobVersion >= 9) {
        if (!readU32(cursor, end, terrain.collisionSampleStep)) return false;
        u8 buildCollision = 1;
        if (!readU8(cursor, end, buildCollision)) return false;
        terrain.buildCollisionMesh = buildCollision != 0;
    }

    if (blobVersion >= 10) {
        if (!readString(cursor, end, terrain.normalMapPath, sizeof(terrain.normalMapPath))) {
            return false;
        }
        if (!readF32(cursor, end, terrain.shininess)) return false;
    }

    return cursor <= end;
}

// ── Serialize ────────────────────────────────────────────────────

bool SceneSerializer::serializeToMemory(std::vector<u8>& out, const std::string& filepath) {
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
            std::vector<u8> data(5);
            u32 parentId = pc.parent.id();
            memcpy(data.data(), &parentId, 4);
            data[4] = pc.dirty ? 1 : 0;
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
        collectMeshGeometryComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeMeshGeometry, std::move(data));
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

    emitPodComponents<ECS::Camera2DComponent>(kTypeCamera2D, entityMap);
    emitPodComponents<Physics2D::RigidBody2D>(kTypeRigidBody2D, entityMap);
    emitPodComponents<Physics2D::Collider2D>(kTypeCollider2D, entityMap);
    emitPodComponents<Physics3D::RigidBody3D>(kTypeRigidBody3D, entityMap);
    emitPodComponents<Physics3D::Collider3D>(kTypeCollider3D, entityMap);
    emitPodComponents<ECS::PersistentComponent>(kTypePersistent, entityMap);
    emitPodComponents<Scene::WorldTransform>(kTypeWorldTransform, entityMap);
    emitPodComponents<Scene::EntityLayer>(kTypeEntityLayer, entityMap);
    emitPodComponents<UI::UIButton>(kTypeUIButton, entityMap);
    emitPodComponents<UI::UILabel>(kTypeUILabel, entityMap);
    emitPodComponents<UI::UIProgressBar>(kTypeUIProgressBar, entityMap);
    emitPodComponents<UI::UISlider>(kTypeUISlider, entityMap);
    emitPodComponents<UI::UICheckbox>(kTypeUICheckbox, entityMap);

    {
        ECS::ComponentQuery q;
        q.with<ECS::DisabledTag>();
        m_world.forEach<ECS::DisabledTag>(q, [&](ECS::Entity e, ECS::DisabledTag&) {
            entityMap[e.id()].emplace_back(kTypeDisabledTag, std::vector<u8>{});
        });
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectScriptComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeScript, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectCppScriptComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeCppScript, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectParticleEmitterComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeParticleEmitter, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectSkinnedMeshRendererComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeSkinnedMeshRenderer, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectUIWidgetComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeUIWidget, std::move(data));
        }
    }
    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectAnimatorComponents(entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeAnimator, std::move(data));
        }
    }

    emitPodComponents<ECS::SkyboxComponent>(kTypeSkybox, entityMap);
    emitPodComponents<ECS::ForwardRenderFeaturesComponent>(kTypeForwardRenderFeatures, entityMap);
    emitPodComponents<ECS::PostProcessComponent>(kTypePostProcess, entityMap);

    {
        ECS::ComponentQuery query;
        query.with<Animation::AnimationPlayer>();
        m_world.forEach<Animation::AnimationPlayer>(query, [&](ECS::Entity entity, Animation::AnimationPlayer& player) {
            const u32 pathLen = static_cast<u32>(std::strlen(player.clipPath));
            std::vector<u8> data(4 + pathLen + sizeof(f32) * 2 + 2);
            u32 offset = 0;
            std::memcpy(data.data() + offset, &pathLen, 4);
            offset += 4;
            if (pathLen > 0) {
                std::memcpy(data.data() + offset, player.clipPath, pathLen);
                offset += pathLen;
            }
            std::memcpy(data.data() + offset, &player.time, sizeof(f32));
            offset += sizeof(f32);
            std::memcpy(data.data() + offset, &player.speed, sizeof(f32));
            offset += sizeof(f32);
            data[offset++] = player.playing ? 1 : 0;
            data[offset++] = player.loop ? 1 : 0;
            entityMap[entity.id()].emplace_back(kTypeAnimationPlayer, std::move(data));
        });
    }
    {
        ECS::ComponentQuery query;
        query.with<Animation::SkinnedPose>();
        m_world.forEach<Animation::SkinnedPose>(query, [&](ECS::Entity entity, Animation::SkinnedPose& pose) {
            const u32 pathLen = static_cast<u32>(std::strlen(pose.meshPath));
            constexpr u32 kHumanoidBytes = static_cast<u32>(sizeof(i32) * static_cast<int>(Animation::HumanoidBone::Count));
            std::vector<u8> data(4 + pathLen + sizeof(i32) + sizeof(f32) * 2 + 2 + 2 + kHumanoidBytes);
            u32 offset = 0;
            std::memcpy(data.data() + offset, &pathLen, 4);
            offset += 4;
            if (pathLen > 0) std::memcpy(data.data() + offset, pose.meshPath, pathLen);
            offset += pathLen;
            std::memcpy(data.data() + offset, &pose.clipIndex, sizeof(i32));
            offset += sizeof(i32);
            std::memcpy(data.data() + offset, &pose.time, sizeof(f32));
            offset += sizeof(f32);
            std::memcpy(data.data() + offset, &pose.speed, sizeof(f32));
            offset += sizeof(f32);
            data[offset++] = pose.playing ? 1 : 0;
            data[offset++] = pose.loop ? 1 : 0;
            data[offset++] = 1;
            data[offset++] = static_cast<u8>((pose.humanoid.matched ? 1 : 0) | (pose.humanoid.manual ? 2 : 0) | (1u << 2));
            std::memcpy(data.data() + offset, pose.humanoid.bones, kHumanoidBytes);
            offset += kHumanoidBytes;
            auto appendBytes = [&](const void* bytes, u32 size) {
                data.resize(offset + size);
                if (size > 0) std::memcpy(data.data() + offset, bytes, size);
                offset += size;
            };
            auto appendVec3 = [&](const Vec3& value) { appendBytes(&value.x, sizeof(f32) * 3); };
            const u32 offsetCount = static_cast<u32>(pose.offsets.size());
            const u32 keyCount = static_cast<u32>(pose.keys.size());
            const u32 objectCount = static_cast<u32>(pose.objectKeys.size());
            appendBytes(&offsetCount, 4);
            for (const Animation::BonePose& bone : pose.offsets) {
                appendVec3(bone.rotation);
                appendVec3(bone.translation);
            }
            appendBytes(&keyCount, 4);
            for (const Animation::BoneKeyframe& key : pose.keys) {
                appendBytes(&key.time, sizeof(f32));
                appendBytes(&key.bone, sizeof(i32));
                appendVec3(key.rotation);
                appendVec3(key.translation);
            }
            appendBytes(&objectCount, 4);
            for (const Animation::ObjectKeyframe& key : pose.objectKeys) {
                appendBytes(&key.time, sizeof(f32));
                appendVec3(key.position);
            }
            const u32 removedCount = static_cast<u32>(pose.removedBones.size());
            appendBytes(&removedCount, 4);
            for (i32 bone : pose.removedBones) appendBytes(&bone, sizeof(i32));
            const u32 curveMarker = kPoseCurveMarker;
            appendBytes(&curveMarker, 4);
            auto appendCurve = [&](const Animation::KeyCurve& curve) {
                const u8 mode = static_cast<u8>(curve.mode);
                appendBytes(&mode, 1);
                appendBytes(&curve.x1, sizeof(f32));
                appendBytes(&curve.y1, sizeof(f32));
                appendBytes(&curve.x2, sizeof(f32));
                appendBytes(&curve.y2, sizeof(f32));
            };
            for (const Animation::BoneKeyframe& key : pose.keys) appendCurve(key.curve);
            for (const Animation::ObjectKeyframe& key : pose.objectKeys) appendCurve(key.curve);
            const u32 stripMarker = kPoseStripMarker;
            appendBytes(&stripMarker, 4);
            const u32 stripCount = static_cast<u32>(pose.strips.size());
            appendBytes(&stripCount, 4);
            for (const Animation::ClipStrip& strip : pose.strips) {
                appendBytes(&strip.clip, sizeof(i32));
                appendBytes(&strip.track, sizeof(i32));
                appendBytes(&strip.start, sizeof(f32));
                appendBytes(&strip.clipIn, sizeof(f32));
                appendBytes(&strip.clipOut, sizeof(f32));
                appendBytes(&strip.speed, sizeof(f32));
                appendBytes(&strip.repeat, sizeof(i32));
                appendBytes(&strip.fadeIn, sizeof(f32));
                appendBytes(&strip.fadeOut, sizeof(f32));
                appendCurve(strip.fadeInCurve);
                appendCurve(strip.fadeOutCurve);
                appendBytes(&strip.weight, sizeof(f32));
                const u8 flags = static_cast<u8>((strip.muted ? 1 : 0) | (strip.reverse ? 2 : 0));
                appendBytes(&flags, 1);
                appendBytes(&strip.loopBlend, sizeof(f32));
                appendBytes(strip.state, 32);
            }
            entityMap[entity.id()].emplace_back(kTypeSkinnedPose, std::move(data));
        });
    }
    emitPodComponents<Animation::SpriteSheet>(kTypeSpriteSheet, entityMap);
    emitPodComponents<Effects::EffectComponent>(kTypeEffect, entityMap);
    emitPodComponents<ECS::EnvironmentEffectsComponent>(kTypeEnvironmentEffects, entityMap);
    {
        ECS::ComponentQuery query;
        query.with<Navigation::NavVolume>();
        m_world.forEach<Navigation::NavVolume>(query, [&](ECS::Entity entity, Navigation::NavVolume& volume) {
            const u32 blockedCount = static_cast<u32>(volume.blocked.size());
            std::vector<u8> data(4 * 2 + sizeof(f32) + sizeof(f32) * 3 + 4 + blockedCount);
            u32 offset = 0;
            std::memcpy(data.data() + offset, &volume.width, 4);
            offset += 4;
            std::memcpy(data.data() + offset, &volume.height, 4);
            offset += 4;
            std::memcpy(data.data() + offset, &volume.cellSize, sizeof(f32));
            offset += sizeof(f32);
            std::memcpy(data.data() + offset, &volume.origin.x, sizeof(f32) * 3);
            offset += sizeof(f32) * 3;
            std::memcpy(data.data() + offset, &blockedCount, 4);
            offset += 4;
            if (blockedCount > 0) {
                std::memcpy(data.data() + offset, volume.blocked.data(), blockedCount);
            }
            entityMap[entity.id()].emplace_back(kTypeNavVolume, std::move(data));
        });
    }
    {
        ECS::ComponentQuery query;
        query.with<Navigation::NavAgent>();
        m_world.forEach<Navigation::NavAgent>(query, [&](ECS::Entity entity, Navigation::NavAgent& agent) {
            const u32 patrolCount = std::min(agent.patrolCount, static_cast<u32>(Navigation::kMaxPatrolPoints));
            std::vector<u8> data(sizeof(f32) * 3 + sizeof(f32) * 2 + 1 + 4 + 4 + patrolCount * sizeof(f32) * 3 + 256 + 1);
            u32 offset = 0;
            std::memcpy(data.data() + offset, &agent.destination.x, sizeof(f32) * 3);
            offset += sizeof(f32) * 3;
            std::memcpy(data.data() + offset, &agent.speed, sizeof(f32));
            offset += sizeof(f32);
            std::memcpy(data.data() + offset, &agent.arriveRadius, sizeof(f32));
            offset += sizeof(f32);
            data[offset++] = static_cast<u8>(agent.mode);
            std::memcpy(data.data() + offset, &agent.followEntity, 4);
            offset += 4;
            std::memcpy(data.data() + offset, &patrolCount, 4);
            offset += 4;
            if (patrolCount > 0) {
                std::memcpy(data.data() + offset, agent.patrol, patrolCount * sizeof(f32) * 3);
                offset += patrolCount * sizeof(f32) * 3;
            }
            std::memcpy(data.data() + offset, agent.behaviorScript, 256);
            offset += 256;
            data[offset++] = agent.hasDestination ? 1 : 0;
            data.resize(offset);
            entityMap[entity.id()].emplace_back(kTypeNavAgent, std::move(data));
        });
    }

    {
        std::vector<std::pair<u32, std::vector<u8>>> entries;
        collectTerrainComponents(filepath, entries);
        for (auto& [eid, data] : entries) {
            entityMap[eid].emplace_back(kTypeTerrain, std::move(data));
        }
    }

    auto push = [&](const void* d, u32 n) {
        const u8* p = static_cast<const u8*>(d);
        out.insert(out.end(), p, p + n);
    };
    out.clear();
    u32 signature = kSignature;
    u32 version   = kFormatVersion;
    u32 count     = static_cast<u32>(entityMap.size());
    push(&signature, 4);
    push(&version, 4);
    push(&count, 4);
    for (auto& [eid, components] : entityMap) {
        u32 compCount = static_cast<u32>(components.size());
        push(&eid, 4);
        push(&compCount, 4);
        for (auto& [typeId, data] : components) {
            u32 dataSize = static_cast<u32>(data.size());
            push(&typeId, 4);
            push(&dataSize, 4);
            if (dataSize > 0) push(data.data(), dataSize);
        }
    }
    return true;
}

bool SceneSerializer::serialize(const std::string& filepath) {
    std::vector<u8> buffer;
    if (!serializeToMemory(buffer, filepath)) return false;
    std::ofstream fout(filepath, std::ios::binary);
    if (!fout.is_open()) return false;
    fout.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    return fout.good();
}

// ── Deserialize ──────────────────────────────────────────────────

bool SceneSerializer::deserialize(const std::string& filepath) {
    std::ifstream fin(filepath, std::ios::binary | std::ios::ate);
    if (!fin.is_open()) return false;
    std::streampos fileSize = fin.tellg();
    fin.seekg(0, std::ios::beg);
    std::vector<u8> buffer(static_cast<usize>(fileSize));
    if (!fin.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        fin.close();
        return false;
    }
    fin.close();
    return deserializeFromMemory(buffer, filepath);
}

bool SceneSerializer::deserializeFromMemory(const std::vector<u8>& buffer, const std::string& filepath) {
    if (buffer.size() < 12) return false;
    u32 signature, version, entityCount;
    memcpy(&signature,   buffer.data(),      4);
    memcpy(&version,     buffer.data() + 4,  4);
    memcpy(&entityCount, buffer.data() + 8,  4);

    if (signature != kSignature) return false;
    if (version != 4 && version != 5 && version != kFormatVersion) return false;

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
                    pc.dirty  = entry.data.size() >= 5 ? (entry.data[4] != 0) : true;
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
            case kTypeMeshGeometry:
                applyMeshGeometryComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
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
            case kTypeCamera2D:
                applyPODComponent<ECS::Camera2DComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeRigidBody2D:
                applyPODComponent<Physics2D::RigidBody2D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeCollider2D:
                applyPODComponent<Physics2D::Collider2D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeRigidBody3D:
                applyPODComponent<Physics3D::RigidBody3D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeCollider3D:
                applyPODComponent<Physics3D::Collider3D>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeScript:
                applyScriptComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeCppScript:
                applyCppScriptComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypePersistent:
                applyPODComponent<ECS::PersistentComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeDisabledTag:
                m_world.add<ECS::DisabledTag>(e);
                break;
            case kTypeParticleEmitter:
                applyParticleEmitterComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeWorldTransform:
                applyPODComponent<Scene::WorldTransform>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeEntityLayer:
                applyPODComponent<Scene::EntityLayer>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeSkinnedMeshRenderer:
                applySkinnedMeshRendererComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeUIWidget:
                applyUIWidgetComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeUIButton:
                applyPODComponent<UI::UIButton>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeUILabel:
                applyPODComponent<UI::UILabel>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeUIProgressBar:
                applyPODComponent<UI::UIProgressBar>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeUISlider:
                applyPODComponent<UI::UISlider>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeUICheckbox:
                applyPODComponent<UI::UICheckbox>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeAnimator:
                applyAnimatorComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()));
                break;
            case kTypeSkybox:
                applyPODComponent<ECS::SkyboxComponent>(e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeForwardRenderFeatures:
                applyPODComponent<ECS::ForwardRenderFeaturesComponent>(
                    e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypePostProcess:
                applyPODComponent<ECS::PostProcessComponent>(
                    e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeAnimationPlayer: {
                if (entry.data.size() < 6) break;
                u32 offset = 0;
                u32 pathLen = 0;
                std::memcpy(&pathLen, entry.data.data(), 4);
                offset += 4;
                if (offset + pathLen + sizeof(f32) * 2 + 2 > entry.data.size()) break;
                Animation::AnimationPlayer player;
                if (pathLen > 0 && pathLen < sizeof(player.clipPath)) {
                    std::memcpy(player.clipPath, entry.data.data() + offset, pathLen);
                }
                offset += pathLen;
                std::memcpy(&player.time, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                std::memcpy(&player.speed, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                player.playing = entry.data[offset++] != 0;
                player.loop = entry.data[offset] != 0;
                player.loaded = false;
                m_world.add<Animation::AnimationPlayer>(e, std::move(player));
                break;
            }
            case kTypeSkinnedPose: {
                if (entry.data.size() < 6) break;
                u32 offset = 0;
                u32 pathLen = 0;
                std::memcpy(&pathLen, entry.data.data(), 4);
                offset += 4;
                if (offset + pathLen + sizeof(i32) + sizeof(f32) * 2 + 2 > entry.data.size()) break;
                Animation::SkinnedPose pose;
                if (pathLen > 0 && pathLen < sizeof(pose.meshPath)) {
                    std::memcpy(pose.meshPath, entry.data.data() + offset, pathLen);
                }
                offset += pathLen;
                std::memcpy(&pose.clipIndex, entry.data.data() + offset, sizeof(i32));
                offset += sizeof(i32);
                std::memcpy(&pose.time, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                std::memcpy(&pose.speed, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                pose.playing = entry.data[offset++] != 0;
                pose.loop = entry.data[offset++] != 0;
                constexpr u32 kHumanoidBytes = static_cast<u32>(sizeof(i32) * static_cast<int>(Animation::HumanoidBone::Count));
                constexpr u32 kLegacyHumanoidBytes = static_cast<u32>(sizeof(i32) * Animation::kHumanoidLegacyCount);
                if (offset + 2 + kLegacyHumanoidBytes <= entry.data.size() && entry.data[offset] == 1) {
                    ++offset;
                    const u8 flags = entry.data[offset++];
                    const u32 storedBytes = ((flags >> 2) & 0x7u) >= 1u ? kHumanoidBytes : kLegacyHumanoidBytes;
                    if (offset + storedBytes > entry.data.size()) break;
                    std::memcpy(pose.humanoid.bones, entry.data.data() + offset,
                                std::min(storedBytes, kHumanoidBytes));
                    pose.humanoid.matched = (flags & 1) != 0;
                    pose.humanoid.manual = (flags & 2) != 0;
                    if (pose.humanoid.manual) pose.loaded = true;
                    offset += storedBytes;
                    auto readBytes = [&](void* bytes, u32 size) -> bool {
                        if (offset + size > entry.data.size()) return false;
                        if (size > 0) std::memcpy(bytes, entry.data.data() + offset, size);
                        offset += size;
                        return true;
                    };
                    auto readVec3 = [&](Vec3& value) -> bool { return readBytes(&value.x, sizeof(f32) * 3); };
                    u32 offsetCount = 0;
                    u32 keyCount = 0;
                    u32 objectCount = 0;
                    if (readBytes(&offsetCount, 4) && offsetCount < 10000) {
                        pose.offsets.resize(offsetCount);
                        bool ok = true;
                        for (Animation::BonePose& bone : pose.offsets) {
                            ok = ok && readVec3(bone.rotation) && readVec3(bone.translation);
                        }
                        ok = ok && readBytes(&keyCount, 4) && keyCount < 100000;
                        if (ok) {
                            pose.keys.resize(keyCount);
                            for (Animation::BoneKeyframe& key : pose.keys) {
                                ok = ok && readBytes(&key.time, sizeof(f32)) && readBytes(&key.bone, sizeof(i32));
                                ok = ok && readVec3(key.rotation) && readVec3(key.translation);
                            }
                        }
                        ok = ok && readBytes(&objectCount, 4) && objectCount < 100000;
                        if (ok) {
                            pose.objectKeys.resize(objectCount);
                            for (Animation::ObjectKeyframe& key : pose.objectKeys) {
                                ok = ok && readBytes(&key.time, sizeof(f32)) && readVec3(key.position);
                            }
                        }
                        if (!ok) {
                            pose.offsets.clear();
                            pose.keys.clear();
                            pose.objectKeys.clear();
                        } else {
                            u32 removedCount = 0;
                            if (readBytes(&removedCount, 4) && removedCount < 10000) {
                                pose.removedBones.resize(removedCount);
                                for (i32& bone : pose.removedBones) {
                                    if (!readBytes(&bone, sizeof(i32))) {
                                        pose.removedBones.clear();
                                        break;
                                    }
                                }
                            }
                            u32 curveMarker = 0;
                            if (readBytes(&curveMarker, 4) && curveMarker == kPoseCurveMarker) {
                                auto readCurve = [&](Animation::KeyCurve& curve) -> bool {
                                    u8 mode = 0;
                                    if (!readBytes(&mode, 1)) return false;
                                    if (mode > static_cast<u8>(Animation::KeyInterp::Custom)) mode = 0;
                                    curve.mode = static_cast<Animation::KeyInterp>(mode);
                                    return readBytes(&curve.x1, sizeof(f32)) && readBytes(&curve.y1, sizeof(f32)) &&
                                           readBytes(&curve.x2, sizeof(f32)) && readBytes(&curve.y2, sizeof(f32));
                                };
                                for (Animation::BoneKeyframe& key : pose.keys) {
                                    if (!readCurve(key.curve)) break;
                                }
                                for (Animation::ObjectKeyframe& key : pose.objectKeys) {
                                    if (!readCurve(key.curve)) break;
                                }
                            }
                            u32 stripMarker = 0;
                            u32 stripCount = 0;
                            if (readBytes(&stripMarker, 4) && stripMarker == kPoseStripMarker &&
                                readBytes(&stripCount, 4) && stripCount < 10000) {
                                pose.strips.resize(stripCount);
                                pose.stripsInitialized = true;
                                for (Animation::ClipStrip& strip : pose.strips) {
                                    u8 flags = 0;
                                    if (!readBytes(&strip.clip, sizeof(i32)) || !readBytes(&strip.track, sizeof(i32)) ||
                                        !readBytes(&strip.start, sizeof(f32)) || !readBytes(&strip.clipIn, sizeof(f32)) ||
                                        !readBytes(&strip.clipOut, sizeof(f32)) || !readBytes(&strip.speed, sizeof(f32)) ||
                                        !readBytes(&strip.repeat, sizeof(i32)) || !readBytes(&strip.fadeIn, sizeof(f32)) ||
                                        !readBytes(&strip.fadeOut, sizeof(f32))) {
                                        pose.strips.clear();
                                        break;
                                    }
                                    auto readCurve = [&](Animation::KeyCurve& curve) -> bool {
                                        u8 mode = 0;
                                        if (!readBytes(&mode, 1)) return false;
                                        if (mode > static_cast<u8>(Animation::KeyInterp::Custom)) mode = 0;
                                        curve.mode = static_cast<Animation::KeyInterp>(mode);
                                        return readBytes(&curve.x1, sizeof(f32)) && readBytes(&curve.y1, sizeof(f32)) &&
                                               readBytes(&curve.x2, sizeof(f32)) && readBytes(&curve.y2, sizeof(f32));
                                    };
                                    if (!readCurve(strip.fadeInCurve) || !readCurve(strip.fadeOutCurve) ||
                                        !readBytes(&strip.weight, sizeof(f32)) || !readBytes(&flags, 1) ||
                                        !readBytes(&strip.loopBlend, sizeof(f32)) || !readBytes(strip.state, 32)) {
                                        pose.strips.clear();
                                        break;
                                    }
                                    strip.muted = (flags & 1) != 0;
                                    strip.reverse = (flags & 2) != 0;
                                    strip.state[31] = 0;
                                }
                            }
                        }
                    }
                }
                m_world.add<Animation::SkinnedPose>(e, std::move(pose));
                break;
            }
            case kTypeSpriteSheet:
                applyPODComponent<Animation::SpriteSheet>(
                    e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeEffect:
                applyPODComponent<Effects::EffectComponent>(
                    e, entry.data.data(), static_cast<u32>(entry.data.size()), m_world);
                break;
            case kTypeEnvironmentEffects: {
                auto& fx = m_world.add<ECS::EnvironmentEffectsComponent>(e);
                const u32 n = std::min(static_cast<u32>(sizeof(fx)),
                                       static_cast<u32>(entry.data.size()));
                if (n > 0) std::memcpy(&fx, entry.data.data(), n);
                break;
            }
            case kTypeNavVolume: {
                if (entry.data.size() < 24) break;
                Navigation::NavVolume volume;
                u32 offset = 0;
                std::memcpy(&volume.width, entry.data.data() + offset, 4);
                offset += 4;
                std::memcpy(&volume.height, entry.data.data() + offset, 4);
                offset += 4;
                std::memcpy(&volume.cellSize, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                std::memcpy(&volume.origin.x, entry.data.data() + offset, sizeof(f32) * 3);
                offset += sizeof(f32) * 3;
                u32 blockedCount = 0;
                if (offset + 4 > entry.data.size()) break;
                std::memcpy(&blockedCount, entry.data.data() + offset, 4);
                offset += 4;
                if (offset + blockedCount <= entry.data.size()) {
                    volume.blocked.assign(entry.data.data() + offset, entry.data.data() + offset + blockedCount);
                }
                m_world.add<Navigation::NavVolume>(e, std::move(volume));
                break;
            }
            case kTypeNavAgent: {
                if (entry.data.size() < 20) break;
                Navigation::NavAgent agent;
                u32 offset = 0;
                std::memcpy(&agent.destination.x, entry.data.data() + offset, sizeof(f32) * 3);
                offset += sizeof(f32) * 3;
                std::memcpy(&agent.speed, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                std::memcpy(&agent.arriveRadius, entry.data.data() + offset, sizeof(f32));
                offset += sizeof(f32);
                agent.mode = static_cast<Navigation::NavMode>(entry.data[offset++]);
                std::memcpy(&agent.followEntity, entry.data.data() + offset, 4);
                offset += 4;
                u32 patrolCount = 0;
                std::memcpy(&patrolCount, entry.data.data() + offset, 4);
                offset += 4;
                agent.patrolCount = std::min(patrolCount, static_cast<u32>(Navigation::kMaxPatrolPoints));
                const u32 patrolBytes = agent.patrolCount * static_cast<u32>(sizeof(f32) * 3);
                if (offset + patrolBytes + 256 <= entry.data.size()) {
                    if (patrolBytes > 0) {
                        std::memcpy(agent.patrol, entry.data.data() + offset, patrolBytes);
                        offset += patrolBytes;
                    }
                    std::memcpy(agent.behaviorScript, entry.data.data() + offset, 256);
                    offset += 256;
                    if (offset < entry.data.size()) agent.hasDestination = entry.data[offset] != 0;
                }
                agent.planDirty = true;
                m_world.add<Navigation::NavAgent>(e, std::move(agent));
                break;
            }
            case kTypeTerrain:
                applyTerrainComponent(e, entry.data.data(), static_cast<u32>(entry.data.size()), filepath);
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

bool SceneSerializer::applyMeshGeometryComponent(ECS::Entity e, const u8* data, u32 size) {
    if (size < 12) return false;
    u32 positionCount = 0;
    u32 indexCount = 0;
    u32 revision = 1;
    memcpy(&positionCount, data, 4);
    memcpy(&indexCount, data + 4, 4);
    memcpy(&revision, data + 8, 4);
    const size_t bytes = 12ull + static_cast<size_t>(positionCount) * sizeof(Vec3) +
                         static_cast<size_t>(indexCount) * sizeof(u32);
    if (bytes != size || positionCount > 200000u || indexCount > 600000u) return false;
    ECS::MeshGeometryComponent geometry;
    geometry.revision = revision == 0 ? 1 : revision;
    geometry.positions.resize(positionCount);
    geometry.indices.resize(indexCount);
    if (positionCount > 0) {
        memcpy(geometry.positions.data(), data + 12, positionCount * sizeof(Vec3));
    }
    if (indexCount > 0) {
        memcpy(geometry.indices.data(), data + 12 + positionCount * sizeof(Vec3), indexCount * sizeof(u32));
    }
    m_world.add<ECS::MeshGeometryComponent>(e, std::move(geometry));
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

    u32 texturePathLen = 0;
    const u32 textureOffset = 5 + pathLen;
    if (textureOffset + 4 <= size) {
        memcpy(&texturePathLen, data + textureOffset, 4);
        if (textureOffset + 4 + texturePathLen <= size && texturePathLen > 0) {
            mf.customTexturePath.assign(reinterpret_cast<const char*>(data + textureOffset + 4), texturePathLen);
        }
    }

    u32 materialPathLen = 0;
    const u32 materialOffset = textureOffset + 4 + texturePathLen;
    if (materialOffset + 4 <= size) {
        memcpy(&materialPathLen, data + materialOffset, 4);
        if (materialOffset + 4 + materialPathLen <= size && materialPathLen > 0) {
            mf.customMaterialPath.assign(reinterpret_cast<const char*>(data + materialOffset + 4), materialPathLen);
        }
    }

    const u32 normalOffset = materialOffset + 4 + materialPathLen;
    if (normalOffset + 4 <= size) {
        u32 normalPathLen = 0;
        memcpy(&normalPathLen, data + normalOffset, 4);
        if (normalOffset + 4 + normalPathLen <= size && normalPathLen > 0) {
            mf.customNormalPath.assign(reinterpret_cast<const char*>(data + normalOffset + 4),
                                       normalPathLen);
        }
        const u32 shininessOffset = normalOffset + 4 + normalPathLen;
        if (shininessOffset + sizeof(f32) <= size) {
            memcpy(&mf.shininess, data + shininessOffset, sizeof(f32));
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

bool SceneSerializer::applyScriptComponent(ECS::Entity e, const u8* data, u32 size) {
    const u8* cursor = data;
    const u8* end = data + size;
    std::string path;
    if (!IO::readString(cursor, end, path)) return false;
    auto& sc = m_world.add<Script::ScriptComponent>(e);
    sc.scriptPath = std::move(path);
    return true;
}

bool SceneSerializer::applyCppScriptComponent(ECS::Entity e, const u8* data, u32 size) {
    const u8* cursor = data;
    const u8* end = data + size;
    std::string className;
    if (!IO::readString(cursor, end, className)) return false;
    auto& sc = m_world.add<Script::CppScriptComponent>(e);
    sc.className = std::move(className);
    sc.instance.reset();
    sc.initialized = false;
    return true;
}

bool SceneSerializer::applyParticleEmitterComponent(ECS::Entity e, const u8* data, u32 size) {
    const u8* cursor = data;
    const u8* end = data + size;
    auto& pe = m_world.add<ECS::ParticleEmitterComponent>(e);
    if (!IO::readI32(cursor, end, pe.maxParticles)) return false;
    if (!IO::readF32(cursor, end, pe.emissionRate)) return false;
    if (!IO::readF32(cursor, end, pe.lifetime)) return false;
    if (!IO::readPOD(cursor, end, pe.velocityMin)) return false;
    if (!IO::readPOD(cursor, end, pe.velocityMax)) return false;
    if (!IO::readU32(cursor, end, pe.startColor)) return false;
    if (!IO::readU32(cursor, end, pe.endColor)) return false;
    if (!IO::readF32(cursor, end, pe.startSize)) return false;
    if (!IO::readF32(cursor, end, pe.endSize)) return false;
    u32 count = 0;
    if (!IO::readU32(cursor, end, count)) return false;
    pe.activeParticles.clear();
    pe.activeParticles.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        ECS::ParticleEmitterComponent::Particle p{};
        if (!IO::readPOD(cursor, end, p)) return false;
        pe.activeParticles.push_back(p);
    }
    return true;
}

bool SceneSerializer::applySkinnedMeshRendererComponent(ECS::Entity e, const u8* data, u32 size) {
    const u8* cursor = data;
    const u8* end = data + size;
    std::string meshPath;
    std::string materialPath;
    std::string skeletonPath;
    if (!IO::readString(cursor, end, meshPath)) return false;
    if (!IO::readString(cursor, end, materialPath)) return false;
    if (!IO::readString(cursor, end, skeletonPath)) return false;
    u8 castShadows = 1;
    u8 receiveShadows = 1;
    if (!IO::readU8(cursor, end, castShadows)) return false;
    if (!IO::readU8(cursor, end, receiveShadows)) return false;
    auto& mr = m_world.add<ECS::SkinnedMeshRendererComponent>(e);
    mr.meshPath = std::move(meshPath);
    mr.materialPath = std::move(materialPath);
    mr.skeletonPath = std::move(skeletonPath);
    mr.castShadows = castShadows != 0;
    mr.receiveShadows = receiveShadows != 0;
    return true;
}

bool SceneSerializer::applyUIWidgetComponent(ECS::Entity e, const u8* data, u32 size) {
    const u8* cursor = data;
    const u8* end = data + size;
    auto& w = m_world.add<UI::UIWidget>(e);
    u8 type = 0;
    if (!IO::readU8(cursor, end, type)) return false;
    w.type = static_cast<UI::UIWidgetType>(type);
    if (!IO::readU32(cursor, end, w.parentId)) return false;
    u8 visible = 1;
    u8 interactable = 1;
    if (!IO::readU8(cursor, end, visible)) return false;
    if (!IO::readU8(cursor, end, interactable)) return false;
    w.visible = visible != 0;
    w.interactable = interactable != 0;
    if (!IO::readI32(cursor, end, w.siblingOrder)) return false;
    if (!IO::readPOD(cursor, end, w.style)) return false;
    if (!IO::readPOD(cursor, end, w.transform)) return false;
    w.computedRect = {};
    w.onClick = nullptr;
    w.onHoverEnter = nullptr;
    w.onHoverExit = nullptr;
    w.onValueChanged = nullptr;
    return true;
}

bool SceneSerializer::applyAnimatorComponent(ECS::Entity e, const u8* data, u32 size) {
    const u8* cursor = data;
    const u8* end = data + size;
    auto& anim = m_world.add<Animation::Animator>(e);
    anim = Animation::Animator{};
    anim.embeddedClips.clear();

    u32 clipCount = 0;
    if (!IO::readU32(cursor, end, clipCount)) return false;
    anim.embeddedClips.resize(clipCount);
    for (u32 i = 0; i < clipCount; ++i) {
        std::string clipName;
        if (!IO::readString(cursor, end, clipName)) return false;
        anim.embeddedClips[i].name = clipName.c_str();
        if (!IO::readU32(cursor, end, anim.embeddedClips[i].fps)) return false;
        u8 loop = 1;
        if (!IO::readU8(cursor, end, loop)) return false;
        anim.embeddedClips[i].loop = loop != 0;
        u32 frameCount = 0;
        if (!IO::readU32(cursor, end, frameCount)) return false;
        anim.embeddedClips[i].frames.resize(frameCount);
        for (u32 f = 0; f < frameCount; ++f) {
            if (!IO::readPOD(cursor, end, anim.embeddedClips[i].frames[f])) return false;
        }
    }

    u32 paramCount = 0;
    if (!IO::readU32(cursor, end, paramCount)) return false;
    anim.parameters.resize(paramCount);
    for (u32 i = 0; i < paramCount; ++i) {
        std::string name;
        if (!IO::readString(cursor, end, name)) return false;
        anim.parameters[i].name = name.c_str();
        u8 type = 0;
        u8 triggered = 0;
        u8 boolValue = 0;
        if (!IO::readU8(cursor, end, type)) return false;
        if (!IO::readU8(cursor, end, triggered)) return false;
        if (!IO::readU8(cursor, end, boolValue)) return false;
        if (!IO::readF32(cursor, end, anim.parameters[i].floatValue)) return false;
        if (!IO::readI32(cursor, end, anim.parameters[i].intValue)) return false;
        anim.parameters[i].type = static_cast<Animation::ParameterType>(type);
        anim.parameters[i].triggered = triggered != 0;
        anim.parameters[i].boolValue = boolValue != 0;
    }

    u32 stateCount = 0;
    if (!IO::readU32(cursor, end, stateCount)) return false;
    for (u32 i = 0; i < stateCount; ++i) {
        std::string stateName;
        if (!IO::readString(cursor, end, stateName)) return false;
        Animation::AnimationState state;
        u32 clipIndex = 0xFFFFFFFFu;
        if (!IO::readU32(cursor, end, clipIndex)) return false;
        if (!IO::readF32(cursor, end, state.speed)) return false;
        if (clipIndex != 0xFFFFFFFFu && clipIndex < anim.embeddedClips.size()) {
            state.clip = &anim.embeddedClips[clipIndex];
        }
        u32 transitionCount = 0;
        if (!IO::readU32(cursor, end, transitionCount)) return false;
        state.transitions.resize(transitionCount);
        for (u32 t = 0; t < transitionCount; ++t) {
            std::string toState;
            if (!IO::readString(cursor, end, toState)) return false;
            state.transitions[t].toState = toState.c_str();
            if (!IO::readF32(cursor, end, state.transitions[t].blendTime)) return false;
            u8 hasExit = 0;
            if (!IO::readU8(cursor, end, hasExit)) return false;
            state.transitions[t].hasExitTime = hasExit != 0;
            u32 condCount = 0;
            if (!IO::readU32(cursor, end, condCount)) return false;
            state.transitions[t].conditions.resize(condCount);
            for (u32 c = 0; c < condCount; ++c) {
                std::string paramName;
                if (!IO::readString(cursor, end, paramName)) return false;
                state.transitions[t].conditions[c].parameterName = paramName.c_str();
                u8 op = 0;
                u8 boolValue = 0;
                if (!IO::readU8(cursor, end, op)) return false;
                if (!IO::readU8(cursor, end, boolValue)) return false;
                if (!IO::readF32(cursor, end, state.transitions[t].conditions[c].floatValue)) return false;
                if (!IO::readI32(cursor, end, state.transitions[t].conditions[c].intValue)) return false;
                state.transitions[t].conditions[c].op = static_cast<Animation::ConditionOperator>(op);
                state.transitions[t].conditions[c].boolValue = boolValue != 0;
            }
        }
        anim.states.set(stateName.c_str(), state);
    }

    std::string currentState;
    std::string previousState;
    if (!IO::readString(cursor, end, currentState)) return false;
    if (!IO::readString(cursor, end, previousState)) return false;
    anim.currentState = currentState.c_str();
    anim.previousState = previousState.c_str();
    if (!IO::readF32(cursor, end, anim.timeInState)) return false;
    if (!IO::readF32(cursor, end, anim.blendWeight)) return false;
    if (!IO::readF32(cursor, end, anim.playbackScale)) return false;
    u8 paused = 0;
    if (!IO::readU8(cursor, end, paused)) return false;
    anim.paused = paused != 0;

    u32 eventCount = 0;
    if (!IO::readU32(cursor, end, eventCount)) return false;
    anim.frameEvents.resize(eventCount);
    for (u32 i = 0; i < eventCount; ++i) {
        u32 frame = 0;
        std::string evt;
        if (!IO::readU32(cursor, end, frame)) return false;
        if (!IO::readString(cursor, end, evt)) return false;
        anim.frameEvents[i] = {frame, evt.c_str()};
    }

    u32 editorMarker = 0;
    if (IO::readU32(cursor, end, editorMarker) && editorMarker == kAnimatorEditorMarker) {
        std::string defaultState;
        u32 count = 0;
        if (IO::readString(cursor, end, defaultState) && IO::readU32(cursor, end, count)) {
            anim.defaultState = defaultState.c_str();
            for (u32 i = 0; i < count; ++i) {
                std::string stateName;
                std::string motion;
                u8 hasPosition = 0;
                f32 x = 0.0f;
                f32 y = 0.0f;
                if (!IO::readString(cursor, end, stateName) || !IO::readString(cursor, end, motion) ||
                    !IO::readU8(cursor, end, hasPosition) || !IO::readF32(cursor, end, x) ||
                    !IO::readF32(cursor, end, y)) {
                    break;
                }
                if (Animation::AnimationState* state = anim.states.get(FixedString<32>(stateName.c_str()))) {
                    state->motion = motion.c_str();
                    state->hasEditorPosition = hasPosition != 0;
                    state->editorX = x;
                    state->editorY = y;
                }
            }
        }
    }

    u32 graphMarker = 0;
    if (IO::readU32(cursor, end, graphMarker) && graphMarker == kAnimatorGraphMarker) {
        auto readCurve = [&](Animation::KeyCurve& curve) -> bool {
            u8 mode = 0;
            if (!IO::readU8(cursor, end, mode) || !IO::readF32(cursor, end, curve.x1) ||
                !IO::readF32(cursor, end, curve.y1) || !IO::readF32(cursor, end, curve.x2) ||
                !IO::readF32(cursor, end, curve.y2)) {
                return false;
            }
            if (mode > static_cast<u8>(Animation::KeyInterp::Custom)) mode = 0;
            curve.mode = static_cast<Animation::KeyInterp>(mode);
            return true;
        };
        auto readTransition = [&](Animation::AnimationTransition& tr) -> bool {
            std::string to;
            u8 hasExit = 0;
            u32 condCount = 0;
            if (!IO::readString(cursor, end, to) || !IO::readF32(cursor, end, tr.blendTime) ||
                !IO::readU8(cursor, end, hasExit) || !IO::readF32(cursor, end, tr.exitTime) ||
                !IO::readF32(cursor, end, tr.offset) || !readCurve(tr.blendCurve) ||
                !IO::readU32(cursor, end, condCount)) {
                return false;
            }
            tr.toState = to.c_str();
            tr.hasExitTime = hasExit != 0;
            if (condCount > 256) return false;
            tr.conditions.resize(condCount);
            for (auto& cond : tr.conditions) {
                std::string param;
                u8 op = 0;
                u8 boolValue = 0;
                if (!IO::readString(cursor, end, param) || !IO::readU8(cursor, end, op) ||
                    !IO::readU8(cursor, end, boolValue) || !IO::readF32(cursor, end, cond.floatValue) ||
                    !IO::readI32(cursor, end, cond.intValue)) {
                    return false;
                }
                cond.parameterName = param.c_str();
                cond.op = static_cast<Animation::ConditionOperator>(op);
                cond.boolValue = boolValue != 0;
            }
            return true;
        };
        auto readMachine = [&](Animation::AnimatorStateMachine& sm) -> bool {
            std::string defaultState;
            u32 anyCount = 0;
            u32 stateCount = 0;
            if (!IO::readString(cursor, end, defaultState) || !IO::readF32(cursor, end, sm.entryX) ||
                !IO::readF32(cursor, end, sm.entryY) || !IO::readF32(cursor, end, sm.anyX) ||
                !IO::readF32(cursor, end, sm.anyY) || !IO::readF32(cursor, end, sm.exitX) ||
                !IO::readF32(cursor, end, sm.exitY) || !IO::readU32(cursor, end, anyCount)) {
                return false;
            }
            sm.defaultState = defaultState.c_str();
            if (anyCount > 256) return false;
            sm.anyStateTransitions.resize(anyCount);
            for (auto& tr : sm.anyStateTransitions) {
                if (!readTransition(tr)) return false;
            }
            if (!IO::readU32(cursor, end, stateCount) || stateCount > 512) return false;
            sm.states.clear();
            for (u32 i = 0; i < stateCount; ++i) {
                std::string name;
                std::string motion;
                u8 hasPos = 0;
                u32 trCount = 0;
                Animation::AnimationState state;
                if (!IO::readString(cursor, end, name) || !IO::readString(cursor, end, motion) ||
                    !IO::readF32(cursor, end, state.speed) || !IO::readU8(cursor, end, hasPos) ||
                    !IO::readF32(cursor, end, state.editorX) || !IO::readF32(cursor, end, state.editorY) ||
                    !IO::readU32(cursor, end, trCount)) {
                    return false;
                }
                state.name = name.c_str();
                state.motion = motion.c_str();
                state.hasEditorPosition = hasPos != 0;
                if (trCount > 256) return false;
                state.transitions.resize(trCount);
                for (auto& tr : state.transitions) {
                    if (!readTransition(tr)) return false;
                }
                sm.states.set(state.name, state);
            }
            if (sm.currentState.empty()) sm.currentState = sm.defaultState;
            return true;
        };
        if (readMachine(anim)) {
            u32 layerCount = 0;
            if (IO::readU32(cursor, end, layerCount) && layerCount < 64) {
                anim.layers.resize(layerCount);
                for (Animation::AnimatorLayer& layer : anim.layers) {
                    std::string name;
                    std::string parent;
                    u8 isGroup = 0, muted = 0, expanded = 1, blend = 0;
                    if (!IO::readString(cursor, end, name) || !IO::readString(cursor, end, parent) ||
                        !IO::readU8(cursor, end, isGroup) || !IO::readU8(cursor, end, muted) ||
                        !IO::readU8(cursor, end, expanded) || !IO::readF32(cursor, end, layer.weight) ||
                        !IO::readU8(cursor, end, blend) || !IO::readU32(cursor, end, layer.mask)) {
                        anim.layers.clear();
                        break;
                    }
                    layer.name = name.c_str();
                    layer.parentGroup = parent.c_str();
                    layer.isGroup = isGroup != 0;
                    layer.muted = muted != 0;
                    layer.expanded = expanded != 0;
                    layer.blend = blend != 0 ? Animation::LayerBlendMode::Additive : Animation::LayerBlendMode::Override;
                    if (!layer.isGroup && !readMachine(layer)) {
                        anim.layers.clear();
                        break;
                    }
                }
            }
        }
    }

    anim.onFrameEvent = nullptr;
    return true;
}

bool SceneSerializer::applyTerrainComponent(ECS::Entity e, const u8* data, u32 size,
                                            const std::string& scenePath) {
    ECS::TerrainComponent terrain;
    if (!deserializeTerrainComponent(data, size, terrain)) return false;

    m_world.add<ECS::TerrainComponent>(e, terrain);
    auto* terrainComponent = m_world.get<ECS::TerrainComponent>(e);
    if (!terrainComponent) return false;

    Terrain::TerrainCache::instance().repairTexturePaths(*terrainComponent);
    const std::filesystem::path projectRoot = projectRootFromScenePath(scenePath);
    auto& cache = Terrain::TerrainCache::instance();
    bool loaded = false;
    if (!projectRoot.empty() && terrainComponent->terrainDataPath[0] != '\0') {
        loaded = cache.loadTerrainFile(m_world, e, *terrainComponent,
                                       projectRoot / terrainComponent->terrainDataPath);
    }

    if (!loaded) {
        cache.initializeEntity(m_world, e);
    } else {
        cache.syncEntity(m_world, e);
    }
    return true;
}

} // namespace Caffeine::Editor
