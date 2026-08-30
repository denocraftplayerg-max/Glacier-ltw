/**
 * On-disk shader cache for LTW.
 * Caches converted ESSL sources to avoid re-running the
 * shaderconv + glsl_optimizer pipeline on every load.
 */
#ifndef LTW_SHADER_CACHE_H
#define LTW_SHADER_CACHE_H

#include <stdbool.h>

/* Called automatically via constructor. Can also be called manually. */
void shader_cache_init(void);

/*
 * Compute a 128-bit FNV-1a cache key from the original GLSL source,
 * shader stage and target ESSL version.
 * out_key must be at least 64 bytes.
 */
void shader_cache_compute_key(char *out_key,
                              const char *source,
                              unsigned int shader_type,
                              int glsl_version);

/*
 * Look up a cached ESSL source on disk.
 * Returns a malloc'd string (caller must free) or NULL on miss.
 */
char *shader_cache_lookup(const char *key);

/* Write an ESSL source into the disk cache. */
void shader_cache_store(const char *key, const char *essl_source);

/* Query whether the cache is active. */
bool shader_cache_is_enabled(void);

#endif /* LTW_SHADER_CACHE_H */
