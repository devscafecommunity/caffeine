#include "terrain/paint/TerrainMaskPaint.hpp"

#include <algorithm>
#include <cmath>

namespace Caffeine::Terrain {
namespace {

void include(LayerBounds& bounds, u32 minX, u32 minZ, u32 maxX, u32 maxZ) {
    if (!bounds.valid) {
        bounds = {minX, minZ, maxX, maxZ, true};
        return;
    }
    bounds.minX = std::min(bounds.minX, minX);
    bounds.minZ = std::min(bounds.minZ, minZ);
    bounds.maxX = std::max(bounds.maxX, maxX);
    bounds.maxZ = std::max(bounds.maxZ, maxZ);
}

u8 toByte(f32 value) { return static_cast<u8>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); }

MaskSnapshot snapshotOf(const TerrainMasks& masks, const std::string& name) {
    MaskSnapshot snapshot;
    for (const TerrainMasks::Stored& stored : masks.stored()) {
        if (stored.name != name) continue;
        snapshot.exists = true;
        snapshot.resolutionX = stored.resolutionX;
        snapshot.resolutionZ = stored.resolutionZ;
        snapshot.data = stored.data;
    }
    return snapshot;
}

bool restore(TerrainMasks& masks, const std::string& name, const MaskSnapshot& snapshot) {
    if (!snapshot.exists) {
        masks.remove(name);
        return true;
    }
    return masks.set(name, snapshot.resolutionX, snapshot.resolutionZ, snapshot.data.data());
}

bool fail(std::string* error, const char* reason) {
    if (error) *error = reason;
    return false;
}

}  // namespace

usize MaskRecord::bytes() const {
    usize total = wholeBefore.data.size() + wholeAfter.data.size();
    for (const MaskBlock& block : blocks) total += block.before.size() + block.after.size();
    return total;
}

bool MaskRecord::applyBefore(TerrainMasks& masks) const {
    if (whole) return restore(masks, mask, wholeBefore);
    TerrainMasks::Stored* stored = masks.findStored(mask);
    if (!stored) return false;
    for (const MaskBlock& block : blocks) {
        if (block.maxX >= stored->resolutionX || block.maxZ >= stored->resolutionZ) return false;
        const u32 width = block.maxX - block.minX + 1;
        for (u32 z = block.minZ; z <= block.maxZ; ++z) {
            for (u32 x = block.minX; x <= block.maxX; ++x) {
                stored->data[static_cast<usize>(z) * stored->resolutionX + x] = block.before[static_cast<usize>(z - block.minZ) * width + (x - block.minX)];
            }
        }
    }
    return true;
}

bool MaskRecord::applyAfter(TerrainMasks& masks) const {
    if (whole) return restore(masks, mask, wholeAfter);
    TerrainMasks::Stored* stored = masks.findStored(mask);
    if (!stored) return false;
    for (const MaskBlock& block : blocks) {
        if (block.maxX >= stored->resolutionX || block.maxZ >= stored->resolutionZ) return false;
        const u32 width = block.maxX - block.minX + 1;
        for (u32 z = block.minZ; z <= block.maxZ; ++z) {
            for (u32 x = block.minX; x <= block.maxX; ++x) {
                stored->data[static_cast<usize>(z) * stored->resolutionX + x] = block.after[static_cast<usize>(z - block.minZ) * width + (x - block.minX)];
            }
        }
    }
    return true;
}

bool createMask(TerrainMasks& masks, const TerrainFrame& frame, const std::string& name, u32 resolutionX, u32 resolutionZ,
                f32 value, const std::string& from, MaskRecord& record, std::string* error) {
    record = MaskRecord{};
    if (!TerrainMasks::validName(name)) return fail(error, "a mask name is 1 to 31 letters, digits, '_' or '-'");
    if (TerrainMasks::isBuiltin(name)) return fail(error, "that name belongs to a built-in mask");
    if (masks.has(name)) return fail(error, "a mask with that name already exists");
    if (masks.stored().size() >= TerrainMasks::kMaxStoredMasks) return fail(error, "a terrain holds at most 16 stored masks");
    if (resolutionX < 2 || resolutionZ < 2 || static_cast<u64>(resolutionX) * resolutionZ > (1u << 26)) {
        return fail(error, "the mask grid must be at least 2 x 2 and at most 64 M texels");
    }
    std::vector<u8> data(static_cast<usize>(resolutionX) * resolutionZ, toByte(value));
    if (!from.empty()) {
        if (!masks.has(from)) return fail(error, "the mask to copy does not exist");
        if (!masks.rect(frame, from, 0.0f, 0.0f, frame.sizeX, frame.sizeZ, resolutionX, resolutionZ, data.data())) {
            return fail(error, "the mask to copy cannot be read");
        }
    }
    if (!masks.set(name, resolutionX, resolutionZ, data.data())) return fail(error, "the mask cannot be stored");
    record.mask = name;
    record.whole = true;
    record.wholeAfter = snapshotOf(masks, name);
    return true;
}

bool editMask(TerrainMasks& masks, const std::string& name, MaskOp op, f32 value, MaskRecord& record, std::string* error) {
    record = MaskRecord{};
    TerrainMasks::Stored* stored = masks.findStored(name);
    if (!stored) return fail(error, "not a stored mask");
    record.mask = name;
    record.whole = true;
    record.wholeBefore = snapshotOf(masks, name);
    switch (op) {
        case MaskOp::Fill: std::fill(stored->data.begin(), stored->data.end(), toByte(value)); break;
        case MaskOp::Invert:
            for (u8& texel : stored->data) texel = static_cast<u8>(255 - texel);
            break;
        case MaskOp::Delete: masks.remove(name); break;
    }
    record.wholeAfter = snapshotOf(masks, name);
    if (record.wholeAfter.exists == record.wholeBefore.exists && record.wholeAfter.data == record.wholeBefore.data) {
        record = MaskRecord{};  // nothing changed: no undo step
    }
    return true;
}

TerrainMaskStroke::TerrainMaskStroke(TerrainMasks& masks, const std::string& name, const TerrainHeightmap& heightmap,
                                     const ECS::TerrainComponent& settings, MaskPaintBrush brush)
    : m_masks(masks),
      m_name(name),
      m_data(masks.findStored(name)),
      m_height(heightmap),
      m_sizeX(settings.worldSizeX),
      m_sizeZ(settings.worldSizeZ),
      m_maxHeight(std::max(settings.maxHeight, 0.001f)),
      m_brush(std::move(brush)) {
    if (m_data) {
        m_stepX = m_sizeX / static_cast<f64>(std::max(m_data->resolutionX, 2u) - 1);
        m_stepZ = m_sizeZ / static_cast<f64>(std::max(m_data->resolutionZ, 2u) - 1);
    } else {
        m_stepX = m_stepZ = 1.0;
    }
    StrokeBrush& shape = m_brush.shape;
    shape.radiusM = std::max(shape.radiusM, 0.01f);
    if (shape.spacingM <= 0.0f) shape.spacingM = shape.radiusM * 0.25f;
    shape.spacingM = std::max(shape.spacingM, 0.001f);
    m_brush.value = std::clamp(m_brush.value, 0.0f, 1.0f);
}

u32 TerrainMaskStroke::addPoint(const StrokePoint& point) {
    if (m_finished || !m_data) return 0;
    if (!m_hasLast) {
        m_hasLast = true;
        m_lastX = point.x;
        m_lastZ = point.z;
        m_lastPressure = point.pressure;
        m_carry = 0.0;
        stamp(point.x, point.z, point.pressure);
        return 1;
    }
    const f64 dx = point.x - m_lastX;
    const f64 dz = point.z - m_lastZ;
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

void TerrainMaskStroke::stamp(f64 x, f64 z, f32 pressure) {
    for (const StampParams& params : placeStamp(m_brush.shape, m_logical++, x, z, pressure, m_dirX, m_dirZ)) applyStamp(params);
}

TerrainMaskStroke::Work& TerrainMaskStroke::touch(u32 blockX, u32 blockZ) {
    const u64 key = (static_cast<u64>(blockZ) << 32) | blockX;
    auto found = m_touched.find(key);
    if (found != m_touched.end()) return found->second;
    Work work;
    MaskBlock& block = work.block;
    block.minX = blockX * kBlock;
    block.minZ = blockZ * kBlock;
    block.maxX = std::min(block.minX + kBlock - 1, m_data->resolutionX - 1);
    block.maxZ = std::min(block.minZ + kBlock - 1, m_data->resolutionZ - 1);
    for (u32 z = block.minZ; z <= block.maxZ; ++z) {
        for (u32 x = block.minX; x <= block.maxX; ++x) {
            const u8 byte = m_data->data[static_cast<usize>(z) * m_data->resolutionX + x];
            block.before.push_back(byte);
            work.value.push_back(static_cast<f32>(byte));
        }
    }
    return m_touched.emplace(key, std::move(work)).first->second;
}

f32 TerrainMaskStroke::read(u32 x, u32 z) const {
    const u64 key = (static_cast<u64>(z / kBlock) << 32) | (x / kBlock);
    const auto found = m_touched.find(key);
    if (found == m_touched.end()) return static_cast<f32>(m_data->data[static_cast<usize>(z) * m_data->resolutionX + x]);
    const MaskBlock& block = found->second.block;
    return found->second.value[static_cast<usize>(z - block.minZ) * (block.maxX - block.minX + 1) + (x - block.minX)];
}

void TerrainMaskStroke::applyStamp(const StampParams& params) {
    const u32 resX = m_data->resolutionX, resZ = m_data->resolutionZ;
    const f64 gridX = (params.x + m_sizeX * 0.5) / m_stepX;
    const f64 gridZ = (params.z + m_sizeZ * 0.5) / m_stepZ;
    const i64 minX = std::max<i64>(0, static_cast<i64>(std::floor(gridX - params.radius / m_stepX)));
    const i64 maxX = std::min<i64>(static_cast<i64>(resX) - 1, static_cast<i64>(std::ceil(gridX + params.radius / m_stepX)));
    const i64 minZ = std::max<i64>(0, static_cast<i64>(std::floor(gridZ - params.radius / m_stepZ)));
    const i64 maxZ = std::min<i64>(static_cast<i64>(resZ) - 1, static_cast<i64>(std::ceil(gridZ + params.radius / m_stepZ)));
    ++m_stamps;
    if (minX > maxX || minZ > maxZ) return;

    const StrokeBrush& shape = m_brush.shape;
    const TerrainFrame frame{&m_height, static_cast<f32>(m_sizeX), static_cast<f32>(m_sizeZ), m_maxHeight};
    struct Cell {
        u32 x, z;
        f32 amount;
        f32 next = 0.0f;
    };
    std::vector<Cell> cells;
    // Every texel's share is decided before any is written: the result does not depend on the order of the texels.
    for (i64 z = minZ; z <= maxZ; ++z) {
        for (i64 x = minX; x <= maxX; ++x) {
            const f64 dx = (static_cast<f64>(x) - gridX) * m_stepX;
            const f64 dz = (static_cast<f64>(z) - gridZ) * m_stepZ;
            const f64 dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= params.radius) continue;
            f32 weight = shapeWeight(shape, params, dx, dz, dist);
            if (weight <= 0.0f) continue;
            if (!shape.protect.empty()) {
                weight *= protectionWeight(shape, &m_masks, frame, static_cast<f32>(static_cast<f64>(x) * m_stepX),
                                           static_cast<f32>(static_cast<f64>(z) * m_stepZ));
            }
            const f32 amount = std::clamp(weight * params.strength, 0.0f, 1.0f);
            if (amount > 0.0f) cells.push_back({static_cast<u32>(x), static_cast<u32>(z), amount});
        }
    }
    if (cells.empty()) return;
    for (const Cell& cell : cells) touch(cell.x / kBlock, cell.z / kBlock);

    const f32 target = m_brush.mode == MaskPaintMode::Erase ? 0.0f : m_brush.value * 255.0f;
    for (Cell& cell : cells) {
        const f32 current = read(cell.x, cell.z);
        f32 goal = target;
        if (m_brush.mode == MaskPaintMode::Smooth) {
            f32 sum = 0.0f;
            u32 count = 0;
            for (i64 dz = -1; dz <= 1; ++dz) {
                for (i64 dx = -1; dx <= 1; ++dx) {
                    const i64 nx = static_cast<i64>(cell.x) + dx, nz = static_cast<i64>(cell.z) + dz;
                    if (nx < 0 || nz < 0 || nx >= static_cast<i64>(resX) || nz >= static_cast<i64>(resZ)) continue;
                    sum += read(static_cast<u32>(nx), static_cast<u32>(nz));
                    ++count;
                }
            }
            goal = sum / static_cast<f32>(count);
        }
        cell.next = current + (goal - current) * cell.amount;
    }
    for (const Cell& cell : cells) {
        Work& work = touch(cell.x / kBlock, cell.z / kBlock);
        work.value[static_cast<usize>(cell.z - work.block.minZ) * (work.block.maxX - work.block.minX + 1) + (cell.x - work.block.minX)] = cell.next;
        m_data->data[static_cast<usize>(cell.z) * resX + cell.x] = toByte(cell.next / 255.0f);
    }
    include(m_dirty, static_cast<u32>(minX), static_cast<u32>(minZ), static_cast<u32>(maxX), static_cast<u32>(maxZ));
}

MaskRecord TerrainMaskStroke::finish() {
    MaskRecord record;
    m_finished = true;
    record.mask = m_name;
    record.stamps = m_stamps;
    if (!m_data) return record;
    std::vector<u64> keys;
    keys.reserve(m_touched.size());
    for (const auto& entry : m_touched) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());  // the same stroke always writes its record in the same order
    for (u64 key : keys) {
        MaskBlock block = std::move(m_touched[key].block);
        for (u32 z = block.minZ; z <= block.maxZ; ++z) {
            for (u32 x = block.minX; x <= block.maxX; ++x) block.after.push_back(m_data->data[static_cast<usize>(z) * m_data->resolutionX + x]);
        }
        u32 loX = ~0u, loZ = ~0u, hiX = 0, hiZ = 0;
        const u32 width = block.maxX - block.minX + 1;
        for (usize i = 0; i < block.after.size(); ++i) {
            if (block.after[i] == block.before[i]) continue;
            const u32 x = block.minX + static_cast<u32>(i % width), z = block.minZ + static_cast<u32>(i / width);
            loX = std::min(loX, x);
            loZ = std::min(loZ, z);
            hiX = std::max(hiX, x);
            hiZ = std::max(hiZ, z);
        }
        if (loX == ~0u) continue;  // a block the stroke left as it was is not kept
        include(record.region, loX, loZ, hiX, hiZ);
        record.blocks.push_back(std::move(block));
    }
    return record;
}

}  // namespace Caffeine::Terrain
