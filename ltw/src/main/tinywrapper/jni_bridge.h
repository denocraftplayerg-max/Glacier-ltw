#ifndef LTW_JNI_BRIDGE_H
#define LTW_JNI_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

/* API pública para mod Java chamar via JNI */

/* Regista a posição de um chunk/draw no mundo */
void ltw_register_chunk_position(uint32_t baseVertex, float x, float y, float z);

/* Limpa todas as posições registadas (chamar no início de cada frame) */
void ltw_clear_chunk_positions(void);

/* Atualiza os 6 frustum planes da câmara (override do método automático) */
void ltw_update_frustum_planes(const float* planes, int numPlanes);

/* Verifica se há frustum planes disponíveis */
bool ltw_has_frustum_planes(void);

/* Obtém posição do chunk por baseVertex (retorna false se não encontrado) */
bool ltw_get_chunk_position(uint32_t baseVertex, float* outX, float* outY, float* outZ);

#endif
