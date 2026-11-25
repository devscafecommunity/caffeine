#include "terrain/vegetation/VegetationStroke.hpp"

#include "core/Hash.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

constexpr f32 kPi = 3.14159265358979f;

// Deterministic unit numbers: a hash of the stroke's seed, the stamp, a key and a purpose.
f32 unit(u64 seed, u32 stamp, u64 key, u32 salt) {
    return static_cast<f32>(Core::toUnit(Core::deriveSeed(Core::hashCombine(seed, key), salt, static_cast<i32>(stamp), static_cast<i32>(salt * 31u + 7u))));
}

u8 byteOf(f32 value) { return static_cast<u8>(std::clamp(value * 255.0f + 0.5f, 0.0f, 255.0f)); }

}  // namespace

usize VegetationRecord::bytes() const {
    return manual.size() * sizeof(VegManualChange) + overrides.size() * sizeof(VegOverrideChange) + speciesBefore.size() + speciesAfter.size();
}

void VegetationRecord::applyBefore(TerrainVegetation& veg) const {
    if (speciesChanged) {
        std::vector<VegetationSpecies> list;
        if (vegetationSpeciesFromText(speciesBefore, list, nullptr)) veg.setAllSpecies(std::move(list));
    }
    if (seedChanged) veg.setSeed(seedBefore);
    if (proceduralChanged) veg.setProceduralEnabled(proceduralBefore);
    for (auto it = manual.rbegin(); it != manual.rend(); ++it) {
        if (it->hasBefore) {
            if (!veg.replaceManual(it->before)) veg.addManual(it->before);
        } else {
            veg.removeManual(it->id);
        }
    }
    for (auto it = overrides.rbegin(); it != overrides.rend(); ++it) {
        veg.setOverride(it->id, it->hasBefore ? it->before : VegetationOverride{});
    }
}

void VegetationRecord::applyAfter(TerrainVegetation& veg) const {
    for (const VegManualChange& c : manual) {
        if (c.hasAfter) {
            if (!veg.replaceManual(c.after)) veg.addManual(c.after);
        } else {
            veg.removeManual(c.id);
        }
    }
    for (const VegOverrideChange& c : overrides) veg.setOverride(c.id, c.hasAfter ? c.after : VegetationOverride{});
    if (seedChanged) veg.setSeed(seedAfter);
    if (proceduralChanged) veg.setProceduralEnabled(proceduralAfter);
    if (speciesChanged) {
        std::vector<VegetationSpecies> list;
        if (vegetationSpeciesFromText(speciesAfter, list, nullptr)) veg.setAllSpecies(std::move(list));
    }
}

VegetationStroke::VegetationStroke(TerrainVegetation& veg, const TerrainHeightmap& ground, const ECS::TerrainComponent& settings,
                                   VegetationBrush brush, const VegetationField& field)
    : m_veg(veg),
      m_ground(ground),
      m_sizeX(settings.worldSizeX),
      m_sizeZ(settings.worldSizeZ),
      m_maxHeight(std::max(settings.maxHeight, 0.001f)),
      m_brush(std::move(brush)),
      m_field(field) {
    StrokeBrush& shape = m_brush.shape;
    shape.radiusM = std::max(shape.radiusM, 0.1f);
    if (shape.spacingM <= 0.0f) shape.spacingM = shape.radiusM * 0.5f;
    shape.spacingM = std::max(shape.spacingM, 0.01f);
}

u32 VegetationStroke::addPoint(const StrokePoint& point) {
    if (m_finished) return 0;
    if (!m_hasLast) {
        m_hasLast = true;
        m_lastX = point.x;
        m_lastZ = point.z;
        m_lastPressure = point.pressure;
        stamp(point.x, point.z, point.pressure);
        return 1;
    }
    const f64 dx = point.x - m_lastX, dz = point.z - m_lastZ;
    const f64 length = std::sqrt(dx * dx + dz * dz);
    u32 laid = 0;
    if (length > 0.0) {
        m_dirX = static_cast<f32>(dx / length);
        m_dirZ = static_cast<f32>(dz / length);
        const f64 spacing = static_cast<f64>(m_brush.shape.spacingM);
        f64 along = spacing - m_carry;
        f64 lastStamp = -m_carry;
        while (along <= length) {
            const f64 t = along / length;
            stamp(m_lastX + dx * t, m_lastZ + dz * t, m_lastPressure + (point.pressure - m_lastPressure) * static_cast<f32>(t));
            ++laid;
            lastStamp = along;
            along += spacing;
        }
        m_carry = length - lastStamp;
    }
    m_lastX = point.x;
    m_lastZ = point.z;
    m_lastPressure = point.pressure;
    return laid;
}

void VegetationStroke::stamp(f64 x, f64 z, f32 pressure) {
    for (const StampParams& params : placeStamp(m_brush.shape, m_logical++, x, z, pressure, m_dirX, m_dirZ)) applyStamp(params, m_stamps++);
}

void VegetationStroke::touchManual(u64 id) {
    if (m_manualBefore.count(id)) return;
    const VegetationInstance* now = m_veg.findManual(id);
    m_manualBefore[id] = now ? std::make_pair(true, *now) : std::make_pair(false, VegetationInstance{});
}

void VegetationStroke::touchOverride(u64 id) {
    if (m_overrideBefore.count(id)) return;
    const VegetationOverride* now = m_veg.findOverride(id);
    m_overrideBefore[id] = now ? std::make_pair(true, *now) : std::make_pair(false, VegetationOverride{});
}

VegetationOverride VegetationStroke::overrideOf(u64 id) const {
    const VegetationOverride* now = m_veg.findOverride(id);
    return now ? *now : VegetationOverride{};
}

void VegetationStroke::applyStamp(const StampParams& params, u32 stampNumber) {
    const u64 seed = m_brush.shape.seed;
    const TerrainFrame frame{&m_ground, static_cast<f32>(m_sizeX), static_cast<f32>(m_sizeZ), m_maxHeight};
    const auto& palette = m_veg.species();
    if (palette.empty()) return;

    // How much of the brush is at an offset from the stamp's centre: falloff, picture, protection masks.
    auto weightAt = [&](f32 x, f32 z) {
        const f64 dx = static_cast<f64>(x) - params.x, dz = static_cast<f64>(z) - params.z;
        const f64 dist = std::sqrt(dx * dx + dz * dz);
        if (dist >= params.radius) return 0.0f;
        f32 w = shapeWeight(m_brush.shape, params, dx, dz, dist);
        if (w > 0.0f && !m_brush.shape.protect.empty()) {
            w *= protectionWeight(m_brush.shape, m_masks, frame, static_cast<f32>(x + m_sizeX * 0.5), static_cast<f32>(z + m_sizeZ * 0.5));
        }
        return w;
    };

    f32 widest = 0.0f;
    for (const VegetationSpecies& s : palette) widest = std::max(widest, s.radius * s.maxScale);
    std::vector<VegetationResolved> list;
    m_veg.resolveArea(static_cast<f32>(params.x), static_cast<f32>(params.z), static_cast<f32>(params.radius) + widest * 2.0f + 1.0f, m_field, list);

    if (m_brush.tool == VegetationTool::Paint) {
        if (m_brush.species >= palette.size()) return;
        const VegetationSpecies& species = palette[m_brush.species];
        if (species.meshes.empty() || !species.visible) return;
        const f64 area = static_cast<f64>(kPi) * params.radius * params.radius;
        const f32 expected = static_cast<f32>(static_cast<f64>(m_brush.per100m2) * area / 100.0);
        u32 count = static_cast<u32>(std::floor(expected));
        if (unit(seed, stampNumber, 0xC0FFEE, 1) < expected - std::floor(expected)) ++count;
        count = std::min(count, 4096u);
        for (u32 k = 0; k < count; ++k) {
            const f32 angle = unit(seed, stampNumber, k, 2) * 2.0f * kPi;
            const f32 reach = std::sqrt(unit(seed, stampNumber, k, 3)) * static_cast<f32>(params.radius);
            const f32 x = static_cast<f32>(params.x) + std::cos(angle) * reach, z = static_cast<f32>(params.z) + std::sin(angle) * reach;
            const f32 w = weightAt(x, z);
            if (w <= 0.0f || unit(seed, stampNumber, k, 4) >= w * std::clamp(m_brush.shape.strength * 4.0f, 0.0f, 1.0f)) continue;
            if (m_brush.respectRules && m_field.density && m_field.density(m_brush.species, x, z) <= 0.0f) continue;
            const f32 scale = species.minScale + (species.maxScale - species.minScale) * unit(seed, stampNumber, k, 5);
            // Nothing overlaps: a neighbour of higher priority, or a manual one of the same, keeps its place.
            bool blocked = false;
            for (const VegetationResolved& other : list) {
                const VegetationSpecies& so = palette[other.species];
                if (so.priority < species.priority || (so.priority == species.priority && !other.manual)) continue;
                const f32 reachSum = so.radius * other.scale + species.radius * scale;
                const f32 dx = other.x - x, dz = other.z - z;
                if (dx * dx + dz * dz < reachSum * reachSum) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;
            VegetationInstance inst;
            inst.id = m_veg.newManualId(x, z);
            inst.x = x;
            inst.z = z;
            inst.species = species.uid;
            inst.variant = static_cast<u8>(std::min<u32>(static_cast<u32>(unit(seed, stampNumber, k, 6) * static_cast<f32>(species.variantCount())), species.variantCount() - 1));
            inst.flags = m_brush.respectRules ? 0 : kVegForced;
            inst.scale = scale;
            inst.yaw = unit(seed, stampNumber, k, 7) * 2.0f * kPi;
            inst.seed = static_cast<u32>(Core::deriveSeed(seed, 8, static_cast<i32>(stampNumber), static_cast<i32>(k)));
            for (int c = 0; c < 3; ++c) inst.tint[c] = byteOf(1.0f + (unit(seed, stampNumber, k, 9 + c) * 2.0f - 1.0f) * species.tintJitter);
            touchManual(inst.id);
            m_veg.addManual(inst);
            VegetationResolved placed;
            placed.id = inst.id;
            placed.species = m_brush.species;
            placed.manual = true;
            placed.x = x;
            placed.z = z;
            placed.scale = scale;
            list.push_back(placed);
        }
        return;
    }

    for (const VegetationResolved& r : list) {
        if (m_brush.onlySpecies >= 0 && r.species != static_cast<u32>(m_brush.onlySpecies)) continue;
        const f32 w = weightAt(r.x, r.z);
        if (w <= 0.0f) continue;
        const VegetationSpecies& species = palette[r.species];
        const f32 share = std::clamp(w * m_brush.shape.strength, 0.0f, 1.0f);
        const VegetationInstance* held = r.manual ? m_veg.findManual(r.id) : nullptr;
        if (r.manual && !held) continue;
        // The raw (per-instance) tint, without the species' own.
        auto rawTint = [&](u8* out) {
            for (int c = 0; c < 3; ++c) out[c] = byteOf(species.tint[c] > 1e-3f ? r.tint[c] / species.tint[c] : r.tint[c]);
        };

        switch (m_brush.tool) {
            case VegetationTool::Erase:
            case VegetationTool::Thin: {
                // Erase rolls the dice per stamp (a few passes clear it); thin ranks each instance once (it only ever removes
                // the lowest ranks first, so the result is the same however the stroke is sampled).
                const f32 roll = m_brush.tool == VegetationTool::Thin ? static_cast<f32>(Core::toUnit(Core::hashCombine(seed, r.id))) : unit(seed, stampNumber, r.id, 20);
                if (roll >= share) break;
                if (r.manual) {
                    touchManual(r.id);
                    m_veg.removeManual(r.id);
                } else {
                    touchOverride(r.id);
                    VegetationOverride o = overrideOf(r.id);
                    o.removed = true;
                    m_veg.setOverride(r.id, o);
                }
                break;
            }
            case VegetationTool::Scale: {
                const f32 factor = std::max(1.0f + m_brush.amount * w, 0.05f);
                if (r.manual) {
                    touchManual(r.id);
                    VegetationInstance i = *held;
                    i.scale = std::clamp(i.scale * factor, 0.05f, 50.0f);
                    m_veg.replaceManual(i);
                } else {
                    touchOverride(r.id);
                    VegetationOverride o = overrideOf(r.id);
                    o.scaleMul = std::clamp(o.scaleMul * factor, 0.05f, 50.0f);
                    m_veg.setOverride(r.id, o);
                }
                break;
            }
            case VegetationTool::Rotate: {
                const f32 turn = m_brush.amount * kPi / 180.0f * w;
                if (r.manual) {
                    touchManual(r.id);
                    VegetationInstance i = *held;
                    i.yaw += turn;
                    m_veg.replaceManual(i);
                } else {
                    touchOverride(r.id);
                    VegetationOverride o = overrideOf(r.id);
                    o.yawAdd += turn;
                    m_veg.setOverride(r.id, o);
                }
                break;
            }
            case VegetationTool::Tint: {
                u8 now[3];
                rawTint(now);
                u8 next[3];
                for (int c = 0; c < 3; ++c) next[c] = byteOf(now[c] / 255.0f + (m_brush.tint[c] - now[c] / 255.0f) * share);
                if (r.manual) {
                    touchManual(r.id);
                    VegetationInstance i = *held;
                    std::copy(next, next + 3, i.tint);
                    m_veg.replaceManual(i);
                } else {
                    touchOverride(r.id);
                    VegetationOverride o = overrideOf(r.id);
                    o.hasTint = true;
                    std::copy(next, next + 3, o.tint);
                    m_veg.setOverride(r.id, o);
                }
                break;
            }
            case VegetationTool::Replace: {
                if (m_brush.species >= palette.size() || r.species == m_brush.species) break;
                if (unit(seed, stampNumber, r.id, 21) >= share) break;
                const VegetationSpecies& target = palette[m_brush.species];
                if (target.meshes.empty()) break;
                if (r.manual) {
                    touchManual(r.id);
                    VegetationInstance i = *held;
                    i.species = target.uid;
                    i.variant = static_cast<u8>(i.variant % target.variantCount());
                    m_veg.replaceManual(i);
                } else {
                    // A procedural instance cannot change species: it is removed and a manual one of the new species takes its place.
                    touchOverride(r.id);
                    VegetationOverride o = overrideOf(r.id);
                    o.removed = true;
                    m_veg.setOverride(r.id, o);
                    VegetationInstance i;
                    i.id = m_veg.newManualId(r.x, r.z);
                    i.x = r.x;
                    i.z = r.z;
                    i.species = target.uid;
                    i.variant = static_cast<u8>(r.variant % target.variantCount());
                    i.flags = 0;
                    i.scale = r.scale;
                    i.yaw = r.yaw;
                    i.seed = static_cast<u32>(r.id);
                    rawTint(i.tint);
                    touchManual(i.id);
                    m_veg.addManual(i);
                }
                break;
            }
            case VegetationTool::Paint: break;
        }
    }
}

VegetationRecord VegetationStroke::finish() {
    VegetationRecord record;
    m_finished = true;
    record.stamps = m_stamps;
    std::vector<u64> ids;
    for (const auto& entry : m_manualBefore) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    auto include = [&](f32 x, f32 z) {
        const i32 cx = TerrainVegetation::chunkOf(x), cz = TerrainVegetation::chunkOf(z);
        if (!record.hasChunks) {
            record.hasChunks = true;
            record.minChunkX = record.maxChunkX = cx;
            record.minChunkZ = record.maxChunkZ = cz;
        } else {
            record.minChunkX = std::min(record.minChunkX, cx);
            record.maxChunkX = std::max(record.maxChunkX, cx);
            record.minChunkZ = std::min(record.minChunkZ, cz);
            record.maxChunkZ = std::max(record.maxChunkZ, cz);
        }
    };
    for (const u64 id : ids) {
        const auto& before = m_manualBefore[id];
        const VegetationInstance* now = m_veg.findManual(id);
        if (before.first && now && before.second == *now) continue;
        if (!before.first && !now) continue;
        VegManualChange change;
        change.id = id;
        change.hasBefore = before.first;
        change.before = before.second;
        change.hasAfter = now != nullptr;
        if (now) change.after = *now;
        if (change.hasBefore) include(change.before.x, change.before.z);
        if (change.hasAfter) include(change.after.x, change.after.z);
        record.manual.push_back(change);
    }
    ids.clear();
    for (const auto& entry : m_overrideBefore) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    for (const u64 id : ids) {
        const auto& before = m_overrideBefore[id];
        const VegetationOverride* now = m_veg.findOverride(id);
        const bool same = before.first == (now != nullptr) && (!now || before.second == *now);
        if (same) continue;
        VegOverrideChange change;
        change.id = id;
        change.hasBefore = before.first;
        change.before = before.second;
        change.hasAfter = now != nullptr;
        if (now) change.after = *now;
        record.overrides.push_back(change);
    }
    // An override carries no position: the chunks it touches are those the stamps covered.
    if (!record.overrides.empty() && !record.hasChunks) {
        record.hasChunks = true;
        record.minChunkX = record.minChunkZ = -1000000;
        record.maxChunkX = record.maxChunkZ = 1000000;
    }
    return record;
}

// ------------------------------------------------------------------------------------------ whole-palette operations

VegetationRecord editSpeciesFromText(TerrainVegetation& veg, const std::string& text, std::string* error) {
    VegetationRecord record;
    std::vector<VegetationSpecies> list;
    if (!vegetationSpeciesFromText(text, list, error)) return record;
    if (list == veg.species()) return record;
    record.speciesChanged = true;
    record.speciesBefore = vegetationSpeciesToText(veg.species());
    // The manual instances of species that are gone leave with them: record them first.
    for (const VegetationInstance& inst : veg.allManual()) {
        bool kept = false;
        for (const VegetationSpecies& s : list) kept |= s.uid == inst.species;
        if (kept) continue;
        VegManualChange change;
        change.id = inst.id;
        change.hasBefore = true;
        change.before = inst;
        record.manual.push_back(change);
    }
    veg.setAllSpecies(std::move(list));
    record.speciesAfter = vegetationSpeciesToText(veg.species());
    return record;
}

VegetationRecord bakeProcedural(TerrainVegetation& veg, u32 speciesIndex, const VegetationField& field, f32 extentX, f32 extentZ) {
    VegetationRecord record;
    if (speciesIndex >= veg.species().size()) return record;
    const VegetationSpecies species = veg.species()[speciesIndex];
    std::vector<VegetationResolved> all;
    const i32 x0 = TerrainVegetation::chunkOf(-extentX * 0.5f), x1 = TerrainVegetation::chunkOf(extentX * 0.5f);
    const i32 z0 = TerrainVegetation::chunkOf(-extentZ * 0.5f), z1 = TerrainVegetation::chunkOf(extentZ * 0.5f);
    for (i32 cz = z0; cz <= z1; ++cz) {
        for (i32 cx = x0; cx <= x1; ++cx) veg.resolveChunk(cx, cz, field, all);
    }
    record.speciesChanged = true;
    record.speciesBefore = vegetationSpeciesToText(veg.species());
    for (const VegetationResolved& r : all) {
        if (r.manual || r.species != speciesIndex) continue;
        if (std::fabs(r.x) > extentX * 0.5f || std::fabs(r.z) > extentZ * 0.5f) continue;
        VegetationInstance inst;
        inst.id = veg.newManualId(r.x, r.z);
        inst.x = r.x;
        inst.z = r.z;
        inst.species = species.uid;
        inst.variant = r.variant;
        inst.flags = kVegBaked;
        inst.scale = r.scale;
        inst.yaw = r.yaw;
        inst.seed = static_cast<u32>(r.id);
        for (int c = 0; c < 3; ++c) inst.tint[c] = byteOf(species.tint[c] > 1e-3f ? r.tint[c] / species.tint[c] : r.tint[c]);
        veg.addManual(inst);
        VegManualChange change;
        change.id = inst.id;
        change.hasAfter = true;
        change.after = inst;
        record.manual.push_back(change);
    }
    // The procedural source of the species goes off: what is on the ground now is the manual copy.
    VegetationSpecies off = species;
    off.procedural = false;
    veg.setSpecies(speciesIndex, off);
    record.speciesAfter = vegetationSpeciesToText(veg.species());
    record.hasChunks = false;
    return record;
}

VegetationRecord rederiveProcedural(TerrainVegetation& veg, u32 speciesIndex) {
    VegetationRecord record;
    if (speciesIndex >= veg.species().size()) return record;
    const VegetationSpecies species = veg.species()[speciesIndex];
    record.speciesChanged = true;
    record.speciesBefore = vegetationSpeciesToText(veg.species());
    for (const VegetationInstance& inst : veg.allManual()) {
        if (inst.species != species.uid || !(inst.flags & kVegBaked)) continue;
        VegManualChange change;
        change.id = inst.id;
        change.hasBefore = true;
        change.before = inst;
        veg.removeManual(inst.id);
        record.manual.push_back(change);
    }
    VegetationSpecies on = species;
    on.procedural = true;
    veg.setSpecies(speciesIndex, on);
    record.speciesAfter = vegetationSpeciesToText(veg.species());
    return record;
}

VegetationRecord reseed(TerrainVegetation& veg, u64 seed) {
    VegetationRecord record;
    if (seed == veg.seed()) return record;
    record.seedChanged = true;
    record.seedBefore = veg.seed();
    record.seedAfter = seed;
    veg.setSeed(seed);
    return record;
}

}  // namespace Caffeine::Terrain
