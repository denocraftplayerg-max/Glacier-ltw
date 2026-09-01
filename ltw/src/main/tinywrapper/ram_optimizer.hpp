#pragma once
#include <GLES3/gl32.h>
#include <vector>
#include <unordered_map>
#include <cstdint>

class BufferPool {
public:
    struct Entry { GLuint id; size_t size; bool inUse; };

    GLuint acquire(size_t size, GLenum target = GL_ARRAY_BUFFER);
    void   release(GLuint id);
    void   trimUnused();
    void   destroyAll();

private:
    std::vector<Entry> pool;
};

class ChunkStreamBuffer {
public:
    static const int BUFFER_COUNT = 2;

    void init(size_t chunkBudgetBytes);
    void* mapForWrite(size_t bytes);
    void  unmap();
    GLuint activeBuffer() const { return buffers[current]; }
    void   swap() { current = (current + 1) % BUFFER_COUNT; }
    void   destroy();

private:
    GLuint buffers[BUFFER_COUNT] = {};
    int    current = 0;
    size_t budgetBytes = 0;
};

class TextureCache {
public:
    struct Entry {
        GLuint texId;
        size_t vramBytes;
        uint64_t lastUsed;
    };

    void   init(size_t vramBudgetBytes);
    GLuint get(uint64_t key);
    void   put(uint64_t key, GLuint texId, size_t vramBytes);
    void   evictLRU(size_t needed);
    void   onFrameEnd(uint64_t frame);
    void   destroyAll();

private:
    size_t budgetBytes = 0;
    size_t usedBytes   = 0;
    std::unordered_map<uint64_t, Entry> cache;
};

class ChunkVBOPacker {
public:
    struct Region { size_t offset; size_t size; int chunkId; };

    void  init(size_t vboSizeBytes);
    bool  alloc(int chunkId, size_t bytes, size_t& outOffset);
    void  free(int chunkId);
    void  defrag();
    GLuint vbo() const { return _vbo; }
    void  destroy();

private:
    GLuint _vbo = 0;
    size_t _totalSize = 0;
    std::vector<Region> regions;
};
