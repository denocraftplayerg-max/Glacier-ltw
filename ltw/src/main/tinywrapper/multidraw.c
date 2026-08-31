/**
 * Created by: artDev
 * Copyright (c) 2025 artDev, SerpentSpirale, CADIndie.
 * For use under LGPL-3.0
 */

#include <proc.h>
#include <egl.h>
#include "basevertex.h"
void glMultiDrawArrays( GLenum mode, GLint *first, GLsizei *count, GLsizei primcount )
{
    // We'd need to merge each buffer attached to the VBO to properly achieve this. Nuh-uh. Aint no way im doin allat
    if(!current_context) return;
    for (int i = 0; i < primcount; i++) {
        if (count[i] > 0)
            es3_functions.glDrawArrays(mode, first[i], count[i]);
    }
}

void glMultiDrawElements( GLenum mode, GLsizei *count, GLenum type, const void * const *indices, GLsizei primcount )
{
    if(!current_context) return;
    GLint elementbuffer;
    es3_functions.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementbuffer);
    es3_functions.glBindBuffer(GL_COPY_WRITE_BUFFER, current_context->multidraw_element_buffer);
    GLsizei total = 0, offset = 0, typebytes = type_bytes(type);
    for (GLsizei i = 0; i < primcount; i++) {
        total += count[i];
    }
    es3_functions.glBufferData(GL_COPY_WRITE_BUFFER, total*typebytes, NULL, GL_STREAM_DRAW);
    for (GLsizei i = 0; i < primcount; i++) {
        GLsizei icount = count[i];
        if(icount == 0) continue;
        icount *= typebytes;
        if(elementbuffer != 0) {
            es3_functions.glCopyBufferSubData(GL_ELEMENT_ARRAY_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)indices[i], offset, icount);
        }else {
            es3_functions.glBufferSubData(GL_COPY_WRITE_BUFFER, offset, icount, indices[i]);
        }
        offset += icount;
    }
    es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, current_context->multidraw_element_buffer);
    es3_functions.glDrawElements(mode, total, type, 0);
    if(elementbuffer != 0) es3_functions.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);

}

/* ═══════════════════════════════════════════
 * Emulação de MultiDrawIndirect (GLES 3.1)
 * ═══════════════════════════════════════════ */

void glMultiDrawArraysIndirect(GLenum mode, const void *indirect, GLsizei drawcount, GLsizei stride) {
    if(!current_context) return;
    if(!stride) stride = sizeof(GLuint) * 4; /* count, instanceCount, first, baseInstance */
    const char* ptr = (const char*)indirect;
    for(GLsizei i = 0; i < drawcount; i++) {
        const GLuint* cmd = (const GLuint*)(ptr + i * stride);
        GLuint count = cmd[0];
        GLuint instanceCount = cmd[1];
        GLuint first = cmd[2];
        /* baseInstance = cmd[3] (ignorado em ES) */
        if(count == 0) continue;
        if(instanceCount > 1) {
            es3_functions.glDrawArraysInstanced(mode, first, count, instanceCount);
        } else {
            es3_functions.glDrawArrays(mode, first, count);
        }
    }
}

void glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect, GLsizei drawcount, GLsizei stride) {
    if(!current_context) return;
    if(!stride) stride = sizeof(GLuint) * 5; /* count, instanceCount, firstIndex, baseVertex, baseInstance */
    const char* ptr = (const char*)indirect;
    for(GLsizei i = 0; i < drawcount; i++) {
        const GLuint* cmd = (const GLuint*)(ptr + i * stride);
        GLuint count = cmd[0];
        GLuint instanceCount = cmd[1];
        if(count == 0) continue;
        /* Usar glDrawElementsIndirect para cada draw individualmente.
         * Isso é correto porque glDrawElementsIndirect processa
         * firstIndex e baseVertex nativamente. */
        es3_functions.glDrawElementsIndirect(mode, type, ptr + i * stride);
    }
}
