/**
 * On-disk shader cache for LTW.
 *
 * Cache directory resolution order:
 *   1. $LTW_SHADER_CACHE_DIR  (if set)
 *   2. /data/local/tmp/ltw_shader_cache  (fallback)
 *
 * Each entry is a flat file named  <fnv128>.essl  containing the
 * converted ESSL source text.
 */
#include "shader_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/* ── state ─────────────────────────────────────────────── */
static char cache_dir[512]  = {0};
static bool cache_enabled   = false;
static bool cache_inited    = false;

/* ── helpers ───────────────────────────────────────────── */
static void mkdirs(const char *path) {
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ── public API ────────────────────────────────────────── */
__attribute__((constructor))
void shader_cache_init(void) {
    if (cache_inited) return;
    cache_inited = true;

    const char *dir = getenv("LTW_SHADER_CACHE_DIR");
    if (dir == NULL || dir[0] == '\0') {
        dir = "/data/local/tmp/ltw_shader_cache";
    }
    strncpy(cache_dir, dir, sizeof(cache_dir) - 1);
    cache_dir[sizeof(cache_dir) - 1] = '\0';

    mkdirs(cache_dir);

    /* quick writability test */
    char probe[600];
    snprintf(probe, sizeof(probe), "%s/.probe", cache_dir);
    FILE *f = fopen(probe, "w");
    if (f == NULL) {
        printf("LTWShaderCache: cannot write to %s — cache disabled\n", cache_dir);
        cache_enabled = false;
        return;
    }
    fclose(f);
    remove(probe);

    cache_enabled = true;
    printf("LTWShaderCache: enabled  dir=%s\n", cache_dir);
}

bool shader_cache_is_enabled(void) { return cache_enabled; }

void shader_cache_compute_key(char *out_key,
                              const char *source,
                              unsigned int shader_type,
                              int glsl_version) {
    /* dual FNV-1a 64-bit → 128-bit hex key */
    const uint64_t FNV_OFFSET = 14695981039346656037ULL;
    const uint64_t FNV_PRIME  = 1099511628211ULL;

    uint64_t h1 = FNV_OFFSET;
    uint64_t h2 = FNV_OFFSET;

    for (const char *p = source; *p; p++) {
        uint64_t c = (uint64_t)(unsigned char)*p;
        h1 ^= c;       h1 *= FNV_PRIME;
        h2 ^= c + 0x9E; h2 *= FNV_PRIME;
    }
    /* mix in stage + version */
    h1 ^= (uint64_t)shader_type;  h1 *= FNV_PRIME;
    h2 ^= (uint64_t)glsl_version;  h2 *= FNV_PRIME;

    snprintf(out_key, 64, "%016llx%016llx",
             (unsigned long long)h1,
             (unsigned long long)h2);
}

char *shader_cache_lookup(const char *key) {
    if (!cache_enabled) return NULL;

    char path[600];
    snprintf(path, sizeof(path), "%s/%s.essl", cache_dir, key);

    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

void shader_cache_store(const char *key, const char *essl_source) {
    if (!cache_enabled || essl_source == NULL) return;

    char path[600];
    snprintf(path, sizeof(path), "%s/%s.essl", cache_dir, key);

    FILE *f = fopen(path, "wb");
    if (f == NULL) return;

    fwrite(essl_source, 1, strlen(essl_source), f);
    fclose(f);
}
