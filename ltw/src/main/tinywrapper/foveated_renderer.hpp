#pragma once
#include <GLES3/gl32.h>

enum class FoveaZone { INNER, MIDDLE, OUTER };

struct FoveaConfig {
    float innerRadius  = 0.25f;
    float middleRadius = 0.55f;
    float innerScale   = 1.00f;
    float middleScale  = 0.60f;
    float outerScale   = 0.25f;
    float centerX = 0.5f;
    float centerY = 0.5f;
    int screenW = 1280;
    int screenH = 720;
};

struct FoveaLayer {
    GLuint fbo        = 0;
    GLuint colorTex   = 0;
    GLuint depthTex   = 0;
    int    width      = 0;
    int    height     = 0;
};

class FoveatedRenderer {
public:
    FoveaConfig cfg;

    FoveaLayer innerLayer;
    FoveaLayer middleLayer;
    FoveaLayer outerLayer;

    GLuint compositeProgram = 0;
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;

    bool hasIMGDownsample = false;

    void init(const FoveaConfig& config, bool imgDownsample);
    void beginZone(FoveaZone zone);
    void endZone();
    void composite();
    void destroy();

    void getViewport(FoveaZone zone, int& x, int& y, int& w, int& h);

private:
    void createLayer(FoveaLayer& layer, int w, int h);
    void destroyLayer(FoveaLayer& layer);
    void buildCompositeShader();
    void buildQuad();

    static const char* COMPOSITE_VERT;
    static const char* COMPOSITE_FRAG;
};
