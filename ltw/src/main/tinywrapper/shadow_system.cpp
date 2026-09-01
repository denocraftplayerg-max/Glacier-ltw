#include "shadow_system.hpp"
#include <cstring>
#include <cmath>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG,"HYENGRA_SHADOW",__VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

const char* ShadowSystem::SHADOW_VERT = R"GLSL(
#version 320 es
layout(location=0) in vec3 aPos;

struct InstanceData {
    mat4 model;
    vec4 lightData;
};
layout(std430, binding=4) readonly buffer Instances {
    InstanceData instances[];
};

uniform mat4 uLightVP;

void main() {
    mat4 model  = instances[gl_InstanceID].model;
    gl_Position = uLightVP * model * vec4(aPos, 1.0);
}
)GLSL";

const char* ShadowSystem::SHADOW_FRAG = R"GLSL(
#version 320 es
precision highp float;

void main() {
}
)GLSL";

static void orthoMatrix(float* m, float l, float r,
                         float b, float t, float n, float f) {
    memset(m, 0, 16*sizeof(float));
    m[0]  =  2.0f/(r-l);
    m[5]  =  2.0f/(t-b);
    m[10] = -2.0f/(f-n);
    m[12] = -(r+l)/(r-l);
    m[13] = -(t+b)/(t-b);
    m[14] = -(f+n)/(f-n);
    m[15] =  1.0f;
}

static void lookAtMatrix(float* m, const float* eye,
                          const float* center, const float* up) {
    float f[3] = { center[0]-eye[0], center[1]-eye[1], center[2]-eye[2] };
    float flen = sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (flen > 0.0001f) { f[0]/=flen; f[1]/=flen; f[2]/=flen; }

    float s[3] = {
        f[1]*up[2]-f[2]*up[1],
        f[2]*up[0]-f[0]*up[2],
        f[0]*up[1]-f[1]*up[0]
    };
    float slen = sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
    if (slen > 0.0001f) { s[0]/=slen; s[1]/=slen; s[2]/=slen; }

    float u[3] = {
        s[1]*f[2]-s[2]*f[1],
        s[2]*f[0]-s[0]*f[2],
        s[0]*f[1]-s[1]*f[0]
    };

    memset(m, 0, 16*sizeof(float));
    m[0]=s[0]; m[4]=s[1]; m[8] =s[2];
    m[1]=u[0]; m[5]=u[1]; m[9] =u[2];
    m[2]=-f[0];m[6]=-f[1];m[10]=-f[2];
    m[12]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    m[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    m[14]= (f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2]);
    m[15]=1.0f;
}

static void mulMat4(float* out, const float* a, const float* b) {
    for(int r=0;r<4;r++) for(int c=0;c<4;c++) {
        out[c*4+r]=0;
        for(int k=0;k<4;k++) out[c*4+r]+=a[k*4+r]*b[c*4+k];
    }
}

void ShadowSystem::createCascade(ShadowCascade& c, int res) {
    c.resolution = res;

    glGenTextures(1, &c.depthTex);
    glBindTexture(GL_TEXTURE_2D, c.depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 res, res, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &c.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, c.depthTex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ShadowSystem::init(const ShadowConfig& config) {
    cfg = config;
    createCascade(cascades[0], cfg.innerResolution);
    createCascade(cascades[1], cfg.midResolution);
    createCascade(cascades[2], cfg.outerResolution);
    cascades[0].splitDist = cfg.cascadeSplits[0];
    cascades[1].splitDist = cfg.cascadeSplits[1];
    cascades[2].splitDist = cfg.cascadeSplits[2];
    buildShadowProgram();
}

void ShadowSystem::updateCascades(const float* sunDir, const float* camPos,
                                   const float* /*corners*/, float /*fovY*/,
                                   float /*aspect*/, float nearZ) {
    float splits[3] = { cfg.cascadeSplits[0], cfg.cascadeSplits[1], cfg.cascadeSplits[2] };
    float prevSplit = nearZ;
    float up[3] = {0,1,0};
    if (fabsf(sunDir[1]) > 0.99f) { up[0] = 1.0f; up[1] = 0.0f; }

    for (int i = 0; i < 3; i++) {
        float splitFar = splits[i];
        float center[3] = {
            camPos[0] - sunDir[0] * (prevSplit + splitFar) * 0.5f,
            camPos[1] - sunDir[1] * (prevSplit + splitFar) * 0.5f,
            camPos[2] - sunDir[2] * (prevSplit + splitFar) * 0.5f
        };
        float radius = splitFar * 0.85f;
        float eye[3] = {
            center[0] - sunDir[0] * radius * 2.0f,
            center[1] - sunDir[1] * radius * 2.0f,
            center[2] - sunDir[2] * radius * 2.0f
        };
        float view[16];
        lookAtMatrix(view, eye, center, up);
        float proj[16];
        orthoMatrix(proj, -radius, radius, -radius, radius, -radius * 4.0f, radius * 4.0f);
        mulMat4(cascades[i].lightVP, proj, view);
        prevSplit = splitFar;
    }
}

void ShadowSystem::renderCascade(int idx, GLuint sceneVAO,
                                  GLuint sceneIBO, int indexCount) {
    ShadowCascade& c = cascades[idx];
    glBindFramebuffer(GL_FRAMEBUFFER, c.fbo);
    glViewport(0, 0, c.resolution, c.resolution);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glClear(GL_DEPTH_BUFFER_BIT);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(cfg.slopeBias, cfg.shadowBias * 1000.0f);

    glUseProgram(shadowProgram);
    glUniformMatrix4fv(glGetUniformLocation(shadowProgram,"uLightVP"), 1, GL_FALSE, c.lightVP);

    glBindVertexArray(sceneVAO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sceneIBO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowSystem::bindForLighting(GLuint lightProg) {
    glUseProgram(lightProg);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, cascades[0].depthTex);
    glUniform1i(glGetUniformLocation(lightProg,"uShadowMap0"), 3);
    glUniformMatrix4fv(glGetUniformLocation(lightProg,"uLightVP0"), 1, GL_FALSE, cascades[0].lightVP);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, cascades[1].depthTex);
    glUniform1i(glGetUniformLocation(lightProg,"uShadowMap1"), 4);
    glUniformMatrix4fv(glGetUniformLocation(lightProg,"uLightVP1"), 1, GL_FALSE, cascades[1].lightVP);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, cascades[2].depthTex);
    glUniform1i(glGetUniformLocation(lightProg,"uShadowMap2"), 5);
    glUniformMatrix4fv(glGetUniformLocation(lightProg,"uLightVP2"), 1, GL_FALSE, cascades[2].lightVP);

    glUniform1f(glGetUniformLocation(lightProg,"uSplitDist0"), cfg.cascadeSplits[0]);
    glUniform1f(glGetUniformLocation(lightProg,"uSplitDist1"), cfg.cascadeSplits[1]);
    glUniform1f(glGetUniformLocation(lightProg,"uShadowBias"), cfg.shadowBias);
    glUniform1f(glGetUniformLocation(lightProg,"uDarkness"), cfg.darkness);
}

void ShadowSystem::buildShadowProgram() {
    auto compile = [](GLenum t, const char* s) {
        GLuint sh = glCreateShader(t);
        glShaderSource(sh,1,&s,nullptr);
        glCompileShader(sh);
        GLint ok; glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
        if(!ok){ char b[1024]; glGetShaderInfoLog(sh,1024,nullptr,b);
            LOG("ERR:%s\n",b);}
        return sh;
    };
    GLuint v = compile(GL_VERTEX_SHADER,   SHADOW_VERT);
    GLuint f = compile(GL_FRAGMENT_SHADER, SHADOW_FRAG);
    shadowProgram = glCreateProgram();
    glAttachShader(shadowProgram,v);
    glAttachShader(shadowProgram,f);
    glLinkProgram(shadowProgram);
    glDeleteShader(v); glDeleteShader(f);
}

void ShadowSystem::destroyCascade(ShadowCascade& c) {
    glDeleteFramebuffers(1, &c.fbo);
    glDeleteTextures(1, &c.depthTex);
    c = {};
}

void ShadowSystem::destroy() {
    for (int i=0;i<3;i++) destroyCascade(cascades[i]);
    glDeleteProgram(shadowProgram);
}
