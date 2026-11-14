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

#ifdef __cplusplus
extern "C" {
#endif

#define CAFFEINE_CAPI_VERSION 1
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

/** Create/destroy a scene session. The session owns its ECS world. */
CaffeineSession* caffeine_session_create(void);
void caffeine_session_destroy(CaffeineSession* session);

/** Load a .caf scene. Returns CAFFEINE_ERROR when the file cannot be read. */
CaffeineResult caffeine_session_load_scene(CaffeineSession* session, const char* path);
/** Current scene path. Valid until the next load or session destruction. */
const char* caffeine_session_scene_path(const CaffeineSession* session);
/** 1 when the session has unsaved edits. */
int caffeine_session_is_dirty(const CaffeineSession* session);
/**
 * Render the scene offscreen to RGBA8 pixels. `pixels` must hold at least
 * width * height * 4 bytes. This is the CPU-readback viewport path; GPU
 * shared-texture interop will replace it later.
 */
CaffeineResult caffeine_session_render(CaffeineSession* session, uint32_t width,
                                       uint32_t height, uint8_t* pixels,
                                       size_t pixel_bytes);
/** Viewport camera (radians for yaw/pitch, world units for distance). */
CaffeineResult caffeine_session_set_camera(CaffeineSession* session, CaffeineCamera camera);
CaffeineResult caffeine_session_get_camera(const CaffeineSession* session, CaffeineCamera* out);
/** Write the current world to a .caf scene and clear the dirty flag. */
CaffeineResult caffeine_session_save_scene(CaffeineSession* session, const char* path);

/** Hierarchy queries. The index is stable within one call sequence; re-query after edits. */
uint32_t caffeine_session_entity_count(const CaffeineSession* session);
CaffeineResult caffeine_session_entity_at(const CaffeineSession* session, uint32_t index,
                                          CaffeineEntityInfo* out);
CaffeineResult caffeine_session_entity_name(const CaffeineSession* session,
                                            CaffeineEntity entity, char* buffer,
                                            size_t buffer_size);

/** Commands. */
CaffeineEntity caffeine_session_create_entity(CaffeineSession* session, const char* name);
CaffeineResult caffeine_session_destroy_entity(CaffeineSession* session, CaffeineEntity entity);
CaffeineResult caffeine_session_set_entity_name(CaffeineSession* session, CaffeineEntity entity,
                                                const char* name);
CaffeineResult caffeine_session_select_entity(CaffeineSession* session, CaffeineEntity entity);
CaffeineEntity caffeine_session_selected_entity(const CaffeineSession* session);

/** Transform helpers. Setting a position creates a Transform when the entity has none. */
CaffeineResult caffeine_session_entity_position(const CaffeineSession* session,
                                                CaffeineEntity entity, CaffeineVec3* out);
CaffeineResult caffeine_session_set_entity_position(CaffeineSession* session,
                                                    CaffeineEntity entity, CaffeineVec3 position);
/** Euler rotation in degrees. */
CaffeineResult caffeine_session_entity_rotation(const CaffeineSession* session,
                                                CaffeineEntity entity, CaffeineVec3* out);
CaffeineResult caffeine_session_set_entity_rotation(CaffeineSession* session,
                                                    CaffeineEntity entity,
                                                    CaffeineVec3 euler_degrees);
CaffeineResult caffeine_session_entity_scale(const CaffeineSession* session,
                                             CaffeineEntity entity, CaffeineVec3* out);
CaffeineResult caffeine_session_set_entity_scale(CaffeineSession* session,
                                                 CaffeineEntity entity, CaffeineVec3 scale);

/** Advance frame bookkeeping; systems will be wired here incrementally. */
void caffeine_session_tick(CaffeineSession* session, float delta_time);
void caffeine_session_frame_stats(const CaffeineSession* session, CaffeineFrameStats* out);

#ifdef __cplusplus
}
#endif
