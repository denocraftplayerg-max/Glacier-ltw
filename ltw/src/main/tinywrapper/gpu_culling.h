#ifndef LTW_GPU_CULLING_H
#define LTW_GPU_CULLING_H

#include <GLES3/gl31.h>
#include <stdbool.h>

/* Estrutura de metadados por draw call (std430 layout) */
typedef struct {
    float posX, posY, posZ;        /* Posição base do draw */
    uint32_t indexCount;           /* Número de índices */
    uint32_t baseVertex;           /* Offset de vértice */
    uint32_t firstIndex;           /* Offset no index buffer */
    uint32_t visible;              /* Flag de visibilidade (atualizado pelo compute) */
} DrawMetadata;

/* Comando indirect final (std430) */
typedef struct {
    uint32_t count;                /* Total de índices a desenhar */
    uint32_t instanceCount;        /* Sempre 1 */
    uint32_t firstIndex;           /* Sempre 0 */
    uint32_t baseVertex;           /* Sempre 0 */
    uint32_t baseInstance;         /* Sempre 0 */
} IndirectCommand;

/* Inicializa buffers persistentes e compila shaders */
void gpu_culling_init(void);

/* Regista um draw call no sistema de culling */
void gpu_culling_register_draw(float x, float y, float z,
                               uint32_t indexCount, uint32_t baseVertex,
                               uint32_t firstIndex);

/* Executa compute shader e retorna número de draws visíveis */
int gpu_culling_execute(const float* frustumPlanes, int numPlanes);

/* Obtém o buffer de comando indirect para usar em glDrawElementsIndirect */
GLuint gpu_culling_get_indirect_buffer(void);

/* Limpa todos os draws registados (chamar no início de cada frame) */
void gpu_culling_clear(void);

#endif
