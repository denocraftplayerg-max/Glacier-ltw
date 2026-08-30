/*
 * LTW On-Disk Shader Cache
 *
 * Stores converted ESSL sources to skip the shaderconv + glsl_optimizer
 * pipeline on subsequent launches.
 *
 * Layout:
 *   <base>/GLSL/<key>.glsl   - original GLSL from app (reference)
 *   <base>/ESSL/<key>.essl   - converted ESSL (reused on next load)
 *   <base>/Binary/           - reserved for future binary cache
 *
 * Base dir: $LTW_CACHE_DIR or /storage/emulated/0/Ltw/cache
 *
 * Cache key: FNV-1a 128-bit hash of (source + shader_type + version + cache_version)
 */
#include "shader_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

/* Bump this when shaderconv/optimizer algorithm changes.
 * Old cache entries become automatic misses. */
#define LTW_CACHE_VERSION 1

#define LTW_CACHE_BASE "/storage/emulated/0/Ltw/cache"

/* ── internal state ────────────────────────────────── */
static struct {
    char base[512];
    char glsl_dir[600];
    char essl_dir[600];
    char bin_dir[600];
    int  enabled;
    int  inited;
    unsigned hits;
    unsigned misses;
    unsigned stores;
} sc = {0};

/* ── helpers ───────────────────────────────────────── */
static void sc_mkdirs(const char *path) {
    char tmp[600];
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

/* ── init (auto-called when .so loads) ─────────────── */
__attribute__((constructor))
void shader_cache_init(void) {
    if (sc.inited) return;
    sc.inited = 1;

    const char *base = getenv("LTW_CACHE_DIR");
    if (!base || !base[0]) base = LTW_CACHE_BASE;

    strncpy(sc.base, base, sizeof(sc.base) - 1);
    sc.base[sizeof(sc.base) - 1] = '\0';
    snprintf(sc.glsl_dir, sizeof(sc.glsl_dir), "%s/GLSL", base);
    snprintf(sc.essl_dir, sizeof(sc.essl_dir), "%s/ESSL", base);
    snprintf(sc.bin_dir, sizeof(sc.bin_dir), "%s/Binary", base);

    /* Create all cache directories */
    sc_mkdirs(sc.glsl_dir);
    sc_mkdirs(sc.essl_dir);
    sc_mkdirs(sc.bin_dir);

    /* Create binary dirs (reserved for future use) */
    char bdir[700];
    snprintf(bdir, sizeof(bdir), "%s/Binary/Resource_shader_extern", base);
    sc_mkdirs(bdir);
    snprintf(bdir, sizeof(bdir), "%s/Binary/minecraft_core", base);
    sc_mkdirs(bdir);

    /* Writability test */
    char probe[700];
    snprintf(probe, sizeof(probe), "%s/.probe", sc.essl_dir);
    FILE *f = fopen(probe, "w");
    if (!f) {
        printf("LTWCache: DISABLED - cannot write to %s (%s)\n",
               sc.essl_dir, strerror(errno));
        sc.enabled = 0;
        return;
    }
    fclose(f);
    remove(probe);

    sc.enabled = 1;
    printf("LTWCache: ENABLED (v%d)\n", LTW_CACHE_VERSION);
    printf("LTWCache:   GLSL  -> %s\n", sc.glsl_dir);
    printf("LTWCache:   ESSL  -> %s\n", sc.essl_dir);
}

int shader_cache_is_enabled(void) {
    return sc.enabled;
}

/* ── hash ──────────────────────────────────────────── */
void shader_cache_compute_key(char *out_key,
                              const char *source,
                              unsigned int shader_type,
                              int glsl_version) {
    /*
     * Dual FNV-1a 64-bit with different seeds -> 128-bit hex key.
     * Collision probability effectively zero for any realistic
     * number of shaders (< 2^32).
     */
    const uint64_t FNV_OFF = 14695981039346656037ULL;
    const uint64_t FNV_P   = 1099511628211ULL;

    uint64_t h1 = FNV_OFF;
    uint64_t h2 = FNV_OFF ^ 0xFF51AFD7ED558CCDULL;

    for (const unsigned char *p = (const unsigned char *)source; *p; p++) {
        h1 ^= *p; h1 *= FNV_P;
        h2 ^= *p; h2 *= FNV_P;
    }

    /* Mix in shader stage, ESSL version and cache version */
    h1 ^= (uint64_t)shader_type;       h1 *= FNV_P;
    h2 ^= (uint64_t)glsl_version;      h2 *= FNV_P;
    h1 ^= (uint64_t)LTW_CACHE_VERSION; h1 *= FNV_P;

    snprintf(out_key, 64, "%016llx%016llx",
             (unsigned long long)h1,
             (unsigned long long)h2);
}

/* ── lookup ────────────────────────────────────────── */
char *shader_cache_lookup(const char *key) {
    if (!sc.enabled) return NULL;

    char path[700];
    snprintf(path, sizeof(path), "%s/%s.essl", sc.essl_dir, key);

    FILE *f = fopen(path, "rb");
    if (!f) {
        sc.misses++;
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) {
        fclose(f);
        sc.misses++;
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        sc.misses++;
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);

    sc.hits++;
    printf("LTWCache: HIT  %.16s.. (%u hits / %u total)\n",
           key, sc.hits, sc.hits + sc.misses);
    return buf;
}

/* ── store ─────────────────────────────────────────── */
void shader_cache_store(const char *key,
                        const char *essl_source,
                        const char *glsl_original) {
    if (!sc.enabled || !essl_source) return;

    char path[700];
    FILE *f;

    /* Save converted ESSL (this is what gets reused) */
    snprintf(path, sizeof(path), "%s/%s.essl", sc.essl_dir, key);
    f = fopen(path, "wb");
    if (f) {
        fwrite(essl_source, 1, strlen(essl_source), f);
        fclose(f);
    }

    /* Save original GLSL (for debugging / reference) */
    if (glsl_original) {
        snprintf(path, sizeof(path), "%s/%s.glsl", sc.glsl_dir, key);
        f = fopen(path, "wb");
        if (f) {
            fwrite(glsl_original, 1, strlen(glsl_original), f);
            fclose(f);
        }
    }

    sc.stores++;
    printf("LTWCache: STORE %.16s.. (%u stored)\n", key, sc.stores);
}


/* ═══════════════════════════════════════════
 * Fase 2: Cache Binário do Driver GLES
 * Formato do arquivo: [GLenum format][GLint length][binary data]
 * ═══════════════════════════════════════════ */
bool shader_cache_load_binary(const char* key, GLenum* out_format, void** out_binary, GLint* out_length) {
    if (!sc.enabled || !key || key[0] == '\0') return false;
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.bin", sc.bin_dir, key);
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    GLenum format = 0;
    GLint length = 0;
    if (fread(&format, sizeof(GLenum), 1, f) != 1 ||
        fread(&length, sizeof(GLint), 1, f) != 1 ||
        length <= 0) {
        fclose(f);
        return false;
    }

    void* data = malloc(length);
    if (!data) { fclose(f); return false; }

    if (fread(data, 1, length, f) != (size_t)length) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    *out_format = format;
    *out_binary = data;
    *out_length = length;
    return true;
}

void shader_cache_save_binary(const char* key, GLenum format, const void* binary, GLint length) {
    if (!sc.enabled || !key || key[0] == '\0' || !binary || length <= 0) return;
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.bin", sc.bin_dir, key);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(&format, sizeof(GLenum), 1, f);
    fwrite(&length, sizeof(GLint), 1, f);
    fwrite(binary, 1, length, f);
    fclose(f);
}

void shader_cache_invalidate_binary(const char* key) {
    if (!key || key[0] == '\0') return;
    char path[700];
    snprintf(path, sizeof(path), "%s/%s.bin", sc.bin_dir, key);
    remove(path);
}
