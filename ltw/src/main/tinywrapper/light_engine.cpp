#include "light_engine.hpp"
#include <cstring>
#include <cmath>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG,"HYENGRA",__VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

// ── GEOMETRY VERTEX ─────────────────────────────────────────────────────────
const char* LightEngine::GEOMETRY_VERT_SRC = R"GLSL(
#version 320 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNorm;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aLight; // skylight, blocklight, ao, unused

uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormalMat;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLight;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos  = world.xyz;
    vNormal    = normalize(uNormalMat * aNorm);
    vUV        = aUV;
    vLight     = aLight;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

// ── GEOMETRY FRAGMENT ───────────────────────────────────────────────────────
const char* LightEngine::GEOMETRY_FRAG_SRC = R"GLSL(
#version 320 es
#extension GL_EXT_shader_pixel_local_storage2 : enable

precision highp float;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLight;

uniform sampler2D uAlbedo;

#ifdef GL_EXT_shader_pixel_local_storage2
__pixel_local_outEXT FragData {
    layout(rgba8)   highp vec4 albedoMat;
    layout(rgba16f) highp vec4 normalLight;
    layout(r32f)    highp float depth;
    layout(rgba8)   highp vec4 blockAO;
} pls;
#else
out vec4 fragColor;
#endif

void main() {
    vec4 albedo = texture(uAlbedo, vUV);
    if (albedo.a < 0.1) discard;

#ifdef GL_EXT_shader_pixel_local_storage2
    pls.albedoMat   = vec4(albedo.rgb, vLight.w);
    pls.normalLight = vec4(vNormal * 0.5 + 0.5, vLight.x);
    pls.depth       = gl_FragCoord.z;
    pls.blockAO     = vec4(vec3(vLight.y), vLight.z);
#else
    fragColor = albedo;
#endif
}
)GLSL";

// ── LIGHTING FRAGMENT ───────────────────────────────────────────────────────
const char* LightEngine::LIGHTING_FRAG_SRC = R"GLSL(
#version 320 es
#extension GL_EXT_shader_pixel_local_storage2 : enable
#extension GL_EXT_shader_framebuffer_fetch     : enable

precision highp float;

#ifdef GL_EXT_shader_pixel_local_storage2
__pixel_local_inEXT FragData {
    layout(rgba8)   highp vec4 albedoMat;
    layout(rgba16f) highp vec4 normalLight;
    layout(r32f)    highp float depth;
    layout(rgba8)   highp vec4 blockAO;
} pls;
#endif

#ifdef GL_EXT_shader_framebuffer_fetch
inout highp vec4 fragColor;
#else
out highp vec4 fragColor;
#endif

struct Light {
    vec4 posRadius;
    vec4 colorInt;
    ivec4 type;
};

layout(std430, binding=1) readonly buffer Lights {
    Light lights[];
};

uniform int   uLightCount;
uniform vec3  uSunDir;
uniform float uTimeOfDay;
uniform vec3  uCamPos;
uniform mat4  uInvVP;

vec3 reconstructWorld(float d) {
    vec2 ndc = (gl_FragCoord.xy / vec2(1280.0, 720.0)) * 2.0 - 1.0;
    vec4 clip = vec4(ndc, d * 2.0 - 1.0, 1.0);
    vec4 world = uInvVP * clip;
    return world.xyz / world.w;
}

vec3 aces(vec3 x) {
    return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0);
}

float pointAtten(float dist, float radius) {
    float x = clamp(1.0 - (dist / radius), 0.0, 1.0);
    return x * x;
}

void main() {
#ifdef GL_EXT_shader_pixel_local_storage2
    vec3  albedo   = pls.albedoMat.rgb;
    vec3  normal   = normalize(pls.normalLight.rgb * 2.0 - 1.0);
    float skylight = pls.normalLight.a;
    float fragD    = pls.depth;
    float ao       = pls.blockAO.a;
    vec3  blColor  = pls.blockAO.rgb;
#else
    vec3  albedo   = vec3(0.8);
    vec3  normal   = vec3(0.0, 1.0, 0.0);
    float skylight = 1.0;
    float fragD    = gl_FragCoord.z;
    float ao       = 1.0;
    vec3  blColor  = vec3(0.5);
#endif

    vec3 worldPos  = reconstructWorld(fragD);
    float sunHeight = clamp(sin(uTimeOfDay * 3.14159), 0.0, 1.0);
    vec3 sunColor   = mix(vec3(1.0, 0.35, 0.05), vec3(1.0, 0.97, 0.88), clamp(sunHeight * 3.0 - 0.5, 0.0, 1.0));
    float sunStr = max(dot(normal, -uSunDir), 0.0) * sunHeight;

    vec3 lighting = albedo * sunColor * sunStr * 1.5;

    for (int i = 0; i < uLightCount && i < 128; i++) {
        if (lights[i].type.x == 1 || lights[i].type.x == 2) continue;
        vec3  lpos  = lights[i].posRadius.xyz;
        float lrad  = lights[i].posRadius.w;
        vec3  lcol  = lights[i].colorInt.xyz;
        float lint  = lights[i].colorInt.w;

        vec3  lvec  = lpos - worldPos;
        float ldist = length(lvec);
        vec3  ldir  = lvec / max(ldist, 0.001);

        float atten = pointAtten(ldist, lrad);
        float diff  = max(dot(normal, ldir), 0.0);
        lighting += albedo * lcol * diff * atten * lint;
    }

    lighting += albedo * blColor * 0.6;
    vec3 tonemapped = aces(lighting * 1.1);

    fragColor = vec4(tonemapped, 1.0);
}
)GLSL";

static GLuint makeProgram(const char* vert, const char* frag) {
    auto compile = [](GLenum type, const char* src) {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[2048]; glGetShaderInfoLog(sh,2048,nullptr,buf);
            LOG("SHADER ERR: %s\n",buf);
        }
        return sh;
    };
    GLuint v = compile(GL_VERTEX_SHADER, vert);
    GLuint f = compile(GL_FRAGMENT_SHADER, frag);
    GLuint p = glCreateProgram();
    glAttachShader(p,v); glAttachShader(p,f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

void LightEngine::init() {
    glGenBuffers(1, &lightSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        MAX_LIGHTS * sizeof(Light), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    geometryPass = makeProgram(GEOMETRY_VERT_SRC, GEOMETRY_FRAG_SRC);
    lightingPass = makeProgram(GEOMETRY_VERT_SRC, LIGHTING_FRAG_SRC);
    LOG("LightEngine: init OK\n");
}

void LightEngine::uploadLights(const std::vector<Light>& lights) {
    lightCount = (int)lights.size();
    if (lightCount > MAX_LIGHTS) lightCount = MAX_LIGHTS;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
        lightCount * sizeof(Light), lights.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void LightEngine::renderGeometryPass() {
    glUseProgram(geometryPass);
}

void LightEngine::renderLightingPass(float sunDir[3], float timeOfDay) {
    glUseProgram(lightingPass);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, lightSSBO);
    glUniform1i(glGetUniformLocation(lightingPass,"uLightCount"), lightCount);
    glUniform3fv(glGetUniformLocation(lightingPass,"uSunDir"), 1, sunDir);
    glUniform1f(glGetUniformLocation(lightingPass,"uTimeOfDay"), timeOfDay);
}

void LightEngine::destroy() {
    glDeleteProgram(geometryPass);
    glDeleteProgram(lightingPass);
    glDeleteBuffers(1, &lightSSBO);
}
