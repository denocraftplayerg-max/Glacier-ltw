#ifndef LTW_SHADER_CACHE_H
#define LTW_SHADER_CACHE_H

#include <stdbool.h>
#include <GLES3/gl3.h>

void shader_cache_init(void);

void shader_cache_compute_key(char *out_key,
                              const char *source,
                              unsigned int shader_type,
                              int glsl_version);

char *shader_cache_lookup(const char *key);

void shader_cache_store(const char *key,
                        const char *essl_source,
                        const char *glsl_original);

int shader_cache_is_enabled(void);

bool shader_cache_load_binary(const char* key, GLenum* out_format, void** out_binary, GLint* out_length);
void shader_cache_save_binary(const char* key, GLenum format, const void* binary, GLint length);
void shader_cache_invalidate_binary(const char* key);

#endif
