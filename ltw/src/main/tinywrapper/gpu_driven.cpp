#include "gpu_driven.hpp"
#include <cstring>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "HYENGRA", __VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

// Compute shader GLSL ES 3.20 — frustum culling + geração de DrawCommand
const char* GpuDrivenRenderer::CULL_SHADER_SRC = R"GLSL(
#version 320 es
layout(local_size_x = 64) in;

struct ObjectData {
    mat4  modelMatrix;
    vec4  aabbMin;
    vec4  aabbMax;
    uvec4 chunkLod; // x,y,z,lod
};

struct DrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

layout(std430, binding = 0) readonly  buffer Objects   { ObjectData objects[]; };
layout(std430, binding = 1) writeonly buffer DrawCmds  { DrawCommand cmds[]; };
layout(std430, binding = 2) writeonly buffer Visibility{ uint visible[]; };
layout(std430, binding = 3) buffer    Counter          { uint drawCount; };

uniform vec4 frustumPlanes[6];
uniform uint objectCount;

// AABB vs frustum — totalmente em GPU
bool isVisible(vec3 bMin, vec3 bMax) {
    for (int i = 0; i < 6; i++) {
        vec3 n = frustumPlanes[i].xyz;
        float d = frustumPlanes[i].w;
        vec3 p = mix(bMin, bMax, greaterThanEqual(n, vec3(0.0)));
        if (dot(n, p) + d < 0.0) return false;
    }
    return true;
}

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= objectCount) return;

    ObjectData obj = objects[id];

    // Transforma AABB para world space
    vec3 bMin = obj.aabbMin.xyz;
    vec3 bMax = obj.aabbMax.xyz;

    vec3 worldMin = (obj.modelMatrix * vec4(bMin, 1.0)).xyz;
    vec3 worldMax = (obj.modelMatrix * vec4(bMax, 1.0)).xyz;
    vec3 wMin = min(worldMin, worldMax);
    vec3 wMax = max(worldMin, worldMax);

    if (isVisible(wMin, wMax)) {
        visible[id] = 1u;
        uint slot = atomicAdd(drawCount, 1u);
        cmds[slot].count         = 36u;
        cmds[slot].instanceCount = 1u;
        cmds[slot].firstIndex    = id * 36u;
        cmds[slot].baseVertex    = 0;
        cmds[slot].baseInstance  = id;
    } else {
        visible[id] = 0u;
    }
}
)GLSL";

static GLuint compileCompute(const char* src) {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048]; glGetShaderInfoLog(sh, 2048, nullptr, buf);
        LOG("COMPUTE SHADER ERROR: %s\n", buf);
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, sh);
    glLinkProgram(prog);
    glDeleteShader(sh);
    return prog;
}

void GpuDrivenRenderer::init(int maxObj) {
    maxObjects = maxObj;
    computeProgram = compileCompute(CULL_SHADER_SRC);

    // Objects SSBO
    glGenBuffers(1, &objectSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, objectSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        maxObj * sizeof(ObjectData), nullptr, GL_DYNAMIC_DRAW);

    // DrawCommands SSBO
    glGenBuffers(1, &drawCmdSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawCmdSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        maxObj * sizeof(GpuDrawCommand), nullptr, GL_DYNAMIC_DRAW);

    // Visibility SSBO
    glGenBuffers(1, &visibilitySSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, visibilitySSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        maxObj * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

    // Draw Count buffer
    glGenBuffers(1, &drawCountBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawCountBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    LOG("GpuDrivenRenderer: init OK, maxObjects=%d\n", maxObj);
}

void GpuDrivenRenderer::uploadObjects(const std::vector<ObjectData>& objects) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, objectSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        objects.size() * sizeof(ObjectData), objects.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void GpuDrivenRenderer::runCulling(const float frustumPlanes[24]) {
    GLuint zero = 0;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawCountBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint), &zero);

    glUseProgram(computeProgram);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, objectSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, drawCmdSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, visibilitySSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, drawCountBuffer);

    GLint loc = glGetUniformLocation(computeProgram, "frustumPlanes");
    glUniform4fv(loc, 6, frustumPlanes);

    GLuint cnt = (GLuint)maxObjects;
    glUniform1ui(glGetUniformLocation(computeProgram, "objectCount"), cnt);

    GLuint groups = (cnt + 63) / 64;
    glDispatchCompute(groups, 1, 1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
}

void GpuDrivenRenderer::destroy() {
    glDeleteProgram(computeProgram);
    glDeleteBuffers(1, &objectSSBO);
    glDeleteBuffers(1, &drawCmdSSBO);
    glDeleteBuffers(1, &visibilitySSBO);
    glDeleteBuffers(1, &drawCountBuffer);
}
