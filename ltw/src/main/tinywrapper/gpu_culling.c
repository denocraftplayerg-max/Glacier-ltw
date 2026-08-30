#include "gpu_culling.h"
#include "proc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DRAWS 4096

static struct {
    GLuint metadataSSBO;
    GLuint commandSSBO;
    GLuint computeShader;
    GLuint indexBuffer;
    
    DrawMetadata* metadata;
    int numDraws;
    bool initialized;
} gpu = {0};

/* Compute shader embutido (GLSL ES 3.10) */
static const char* computeShaderSource = R"(#version 310 es
layout(local_size_x = 64) in;

struct DrawMetadata {
    vec4 position;        // xyz = posição, w = padding
    uint indexCount;
    uint baseVertex;
    uint firstIndex;
    uint visible;
};

struct IndirectCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

layout(std430, binding = 0) readonly buffer InMetadata {
    DrawMetadata draws[];
};

layout(std430, binding = 1) buffer OutCommand {
    IndirectCommand cmd;
};

uniform vec4 u_FrustumPlanes[6];
uniform int u_NumDraws;

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= uint(u_NumDraws)) return;
    
    DrawMetadata draw = draws[id];
    if (draw.indexCount == 0u) return;
    
    // AABB do draw (assumindo tamanho padrão de chunk: 16x16x16)
    vec3 minBox = draw.position.xyz;
    vec3 maxBox = draw.position.xyz + vec3(16.0, 16.0, 16.0);
    
    // Frustum culling: testa contra os 6 planos
    bool visible = true;
    for (int i = 0; i < 6; i++) {
        vec4 plane = u_FrustumPlanes[i];
        vec3 p = vec3(
            plane.x >= 0.0 ? maxBox.x : minBox.x,
            plane.y >= 0.0 ? maxBox.y : minBox.y,
            plane.z >= 0.0 ? maxBox.z : minBox.z
        );
        if (dot(plane.xyz, p) + plane.w < 0.0) {
            visible = false;
            break;
        }
    }
    
    if (visible) {
        // Operação atómica para adicionar índices ao comando indirect
        atomicAdd(cmd.count, draw.indexCount);
    }
}
)";

static GLuint compileComputeShader(void) {
    GLuint shader = es3_functions.glCreateShader(GL_COMPUTE_SHADER);
    es3_functions.glShaderSource(shader, 1, &computeShaderSource, NULL);
    es3_functions.glCompileShader(shader);
    
    GLint status;
    es3_functions.glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[512];
        es3_functions.glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        printf("LTWGPU: Compute shader compile error: %s\n", log);
        es3_functions.glDeleteShader(shader);
        return 0;
    }
    
    GLuint program = es3_functions.glCreateProgram();
    es3_functions.glAttachShader(program, shader);
    es3_functions.glLinkProgram(program);
    es3_functions.glDeleteShader(shader);
    
    es3_functions.glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char log[512];
        es3_functions.glGetProgramInfoLog(program, sizeof(log), NULL, log);
        printf("LTWGPU: Compute program link error: %s\n", log);
        es3_functions.glDeleteProgram(program);
        return 0;
    }
    
    return program;
}

void gpu_culling_init(void) {
    if (gpu.initialized) return;
    
    // Alocar SSBO de metadados (persistente, sem resize)
    es3_functions.glGenBuffers(1, &gpu.metadataSSBO);
    es3_functions.glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu.metadataSSBO);
    es3_functions.glBufferData(GL_SHADER_STORAGE_BUFFER, 
                               MAX_DRAWS * sizeof(DrawMetadata), 
                               NULL, GL_DYNAMIC_DRAW);
    
    // Mapear persistente para escrita direta da CPU
    gpu.metadata = (DrawMetadata*)es3_functions.glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER, 0, 
        MAX_DRAWS * sizeof(DrawMetadata),
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT
    );
    
    // Alocar SSBO de comando indirect
    es3_functions.glGenBuffers(1, &gpu.commandSSBO);
    es3_functions.glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu.commandSSBO);
    es3_functions.glBufferData(GL_SHADER_STORAGE_BUFFER, 
                               sizeof(IndirectCommand), NULL, GL_DYNAMIC_DRAW);
    
    // Também usar como DRAW_INDIRECT_BUFFER
    es3_functions.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, gpu.commandSSBO);
    
    // Compilar compute shader
    gpu.computeShader = compileComputeShader();
    if (gpu.computeShader == 0) {
        printf("LTWGPU: Failed to initialize GPU culling system\n");
        return;
    }
    
    gpu.numDraws = 0;
    gpu.initialized = true;
    printf("LTWGPU: GPU-driven culling system initialized (max %d draws)\n", MAX_DRAWS);
}

void gpu_culling_register_draw(float x, float y, float z,
                               uint32_t indexCount, uint32_t baseVertex,
                               uint32_t firstIndex) {
    if (!gpu.initialized || gpu.numDraws >= MAX_DRAWS) return;
    
    DrawMetadata* meta = &gpu.metadata[gpu.numDraws];
    meta->posX = x;
    meta->posY = y;
    meta->posZ = z;
    meta->indexCount = indexCount;
    meta->baseVertex = baseVertex;
    meta->firstIndex = firstIndex;
    meta->visible = 0;
    
    gpu.numDraws++;
}

int gpu_culling_execute(const float* frustumPlanes, int numPlanes) {
    if (!gpu.initialized || gpu.numDraws == 0) return 0;
    
    // Resetar comando indirect
    IndirectCommand initCmd = {0, 1, 0, 0, 0};
    es3_functions.glBindBuffer(GL_SHADER_STORAGE_BUFFER, gpu.commandSSBO);
    es3_functions.glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 
                                  sizeof(IndirectCommand), &initCmd);
    
    // Executar compute shader
    es3_functions.glUseProgram(gpu.computeShader);
    
    // Bind SSBOs
    es3_functions.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gpu.metadataSSBO);
    es3_functions.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gpu.commandSSBO);
    
    // Passar uniformes
    GLint planesLoc = es3_functions.glGetUniformLocation(gpu.computeShader, "u_FrustumPlanes");
    GLint numDrawsLoc = es3_functions.glGetUniformLocation(gpu.computeShader, "u_NumDraws");
    
    es3_functions.glUniform4fv(planesLoc, numPlanes, frustumPlanes);
    es3_functions.glUniform1i(numDrawsLoc, gpu.numDraws);
    
    // Dispatch: 64 threads por workgroup
    int numGroups = (gpu.numDraws + 63) / 64;
    es3_functions.glDispatchCompute(numGroups, 1, 1);
    
    // Barreira de memória: garantir que o comando indirect está pronto
    es3_functions.glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    
    return gpu.numDraws;
}

GLuint gpu_culling_get_indirect_buffer(void) {
    return gpu.commandSSBO;
}

void gpu_culling_clear(void) {
    gpu.numDraws = 0;
}
