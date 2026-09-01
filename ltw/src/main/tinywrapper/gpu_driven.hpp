#pragma once
#include <GLES3/gl32.h>
#include <vector>
#include <cstdint>

struct GpuDrawCommand {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint  baseVertex;
    GLuint baseInstance; // ignorado no GLES, reservado
};

struct ObjectData {
    float modelMatrix[16];
    float aabbMin[4];
    float aabbMax[4];
    GLuint chunkX, chunkY, chunkZ, lod;
};

class GpuDrivenRenderer {
public:
    GLuint computeProgram   = 0;
    GLuint objectSSBO       = 0;
    GLuint drawCmdSSBO      = 0;
    GLuint visibilitySSBO   = 0;
    GLuint drawCountBuffer  = 0;

    int maxObjects = 0;

    void init(int maxObj);
    void uploadObjects(const std::vector<ObjectData>& objects);
    void runCulling(const float frustumPlanes[24]); // 6 planos * 4 floats
    void destroy();

private:
    static const char* CULL_SHADER_SRC;
};
