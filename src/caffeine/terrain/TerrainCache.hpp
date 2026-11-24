#pragma once

#include "assets/MeshTypes.hpp"
#include "ecs/Entity.hpp"
#include "ecs/TerrainComponents.hpp"
#include "ecs/World.hpp"
#include "terrain/TerrainHeightmap.hpp"
#include "terrain/TerrainLodSystem.hpp"
#include "terrain/TerrainContainer.hpp"
#include "terrain/TerrainMasks.hpp"
#include "terrain/TerrainSplatPainter.hpp"
#include "terrain/edit/TerrainTiles.hpp"
#include "terrain/TerrainSplatmap.hpp"
#include "terrain/paint/TerrainLayerMap.hpp"
#include "terrain/paint/TerrainPalette.hpp"
#include "terrain/paint/TerrainPaintStroke.hpp"

#ifdef CF_HAS_SDL3
#include "rhi/RenderDevice.hpp"
#endif

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Caffeine::Terrain {

struct TerrainGpuTextures;

/// One tile of a terrain as the global illumination sees it: a coarse mesh of the tile, and a revision that moves only
/// when an edit touched this tile. Photon revoxelises just the tiles whose revision moved (regional GI).
struct TerrainGiTile {
    u32 index = 0;      ///< the tile's index in the terrain's `TileGrid`
    u64 revision = 0;   ///< changes when the tile's heights do
    Assets::Mesh3D mesh;
    Vec3 lo{};          ///< terrain-local bounds
    Vec3 hi{};
};

class TerrainCache {
public:
    static TerrainCache& instance();

    void initializeEntity(ECS::World& world, ECS::Entity entity);
    void syncEntity(ECS::World& world, ECS::Entity entity);
    void removeEntity(ECS::Entity entity);
    void clear();

    TerrainHeightmap* heightmapFor(ECS::Entity entity);
    const TerrainHeightmap* heightmapFor(ECS::Entity entity) const;
    // --- layered painting (phase 2)
    TerrainPalette* paletteFor(ECS::Entity entity);
    const TerrainPalette* paletteFor(ECS::Entity entity) const;
    TerrainLayerMap* layerMapFor(ECS::Entity entity);
    const TerrainLayerMap* layerMapFor(ECS::Entity entity) const;
    /// Turns layered painting on: the palette is the four layers of the terrain, the layer map is its splat map (kept
    /// if one is already there). Returns false for an unknown terrain.
    bool enableLayers(ECS::Entity entity, ECS::TerrainComponent& terrain);
    /// Back to the four-layer splat map, rebuilt from layers 0 to 3 of the layer map (what it can show).
    void disableLayers(ECS::Entity entity, ECS::TerrainComponent& terrain);
    /// A painted region reaches the GPU: moves the layer revision.
    /// `region` is the texels that changed (invalid: the whole map); only those are uploaded.
    /// The palette and layer map of a terrain, as a unit that can be moved between cache entries.
    struct LayerState {
        TerrainPalette palette;
        TerrainLayerMap layers;
    };
    /// Moves the layers out of an entry (it keeps none). Used by scene restores, which rebuild every entry.
    LayerState takeLayers(ECS::Entity entity);
    void adoptLayers(ECS::Entity entity, LayerState&& state);
    void commitLayerEdit(ECS::Entity entity, ECS::TerrainComponent& terrain, const LayerBounds& region = {});
    /// Writes the palette and the layer map next to the terrain data (default names from `terrainDataPath` when the
    /// component has none) and records the paths. False if there are no layers or a file cannot be written.
    bool saveLayers(ECS::Entity entity, ECS::TerrainComponent& terrain, const std::filesystem::path& projectRoot,
                    std::string* error = nullptr);
    /// Reads them (a missing palette file is the legacy four layers, a missing map is the splat map). False on a damaged file.
    bool loadLayers(ECS::Entity entity, ECS::TerrainComponent& terrain, const std::filesystem::path& projectRoot,
                    std::string* error = nullptr);

    TerrainSplatmap* splatmapFor(ECS::Entity entity);
    const TerrainSplatmap* splatmapFor(ECS::Entity entity) const;
    /// The terrain's stored masks (the built-ins need no storage). Null for an entity without a terrain.
    TerrainMasks* masksFor(ECS::Entity entity);
    const TerrainMasks* masksFor(ECS::Entity entity) const;
    Assets::Mesh3D* meshFor(ECS::Entity entity);

    void rebuildRegion(ECS::Entity entity, const ECS::TerrainComponent& settings,
                       u32 minVertexX, u32 minVertexZ,
                       u32 maxVertexX, u32 maxVertexZ);
    /// After an edit changed `region` of the height grid: rebuilds only the meshes the region touches, moves the
    /// revisions together (so `syncEntity` does not rebuild the whole terrain) and, when `final`, refreshes the coarse
    /// collision mesh too, which is too costly to redo on every stamp.
    void commitHeightEdit(ECS::Entity entity, ECS::TerrainComponent& settings, const TerrainVertexBounds& region,
                          bool final);

    void gatherDrawMeshes(ECS::Entity entity,
                          const ECS::TerrainComponent& settings,
                          const Mat4& worldMatrix,
                          const Vec3& cameraPos,
                          const Spatial::Frustum& frustum,
                          std::vector<TerrainDrawChunk>& outDraws,
                          TerrainCullStats* stats = nullptr,
                          f32 lodDistanceScale = 1.0f);

    void syncTextureToFilter(ECS::World& world, ECS::Entity entity,
                             const ECS::TerrainComponent& terrain);
    void repairTexturePaths(ECS::TerrainComponent& terrain);

    /// The terrain cut into tiles for the global illumination, brought up to date: only tiles an edit touched since the
    /// last call are meshed again (at a step that keeps the whole terrain within a triangle budget, and cells of at
    /// least about a metre, which is all the voxel volume resolves). The references stay valid until the next edit.
    const std::vector<TerrainGiTile>& giTilesFor(ECS::Entity entity, const ECS::TerrainComponent& settings);
    /// The step (in height-grid cells) `giTilesFor` meshes at for a terrain of this size and sampling.
    static u32 giStepFor(u32 resolutionX, u32 resolutionZ, f64 sizeX, f64 sizeZ);

    Assets::Mesh3D* collisionMeshFor(ECS::Entity entity);
    const Assets::Mesh3D* collisionMeshFor(ECS::Entity entity) const;

#ifdef CF_HAS_SDL3
    void syncGpuTextures(RHI::RenderDevice* device, ECS::Entity entity,
                           const ECS::TerrainComponent& terrain,
                           const std::string& projectRoot);
    const TerrainGpuTextures* gpuTexturesFor(ECS::Entity entity) const;
#endif

    bool saveTerrainFile(ECS::Entity entity, const std::filesystem::path& path,
                         bool includeSplat) const;
    /// Saves the native `.terrain` container (see `TerrainContainer`), recording the component's extent and height
    /// scale. This is what an old project offers after opening a legacy `.cterrain`: `nativeTerrainPath(legacy)`.
    bool saveNativeTerrainFile(ECS::Entity entity, const ECS::TerrainComponent& terrain,
                               const std::filesystem::path& path, bool includeSplat) const;
    /// True when the terrain was opened from a legacy `.cterrain` and has not been saved as a `.terrain` since.
    bool dataFileIsLegacy(ECS::Entity entity) const;
    /// Writes the terrain as a native `.terrain` next to its legacy file, checks it by reading it back (bit for bit),
    /// and points `terrain.terrainDataPath` at it (relative to `projectRoot`). The legacy file is left where it is.
    bool convertToNative(ECS::Entity entity, ECS::TerrainComponent& terrain,
                         const std::filesystem::path& projectRoot);
    bool loadTerrainFile(ECS::World& world, ECS::Entity entity,
                         ECS::TerrainComponent& terrain,
                         const std::filesystem::path& path);

    /// Restore height/splat data from an in-memory scene snapshot (undo/redo).
    void restoreSnapshot(ECS::World& world, ECS::Entity entity,
                         ECS::TerrainComponent& terrain,
                         const TerrainHeightmap& heightmap,
                         const TerrainSplatmap& splatmap);

#ifdef CF_HAS_SDL3
    void releaseGpuResources(RHI::RenderDevice* device);
#endif

private:
    struct TerrainEntry {
        TerrainHeightmap heightmap;
        TerrainSplatmap splatmap;
        TerrainPalette palette;
        TerrainLayerMap layers;
        LayerBounds layerDirty;   // texels the GPU has not seen yet
        bool layerDirtyAll = true;
        TerrainMasks masks;
        std::unique_ptr<Assets::Mesh3D> mesh;
        std::unique_ptr<Assets::Mesh3D> collisionMesh;
        std::vector<TerrainChunk> chunks;
        std::vector<u32> chunkActiveLods;
        bool useChunks = false;
        u32 builtRevision = 0;
        // Regional GI: a revision per tile (moved by `rebuildRegion` and `rebuildMesh`) and the tiles' coarse meshes.
        TileGrid giGrid;
        u64 editSerial = 1;
        std::vector<u64> tileRevision;
        std::vector<TerrainGiTile> giTiles;
        u64 giSignature = 0;
        bool fromLegacy = false;  // opened from a `.cterrain`
    };

    TerrainEntry& ensureEntry(ECS::Entity entity);
    void rebuildMesh(ECS::Entity entity, TerrainEntry& entry, ECS::TerrainComponent& component);
    /// Gives the tiles a region of the height grid touches a new revision (all tiles when `region` is null).
    static void markTilesEdited(TerrainEntry& entry, const ECS::TerrainComponent& settings,
                                const TerrainVertexBounds* region);
    void rebuildCollisionMesh(TerrainEntry& entry, const ECS::TerrainComponent& component);
#ifdef CF_HAS_SDL3
    void releaseMeshGpu(Assets::Mesh3D& mesh);
    void releaseChunkGpu(TerrainChunk& chunk);
    void releaseChunksGpu(std::vector<TerrainChunk>& chunks);
    void releaseEntryGpu(TerrainEntry& entry);
#endif

    std::unordered_map<u32, TerrainEntry> m_entries;
#ifdef CF_HAS_SDL3
    RHI::RenderDevice* m_gpuDevice = nullptr;
#endif
};

}  // namespace Caffeine::Terrain
