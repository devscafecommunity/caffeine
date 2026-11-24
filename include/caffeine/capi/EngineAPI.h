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

#define CAFFEINE_CAPI_VERSION 29
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

/**
 * How a path was understood by the project's asset manager (see caffeine_session_asset_reference).
 * Anything that refers to an asset (a component field, a generator node) stores `asset_path` of an OK
 * reference (or `project_path` in files that are relative to the project), so it always names a file that is inside the project (or an engine built-in).
 */
typedef enum CaffeineReferenceStatus {
    CAFFEINE_REF_OK = 0,
    CAFFEINE_REF_EMPTY = 1,
    CAFFEINE_REF_MISSING = 2,         /* no such file in the project, its assets folder or the engine's assets */
    CAFFEINE_REF_OUTSIDE_PROJECT = 3, /* a real file, but outside the project: import it first */
    CAFFEINE_REF_NOT_A_FILE = 4
} CaffeineReferenceStatus;

typedef struct CaffeineAssetReference {
    int status;        /* CaffeineReferenceStatus */
    int engine_asset;  /* 1 for an engine built-in, 0 for a project file */
    int imported;      /* 1 when this call copied the file into the project */
    char project_path[512];  /* relative to the project root: what scenes and .mat files store */
    char asset_path[512];    /* relative to the assets folder when the file is in it: what components store */
    char absolute_path[512];
    char kind[32];     /* image, mesh, audio, material, script, animation, generator, asset, file */
} CaffeineAssetReference;

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

/**
 * Scene views (v23): the scene seen through one of its Camera 3D entities rather than the editor camera, each with
 * its own target and readback, used the same way as render_begin / render_poll. View 0 is the game (the active
 * camera), view 1 the camera preview (the camera chosen with `caffeine_session_set_preview_camera`, else the active
 * one). Neither draws the editor grid. A scene without a Camera 3D has no scene view.
 */
#define CAFFEINE_VIEW_GAME 0
#define CAFFEINE_VIEW_CAMERA 1
/** The camera a scene view would use and its field of view; CAFFEINE_ERROR when the scene has no Camera 3D. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_scene_camera(const CaffeineSession* session, int32_t view,
                                                               uint32_t* entity, float* fov_degrees);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_preview_camera(CaffeineSession* session, uint32_t entity);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_view_render_begin(CaffeineSession* session, int32_t view,
                                                                    uint32_t width, uint32_t height);
CAFFEINE_CAPI_API CaffeineRenderState caffeine_session_view_render_poll(CaffeineSession* session, int32_t view,
                                                                        uint8_t* pixels, size_t pixel_bytes);

/**
 * The game (v23): Play. `game_start` begins the scene's simulation the way the compiled game runs it (animation,
 * effects, fluids, navigation, 2D and 3D physics, Lua and Dryad scripts, procedural worlds, UI) on the session's own
 * world; `game_tick` runs one frame of it; the caller renders the game view and, on Stop, restores the scene it
 * snapshotted before Play (`caffeine_session_snapshot` / `caffeine_session_restore`). While the game runs the
 * editor's own `caffeine_session_tick` does nothing, so the scene is never advanced twice.
 */
typedef struct CaffeineGameState {
    int32_t running;
    int32_t scripts_running;  /**< 1 when the script engine started (a build without scripting runs none) */
    uint64_t frames;
    float time_s;
} CaffeineGameState;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_game_start(CaffeineSession* session);
CAFFEINE_CAPI_API void caffeine_session_game_stop(CaffeineSession* session);
CAFFEINE_CAPI_API int32_t caffeine_session_game_running(const CaffeineSession* session);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_game_tick(CaffeineSession* session, float delta_time);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_game_state(const CaffeineSession* session, CaffeineGameState* out);
/** A key the scripts read: `scancode` is the USB HID / SDL scancode (A = 4, Space = 44, arrows 79..82). */
CAFFEINE_CAPI_API void caffeine_session_game_key(CaffeineSession* session, uint32_t scancode, int32_t down);
CAFFEINE_CAPI_API void caffeine_session_game_release_keys(CaffeineSession* session);
/** The pointer over the game view, in its pixels; `button` 1 left, 2 middle, 3 right (0: only a move). */
CAFFEINE_CAPI_API void caffeine_session_game_mouse(CaffeineSession* session, float x, float y, uint32_t button,
                                                   int32_t down);
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
    int shadows;  /**< 1 renders the lights' shadow maps in the viewport (off by default, as in the editor; v14) */
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
/**
 * The project's asset manager decides what a path refers to: relative to the project, the assets folder or the
 * engine's assets, or absolute. With `import` set, a real file outside the project is first copied into the
 * assets folder (in the folder of its kind; identical files are not duplicated) and the reference points at the
 * copy. `out->status` says what happened; the result is CAFFEINE_OK even for a missing file (look at the status).
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_asset_reference(CaffeineSession* session, const char* path,
                                                  int import, CaffeineAssetReference* out);
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
    /** Foliage (v9). Wind sway per metre of height, time scale, leaf flutter 0..1. */
    float wind_strength;
    float wind_speed;
    float wind_flutter;
    /** Light passing through thin leaves and petals, 0..1. */
    float translucency;
    /** Run-time modifiers (v10): albedo tint, wetness, snow cover, age. */
    float mod_tint[3];
    float mod_wetness;
    float mod_snow;
    float mod_age;
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

/**
 * Animation (C ABI v22). The PhaseShift is the entity's state machine (formerly
 * "Animator"). These calls read and write its parameters, states and base-layer transitions; layers
 * are not exposed yet. Names are NUL-terminated and at most 31 characters.
 */
typedef struct CaffeinePhaseShiftCounts {
    uint32_t parameters;
    uint32_t states;
    uint32_t transitions;  /* any-state transitions first, then each state's, in state order */
} CaffeinePhaseShiftCounts;

/** kind: 0 bool, 1 float, 2 int, 3 trigger. value is the float, int or 0/1 bool value. */
typedef struct CaffeinePhaseShiftParameter {
    char     name[32];
    uint32_t kind;
    float    value;
    float    smoothing;  /* seconds; zero when the float is not smoothed */
} CaffeinePhaseShiftParameter;

typedef struct CaffeinePhaseShiftState {
    char     name[32];
    char     clip[260];  /* motion name, or the clip name when the state has no motion */
    float    speed;
    uint32_t is_current;
} CaffeinePhaseShiftState;

/** op: 0 equals, 1 not equals, 2 greater, 3 less, 4 greater or equal, 5 less or equal. */
typedef struct CaffeinePhaseShiftCondition {
    char     parameter[32];
    uint32_t op;
    float    value;
} CaffeinePhaseShiftCondition;

typedef struct CaffeinePhaseShiftTransition {
    char     from[32];  /* "Any State" for any-state transitions */
    char     to[32];
    float    blend_time;
    uint32_t has_exit_time;
    uint32_t condition_count;
} CaffeinePhaseShiftTransition;

CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_counts(const CaffeineSession* session,
                                                                     CaffeineEntity entity,
                                                                     CaffeinePhaseShiftCounts* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_parameter(const CaffeineSession* session,
                                                                        CaffeineEntity entity,
                                                                        uint32_t index,
                                                                        CaffeinePhaseShiftParameter* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_state(const CaffeineSession* session,
                                                                    CaffeineEntity entity,
                                                                    uint32_t index,
                                                                    CaffeinePhaseShiftState* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_transition(const CaffeineSession* session,
                                                                         CaffeineEntity entity,
                                                                         uint32_t index,
                                                                         CaffeinePhaseShiftTransition* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_condition(const CaffeineSession* session,
                                                                        CaffeineEntity entity,
                                                                        uint32_t transition_index,
                                                                        uint32_t index,
                                                                        CaffeinePhaseShiftCondition* out);
/** Sets a float parameter. Bool, int and trigger parameters return CAFFEINE_INVALID_ARGUMENT. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_set_float(CaffeineSession* session,
                                                                        CaffeineEntity entity,
                                                                        uint32_t index, float value);
/** Smooths a float parameter over `seconds`. Zero turns smoothing off. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_set_smoothing(CaffeineSession* session,
                                                                            CaffeineEntity entity,
                                                                            uint32_t index,
                                                                            float seconds);
/** Adds a PhaseShift to the entity (no-op when it has one). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_add(CaffeineSession* session, CaffeineEntity entity);
/**
 * Adds a parameter to the entity's PhaseShift. kind: 0 bool, 1 float, 2 int, 3 trigger. The name must be non-empty,
 * at most 31 characters, and unused. INVALID_ARGUMENT for a bad name or kind, NOT_FOUND without a PhaseShift.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_add_parameter(CaffeineSession* session,
                                                                            CaffeineEntity entity,
                                                                            const char* name, uint32_t kind);
/** Removes a parameter by index. Later indices shift down. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_phase_shift_remove_parameter(CaffeineSession* session,
                                                                               CaffeineEntity entity,
                                                                               uint32_t index);

/** Counts of the 2D rig attached to an entity. Zero everywhere when no rig is attached or it failed to load. */
typedef struct CaffeineRig2DCounts {
    uint32_t bones;
    uint32_t vertices;
    uint32_t triangles;
    uint32_t parameters;
    uint32_t bindings;
    uint32_t loaded;  /* 1 when the rig file loaded */
} CaffeineRig2DCounts;

typedef struct CaffeineRig2DBone {
    char    name[32];
    int32_t parent;  /* -1 for a root */
} CaffeineRig2DBone;

/** value is the parameter as the last tick evaluated it (PhaseShift value when one is set, else the clip, else the default). */
typedef struct CaffeineRig2DParameter {
    char  name[32];
    float minimum;
    float default_value;
    float maximum;
    float value;
} CaffeineRig2DParameter;

typedef struct CaffeineRig2DVertex {
    uint32_t influence_count;  /* up to 4 */
    uint32_t bones[4];
    float    weights[4];
} CaffeineRig2DVertex;

/** channel: 0 rotation, 1 translate x, 2 translate y, 3 scale x, 4 scale y. */
typedef struct CaffeineRig2DBinding {
    uint32_t parameter;
    uint32_t bone;
    uint32_t channel;
    uint32_t key_count;
} CaffeineRig2DBinding;

/** Attaches a .rig2d file to the entity, adding the Rig 2D player when needed. The rig loads on the next access. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_attach(CaffeineSession* session,
                                                               CaffeineEntity entity,
                                                               const char* rig_path);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_counts(const CaffeineSession* session,
                                                               CaffeineEntity entity,
                                                               CaffeineRig2DCounts* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_bone(const CaffeineSession* session,
                                                             CaffeineEntity entity, uint32_t index,
                                                             CaffeineRig2DBone* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_parameter(const CaffeineSession* session,
                                                                  CaffeineEntity entity,
                                                                  uint32_t index,
                                                                  CaffeineRig2DParameter* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_vertex(const CaffeineSession* session,
                                                               CaffeineEntity entity, uint32_t index,
                                                               CaffeineRig2DVertex* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_binding(const CaffeineSession* session,
                                                                CaffeineEntity entity, uint32_t index,
                                                                CaffeineRig2DBinding* out);
/** Sets the rig parameter by writing the PhaseShift float with the same name (added when missing), clamped to its range. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_rig2d_set_parameter(CaffeineSession* session,
                                                                      CaffeineEntity entity,
                                                                      uint32_t index, float value);

/**
 * What the last rendered frame drew and cost, and what the GPU can do (v11). Times are CPU-side
 * (encoding): SDL3 GPU has no portable GPU timestamps. All zero before the first render.
 */
typedef struct CaffeineRenderStats {
    uint64_t triangles;           /**< submitted, counting every instance */
    uint32_t meshes;              /**< draws that survived culling */
    uint32_t frustum_culled;
    uint32_t draw_calls;          /**< an instanced batch is one call */
    uint32_t instanced_draw_calls;
    float gather_ms;
    float shadows_ms;
    float opaque_ms;
    float translucent_ms;
    float total_ms;
    int32_t indirect_draw;        /**< 1 when the device supports indirect draws */
    int32_t graphics_storage_buffers;
    int32_t compute_storage_write;
    int32_t mesh_shaders;         /**< always 0 on SDL3 GPU */
    uint32_t gpu_water_draws;     /**< water surfaces waved in the vertex shader (v13) */
    uint32_t micromesh_draws;     /**< meshes drawn as a GPU-selected Micromesh cut (v14; was `reserved`) */
    uint32_t shadow_draws;        /**< caster submissions into the sun's cascades, one per mesh and cascade (v19) */
    uint32_t shadow_draws_culled; /**< casters skipped because they lie outside a cascade (v19) */
} CaffeineRenderStats;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_render_stats(const CaffeineSession* session,
                                                               CaffeineRenderStats* out);

/**
 * What the last frame's Micromesh draws kept (v14). Reading it copies the GPU's draw arguments back and
 * waits for the GPU: for tools and tests, not for every frame. All zero without Micromesh draws.
 */
typedef struct CaffeineMicromeshFrameStats {
    uint32_t draws;               /**< entities drawn as a cluster cut */
    uint32_t clusters;            /**< clusters the cut selected, over every draw */
    uint64_t triangles;           /**< triangles of the camera's cut */
    uint64_t source_triangles;    /**< triangles those meshes have in full */
    uint32_t views;               /**< views cut this frame: the camera and each shadow cascade */
    uint32_t arena_used;          /**< indices every view's cut took from the shared index arena */
    uint32_t arena_capacity;
    uint32_t overflowed;          /**< cuts that did not fit in the arena and were not drawn this frame */
    uint32_t occlusion;           /**< 1 when the camera's cut was tested against last frame's depth */
    uint32_t reserved;
} CaffeineMicromeshFrameStats;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_micromesh_stats(const CaffeineSession* session,
                                                                  CaffeineMicromeshFrameStats* out);

/** What Photon's voxel far field holds and cost in the latest frame (v17). */
typedef struct CaffeinePhotonStats {
    uint32_t objects;          /**< solid objects in the triangle volume */
    uint32_t triangles;        /**< triangles voxelised (0 when over budget) */
    uint32_t over_budget;      /**< 1 when the scene exceeded the 1M-triangle cap: only the screen fill remains */
    uint32_t far_enabled;      /**< 1 when the far field ran in the latest frame */
    uint32_t full_rebuilds;    /**< clipmap levels rebuilt whole since the session began */
    uint32_t region_rebuilds;  /**< dirty boxes revoxelised since the session began */
    uint32_t last_level_mask;  /**< levels rebuilt whole in the latest update (bit per level) */
    uint32_t last_regions;     /**< dirty boxes revoxelised in the latest update */
    float last_update_ms;      /**< CPU time to encode the latest voxel update */
    uint32_t target_rebuilds;  /**< times the per-size targets were created again (a view of another size makes it climb) */
    uint32_t far_skipped;      /**< frames the far field was left as it was because nothing it depends on changed (v23) */
    uint32_t far_half_rate;    /**< frames it was skipped because the camera only turned in a calm scene (v23) */
} CaffeinePhotonStats;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_photon_stats(const CaffeineSession* session,
                                                               CaffeinePhotonStats* out);
/** Discards Photon's triangle volume; the next frame revoxelises the whole scene (v17). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_photon_rebuild(CaffeineSession* session);

/**
 * The engine's scope profiler (v18). One process-wide tree of timed scopes (CPU time of the renderer's stages,
 * and of the GPU passes when GPU timing is on). Scopes come in depth-first order; `depth` 0 are the top ones.
 * Times are milliseconds. With GPU timing on, the GPU passes wait for the GPU at their edges so their time is
 * the real cost; that serialises CPU and GPU, so frames get slower while it is on: a measuring mode.
 */
typedef struct CaffeineProfileScope {
    char name[48];
    uint32_t depth;
    uint32_t calls;
    float last_ms;      /**< the most recent call */
    float avg_ms;       /**< over every call since the last reset */
    float max_ms;
    float self_ms;      /**< average time not spent in child scopes */
    float recent_avg_ms;  /**< mean of the latest 120 calls (v19): follows what the scene does now */
    float recent_max_ms;  /**< worst of the latest 120 calls (v19) */
} CaffeineProfileScope;
/** Copies up to `capacity` scopes into `out` and sets `*out_count` to how many exist (it may exceed `capacity`). */
CAFFEINE_CAPI_API CaffeineResult caffeine_profiler_report(CaffeineProfileScope* out, uint32_t capacity,
                                                          uint32_t* out_count);
/** Forgets every scope and sample. */
CAFFEINE_CAPI_API CaffeineResult caffeine_profiler_reset(void);
/** Turns GPU timing on or off (see above); `enabled` 0 or 1. */
CAFFEINE_CAPI_API CaffeineResult caffeine_profiler_set_gpu_timing(int32_t enabled);
/** Whether GPU timing is on (`*out` is 0 or 1). */
CAFFEINE_CAPI_API CaffeineResult caffeine_profiler_gpu_timing(int32_t* out);

/**
 * The machine as the profiler's graphs show it (v19). Percentages cover the time since the previous call (the first
 * call reports zeros), so call it at a steady rate. What a platform cannot provide is reported as unavailable:
 * `cpu_core_count` 0, the GPU and VRAM fields -1. Linux reads /proc, NVML and sysfs; other platforms report nothing yet.
 */
#define CAFFEINE_MAX_CPU_CORES 64
typedef struct CaffeineSystemMetrics {
    uint32_t cpu_core_count;                         /**< logical cores reported (at most 64) */
    float cpu_core_percent[CAFFEINE_MAX_CPU_CORES];  /**< busy share of each logical core, 0..100 */
    float cpu_total_percent;                         /**< the whole machine, 0..100 */
    float process_cpu_percent;                       /**< this process, percent of ONE core (can exceed 100) */
    float ram_process_mib;                           /**< resident memory of this process (-1 unavailable) */
    float ram_used_mib;                              /**< memory in use on the whole machine (-1 unavailable) */
    float ram_total_mib;
    float gpu_busy_percent;                          /**< -1 unavailable */
    float vram_used_mib;                             /**< -1 unavailable */
    float vram_total_mib;
} CaffeineSystemMetrics;
CAFFEINE_CAPI_API CaffeineResult caffeine_system_metrics(CaffeineSystemMetrics* out);

/**
 * The weather's moving parts at the latest frame (v19): the lightning that is lighting the scene and the wind's
 * surge. All zero before the first render or when the scene has no lightning.
 */
typedef struct CaffeineWeatherState {
    float flash;                 /**< 0 none, about 1 at a full return stroke (more for a close strike) */
    float bolt;                  /**< 0..1: how visible the bolt in the sky is */
    float gust;                  /**< the wind's strength relative to its mean (1 = mean) */
    float clock_s;               /**< the clock the strikes use, seconds */
    int32_t lightning_active;    /**< 1 when the scene has lightning running */
    float strike_distance_m;     /**< of the strike being shown */
    float strike_azimuth_rad;
} CaffeineWeatherState;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_weather_state(const CaffeineSession* session,
                                                                CaffeineWeatherState* out);

/** A clap of thunder that has reached the listener (v19). */
typedef struct CaffeineThunderEvent {
    float distance_m;     /**< of the strike (the sound took distance / 343 s to arrive) */
    float azimuth_rad;    /**< direction of the strike around +Y */
    float volume;         /**< 0..1, falling with the distance */
    float arrival_s;      /**< on the clock `caffeine_session_weather_state` reports */
} CaffeineThunderEvent;
/**
 * Hands out the thunder that has arrived since the last call (at most `capacity`; `*out_count` is how many were
 * written). The engine makes no sound: a player (a game, a script) turns each event into one.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_poll_thunder(CaffeineSession* session, CaffeineThunderEvent* out,
                                                               uint32_t capacity, uint32_t* out_count);

/**
 * What the weather's sound is doing (v19). The sound is made from noise by the session itself (no files) when the
 * scene's `Weather` component has `Audio Enabled`; without an audio device the levels still follow the weather and
 * `device_ready` is 0.
 */
typedef struct CaffeineAudioState {
    int32_t enabled;         /**< the scene asks for the weather's sound */
    int32_t device_ready;    /**< an audio device is open */
    float volume;            /**< 0..1, the weather's master volume */
    float rain_level;        /**< 0..1 loudness the rain bed is heading for */
    float wind_level;
    int32_t rain_playing;    /**< the rain bed is running on the device */
    int32_t wind_playing;
    uint32_t thunder_heard;  /**< claps handed to the player since the session began */
    uint32_t thunder_played; /**< the ones that actually started on a device */
} CaffeineAudioState;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_audio_state(const CaffeineSession* session, CaffeineAudioState* out);

/**
 * Volumetric audio (v20): the scene's `Audio Emitter` entities become voices that travel through the scene's `Acoustic
 * Obstacle` boxes (delay, Doppler, occlusion, diffraction, the room's reverb and echoes), set by the `Volumetric Audio`
 * component. Without that component nothing is mixed. The session mixes silently ("monitors") while no sound device is
 * open, so the state below keeps moving; `caffeine_session_set_audio_output` opens the device.
 */
typedef struct CaffeineVolumetricState {
    int32_t enabled;         /**< an enabled `Volumetric Audio` component is in the scene */
    int32_t device_open;     /**< the mixer is playing on a sound device */
    uint32_t voices;         /**< emitters tracked (see caffeine_session_volumetric_voice) */
    uint32_t obstacles;      /**< `Acoustic Obstacle` boxes in the world */
    uint32_t clips_missing;  /**< emitters whose clip could not be found or decoded */
    float openness;          /**< the room round the listener: 0 shut in .. 1 open */
    float sky_exposure;      /**< 0 under a roof .. 1 open sky */
    float rt60_s;            /**< reverb tail */
    float wet;               /**< how much reverb the room adds */
    uint32_t echoes;         /**< distinct echoes from far surfaces */
    float time_s;            /**< the mixer's clock */
} CaffeineVolumetricState;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_volumetric_state(const CaffeineSession* session,
                                                                   CaffeineVolumetricState* out);

/** What the mixer decided for one emitter (v20). */
typedef struct CaffeineVolumetricVoice {
    uint32_t entity;
    int32_t layer;           /**< 0 World, 1 Ambient, 2 Music, 3 Ui */
    int32_t loaded;          /**< the clip was found */
    int32_t playing;
    float distance_m;
    float delay_s;           /**< the sound's travel time */
    float pitch_ratio;       /**< the Doppler shift: > 1 while approaching */
    float gain_db;           /**< direct path, after distance and what the walls keep */
    float cutoff_hz;         /**< the low-pass the air and the walls leave */
    int32_t blocked;
    int32_t diffracted;
    float dir_x, dir_y, dir_z;  /**< where it seems to come from, in the world */
} CaffeineVolumetricVoice;
CAFFEINE_CAPI_API CaffeineResult caffeine_session_volumetric_voice(const CaffeineSession* session, uint32_t index,
                                                                   CaffeineVolumetricVoice* out);

/** Opens (1) or closes (0) the sound device the volumetric mixer plays on. CAFFEINE_ERROR when there is none. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_audio_output(CaffeineSession* session, int32_t on);

/** How many sound devices can play (v20). */
CAFFEINE_CAPI_API int32_t caffeine_audio_output_device_count(void);
/** The name of device `index` (UTF-8, truncated to `capacity - 1`); CAFFEINE_INVALID_ARGUMENT for a bad index (v20). */
CAFFEINE_CAPI_API CaffeineResult caffeine_audio_output_device_name(int32_t index, char* buffer, size_t capacity);
/**
 * Chooses the device the volumetric mixer plays on: `index` into the list above, or -1 for the system default. When the
 * speakers are on the mixer moves there at once (v20).
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_audio_output_device(CaffeineSession* session, int32_t index);

/**
 * Mixes `frames` stereo frames (interleaved float) for the caller, for tests and captures. Only while no device is
 * open; `*written` is how many were mixed.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_volumetric_render(CaffeineSession* session, float* out_stereo,
                                                                    uint32_t frames, uint32_t* written);

/** Result summary of caffeine_micromesh_build (v12). */
typedef struct CaffeineMicromeshStats {
    uint32_t source_triangles;
    uint32_t clusters;
    uint32_t levels;
    uint32_t root_clusters;
    uint32_t max_cluster_triangles;
    float max_error;          /**< largest simplification error, in the mesh's units */
} CaffeineMicromeshStats;
/**
 * Builds the cluster hierarchy (Micromeshes) of an indexed triangle mesh: clusters of at most 128
 * triangles, simplified levels with crack-free cuts. `positions` is `vertex_count` x 3 floats.
 * `out_bytes` is the versioned `.micromesh` section (free it with caffeine_session_free).
 * Deterministic: the same mesh gives the same bytes. Offline work: do not call it per frame.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_micromesh_build(const float* positions,
                                                          uint32_t vertex_count,
                                                          const uint32_t* indices,
                                                          uint32_t index_count,
                                                          CaffeineMicromeshStats* out_stats,
                                                          uint8_t** out_bytes, size_t* out_size);

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
/**
 * Terrain editing strokes (terrain editing roadmap, phase 0). A brush is dragged over a terrain: the engine lays
 * stamps along the path a fixed number of metres apart (never one per frame, so the result does not depend on the
 * frame rate), rebuilds only the tiles of the mesh the stamps touched, and keeps what changed, tile by tile, so a
 * finished stroke can be undone and redone exactly. Units are real: metres for radius, spacing and heights.
 */
typedef enum CaffeineTerrainBrushMode {
    CAFFEINE_BRUSH_RAISE = 0,
    CAFFEINE_BRUSH_LOWER = 1,
    CAFFEINE_BRUSH_SMOOTH = 2,
    CAFFEINE_BRUSH_FLATTEN = 3,
    CAFFEINE_BRUSH_NOISE = 4,
    /* v28 */
    CAFFEINE_BRUSH_RAMP = 5,
    CAFFEINE_BRUSH_TERRACE = 6,
    CAFFEINE_BRUSH_PINCH = 7,
    CAFFEINE_BRUSH_BULGE = 8,
    CAFFEINE_BRUSH_STAMP_ABSOLUTE = 9,
    CAFFEINE_BRUSH_STAMP_MAX = 10,
    CAFFEINE_BRUSH_STAMP_MIN = 11,
    CAFFEINE_BRUSH_CLONE = 12,
    CAFFEINE_BRUSH_SMUDGE = 13,
    CAFFEINE_BRUSH_ERODE_THERMAL = 14,
    CAFFEINE_BRUSH_ERODE_HYDRAULIC = 15
} CaffeineTerrainBrushMode;

typedef enum CaffeineTerrainFalloff {
    CAFFEINE_FALLOFF_SMOOTH = 0,
    CAFFEINE_FALLOFF_LINEAR = 1,
    CAFFEINE_FALLOFF_SHARP = 2,
    CAFFEINE_FALLOFF_CONSTANT = 3
} CaffeineTerrainFalloff;

typedef struct CaffeineTerrainBrush {
    int32_t mode;               /* CaffeineTerrainBrushMode */
    float radius_m;
    float strength;             /* raise, lower, noise: metres per stamp; smooth, flatten: 0..1 per stamp */
    float spacing_m;            /* metres between stamps; 0 = a quarter of the radius */
    int32_t falloff;            /* CaffeineTerrainFalloff */
    float rotation_degrees;     /* of the stamp image */
    float target_height_m;      /* flatten: the height to go to; negative = the height under the first stamp */
    float pressure_to_strength; /* 0 ignores pen pressure, 1 follows it */
    uint64_t seed;              /* noise */
} CaffeineTerrainBrush;

typedef struct CaffeineTerrainStrokeResult {
    uint32_t record;            /* id for revert / reapply; 0 when the stroke changed nothing */
    uint32_t stamps;
    uint32_t tiles_changed;
    uint64_t bytes;             /* memory the undo record holds */
    int has_region;
    uint32_t region_min_x, region_min_z, region_max_x, region_max_z; /* height-grid vertices that changed (a tight rectangle) */
} CaffeineTerrainStrokeResult;

/**
 * The file format of a terrain's data (v26). `out_legacy` is 1 when the terrain was opened from an old `.cterrain` and
 * has not been saved as the native `.terrain` since: it still opens and edits as it is, and the editor offers to
 * convert it.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_data_legacy(const CaffeineSession* session,
                                                      CaffeineEntity entity, int* out_legacy);
/**
 * Saves a terrain as the native `.terrain` next to its legacy file, checks the file by reading it back bit for bit and
 * points the scene at it (the scene is marked changed; the legacy file stays). `out_path` (optional) receives the new
 * project-relative path. CAFFEINE_ERROR when the terrain has no data file, there is no project folder, or the file read
 * back is not identical.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_convert_native(CaffeineSession* session,
                                                         CaffeineEntity entity, char* out_path,
                                                         size_t out_path_size);

/**
 * Brushes as documents (v28). A `CaffeineBrushPreset` is everything a stroke needs besides the path: the mode and its
 * numbers, the falloff, variation per stamp, symmetry, protection masks and a stamp (a procedural one of the library, or a
 * project picture). The engine ships `caffeine_terrain_brush_count()` built-in brushes (one for each of the 42 procedural
 * stamps and a tool for every mode); `.brush` files are text (`caffeine_terrain_brush_parse` / `_format`).
 */
enum { CAFFEINE_BRUSH_PROTECT_MAX = 4 };
typedef enum CaffeineStampKind { CAFFEINE_STAMP_NONE = 0, CAFFEINE_STAMP_PROCEDURAL = 1, CAFFEINE_STAMP_IMAGE = 2 } CaffeineStampKind;
typedef enum CaffeineSymmetryKind {
    CAFFEINE_SYMMETRY_NONE = 0,
    CAFFEINE_SYMMETRY_MIRROR_X = 1,
    CAFFEINE_SYMMETRY_MIRROR_Z = 2,
    CAFFEINE_SYMMETRY_MIRROR_BOTH = 3,
    CAFFEINE_SYMMETRY_RADIAL = 4
} CaffeineSymmetryKind;
typedef struct CaffeineBrushProtect {
    char mask[32];  /* slope, height, curvature, or a stored mask */
    float rise0, rise1, fall1, fall0;
} CaffeineBrushProtect;
typedef struct CaffeineBrushPreset {
    char name[64];
    char category[32];
    int32_t stamp_kind;   /* CaffeineStampKind */
    char stamp[256];      /* a procedural stamp's id, or the project path of the picture */
    uint32_t stamp_size;
    int32_t mode;         /* CaffeineTerrainBrushMode, 0 to 15 */
    float radius_m, strength, spacing_m;
    int32_t falloff;      /* CaffeineTerrainFalloff */
    float rotation_degrees, target_height_m;
    uint64_t seed;
    int32_t signed_stamp; /* 0.5 of the stamp is neutral */
    float step_m, talus_degrees;
    uint32_t iterations, droplets;
    float offset_x, offset_z;
    float position_jitter, rotation_jitter_degrees, size_jitter, opacity_jitter;
    int32_t symmetry;     /* CaffeineSymmetryKind */
    uint32_t radial_count;
    float symmetry_center_x, symmetry_center_z;
    float pressure_to_strength;
    uint32_t protect_count;
    CaffeineBrushProtect protect[CAFFEINE_BRUSH_PROTECT_MAX];
} CaffeineBrushPreset;

CAFFEINE_CAPI_API uint32_t caffeine_terrain_brush_count(void);
CAFFEINE_CAPI_API CaffeineResult caffeine_terrain_brush_at(uint32_t index, CaffeineBrushPreset* out);
/** The 0..1 image of a procedural stamp (`size` x `size` floats, 4 to 1024, row-major): the brush grid's preview. */
CAFFEINE_CAPI_API CaffeineResult caffeine_terrain_brush_stamp(const char* procedural_id, uint32_t size, uint64_t seed,
                                              float* out_alpha);
/** Reads the text of a `.brush`; on a bad line returns CAFFEINE_INVALID_ARGUMENT and says which in `error`. */
CAFFEINE_CAPI_API CaffeineResult caffeine_terrain_brush_parse(const char* text, CaffeineBrushPreset* out, char* error,
                                              size_t error_size);
/** The `.brush` text of a preset (CAFFEINE_ERROR when `text_size` is too small). */
CAFFEINE_CAPI_API CaffeineResult caffeine_terrain_brush_format(const CaffeineBrushPreset* preset, char* text,
                                               size_t text_size);
/**
 * Starts a stroke with a preset. A picture stamp is read through the session's asset manager (a project path); a stamp
 * that cannot be made fails the call with CAFFEINE_ERROR. Protection masks name the terrain's built-in masks or the
 * masks stored on it. Like `caffeine_session_terrain_stroke_begin`, an open stroke is ended first.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_stroke_begin_preset(CaffeineSession* session,
                                                              CaffeineEntity entity,
                                                              const CaffeineBrushPreset* preset);

/**
 * Where a world-space ray meets a terrain (v27), in world space (metres): what a brush follows under the mouse.
 * CAFFEINE_NOT_FOUND when the ray misses the terrain.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_raycast(const CaffeineSession* session,
                                                  CaffeineEntity entity, const double origin[3],
                                                  const double direction[3], double out_hit[3]);
/**
 * The ring a brush of `radius_m` metres makes on the terrain surface around `center` (a world-space point, usually the
 * raycast hit): `segments` points (3 to 512) in world space, each on the terrain's height a few centimetres above it,
 * written as x, y, z triples to `out_xyz`. It follows the relief, so a viewport draws the brush as a decal that hugs the
 * ground.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_brush_outline(const CaffeineSession* session,
                                                        CaffeineEntity entity, const double center[3],
                                                        float radius_m, uint32_t segments, float* out_xyz);

/** Starts a stroke on a terrain entity. `stamp_alpha` (optional, `stamp_size` squared floats in 0..1) shapes the brush. A
 *  stroke that is still open is ended first. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_stroke_begin(CaffeineSession* session,
                                                       CaffeineEntity entity,
                                                       const CaffeineTerrainBrush* brush,
                                                       const float* stamp_alpha, uint32_t stamp_size);
/** Extends the open stroke to a point in world space (metres); `out_stamps` (optional) is how many stamps it laid. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_stroke_point(CaffeineSession* session, double x,
                                                       double y, double z, float pressure,
                                                       uint32_t* out_stamps);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_stroke_end(CaffeineSession* session,
                                                     CaffeineTerrainStrokeResult* out);
/** Undo and redo of a finished stroke by its record id. Records are kept within a memory budget, oldest dropped first. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_stroke_revert(CaffeineSession* session,
                                                        uint32_t record);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_stroke_reapply(CaffeineSession* session,
                                                         uint32_t record);
/**
 * Terrain layers (v29): a material palette of up to 64 layers (CAFFEINE_PALETTE_MAX) a terrain is painted with, and a
 * layer map naming up to four of them, with weights, at every splat texel. A terrain with layers off is painted with its
 * four splat layers as before. Painting is a stroke like sculpting (`..._stroke_point`, `_stroke_end`, `_revert`,
 * `_reapply` work for it unchanged); a record id may belong to a height stroke, a layer stroke or a palette edit, and
 * revert and reapply know which.
 */
#define CAFFEINE_PALETTE_MAX 64

typedef struct CaffeinePaletteLayer {
    char name[64];
    char albedo[256];           /* project path of the picture; empty draws the tint as a flat colour */
    char normal[256];           /* reserved: kept in the file, not drawn yet */
    float tile_size_m;          /* metres one repeat covers */
    float rotation_degrees;     /* turns the picture on the ground */
    float tint[3];
    uint32_t resolution;        /* 256..4096, a power of two (rounded to the nearest) */
    int32_t locked;             /* painting never changes the weight of a locked layer */
    uint32_t texels;            /* out only: how many texels use the layer */
} CaffeinePaletteLayer;

typedef enum CaffeinePaintMode {
    CAFFEINE_PAINT_PAINT = 0,    /* moves weight toward `layer` */
    CAFFEINE_PAINT_ERASE = 1,    /* takes `layer` away, the others fill in */
    CAFFEINE_PAINT_REPLACE = 2,  /* moves the weight of `from_layer` to `layer` */
    CAFFEINE_PAINT_SMOOTH = 3    /* blends each texel toward its neighbours */
} CaffeinePaintMode;

typedef struct CaffeinePaintBrush {
    CaffeineBrushPreset shape;   /* radius, strength (share per stamp), spacing, falloff, stamp, jitter, symmetry, protection */
    int32_t mode;                /* CaffeinePaintMode */
    uint32_t layer;
    uint32_t from_layer;
} CaffeinePaintBrush;

typedef struct CaffeineTerrainLayers {
    int32_t enabled;
    uint32_t palette_count;
    uint32_t map_x, map_z;       /* texels of the layer map */
    uint32_t gpu_slices;         /* array slices on the GPU (the layers in use) */
    uint32_t gpu_array_size;     /* pixels a side of every slice (lowered to fit the budget) */
    uint64_t gpu_array_bytes;    /* VRAM the array takes */
} CaffeineTerrainLayers;

typedef struct CaffeineLayerSample {
    uint32_t layer[4];
    float weight[4];             /* add up to 1 */
} CaffeineLayerSample;

/** Layered painting on or off for a terrain. On: the palette is the terrain's four layers and the map its splat map, unless it has them already. Off: the splat map is rebuilt from layers 0 to 3. Marks the scene changed. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_layers_enable(CaffeineSession* session, CaffeineEntity entity,
                                                                        int enabled);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_layers(const CaffeineSession* session, CaffeineEntity entity,
                                                                 CaffeineTerrainLayers* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_palette_layer(const CaffeineSession* session, CaffeineEntity entity,
                                                                        uint32_t index, CaffeinePaletteLayer* out);
/** The palette as `.terrainpalette` text (`out` may be null to ask for `*needed`, which counts the terminating zero). */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_palette_text(const CaffeineSession* session, CaffeineEntity entity,
                                                                       char* out, size_t capacity, size_t* needed);
/**
 * Replaces the palette by the layers of `.terrainpalette` text, as one undoable edit (`out_record`, 0 if nothing changed).
 * The text must hold at least as many layers as the palette has (a layer is removed with `_palette_remove`, which
 * renumbers the map); extra layers are appended. `error` (optional) receives the reason with its line number.
 * `merge_into` (0 for none) is the record of an earlier palette edit to fold this one into instead of making a new
 * record, so dragging a slider is one undo step: it works only when that record is a palette edit of this terrain; the
 * caller says when it is still the latest step.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_palette_apply(CaffeineSession* session, CaffeineEntity entity,
                                                                        const char* text, uint32_t merge_into,
                                                                        uint32_t* out_record, char* error, size_t error_size);
/** Removes a layer: its weight goes to the other layers of each texel (layer 0 when it stood alone) and the layers after it move down. Undoable. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_palette_remove(CaffeineSession* session, CaffeineEntity entity,
                                                                         uint32_t index, uint32_t* out_record);
/** Moves a layer to another position; the map is renumbered with it. Undoable. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_palette_move(CaffeineSession* session, CaffeineEntity entity,
                                                                       uint32_t from, uint32_t to, uint32_t* out_record);
/** Starts a paint stroke (an open stroke is ended first). Points go through `caffeine_session_terrain_stroke_point` (world space, metres) and it ends with `_stroke_end`. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_paint_begin(CaffeineSession* session, CaffeineEntity entity,
                                                                      const CaffeinePaintBrush* brush);
/**
 * Fills the layers from rules on slope, height and masks: `rules` is the biome-table text (`class 2 base 0.9` and indented
 * `slope 0 0.1 0.3 0.4` terms) where the class number is the palette layer. A texel no rule claims takes `fallback`.
 * `region` (optional, four texel coordinates min_x, min_z, max_x, max_z) limits it. One undoable edit.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_autolayer(CaffeineSession* session, CaffeineEntity entity,
                                                                    const char* rules, uint32_t fallback,
                                                                    const uint32_t* region, uint32_t* out_record,
                                                                    char* error, size_t error_size);
/** The layers under a world-space point (metres): what an eyedropper reads. CAFFEINE_NOT_FOUND off the terrain or with layers off. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_layer_at(const CaffeineSession* session, CaffeineEntity entity,
                                                                   double x, double y, double z, CaffeineLayerSample* out);

/** Terrain height in metres at (x, z) metres from the terrain's corner. The mesh is centred on its entity, so the corner is at the entity position minus half the size. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_height(const CaffeineSession* session,
                                                                 CaffeineEntity entity, float x,
                                                                 float z, float* out_height);

/**
 * Terrain masks (v15): named 0..1 fields over a terrain, for placing things by what the terrain is (steep, high,
 * wet). Coordinates are metres from the terrain's corner, like `caffeine_session_terrain_height`; a point
 * outside the terrain reads the nearest edge.
 *
 * Built-in masks the engine derives from the heightfield (nothing stored): "height" (normalised height),
 * "slope" (angle from the horizontal, 0 flat .. 1 = 90 degrees) and "curvature" (0.5 on flat ground, towards 1 in
 * a valley, towards 0 on a ridge; +-0.125 per metre is the full range). Stored masks (up to 16) come from a
 * generator: 8 bits per texel on a grid of their own (any size >= 2 x 2), sampled bilinearly. Names are 1 to 31
 * letters, digits, '_' or '-'. Stored masks are saved with the terrain file.
 */
#define CAFFEINE_TERRAIN_MASK_NAME_MAX 32
typedef struct CaffeineTerrainMaskInfo {
    char name[CAFFEINE_TERRAIN_MASK_NAME_MAX];
    uint32_t resolution_x;  /**< 0 for a built-in mask */
    uint32_t resolution_z;
    int builtin;
} CaffeineTerrainMaskInfo;

/** Stores or replaces a mask; `data` NULL removes a stored one. `data` holds resolution_x * resolution_z bytes, row-major, z rows. INVALID_ARGUMENT for a bad or built-in name, a grid under 2 x 2 or a 17th mask. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_set_terrain_mask(CaffeineSession* session,
                                                                   CaffeineEntity entity, const char* name,
                                                                   uint32_t resolution_x,
                                                                   uint32_t resolution_z,
                                                                   const uint8_t* data);
/** Masks of a terrain: the three built-ins first, then the stored ones in the order they were first set. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_mask_count(const CaffeineSession* session,
                                                                     CaffeineEntity entity, uint32_t* out);
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_mask_at(const CaffeineSession* session,
                                                                  CaffeineEntity entity, uint32_t index,
                                                                  CaffeineTerrainMaskInfo* out);
/** One mask at (x, z): 0..1. NOT_FOUND for an unknown mask. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_mask(const CaffeineSession* session,
                                                               CaffeineEntity entity, const char* name, float x,
                                                               float z, float* out_value);
/** A rectangle (x0, z0)-(x1, z1) in metres resampled to width x height bytes (texel centres, row 0 at z0), `out` holds width * height bytes. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_mask_rect(const CaffeineSession* session,
                                                                    CaffeineEntity entity, const char* name,
                                                                    float x0, float z0, float x1, float z1,
                                                                    uint32_t width, uint32_t height,
                                                                    uint8_t* out);
/** The unit surface normal (x, y, z) at (x, z) metres from the corner. */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_terrain_normal(const CaffeineSession* session,
                                                                 CaffeineEntity entity, float x, float z,
                                                                 float out_normal[3]);

/**
 * Scatter on a terrain (v16): deterministic placement of instances (trees, rocks, grass) by what the terrain is. The
 * engine computes WHERE; the caller creates the entities (Doppio's Scatter action does).
 *
 * Each class is a kind of thing with a spacing and an exclusion radius; a candidate per grid cell is kept with a
 * chance of `density` times the biome table's density at that point, and a point yields to any closer, higher
 * priority one, so nothing overlaps, also across the chunks the work is cut into. The result is a pure function of
 * (seed, terrain, masks, table, classes): the same call gives the same instances on any machine, for any region
 * split, in the same order.
 */
typedef struct CaffeineScatterClass {
    uint32_t id;
    int32_t priority;       /**< higher wins a conflict (rocks, then trees, then grass) */
    float cell_size;        /**< candidate grid spacing in metres */
    float radius;           /**< exclusion radius in metres */
    float density;          /**< 0..1 chance, times the biome density */
    float min_scale;
    float max_scale;
    float tilt_to_normal;   /**< 0 keeps up, 1 follows the terrain normal */
    float sink;             /**< metres the base sinks, per unit of scale */
    float tint_jitter;      /**< the tint is 1 +- this, per channel */
    uint32_t variants;      /**< baked variants to choose from (>= 1) */
} CaffeineScatterClass;

typedef struct CaffeineScatterInstance {
    uint32_t class_id;
    uint32_t variant;
    float position[3];          /**< scene space: the terrain entity's transform applied */
    float up[3];                /**< unit up axis (terrain normal blended with world up) */
    float rotation_degrees[3];  /**< the same orientation, with the yaw, as Euler degrees for set_entity_rotation */
    float scale;
    float tint[3];
    uint64_t seed;              /**< for per-instance variation (wind phase, a recipe seed) */
} CaffeineScatterInstance;

/**
 * Scatters over the terrain of `entity`. `biome_text` is the biome table (see terrain/TerrainBiome.hpp; NULL or "" for
 * none). `region` is (x0, z0, x1, z1) in metres from the terrain's corner, or NULL for the whole terrain. At most
 * `capacity` instances are written to `out` (which may be NULL to only count); `out_total` receives how many the region
 * holds, up to 200000. INVALID_ARGUMENT for a biome table that does not parse (see caffeine_biome_check), no classes
 * or a class with cell_size or radius <= 0; NOT_FOUND without a terrain. The terrain entity is assumed not rotated.
 */
CAFFEINE_CAPI_API CaffeineResult caffeine_session_scatter_terrain(const CaffeineSession* session,
                                                                  CaffeineEntity entity, uint64_t seed,
                                                                  const CaffeineScatterClass* classes,
                                                                  uint32_t class_count,
                                                                  const char* biome_text,
                                                                  const float* region,
                                                                  CaffeineScatterInstance* out,
                                                                  uint32_t capacity, uint32_t* out_total);
/** Parses a biome table without a session: OK, or INVALID_ARGUMENT with the reason (line number included) in `error`. */
CAFFEINE_CAPI_API CaffeineResult caffeine_biome_check(const char* biome_text, char* error,
                                                      uint32_t error_size);

/** Advance frame bookkeeping; systems will be wired here incrementally. */
CAFFEINE_CAPI_API void caffeine_session_tick(CaffeineSession* session, float delta_time);
/**
 * Whether the scene is simulating (v21). Off, a rendered frame stands still: clouds, weather, wind on foliage and
 * water do not advance and the scene's sound is not driven; on, every frame advances them by 1/60 s. A new session
 * simulates (the previous behaviour); an editor turns it off and switches it on for its Play mode.
 */
CAFFEINE_CAPI_API void caffeine_session_set_simulating(CaffeineSession* session, int32_t simulating);
CAFFEINE_CAPI_API int32_t caffeine_session_simulating(const CaffeineSession* session);
CAFFEINE_CAPI_API void caffeine_session_frame_stats(const CaffeineSession* session, CaffeineFrameStats* out);

#ifdef __cplusplus
}
#endif
