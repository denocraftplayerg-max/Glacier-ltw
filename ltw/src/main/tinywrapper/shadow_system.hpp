#pragma once
#include <GLES3/gl32.h>

struct ShadowConfig {
    int   cascadeCount    = 3;
    int   innerResolution = 2048;
    int   midResolution   = 1024;
    int   outerResolution = 512;
    float cascadeSplits[3] = { 16.0f, 64.0f, 256.0f };
    float shadowBias        = 0.003f;
    float slopeBias         = 0.5f;
    float darkness          = 0.85f;
};

struct ShadowCascade {
    GLuint fbo        = 0;
    GLuint depthTex   = 0;
    int    resolution = 0;
    float  lightVP[16];
    float  splitDist;
};

class ShadowSystem {
public:
    ShadowConfig cfg;
    ShadowCascade cascades[3];

    GLuint shadowProgram = 0;

    void init(const ShadowConfig& config);
    void updateCascades(const float* sunDir, const float* camPos,
                        const float* camFrustumCorners, float fovY,
                        float aspect, float nearZ);
    void renderCascade(int cascadeIdx, GLuint sceneVAO,
                       GLuint sceneIBO, int indexCount);
    void bindForLighting(GLuint lightProgram);
    void destroy();

private:
    void createCascade(ShadowCascade& c, int res);
    void destroyCascade(ShadowCascade& c);
    void buildShadowProgram();

    static const char* SHADOW_VERT;
    static const char* SHADOW_FRAG;
};
