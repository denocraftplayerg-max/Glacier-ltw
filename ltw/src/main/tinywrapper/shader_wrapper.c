/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include "unordered_map/unordered_map.h"
#include <stdint.h>
#include "vgpu_shaderconv/shaderconv.h"
#include "glsl_optimizer/src/code/c_wrapper.h"
#include <GLES3/gl3.h>
#include <string.h>
#include <stdio.h>
#include "string_utils.h"
#include "egl.h"
#include "proc.h"

/* Cache statistics */
static unsigned int shader_cache_hits = 0;
static unsigned int shader_cache_misses = 0;
#include "shader_cache.h"

typedef struct {
    GLenum shader_type;
    const GLchar* source;
} shader_info_t;

typedef struct {
    GLuint frag_shader;
    GLchar* colorbindings[MAX_DRAWBUFFERS];
    bool is_gui_text;
} program_info_t;


/**
* Classifica se um shader é de GUI/texto (bloquear binário)
* ou de mundo/terreno (permitir binário).
* Heurística conservadora: se NÃO tem fog/light/normal, é GUI/texto.
*/
static bool classify_gui_text(const char* source) {
    if (!source) return false;
    /* Shaders de mundo têm fog, lighting ou normals */
    if (strstr(source, "fog") || strstr(source, "Fog") ||
        strstr(source, "Light") || strstr(source, "light") ||
        strstr(source, "Normal") || strstr(source, "normal") ||
        strstr(source, "Overlay") || strstr(source, "overlay")) {
        return false; /* mundo → permite binário */
    }
    /* Shaders simples com textura/cor → provavelmente GUI/texto */
    if (strstr(source, "Sampler") || strstr(source, "sampler") ||
        strstr(source, "texture") || strstr(source, "Color") ||
        strstr(source, "color")) {
        return true; /* GUI/texto → bloquear binário */
    }
    return false; /* na dúvida, permite binário */
}

GLuint glCreateProgram(void) {
    if(!current_context) return 0;
    GLuint phys_program = es3_functions.glCreateProgram();
    if(phys_program == 0) return phys_program;
    program_info_t *prog_info = calloc(1, sizeof(program_info_t));
    if(prog_info == NULL) {
        printf("LTWShdrWp: failed to allocate program_info\n");
        abort();
    }
    unordered_map_put(current_context->program_map, (void*)(uintptr_t)phys_program, prog_info);
    return phys_program;
}

void glDeleteProgram(GLuint program) {
    if(!current_context) return;
    es3_functions.glDeleteProgram(program);
    program_info_t *old_programinfo = unordered_map_remove(current_context->program_map, (void*)(uintptr_t)program);
    if(old_programinfo == NULL) return;
    for(GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        const GLchar* binding = old_programinfo->colorbindings[i];
        if(binding != NULL) free((void*)binding);
    }
    free(old_programinfo);
}

void glAttachShader( 	GLuint program,
                        GLuint shader) {
    if(!current_context) return;
    es3_functions.glAttachShader(program, shader);
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)shader);
    if(program_info == NULL || shader_info == NULL || shader_info->shader_type != GL_FRAGMENT_SHADER) return;
    program_info->frag_shader = shader;
}

void glBindFragDataLocation( 	GLuint program,
                                GLuint colorNumber,
                                const char * name) {
    if(!current_context) return;
    program_info_t *program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    if(program_info == NULL || colorNumber >= MAX_DRAWBUFFERS) return;
    // Insert binding name at the specific index
    GLchar** pname = &program_info->colorbindings[colorNumber];
    if(asprintf(pname, "%s", name) == -1) {
        *pname = NULL;
    }
}

void glGetShaderiv(GLuint shader, GLuint pname, GLint* params) {
    if(!current_context) return;
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)shader);
    if(shader_info != NULL && shader_info->shader_type == GL_FRAGMENT_SHADER && pname == GL_COMPILE_STATUS) {
        // HACK: ignore compile results for frag shaders, as some drivers may not compile them without explicit fragouts
        // (which we add at link-time)
        *params = GL_TRUE;
        return;
    }
    es3_functions.glGetShaderiv(shader, pname, params);
}

static void insert_fragout_pos(char* source, int* size, const char* name, GLuint pos) {
    char src_string[256] = { 0 };
    char dst_string[256] = { 0 };
    snprintf(src_string, sizeof(src_string), "/* LTW INSERT LOCATION %s LTW */", name);
    snprintf(dst_string, sizeof(dst_string), "layout(location = %u) ", pos);
    gl4es_inplace_replace_simple(source, size, src_string, dst_string);
}


/**
 * Fase 2: Computa hash único do programa combinando
 * o hash do fragment shader + colorbindings.
 * Usado como chave do cache binário.
 */
static void ltw_compute_program_hash(program_info_t* info, char* out_key) {
    out_key[0] = '\0';
    if (!info || !current_context) return;
    shader_info_t* frag = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)info->frag_shader);
    if (!frag || !frag->source) return;

    char frag_key[64];
    shader_cache_compute_key(frag_key, frag->source, GL_FRAGMENT_SHADER, current_context->shader_version);

    char combined[8192];
    snprintf(combined, sizeof(combined), "%s", frag_key);
    for (int i = 0; i < MAX_DRAWBUFFERS; i++) {
        if (info->colorbindings[i]) {
            strncat(combined, info->colorbindings[i], sizeof(combined) - strlen(combined) - 1);
            strncat(combined, "|", sizeof(combined) - strlen(combined) - 1);
        }
    }
    shader_cache_compute_key(out_key, combined, GL_FRAGMENT_SHADER, current_context->shader_version);
}

/**
 * Fase 2: Após link bem-sucedido, salva o binário do driver
 * em /Ltw/cache/Binary/ se o programa não for GUI/Text.
 */
static void ltw_post_link_save(GLuint program, program_info_t* info) {
    if (!current_context || !info || info->is_gui_text) return;

    GLint link_status = GL_FALSE;
    es3_functions.glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) return;

    char prog_hash[64];
    ltw_compute_program_hash(info, prog_hash);
    if (prog_hash[0] == '\0') return;

    GLint bin_length = 0;
    es3_functions.glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &bin_length);
    if (bin_length <= 0) return;

    void* binary = malloc(bin_length);
    if (!binary) return;

    GLenum bin_format = 0;
    GLsizei actual_len = 0;
    es3_functions.glGetProgramBinary(program, bin_length, &actual_len, &bin_format, binary);

    if (actual_len > 0) {
        shader_cache_save_binary(prog_hash, bin_format, binary, actual_len);
        printf("LTWBinCache: STORE %s (%d bytes)\n", prog_hash, actual_len);
    }
    free(binary);
}

void glLinkProgram(GLuint program) {
    if(!current_context) return;
    program_info_t* program_info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    if(program_info == NULL || program_info->frag_shader == 0) {
        // Don't have any fragment shader to patch the locations in, fall through.
        goto fallthrough;
    }
    /* Classificar programa: GUI/texto bloqueia binário */
    {
        shader_info_t* cls_shader = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)program_info->frag_shader);
        if(cls_shader && cls_shader->source) {
            program_info->is_gui_text = classify_gui_text(cls_shader->source);
        }
    }
    /* Fase 2: tentar carregar binário do cache LTW */
    if(!program_info->is_gui_text) {
        char prog_hash[64];
        ltw_compute_program_hash(program_info, prog_hash);
        if(prog_hash[0] != '\0') {
            GLenum bin_format = 0;
            void* bin_data = NULL;
            GLint bin_len = 0;
            if(shader_cache_load_binary(prog_hash, &bin_format, &bin_data, &bin_len)) {
                es3_functions.glProgramBinary(program, bin_format, bin_data, bin_len);
                free(bin_data);
                GLint link_status = GL_FALSE;
                es3_functions.glGetProgramiv(program, GL_LINK_STATUS, &link_status);
                if(link_status == GL_TRUE) {
                    printf("LTWBinCache: HIT %s\n", prog_hash);
                    return;
                }
                shader_cache_invalidate_binary(prog_hash);
                printf("LTWBinCache: STALE %s, relinking\n", prog_hash);
            }
        }
    }
    shader_info_t *shader = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)program_info->frag_shader);
    if(shader == NULL) {
        printf("LTWShdrWp: failed to patch frag data location due to missing shader info\n");
        goto fallthrough;
    }
    int nsrc_size = (int)(strlen(shader->source) + 1);
    char* new_source = malloc(nsrc_size);
    memcpy(new_source, shader->source, nsrc_size);
    bool changesMade = false;
    for(GLuint i = 0; i < MAX_DRAWBUFFERS; i++) {
        const char* colorbind = program_info->colorbindings[i];
        if(colorbind == NULL) continue;
        insert_fragout_pos(new_source, &nsrc_size, colorbind, i);
        changesMade = true;
    }
    if(!changesMade) {
        free(new_source);
        goto fallthrough;
    }else {
        //printf("\n\n\nShader Result POST PATCH\n%s\n\n\n", new_source);
    }
    /* cache the location-patched fragment source */
    char link_cache_key[64];
    shader_cache_compute_key(link_cache_key, new_source, GL_FRAGMENT_SHADER, current_context->shader_version);
    const GLchar* const_source = (const GLchar*)new_source;
    GLuint patched_shader = es3_functions.glCreateShader(GL_FRAGMENT_SHADER);
    if(patched_shader == 0) {
        free(new_source);
        printf("LTWShdrWp: failed to initialize patched shader\n");
        goto fallthrough;
    }
    es3_functions.glShaderSource(patched_shader, 1, &const_source, NULL);
    es3_functions.glCompileShader(patched_shader);
    GLint compileStatus;
    es3_functions.glGetShaderiv(patched_shader, GL_COMPILE_STATUS, &compileStatus);
    if(compileStatus != GL_TRUE) {
        GLint logSize;
        es3_functions.glGetShaderiv(patched_shader, GL_INFO_LOG_LENGTH, &logSize);
        GLchar log[logSize];
        es3_functions.glGetShaderInfoLog(patched_shader, logSize, NULL, log);
        printf("LTWShdrWp: failed to compile patched fragment shader, using default. Log:\n\n%s\n\nShader content:\n\n%s\n\n", log, const_source);
        free(new_source);
        goto fallthrough;
    }
    es3_functions.glDetachShader(program, program_info->frag_shader);
    es3_functions.glAttachShader(program, patched_shader);
    es3_functions.glLinkProgram(program);
    ltw_post_link_save(program, program_info);
    es3_functions.glDeleteShader(patched_shader);
    return;
    fallthrough:
    es3_functions.glLinkProgram(program);
    ltw_post_link_save(program, program_info);
}

GLuint glCreateShader(GLenum shaderType) {
    if(!current_context) return 0;
    GLuint phys_shader = es3_functions.glCreateShader(shaderType);
    if(phys_shader == 0) return 0;
    shader_info_t* info_struct = calloc(1, sizeof(shader_info_t));
    if(info_struct == NULL) {
        printf("LTWShdrWp: failed to allocate shader_info\n");
        abort();
    }
    info_struct->shader_type = shaderType;
    unordered_map_put(current_context->shader_map, (void*)(uintptr_t)phys_shader, info_struct);
    return phys_shader;
}

void glDeleteShader(GLuint shader) {
    if(!current_context) return;
    es3_functions.glDeleteShader(shader);
    shader_info_t * old_shaderinfo = unordered_map_remove(current_context->shader_map, (void*)(uintptr_t)shader);
    if(old_shaderinfo == NULL) return;
    if(old_shaderinfo->source != NULL) free((void*)old_shaderinfo->source);
    free(old_shaderinfo);
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) {
    if(!current_context) return;
    shader_info_t* shader_info = unordered_map_get(current_context->shader_map, (void*)(uintptr_t)shader);
    if(shader_info == NULL) {
        printf("LTWShdrWp: shader_info missing for shader %u\n", shader);
        es3_functions.glShaderSource(shader, count, string, length);
        return;
    }

    size_t target_length = 0;
#define SRC_LEN(x) length != NULL ? length[x] : strlen(string[x])
    for(GLsizei i = 0; i < count; i++) target_length += SRC_LEN(i);
    GLchar* target_string = malloc((target_length + 1) * sizeof(GLchar));
    size_t offset = 0;
    for(GLsizei i = 0; i < count; i++) {
        memcpy(&target_string[offset], string[i], SRC_LEN(i));
    }
    target_string[target_length] = 0;

#undef SRC_LEN
    char cache_key[64];
    shader_cache_compute_key(cache_key, target_string, shader_info->shader_type, current_context->shader_version);
    GLchar* new_source = shader_cache_lookup(cache_key);
    if(new_source == NULL) {
        new_source = optimize_shader(target_string, shader_info->shader_type, 460, current_context->shader_version);
        if(new_source != NULL) {
            shader_cache_store(cache_key, new_source, target_string);
        }
    }
    if(shader_info->source != NULL) free((void*)(uintptr_t)shader_info->source);
    if(!new_source) {
        printf("LTWShdrWp: failed to convert&optimize shader %u, skipping\n", shader);
        goto end;
    } else {
        //printf("\n\n\nShader Result\n%s\n\n\n", new_source);
        shader_info->source = new_source;
    }
    es3_functions.glShaderSource(shader, 1, &shader_info->source, 0);
    end:
    free(target_string);
}


/* ═══════════════════════════════════════════
 * Cache Binário com Filtro GUI/Texto
 * GUI/Texto: NUNCA gerar binário (texto sempre renderiza)
 * Mundo/Terreno: gerar binário (load instantâneo)
 * ═══════════════════════════════════════════ */
void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat, void *binary) {
    if(!current_context) {
        if(length) *length = 0;
        return;
    }
    program_info_t* info = unordered_map_get(current_context->program_map, (void*)(uintptr_t)program);
    if(info && info->is_gui_text) {
        /* GUI/Texto: não gerar binário.
         * O Minecraft vai recompilar do ESSL cacheado.
         * Isso evita o bug de texto não renderizado. */
        if(length) *length = 0;
        if(binaryFormat) *binaryFormat = 0;
        return;
    }
    /* Mundo/Terreno: gerar binário do driver */
    es3_functions.glGetProgramBinary(program, bufSize, length, binaryFormat, binary);
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length) {
    if(!current_context) return;
    /* Encaminhar para o driver.
     * Como filtramos GUI/Texto no glGetProgramBinary,
     * o Minecraft não deve ter binários de GUI/Texto para carregar.
     * Se um binário antigo de GUI/Texto existir (de versão anterior),
     * limpe o cache de shaders do Minecraft. */
    es3_functions.glProgramBinary(program, binaryFormat, binary, length);
}
