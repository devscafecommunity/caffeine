#pragma once

/**
 * Caffeine engine C ABI — first vertical slice.
 *
 * The engine library is UI-agnostic. Frontends (ImGui, Iced, tools, tests)
 * talk to it through this opaque, versioned interface instead of including
 * C++ editor/scene headers.
 *
 * This slice covers scene sessions and the hierarchy operations needed by the
 * Rust/Iced prototype. Rendering, assets, component schemas and commands will
 * be added incrementally without breaking this ABI.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(CAFFEINE_CAPI_BUILD)
#  define CAFFEINE_CAPI_API __declspec(dllexport)
#elif defined(_WIN32) && defined(CAFFEINE_CAPI_DLL)
#  define CAFFEINE_CAPI_API __declspec(dllimport)
#elif defined(CAFFEINE_CAPI_BUILD) && defined(__GNUC__)
#  define CAFFEINE_CAPI_API __attribute__((visibility("default")))
#else
#  define CAFFEINE_CAPI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CAFFEINE_CAPI_VERSION 8
#define CAFFEINE_ENTITY_NONE UINT32_MAX

typedef struct CaffeineSession CaffeineSession;
typedef uint32_t CaffeineEntity;

typedef enum CaffeineResult {
    CAFFEINE_OK = 0,
    CAFFEINE_ERROR = 1,
    CAFFEINE_INVALID_ARGUMENT = 2,
    CAFFEINE_NOT_FOUND = 3,
} CaffeineResult;

typedef struct CaffeineEntityInfo {
    CaffeineEntity id;
    char name[64];
} CaffeineEntityInfo;

typedef struct CaffeineFrameStats {
    float delta_time;
    uint64_t frame_index;
    uint32_t entity_count;
} CaffeineFrameStats;

typedef struct CaffeineVec3 {
    float x;
    float y;
    float z;
} CaffeineVec3;

typedef struct CaffeineCamera {
    float yaw;
    float pitch;
    float distance;
    CaffeineVec3 focus;
} CaffeineCamera;

/** Material-preview subject shape. */
typedef enum CaffeinePreviewShape {
    CAFFEINE_PREVIEW_SPHERE = 0,
    CAFFEINE_PREVIEW_CUBE = 1,
    CAFFEINE_PREVIEW_PLANE = 2,
} CaffeinePreviewShape;

/** Mesh-map kinds baked from an entity's UV mesh. */
typedef enum CaffeineMeshMapKind {
    CAFFEINE_MESH_MAP_NORMAL = 0,
    CAFFEINE_MESH_MAP_POSITION = 1,
    CAFFEINE_MESH_MAP_OBJECT_ID = 2,
} CaffeineMeshMapKind;

/** Result of a Photon ambient-occlusion bake into an entity's ORM map. */
typedef struct CaffeineAOBakeResult {
    int ok;
    int used_gpu;
    uint32_t covered_texels;
    uint32_t triangles;
    char orm_path[512];
    char material_path[512];
    char error[256];
} CaffeineAOBakeResult;

typedef struct CaffeineAssetInfo {
    char name[128];
    char relative_path[512];
    char kind[32];
    uint64_t size_bytes;
    int is_directory;
} CaffeineAssetInfo;

typedef enum CaffeineFieldKind {
    CAFFEINE_FIELD_F32 = 0,
    CAFFEINE_FIELD_BOOL = 1,
    CAFFEINE_FIELD_I32 = 2,
    CAFFEINE_FIELD_U32 = 3,
    CAFFEINE_FIELD_VEC2 = 4,
    CAFFEINE_FIELD_VEC3 = 5,
    CAFFEINE_FIELD_VEC4 = 6,
    CAFFEINE_FIELD_STRING = 7,
    CAFFEINE_FIELD_ENUM = 8,
    CAFFEINE_FIELD_COLOR = 9,
} CaffeineFieldKind;

typedef struct CaffeineFieldInfo {
    char name[64];
    CaffeineFieldKind kind;
    float min_value;
    float max_value;
    float step;
    char enum_options[256]; /**< '|' separated labels for enum fields. */
} CaffeineFieldInfo;

typedef struct CaffeineFieldValue {
    CaffeineFieldKind kind;
    float vec[4];
    int32_t integer;
    int32_t boolean;
    char text[256];
} CaffeineFieldValue;

typedef struct CaffeineComponentTypeInfo {
    uint32_t id;
    char name[64];
    uint32_t field_count;
} CaffeineComponentTypeInfo;

typedef struct CaffeineComponentInfo {
    uint32_t type_id;
    char name[64];
    uint32_t field_count;
} CaffeineComponentInfo;

/** Create/destroy a scene session. The session owns its ECS world. */
CAFFEINE_CAPI_API int caffeine_capi_version(void);

CAFFEINE_CAPI_API CaffeineSession* caffeine_session_create(void);
CAFFEINE_CAPI_API void caffeine_session_destroy(CaffeineSession* session);

/** Load a .caf scene. Returns CAFFEINE_ERROR when the file cannot be read. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_load_scene(CaffeineSession* session, const char* path);
/** Current scene path. Valid until the next load or session destruction. */
CAFFEINE_CAPI_API const char* caffeine_session_scene_path(const CaffeineSession* session);
/** 1 when the session has unsaved edits. */
CAFFEINE_CAPI_API int caffeine_session_is_dirty(const CaffeineSession* session);
/**
 * Render the scene offscreen to RGBA8 pixels. `pixels` must hold at least
 * width * height * 4 bytes. This is the CPU-readback viewport path; GPU
 * shared-texture interop will replace it later.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_render(CaffeineSession* session, uint32_t width,
                                       uint32_t height, uint8_t* pixels,
                                       size_t pixel_bytes);

/** Non-blocking render: submit once, poll until READY, then reuse the pixels. */
typedef enum CaffeineRenderState {
    CAFFEINE_RENDER_IDLE = 0,
    CAFFEINE_RENDER_PENDING = 1,
    CAFFEINE_RENDER_READY = 2,
    CAFFEINE_RENDER_FAILED = 3,
} CaffeineRenderState;

CAFFEINE_CAPI_API CaffeineResult caffeine_session_render_begin(CaffeineSession* session,
                                             uint32_t width, uint32_t height);
CAFFEINE_CAPI_API CaffeineRenderState caffeine_session_render_poll(CaffeineSession* session,
                                                 uint8_t* pixels,
                                                 size_t pixel_bytes);
/** Viewport camera (radians for yaw/pitch, world units for distance). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_camera(CaffeineSession* session, CaffeineCamera camera);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_get_camera(const CaffeineSession* session, CaffeineCamera* out);

/**
 * Offscreen viewport quality and overlays.
 *
 * `anti_aliasing` is 0 = off, 1 = FXAA, 2 = TAA. `render_scale` is the
 * internal supersampling factor and is clamped to [0.5, 2.0].
 */
typedef struct CaffeineViewportSettings {
    int show_grid;
    int anti_aliasing;
    float render_scale;
} CaffeineViewportSettings;

CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_viewport_settings(
    CaffeineSession* session, CaffeineViewportSettings settings);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_get_viewport_settings(
    const CaffeineSession* session, CaffeineViewportSettings* out);

/** Component schema and per-entity component instances. */
CAFFEINE_CAPI_API uint32_t caffeine_component_type_count(void);
CAFFEINE_CAPI_API CaffeineResult caffeine_component_type_at(uint32_t index,
                                            CaffeineComponentTypeInfo* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_component_type_field_at(uint32_t type_id,
                                                  uint32_t field,
                                                  CaffeineFieldInfo* out);
CAFFEINE_CAPI_API uint32_t caffeine_session_entity_component_count(const CaffeineSession* session,
                                                   CaffeineEntity entity);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_component_at(const CaffeineSession* session,
                                                      CaffeineEntity entity,
                                                      uint32_t index,
                                                      CaffeineComponentInfo* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_component_field_get(
    const CaffeineSession* session, CaffeineEntity entity, uint32_t component,
    uint32_t field, CaffeineFieldValue* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_component_field_set(
    CaffeineSession* session, CaffeineEntity entity, uint32_t component,
    uint32_t field, const CaffeineFieldValue* value);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_add_component(CaffeineSession* session,
                                                CaffeineEntity entity,
                                                uint32_t type_id);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_remove_component(CaffeineSession* session,
                                                   CaffeineEntity entity,
                                                   uint32_t type_id);

/** In-memory world snapshot for editor undo/redo. Free `out_data` with caffeine_session_free. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_snapshot(const CaffeineSession* session,
                                          uint8_t** out_data, size_t* out_size,
                                          int* out_dirty);
CAFFEINE_CAPI_API void caffeine_session_free(void* data);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_restore(CaffeineSession* session,
                                          const uint8_t* data, size_t size, int dirty);

/** Project assets. The root defaults to <project>/assets/raw (or assets). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_asset_root(CaffeineSession* session, const char* path);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_refresh_assets(CaffeineSession* session);
/** Absolute path for an asset path relative to the asset root. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_asset_path(const CaffeineSession* session,
                                             const char* relative_path, char* buffer,
                                             size_t buffer_size);
/** Project-relative form used by material files and scene references. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_asset_project_path(const CaffeineSession* session,
                                                     const char* relative_path, char* buffer,
                                                     size_t buffer_size);
/** Resolves a texture/material path against the scene, asset root and engine assets. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_resolve_path(const CaffeineSession* session,
                                                const char* path, char* buffer,
                                                size_t buffer_size);
CAFFEINE_CAPI_API uint32_t caffeine_session_asset_count(const CaffeineSession* session);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_asset_at(const CaffeineSession* session, uint32_t index,
                                         CaffeineAssetInfo* out);

/** PBR material assets (.mat). Colours are sRGB; path fields are project-relative. */
typedef struct CaffeineMaterial {
    char name[128];
    float albedo[4];
    float metallic;
    float roughness;
    float reflectance;
    float emission[3];
    float emission_strength;
    char albedo_map[512];
    char normal_map[512];
    char orm_map[512];
    char emission_map[512];
    float uv_tiling[2];
    float uv_offset[2];
    float normal_strength;
    int normal_directx; /**< Normal map green channel follows DirectX (Y down). */
    float ao_strength;
    int alpha_mode; /**< 0 opaque, 1 cutout, 2 blend. */
    float alpha_cutoff;
    float transmission;
    float ior;
    float clearcoat;
    float clearcoat_roughness;
    float sheen_color[3];
    float sheen_roughness;
    float iridescence;
    float iridescence_thickness;
    float iridescence_ior;
    float reflection;
    int reflection_budget;
    int reflection_performance;
    int reflection_quality;
    int reflection_planar;
    float ssr_resolution;
    float ssr_max_steps;
    float ssr_temporal;
    float ssr_distance;
    float ssr_samples;
    float ssr_denoise;
    float ssr_probe_blend;
    float ssr_bounces;
    /** Parallax depth in UV units (0 = off); height is the normal map's alpha channel. */
    float height_scale;
    float parallax_steps;
    /** 0 parallax occlusion, 1 geometric (subdivided mesh + vertex displacement). */
    int displacement_mode;
    float displacement_scale; /**< metres */
    int mesh_subdivisions;    /**< levels, 1..9 */
    /** 1 = UVs follow the object's scale (texel density is kept), 0 = authored 1:1 UVs. */
    int uv_object_scale;
} CaffeineMaterial;

typedef struct CaffeineMaterialPresetInfo {
    char category[64];
    char name[64];
} CaffeineMaterialPresetInfo;

CAFFEINE_CAPI_API CaffeineResult caffeine_material_default(CaffeineMaterial* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_material_sanitize(CaffeineMaterial* material);
CAFFEINE_CAPI_API CaffeineResult caffeine_material_load(const char* path, CaffeineMaterial* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_material_save(const char* path,
                                          const CaffeineMaterial* material);
CAFFEINE_CAPI_API uint32_t caffeine_material_preset_count(void);
CAFFEINE_CAPI_API CaffeineResult caffeine_material_preset_at(uint32_t index,
                                               CaffeineMaterialPresetInfo* info,
                                               CaffeineMaterial* out);
/** Assigns a .mat to the entity's Mesh Filter (the material the renderer uses). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_apply_material(CaffeineSession* session,
                                                   CaffeineEntity entity, const char* path);
/**
 * Bakes static ambient occlusion for an entity from the scene's meshes and
 * assigns the resulting ORM map to the entity material. `output_directory` is
 * project-relative (default `processed/ao` when null/empty).
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_bake_ao(
    CaffeineSession* session, CaffeineEntity entity, uint32_t resolution, uint32_t rays,
    float max_distance, const char* output_directory, CaffeineAOBakeResult* out);
/**
 * Ray-picks the entity's mesh from a viewport pixel and returns its UV. The
 * coordinates use the current editor camera and the logical viewport size.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_pick_uv(CaffeineSession* session,
                                            CaffeineEntity entity, float x, float y,
                                            float width, float height, float* u,
                                            float* v);
/** Bakes world normal, world position or object ID from the entity mesh into a PNG. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_bake_mesh_map(
    CaffeineSession* session, CaffeineEntity entity, CaffeineMeshMapKind kind,
    uint32_t resolution, const char* output_path);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_material(const CaffeineSession* session,
                                                    CaffeineEntity entity, char* buffer,
                                                    size_t buffer_size);
/**
 * Renders a material sphere preview to RGBA8. Blocks on the GPU readback; the
 * editor calls it when the material changes, not every frame.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_render_material_preview(
    CaffeineSession* session, const CaffeineMaterial* material, uint32_t size, float yaw_degrees,
    float pitch_degrees, uint8_t* pixels, size_t pixel_bytes);
/** Preview with shape, HDRI environment and exposure. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_render_material_preview_ex(
    CaffeineSession* session, const CaffeineMaterial* material, uint32_t size, float yaw_degrees,
    float pitch_degrees, CaffeinePreviewShape shape, const char* environment_path,
    float exposure, uint8_t* pixels, size_t pixel_bytes);
/**
 * Sun used by the material preview (applies to every later preview render): direction as
 * yaw/elevation in degrees, intensity, and whether it casts shadow-mapped shadows.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_material_preview_light(
    CaffeineSession* session, float sun_yaw_degrees, float sun_elevation_degrees,
    float sun_intensity, int shadows);
/** Starts an asynchronous preview render/readback; poll with render_material_preview_poll. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_render_material_preview_begin(
    CaffeineSession* session, const CaffeineMaterial* material, uint32_t size, float yaw_degrees,
    float pitch_degrees, CaffeinePreviewShape shape, const char* environment_path,
    float exposure);
CAFFEINE_CAPI_API CaffeineRenderState caffeine_session_render_material_preview_poll(
    CaffeineSession* session, uint8_t* pixels, size_t pixel_bytes);

/** Texture map generation. All paths are resolved as given. */
typedef enum CaffeineTextureMapKind {
    CAFFEINE_TEXTURE_NORMAL = 0,
    CAFFEINE_TEXTURE_AO = 1,
    CAFFEINE_TEXTURE_ROUGHNESS = 2,
    CAFFEINE_TEXTURE_METALLIC = 3,
    CAFFEINE_TEXTURE_HEIGHT = 4,
} CaffeineTextureMapKind;

CAFFEINE_CAPI_API CaffeineResult caffeine_texture_generate_map(
    const char* source_path, const char* output_path, CaffeineTextureMapKind kind, float strength,
    int directx, int wrap, float min_value, float max_value, int invert, uint32_t radius);
CAFFEINE_CAPI_API CaffeineResult caffeine_texture_pack_maps(
    const char* ao_path, const char* roughness_path, const char* metallic_path,
    const char* output_path, int kind);
/** Writes raw RGBA8 pixels (generator output) as a PNG. */
CAFFEINE_CAPI_API CaffeineResult caffeine_texture_save_rgba(const char* output_path, uint32_t width,
                                              uint32_t height, const uint8_t* pixels,
                                              size_t pixel_bytes);
/** Decodes an image into a downscaled RGBA8 thumbnail. Free pixels with caffeine_session_free. */
CAFFEINE_CAPI_API CaffeineResult caffeine_texture_load_thumbnail(
    const char* input_path, uint32_t max_size, uint32_t* width, uint32_t* height,
    uint8_t** pixels, size_t* pixel_bytes);
/** Writes raw RGBA16 pixels as a 16-bit PNG. */
CAFFEINE_CAPI_API CaffeineResult caffeine_texture_save_rgba16(
    const char* output_path, uint32_t width, uint32_t height, const uint16_t* pixels,
    size_t pixel_bytes);
/**
 * Loads an image as RGBA8. The returned buffer is allocated and must be freed
 * with `caffeine_session_free`.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_texture_load_rgba(const char* input_path,
                                              uint32_t* width, uint32_t* height,
                                              uint8_t** pixels, size_t* pixel_bytes);

/** Write the current world to a .caf scene and clear the dirty flag. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_save_scene(CaffeineSession* session, const char* path);

/** 1 when this build includes the Dryad interpreter. */
CAFFEINE_CAPI_API int caffeine_dryad_available(void);

/**
 * Runs onCreate plus one onUpdate for a Dryad `.dyd` script on an entity.
 * `script_path` may be absolute or project-relative. Returns CAFFEINE_ERROR when
 * the build has no Dryad interpreter or the script fails to load/run.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_run_dryad(CaffeineSession* session,
                                             CaffeineEntity entity,
                                             const char* script_path, float dt);

/** Hierarchy queries. The index is stable within one call sequence; re-query after edits. */
CAFFEINE_CAPI_API uint32_t caffeine_session_entity_count(const CaffeineSession* session);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_at(const CaffeineSession* session, uint32_t index,
                                          CaffeineEntityInfo* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_name(const CaffeineSession* session,
                                            CaffeineEntity entity, char* buffer,
                                            size_t buffer_size);
/** Hierarchy links. Pass CAFFEINE_ENTITY_NONE to set_entity_parent to unparent. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_parent(const CaffeineSession* session,
                                              CaffeineEntity entity,
                                              CaffeineEntity* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_entity_parent(CaffeineSession* session,
                                                  CaffeineEntity entity,
                                                  CaffeineEntity parent);

/** Commands. */
CAFFEINE_CAPI_API CaffeineEntity caffeine_session_create_entity(CaffeineSession* session, const char* name);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_destroy_entity(CaffeineSession* session, CaffeineEntity entity);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_entity_name(CaffeineSession* session, CaffeineEntity entity,
                                                const char* name);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_select_entity(CaffeineSession* session, CaffeineEntity entity);
CAFFEINE_CAPI_API CaffeineEntity caffeine_session_selected_entity(const CaffeineSession* session);

/** Transform helpers. Setting a position creates a Transform when the entity has none. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_position(const CaffeineSession* session,
                                                CaffeineEntity entity, CaffeineVec3* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_entity_position(CaffeineSession* session,
                                                    CaffeineEntity entity, CaffeineVec3 position);
/** Euler rotation in degrees. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_rotation(const CaffeineSession* session,
                                                CaffeineEntity entity, CaffeineVec3* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_entity_rotation(CaffeineSession* session,
                                                    CaffeineEntity entity,
                                                    CaffeineVec3 euler_degrees);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_scale(const CaffeineSession* session,
                                             CaffeineEntity entity, CaffeineVec3* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_entity_scale(CaffeineSession* session,
                                                 CaffeineEntity entity, CaffeineVec3 scale);
/** Resolved transform in scene space, including every parent link. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_entity_world_position(const CaffeineSession* session,
                                                      CaffeineEntity entity,
                                                      CaffeineVec3* out);
/** Sets a scene-space position; the local transform is derived from the parent. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_entity_world_position(CaffeineSession* session,
                                                          CaffeineEntity entity,
                                                          CaffeineVec3 position);

/** One authored Gerstner wave. */
typedef struct CaffeineWaterWave {
    float dir_x;
    float dir_z;
    float wavelength;
    float amplitude;
    float steepness;
    float phase;
} CaffeineWaterWave;

#define CAFFEINE_WATER_MAX_WAVES 16

/** A water surface (NaturalFluid component in water mode) with authored waves. POD. */
typedef struct CaffeineWater {
    int enabled;
    float size_x;
    float size_z;
    float water_level;
    float depth;
    uint32_t resolution;
    float time_scale;
    float albedo[4];
    float roughness;
    float foam_threshold;
    uint32_t seed;
    uint32_t wave_count;
    CaffeineWaterWave waves[CAFFEINE_WATER_MAX_WAVES];
} CaffeineWater;

/** Adds (or updates) a water surface on an entity. With wave_count 0 the wind spectrum is used. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_water(CaffeineSession* session,
                                                            CaffeineEntity entity,
                                                            const CaffeineWater* water);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_water(const CaffeineSession* session,
                                                        CaffeineEntity entity,
                                                        CaffeineWater* out);
/** Surface height of the water at a rest position and time (for floating objects and previews). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_water_height(const CaffeineSession* session,
                                                               CaffeineEntity entity, float x,
                                                               float z, float* out_height);

/** Terrain settings of an entity (POD). Heights are normalised 0..1 and scaled by max_height. */
typedef struct CaffeineTerrain {
    uint32_t resolution_x;
    uint32_t resolution_z;
    float size_x;
    float size_z;
    float max_height;
    int cast_shadows;
} CaffeineTerrain;

/**
 * Creates or updates a terrain on an entity from generated data. `heights` holds
 * resolution_x * resolution_z floats in 0..1 (row-major, z rows); `splat_rgba` (optional, same
 * grid) holds the four layer weights as bytes and is resampled to the splat map.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_terrain(CaffeineSession* session,
                                                              CaffeineEntity entity,
                                                              const CaffeineTerrain* terrain,
                                                              const float* heights,
                                                              const uint8_t* splat_rgba);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain(const CaffeineSession* session,
                                                          CaffeineEntity entity,
                                                          CaffeineTerrain* out);
/** Terrain height in metres at (x, z) metres from the terrain's corner. The mesh is centred on its entity, so the corner is at the entity position minus half the size. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_height(const CaffeineSession* session,
                                                                 CaffeineEntity entity, float x,
                                                                 float z, float* out_height);

/** Advance frame bookkeeping; systems will be wired here incrementally. */
CAFFEINE_CAPI_API void caffeine_session_tick(CaffeineSession* session, float delta_time);
CAFFEINE_CAPI_API void caffeine_session_frame_stats(const CaffeineSession* session, CaffeineFrameStats* out);

#ifdef __cplusplus
}
#endif
