#pragma once
#include <GLES3/gl32.h>

struct TessConfig {
    float maxTessLevel  = 8.0f;
    float minTessLevel  = 1.0f;
    float nearDist      = 8.0f;
    float farDist       = 128.0f;
    float lodBias       = 0.0f;
};

class TessLODSystem {
public:
    GLuint program = 0;

    void init();
    void bind(const TessConfig& cfg, const float* camPos);
    void destroy();

private:
    static const char* VERT_SRC;
    static const char* TCS_SRC;
    static const char* TES_SRC;
    static const char* FRAG_SRC;
};
