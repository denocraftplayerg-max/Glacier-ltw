#include "multi_draw_indirect.hpp"
#include <cstdint>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG,"HYENGRA",__VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

struct MdiDrawCommand {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint  baseVertex;
    GLuint baseInstance;
};

void MultiDrawIndirectEmulated::draw(
        GLuint drawCmdBuffer,
        GLuint drawCountBuffer,
        GLuint vao,
        GLuint ibo,
        int    maxDraws)
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawCountBuffer);
    GLuint* pCount = (GLuint*)glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER, 0, sizeof(GLuint),
        GL_MAP_READ_BIT);
    GLuint actualDraws = pCount ? *pCount : 0;
    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

    if (actualDraws == 0) return;
    if ((int)actualDraws > maxDraws) actualDraws = maxDraws;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, drawCmdBuffer);
    MdiDrawCommand* cmds = (MdiDrawCommand*)glMapBufferRange(
        GL_SHADER_STORAGE_BUFFER, 0,
        actualDraws * sizeof(MdiDrawCommand),
        GL_MAP_READ_BIT);

    if (!cmds) {
        glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        return;
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    for (GLuint i = 0; i < actualDraws; i++) {
        const MdiDrawCommand& cmd = cmds[i];
        if (cmd.instanceCount == 0) continue;

        glDrawElementsInstanced(
            GL_TRIANGLES,
            (GLsizei)cmd.count,
            GL_UNSIGNED_INT,
            (const void*)(uintptr_t)(cmd.firstIndex * sizeof(GLuint)),
            (GLsizei)cmd.instanceCount
        );
    }

    glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    glBindVertexArray(0);
}

void MultiDrawIndirectEmulated::drawInstanced(
        GLuint vao, GLuint ibo, int indexCount, int instanceCount)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0, instanceCount);
    glBindVertexArray(0);
}
