#pragma once

#include "ecs/World.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentQuery.hpp"
#include "core/io/CafTypes.hpp"
#include "core/io/CafWriter.hpp"
#include "core/io/Crc32.hpp"
#include "scene/SceneComponents.hpp"

#include <vector>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <tuple>

namespace Caffeine::Scene {

class SceneSerializer {
public:
    explicit SceneSerializer(ECS::World& world) : m_world(world) {}

    // ── Binary .caf ──────────────────────────────────────────────────────────

    bool serialize(const char* path) const {
        auto [meta, payload] = buildBlocks();
        auto result = IO::CafWriter::write(
            path,
            AssetType::Scene,
            CAF_FLAG_NONE,
            meta.data(),    static_cast<u64>(meta.size()),
            payload.data(), static_cast<u64>(payload.size()));
        return result.success;
    }

    bool deserialize(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;

        CafHeader hdr{};
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); return false; }
        if (hdr.magic != CafHeader::kMagic || hdr.type != AssetType::Scene) { fclose(f); return false; }

        if (fseek(f, static_cast<long>(sizeof(CafHeader) + hdr.metadataSize), SEEK_SET) != 0) {
            fclose(f); return false;
        }

        std::vector<u8> payload(static_cast<usize>(hdr.dataSize));
        if (fread(payload.data(), 1, hdr.dataSize, f) != hdr.dataSize) { fclose(f); return false; }
        fclose(f);

        return parsePayload(payload.data(), hdr.dataSize);
    }

    // ── JSON (dev/debug) ─────────────────────────────────────────────────────

    bool serializeJson(const char* path) const {
        FILE* f = fopen(path, "w");
        if (!f) return false;

        ECS::World& w = const_cast<ECS::World&>(m_world);

        fprintf(f, "{\n");
        fprintf(f, "  \"entityCount\": %u,\n", m_world.entityCount());
        fprintf(f, "  \"components\": {");

        bool firstSection = true;

        auto beginSection = [&](const char* name) {
            if (!firstSection) fprintf(f, ",");
            firstSection = false;
            fprintf(f, "\n    \"%s\": [", name);
        };

        // Transform
        {
            std::vector<std::pair<u32, ECS::Transform>> entries;
            ECS::ComponentQuery q; q.with<ECS::Transform>();
            w.forEach<ECS::Transform>(q, [&](ECS::Entity e, ECS::Transform& c) {
                entries.push_back({e.id(), c});
            });
            if (!entries.empty()) {
                beginSection("Transform");
                for (usize i = 0; i < entries.size(); ++i) {
                    if (i > 0) fprintf(f, ", ");
                    fprintf(f,
                        "{\"entity\": %u, \"px\": %.6f, \"py\": %.6f, \"pz\": %.6f, "
                        "\"rx\": %.6f, \"ry\": %.6f, \"rz\": %.6f, "
                        "\"sx\": %.6f, \"sy\": %.6f, \"sz\": %.6f}",
                        entries[i].first,
                        entries[i].second.position.x, entries[i].second.position.y, entries[i].second.position.z,
                        entries[i].second.rotation.x, entries[i].second.rotation.y, entries[i].second.rotation.z,
                        entries[i].second.scale.x,    entries[i].second.scale.y,    entries[i].second.scale.z);
                }
                fprintf(f, "]");
            }
        }

        // WorldTransform
        {
            std::vector<std::pair<u32, WorldTransform>> entries;
            ECS::ComponentQuery q; q.with<WorldTransform>();
            w.forEach<WorldTransform>(q, [&](ECS::Entity e, WorldTransform& c) {
                entries.push_back({e.id(), c});
            });
            if (!entries.empty()) {
                beginSection("WorldTransform");
                for (usize i = 0; i < entries.size(); ++i) {
                    if (i > 0) fprintf(f, ", ");
                    fprintf(f, "{\"entity\": %u, \"m00\": %.6f}",
                        entries[i].first, entries[i].second.matrix.data()[0]);
                }
                fprintf(f, "]");
            }
        }

        fprintf(f, "\n  }\n}\n");
        fclose(f);
        return true;
    }

    bool deserializeJson(const char*) { return false; }

    // ── Memory (no file I/O) ──────────────────────────────────────────────────

    bool serializeToMemory(std::vector<u8>& out) const {
        auto [meta, payload] = buildBlocks();

        u32 payloadCrc = IO::crc32(payload.data(), static_cast<u64>(payload.size()));

        CafHeader hdr{};
        hdr.magic        = CafHeader::kMagic;
        hdr.versionMajor = CafHeader::kVersionMajor;
        hdr.versionMinor = CafHeader::kVersionMinor;
        hdr.type         = AssetType::Scene;
        hdr.flags        = CAF_FLAG_NONE;
        hdr.crc32        = payloadCrc;
        hdr.metadataSize = static_cast<u64>(meta.size());
        hdr.dataSize     = static_cast<u64>(payload.size());

        out.clear();
        auto push = [&](const void* d, u64 n) {
            const u8* p = static_cast<const u8*>(d);
            out.insert(out.end(), p, p + n);
        };

        push(&hdr, sizeof(hdr));
        push(meta.data(),    meta.size());
        push(payload.data(), payload.size());

        u32 footerCrc = IO::crc32(out.data(), static_cast<u64>(out.size()));
        push(&footerCrc, 4);

        return true;
    }

    bool deserializeFromMemory(const std::vector<u8>& data) {
        if (data.size() < sizeof(CafHeader)) return false;

        CafHeader hdr{};
        memcpy(&hdr, data.data(), sizeof(hdr));

        if (hdr.magic != CafHeader::kMagic) return false;
        if (hdr.type  != AssetType::Scene)  return false;

        const u64 minSize = sizeof(CafHeader) + hdr.metadataSize + hdr.dataSize + CafHeader::kFooterSize;
        if (static_cast<u64>(data.size()) < minSize) return false;

        const u8* payloadPtr = data.data() + sizeof(CafHeader) + hdr.metadataSize;
        return parsePayload(payloadPtr, hdr.dataSize);
    }

private:
    ECS::World& m_world;

    static constexpr u32 kTypeTransform       = 0;
    static constexpr u32 kTypeAcceleration2D = 1;
    static constexpr u32 kTypeParent         = 2;
    static constexpr u32 kTypeWorldTransform = 3;
    static constexpr u32 kTypeCount          = 4;

    static constexpr u64 kCompSizes[kTypeCount] = {
        sizeof(ECS::Transform),       // 0
        sizeof(ECS::Acceleration2D),  // 1
        5u,                           // 2: Parent → u32 parentId + u8 dirty
        sizeof(WorldTransform),       // 3
    };

    // Generic helper: serialize a POD component type into the payload buffer.
    template<typename T>
    static void appendSection(std::vector<u8>& payload, u32 typeId,
                              ECS::World& world, u32& secCount) {
        std::vector<std::pair<u32, T>> entries;
        ECS::ComponentQuery q;
        q.with<T>();
        world.forEach<T>(q, [&](ECS::Entity e, T& c) {
            entries.push_back({e.id(), c});
        });
        if (entries.empty()) return;

        auto push = [&](const void* d, u64 n) {
            const u8* p = static_cast<const u8*>(d);
            payload.insert(payload.end(), p, p + n);
        };

        u32 count = static_cast<u32>(entries.size());
        push(&typeId, 4);
        push(&count,  4);
        for (auto& [eid, comp] : entries) {
            push(&eid,  4);
            push(&comp, sizeof(T));
        }
        ++secCount;
    }

    std::pair<std::vector<u8>, std::vector<u8>> buildBlocks() const {
        std::vector<u8> payload;
        u32 sectionCount = 0;

        ECS::World& w = const_cast<ECS::World&>(m_world);

        appendSection<ECS::Transform>     (payload, kTypeTransform,       w, sectionCount);
        appendSection<ECS::Acceleration2D>(payload, kTypeAcceleration2D, w, sectionCount);

        // Parent — stores entity reference: must be handled specially
        {
            std::vector<std::tuple<u32, u32, u8>> entries; // eid, parentId, dirty
            ECS::ComponentQuery q; q.with<Parent>();
            w.forEach<Parent>(q, [&](ECS::Entity e, Parent& p) {
                u32 parentId = p.parent.isValid() ? p.parent.id() : u32_max;
                entries.emplace_back(e.id(), parentId, p.dirty ? u8(1) : u8(0));
            });
            if (!entries.empty()) {
                auto push = [&](const void* d, u64 n) {
                    const u8* p = static_cast<const u8*>(d);
                    payload.insert(payload.end(), p, p + n);
                };
                u32 count = static_cast<u32>(entries.size());
                push(&kTypeParent, 4);
                push(&count,       4);
                for (auto& [eid, parentId, dirty] : entries) {
                    push(&eid,      4);
                    push(&parentId, 4);
                    push(&dirty,    1);
                }
                ++sectionCount;
            }
        }

        appendSection<WorldTransform>(payload, kTypeWorldTransform, w, sectionCount);

        SceneMetadata meta{};
        meta.entityCount         = m_world.entityCount();
        meta.archetypeCount      = sectionCount;
        meta.stringTableOffset   = 0;
        meta.assetRefTableOffset = 0;
        meta.reserved            = 0;

        std::vector<u8> metaBytes(sizeof(SceneMetadata));
        memcpy(metaBytes.data(), &meta, sizeof(meta));

        return {metaBytes, payload};
    }

    bool parsePayload(const u8* data, u64 size) {
        struct RawSection {
            u32      typeId;
            u32      count;
            const u8* ptr;   // points to first entry in section
        };
        std::vector<RawSection> sections;
        std::unordered_set<u32> entityIds;

        const u8* cursor = data;
        const u8* endPtr = data + size;

        // Pass 1: parse section headers, collect all entity IDs
        while (cursor + 8 <= endPtr) {
            u32 typeId, count;
            memcpy(&typeId, cursor, 4); cursor += 4;
            memcpy(&count,  cursor, 4); cursor += 4;

            if (typeId >= kTypeCount) return false;

            u64 entrySize = 4u + kCompSizes[typeId];
            u64 secBytes  = static_cast<u64>(count) * entrySize;
            if (cursor + secBytes > endPtr) return false;

            sections.push_back({typeId, count, cursor});

            for (u32 i = 0; i < count; ++i) {
                u32 eid;
                memcpy(&eid, cursor + i * entrySize, 4);
                entityIds.insert(eid);

                if (typeId == kTypeParent) {
                    u32 parentId;
                    memcpy(&parentId, cursor + i * entrySize + 4, 4);
                    if (parentId != u32_max) entityIds.insert(parentId);
                }
            }
            cursor += secBytes;
        }

        // Create entities, build old-id → new Entity remap
        std::unordered_map<u32, ECS::Entity> remap;
        remap.reserve(entityIds.size());
        for (u32 oldId : entityIds) {
            remap[oldId] = m_world.create();
        }

        // Pass 2: apply POD components
        for (auto& sec : sections) {
            if (sec.typeId == kTypeParent) continue;

            u64        entrySize = 4u + kCompSizes[sec.typeId];
            const u8*  p        = sec.ptr;

            for (u32 i = 0; i < sec.count; ++i) {
                u32 eid;
                memcpy(&eid, p, 4);
                const u8* comp = p + 4;
                p += entrySize;

                auto it = remap.find(eid);
                if (it == remap.end()) continue;
                ECS::Entity e = it->second;

                switch (sec.typeId) {
                    case kTypeTransform:      { auto& c = m_world.add<ECS::Transform>(e);       memcpy(&c, comp, sizeof(ECS::Transform));       break; }
                    case kTypeAcceleration2D: { auto& c = m_world.add<ECS::Acceleration2D>(e); memcpy(&c, comp, sizeof(ECS::Acceleration2D)); break; }
                    case kTypeWorldTransform: { auto& c = m_world.add<WorldTransform>(e);      memcpy(&c, comp, sizeof(WorldTransform));      break; }
                    default: break;
                }
            }
        }

        // Pass 3: apply Parent components (entity references need remapping)
        for (auto& sec : sections) {
            if (sec.typeId != kTypeParent) continue;

            const u8* p = sec.ptr;
            for (u32 i = 0; i < sec.count; ++i) {
                u32 eid, parentId;
                u8  dirty;
                memcpy(&eid,      p,     4);
                memcpy(&parentId, p + 4, 4);
                memcpy(&dirty,    p + 8, 1);
                p += 9;

                auto it = remap.find(eid);
                if (it == remap.end()) continue;

                auto& comp  = m_world.add<Parent>(it->second);
                comp.dirty  = (dirty != 0);
                if (parentId != u32_max) {
                    auto pit = remap.find(parentId);
                    comp.parent = (pit != remap.end()) ? pit->second : ECS::Entity::INVALID;
                } else {
                    comp.parent = ECS::Entity::INVALID;
                }
            }
        }

        return true;
    }
};

} // namespace Caffeine::Scene
