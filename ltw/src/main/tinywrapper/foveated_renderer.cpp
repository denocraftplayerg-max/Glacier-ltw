#include "foveated_renderer.hpp"
#include <cstring>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG,"HYENGRA_FOVEA",__VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

const char* FoveatedRenderer::COMPOSITE_VERT = R"GLSL(
#version 320 es
precision highp float;

layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;

out vec2 vUV;
out vec2 vScreen;

void main() {
    vUV     = aUV;
    vScreen = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

const char* FoveatedRenderer::COMPOSITE_FRAG = R"GLSL(
#version 320 es
precision highp float;
precision highp sampler2D;

in vec2 vUV;
in vec2 vScreen;

uniform sampler2D uInner;
uniform sampler2D uMiddle;
uniform sampler2D uOuter;

uniform vec2  uCenter;
uniform float uInnerR;
uniform float uMiddleR;

out highp vec4 fragColor;

void main() {
    float dist = distance(vScreen, uCenter);

    float innerBlend  = smoothstep(uInnerR - 0.03, uInnerR + 0.03, dist);
    float middleBlend = smoothstep(uMiddleR - 0.03, uMiddleR + 0.03, dist);

    vec4 colorInner  = texture(uInner,  vUV);
    vec4 colorMiddle = texture(uMiddle, vUV);
    vec4 colorOuter  = texture(uOuter,  vUV);

    vec4 color = colorInner;
    color = mix(color, colorMiddle, innerBlend);
    color = mix(color, colorOuter,  middleBlend);

    fragColor = vec4(color.rgb, 1.0);
}
)GLSL";

static GLuint compileProgram(const char* vert, const char* frag) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[2048];
            glGetShaderInfoLog(sh, 2048, nullptr, buf);
            LOG("SHADER ERR: %s\n", buf);
        }
        return sh;
    };
    GLuint v = compile(GL_VERTEX_SHADER,   vert);
    GLuint f = compile(GL_FRAGMENT_SHADER, frag);
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

void FoveatedRenderer::createLayer(FoveaLayer& layer, int w, int h) {
    layer.width  = w;
    layer.height = h;

    glGenTextures(1, &layer.colorTex);
    glBindTexture(GL_TEXTURE_2D, layer.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &layer.depthTex);
    glBindTexture(GL_TEXTURE_2D, layer.depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &layer.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, layer.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, layer.colorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, layer.depthTex, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FoveatedRenderer::init(const FoveaConfig& config, bool imgDownsample) {
    cfg = config;
    hasIMGDownsample = imgDownsample;

    int W = cfg.screenW;
    int H = cfg.screenH;

    int iW = (int)(W * cfg.innerScale);
    int iH = (int)(H * cfg.innerScale);

    int mW = (int)(W * cfg.middleScale);
    int mH = (int)(H * cfg.middleScale);

    int oW = (int)(W * cfg.outerScale);
    int oH = (int)(H * cfg.outerScale);

    createLayer(innerLayer,  iW, iH);
    createLayer(middleLayer, mW, mH);
    createLayer(outerLayer,  oW, oH);

    buildCompositeShader();
    buildQuad();
}

void FoveatedRenderer::beginZone(FoveaZone zone) {
    FoveaLayer* layer = nullptr;
    switch (zone) {
        case FoveaZone::INNER:  layer = &innerLayer;  break;
        case FoveaZone::MIDDLE: layer = &middleLayer; break;
        case FoveaZone::OUTER:  layer = &outerLayer;  break;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, layer->fbo);
    glViewport(0, 0, layer->width, layer->height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void FoveatedRenderer::endZone() {
    GLenum discard[] = { GL_DEPTH_ATTACHMENT };
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, discard);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void FoveatedRenderer::getViewport(FoveaZone zone,
                                   int& x, int& y, int& w, int& h) {
    x = 0; y = 0;
    switch (zone) {
        case FoveaZone::INNER:
            w = innerLayer.width;  h = innerLayer.height; break;
        case FoveaZone::MIDDLE:
            w = middleLayer.width; h = middleLayer.height; break;
        case FoveaZone::OUTER:
            w = outerLayer.width;  h = outerLayer.height; break;
    }
}

void FoveatedRenderer::composite() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, cfg.screenW, cfg.screenH);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(compositeProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, innerLayer.colorTex);
    glUniform1i(glGetUniformLocation(compositeProgram, "uInner"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, middleLayer.colorTex);
    glUniform1i(glGetUniformLocation(compositeProgram, "uMiddle"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, outerLayer.colorTex);
    glUniform1i(glGetUniformLocation(compositeProgram, "uOuter"), 2);

    glUniform2f(glGetUniformLocation(compositeProgram, "uCenter"),
                cfg.centerX, cfg.centerY);
    glUniform1f(glGetUniformLocation(compositeProgram, "uInnerR"),
                cfg.innerRadius);
    glUniform1f(glGetUniformLocation(compositeProgram, "uMiddleR"),
                cfg.middleRadius);

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

void FoveatedRenderer::buildQuad() {
    float verts[] = {
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
        -1.f,  1.f,  0.f, 1.f,
         1.f,  1.f,  1.f, 1.f,
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                          (void*)(2*sizeof(float)));
    glBindVertexArray(0);
}

void FoveatedRenderer::buildCompositeShader() {
    compositeProgram = compileProgram(COMPOSITE_VERT, COMPOSITE_FRAG);
}

void FoveatedRenderer::destroyLayer(FoveaLayer& layer) {
    glDeleteFramebuffers(1, &layer.fbo);
    glDeleteTextures(1, &layer.colorTex);
    glDeleteTextures(1, &layer.depthTex);
    layer = {};
}

void FoveatedRenderer::destroy() {
    destroyLayer(innerLayer);
    destroyLayer(middleLayer);
    destroyLayer(outerLayer);
    glDeleteProgram(compositeProgram);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
}
