#pragma once
#include <GLES3/gl32.h>
#include <vector>

struct Light {
    float pos[3];
    float radius;
    float color[3];
    float intensity;
    int   type;       // 0=point, 1=sun, 2=sky
    float padding[3];
};

class LightEngine {
public:
    GLuint lightSSBO       = 0;
    GLuint geometryPass    = 0;
    GLuint lightingPass    = 0;
    GLuint shadowProgram   = 0;

    static const int MAX_LIGHTS = 128;

    void init();
    void uploadLights(const std::vector<Light>& lights);
    void renderGeometryPass();
    void renderLightingPass(float sunDir[3], float timeOfDay);
    void destroy();

private:
    int lightCount = 0;
    static const char* GEOMETRY_VERT_SRC;
    static const char* GEOMETRY_FRAG_SRC;
    static const char* LIGHTING_FRAG_SRC;
};
