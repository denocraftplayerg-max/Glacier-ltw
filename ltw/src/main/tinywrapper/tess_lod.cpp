#include "tess_lod.hpp"
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG,"HYENGRA_TESS",__VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

const char* TessLODSystem::VERT_SRC = R"GLSL(
#version 320 es
#extension GL_EXT_tessellation_shader     : enable
#extension GL_EXT_gpu_shader5             : enable
#extension GL_EXT_primitive_bounding_box  : enable

precision highp float;

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;

out vec3 vPos;
out vec3 vNorm;
out vec2 vUV;

void main() {
    vPos  = aPos;
    vNorm = aNorm;
    vUV   = aUV;
    gl_Position = vec4(aPos, 1.0);
}
)GLSL";

const char* TessLODSystem::TCS_SRC = R"GLSL(
#version 320 es
#extension GL_EXT_tessellation_shader     : enable
#extension GL_EXT_primitive_bounding_box  : enable
#extension GL_EXT_gpu_shader5             : enable

precision highp float;

layout(vertices = 3) out;

in  vec3 vPos[];
in  vec3 vNorm[];
in  vec2 vUV[];

out vec3 tcPos[];
out vec3 tcNorm[];
out vec2 tcUV[];

uniform vec3  uCamPos;
uniform float uMaxTess;
uniform float uMinTess;
uniform float uNearDist;
uniform float uFarDist;
uniform float uLodBias;

float tessLevel(vec3 pos) {
    float d = distance(pos, uCamPos) + uLodBias;
    float t = 1.0 - clamp((d - uNearDist) / (uFarDist - uNearDist), 0.0, 1.0);
    return t * (uMaxTess - uMinTess) + uMinTess;
}

void main() {
    tcPos[gl_InvocationID]  = vPos[gl_InvocationID];
    tcNorm[gl_InvocationID] = vNorm[gl_InvocationID];
    tcUV[gl_InvocationID]   = vUV[gl_InvocationID];

    if (gl_InvocationID == 0) {
#ifdef GL_EXT_primitive_bounding_box
        vec3 bMin = min(min(vPos[0], vPos[1]), vPos[2]) - vec3(2.0);
        vec3 bMax = max(max(vPos[0], vPos[1]), vPos[2]) + vec3(2.0);
        gl_BoundingBoxEXT[0] = vec4(bMin, 1.0);
        gl_BoundingBoxEXT[1] = vec4(bMax, 1.0);
#endif
        float t0 = tessLevel(mix(vPos[1], vPos[2], 0.5));
        float t1 = tessLevel(mix(vPos[0], vPos[2], 0.5));
        float t2 = tessLevel(mix(vPos[0], vPos[1], 0.5));

        gl_TessLevelOuter[0] = t0;
        gl_TessLevelOuter[1] = t1;
        gl_TessLevelOuter[2] = t2;
        gl_TessLevelInner[0] = (t0 + t1 + t2) / 3.0;
    }
}
)GLSL";

const char* TessLODSystem::TES_SRC = R"GLSL(
#version 320 es
#extension GL_EXT_tessellation_shader : enable
#extension GL_EXT_gpu_shader5         : enable

precision highp float;

layout(triangles, fractional_even_spacing, ccw) in;

in  vec3 tcPos[];
in  vec3 tcNorm[];
in  vec2 tcUV[];

out vec3 teWorldPos;
out vec3 teNormal;
out vec2 teUV;

uniform mat4  uVP;
uniform mat4  uModel;
uniform mat3  uNormalMat;

#define BARY(x) (gl_TessCoord.x*(x)[0] + gl_TessCoord.y*(x)[1] + gl_TessCoord.z*(x)[2])

void main() {
    vec3 pos  = BARY(tcPos);
    vec3 norm = normalize(BARY(tcNorm));
    vec2 uv   = BARY(tcUV);

    vec4 worldPos = uModel * vec4(pos, 1.0);
    teWorldPos  = worldPos.xyz;
    teNormal    = normalize(uNormalMat * norm);
    teUV        = uv;

    gl_Position = uVP * worldPos;
}
)GLSL";

const char* TessLODSystem::FRAG_SRC = R"GLSL(
#version 320 es
#extension GL_EXT_shader_pixel_local_storage2 : enable

precision highp float;
precision highp sampler2D;

in vec3  teWorldPos;
in vec3  teNormal;
in vec2  teUV;

uniform sampler2D uAlbedo;

#ifdef GL_EXT_shader_pixel_local_storage2
__pixel_local_outEXT GBuffer {
    layout(rgba8)   highp vec4 albedoAO;
    layout(rgba8)   highp vec4 normalMat;
    layout(rgba16f) highp vec4 lightSky;
} pls;
#else
out vec4 fragColor;
#endif

void main() {
    vec4 albedo = texture(uAlbedo, teUV);
    if (albedo.a < 0.1) discard;

#ifdef GL_EXT_shader_pixel_local_storage2
    pls.albedoAO  = vec4(albedo.rgb, 1.0);
    pls.normalMat = vec4(teNormal * 0.5 + 0.5, 0.0);
    pls.lightSky  = vec4(0.5, 0.5, 1.0, gl_FragCoord.z);
#else
    fragColor = albedo;
#endif
}
)GLSL";

static GLuint compileTessProgram(const char* vert, const char* tcs,
                                  const char* tes,  const char* frag) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[2048];
            glGetShaderInfoLog(sh, 2048, nullptr, buf);
            LOG("ERR: %s\n",buf);
        }
        return sh;
    };
    GLuint v  = compile(GL_VERTEX_SHADER,                  vert);
    GLuint tc = compile(GL_TESS_CONTROL_SHADER,            tcs);
    GLuint te = compile(GL_TESS_EVALUATION_SHADER,         tes);
    GLuint f  = compile(GL_FRAGMENT_SHADER,                frag);
    GLuint p  = glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,tc);
    glAttachShader(p,te); glAttachShader(p,f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(tc);
    glDeleteShader(te); glDeleteShader(f);
    return p;
}

void TessLODSystem::init() {
    program = compileTessProgram(VERT_SRC, TCS_SRC, TES_SRC, FRAG_SRC);
}

void TessLODSystem::bind(const TessConfig& cfg, const float* camPos) {
    glUseProgram(program);
    glUniform3fv(glGetUniformLocation(program,"uCamPos"),    1, camPos);
    glUniform1f (glGetUniformLocation(program,"uMaxTess"),   cfg.maxTessLevel);
    glUniform1f (glGetUniformLocation(program,"uMinTess"),   cfg.minTessLevel);
    glUniform1f (glGetUniformLocation(program,"uNearDist"),  cfg.nearDist);
    glUniform1f (glGetUniformLocation(program,"uFarDist"),   cfg.farDist);
    glUniform1f (glGetUniformLocation(program,"uLodBias"),   cfg.lodBias);
    glPatchParameteri(GL_PATCH_VERTICES, 3);
}

void TessLODSystem::destroy() {
    glDeleteProgram(program);
    program = 0;
}
