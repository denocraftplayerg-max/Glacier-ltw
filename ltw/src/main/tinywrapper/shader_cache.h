#ifndef LTW_SHADER_CACHE_H
#define LTW_SHADER_CACHE_H

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

#endif
