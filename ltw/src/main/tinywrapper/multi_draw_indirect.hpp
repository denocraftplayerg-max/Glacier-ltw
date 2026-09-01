#pragma once
#include <GLES3/gl32.h>

class MultiDrawIndirectEmulated {
public:
    void draw(GLuint drawCmdBuffer,
              GLuint drawCountBuffer,
              GLuint vao,
              GLuint ibo,
              int    maxDraws);

    void drawInstanced(GLuint vao, GLuint ibo,
                       int indexCount, int instanceCount);
};
