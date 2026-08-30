#include "jni_bridge.h"
#include "unordered_map/unordered_map.h"
#include <string.h>
#include <stdio.h>

#define MAX_CHUNKS 8192
#define MAX_FRUSTUM_PLANES 6

static struct {
    unordered_map* chunkPositions;
    float frustumPlanes[MAX_FRUSTUM_PLANES * 4];
    int numFrustumPlanes;
    bool hasPlanes;
} bridge = {0};

void ltw_register_chunk_position(uint32_t baseVertex, float x, float y, float z) {
    if (!bridge.chunkPositions) {
        bridge.chunkPositions = alloc_intmap(MAX_CHUNKS);
        if (!bridge.chunkPositions) {
            printf("LTWJNI: Failed to allocate chunk position map\n");
            return;
        }
    }
    
    float* pos = malloc(sizeof(float) * 3);
    if (!pos) return;
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    
    void* old = unordered_map_put(bridge.chunkPositions, (void*)(uintptr_t)baseVertex, pos);
    if (old) free(old);
}

void ltw_clear_chunk_positions(void) {
    if (bridge.chunkPositions) {
        unordered_map_free(bridge.chunkPositions);
        bridge.chunkPositions = NULL;
    }
}

void ltw_update_frustum_planes(const float* planes, int numPlanes) {
    if (numPlanes > MAX_FRUSTUM_PLANES) numPlanes = MAX_FRUSTUM_PLANES;
    memcpy(bridge.frustumPlanes, planes, numPlanes * 4 * sizeof(float));
    bridge.numFrustumPlanes = numPlanes;
    bridge.hasPlanes = true;
}

bool ltw_has_frustum_planes(void) {
    return bridge.hasPlanes;
}

bool ltw_get_chunk_position(uint32_t baseVertex, float* outX, float* outY, float* outZ) {
    if (!bridge.chunkPositions) return false;
    float* pos = unordered_map_get(bridge.chunkPositions, (void*)(uintptr_t)baseVertex);
    if (!pos) return false;
    *outX = pos[0];
    *outY = pos[1];
    *outZ = pos[2];
    return true;
}
