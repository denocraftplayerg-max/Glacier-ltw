#pragma once
#include <stdint.h>

#define LTW_API __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle
LTW_API void ltw_bridge_init();

// Frustum Culling (24 floats = 6 planes * 4 components)
LTW_API void ltw_update_frustum(const float* planes_24);

// Chunk Mapping (BaseVertex -> World Space)
LTW_API void ltw_clear_chunks();
LTW_API void ltw_register_chunk(int32_t baseVertex, float x, float y, float z);

// ASTC Injection
LTW_API int32_t ltw_inject_astc(const char* resource_path, uint32_t gl_id);

// Entity Snapshots
LTW_API void ltw_submit_entity_snapshot(const void* buffer, int32_t count);

// Light Engine & Shadow Integration
LTW_API void ltw_upload_sun_state(float sunX, float sunY, float sunZ, float timeOfDay, float camX, float camY, float camZ);
LTW_API void ltw_upload_lights(const void* lightBuffer, int32_t count);
LTW_API void ltw_set_foveated_config(int32_t enabled, float innerR, float middleR);

#ifdef __cplusplus
}
#endif
