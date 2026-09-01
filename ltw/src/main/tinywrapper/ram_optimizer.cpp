#include "ram_optimizer.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG,"HYENGRA",__VA_ARGS__)
#else
#define LOG(...) printf(__VA_ARGS__)
#endif

// ── BufferPool ───────────────────────────────────────────────────────────────
GLuint BufferPool::acquire(size_t size, GLenum target) {
    for (auto& e : pool) {
        if (!e.inUse && e.size >= size) {
            e.inUse = true;
            return e.id;
        }
    }
    Entry e;
    glGenBuffers(1, &e.id);
    glBindBuffer(target, e.id);
    glBufferData(target, (GLsizeiptr)size, nullptr, GL_DYNAMIC_DRAW);
    e.size  = size;
    e.inUse = true;
    pool.push_back(e);
    return e.id;
}

void BufferPool::release(GLuint id) {
    for (auto& e : pool)
        if (e.id == id) { e.inUse = false; return; }
}

void BufferPool::trimUnused() {
    for (auto it = pool.begin(); it != pool.end(); ) {
        if (!it->inUse) {
            glDeleteBuffers(1, &it->id);
            it = pool.erase(it);
        } else ++it;
    }
}

void BufferPool::destroyAll() {
    for (auto& e : pool) glDeleteBuffers(1, &e.id);
    pool.clear();
}

// ── ChunkStreamBuffer ────────────────────────────────────────────────────────
void ChunkStreamBuffer::init(size_t budgetBytes_) {
    budgetBytes = budgetBytes_;
    glGenBuffers(BUFFER_COUNT, buffers);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, buffers[i]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)budgetBytes,
                     nullptr, GL_STREAM_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void* ChunkStreamBuffer::mapForWrite(size_t bytes) {
    glBindBuffer(GL_ARRAY_BUFFER, buffers[current]);
    return glMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes,
        GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
        GL_MAP_UNSYNCHRONIZED_BIT);
}

void ChunkStreamBuffer::unmap() {
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ChunkStreamBuffer::destroy() {
    glDeleteBuffers(BUFFER_COUNT, buffers);
}

// ── TextureCache LRU ─────────────────────────────────────────────────────────
void TextureCache::init(size_t budget) {
    budgetBytes = budget;
}

GLuint TextureCache::get(uint64_t key) {
    auto it = cache.find(key);
    if (it == cache.end()) return 0;
    return it->second.texId;
}

void TextureCache::put(uint64_t key, GLuint texId, size_t vramBytes) {
    if (usedBytes + vramBytes > budgetBytes)
        evictLRU(vramBytes);
    cache[key] = {texId, vramBytes, 0};
    usedBytes += vramBytes;
}

void TextureCache::evictLRU(size_t needed) {
    std::vector<std::pair<uint64_t,uint64_t>> ordered;
    for (auto& [k,v] : cache) ordered.push_back({v.lastUsed, k});
    std::sort(ordered.begin(), ordered.end());

    for (auto& [lu, key] : ordered) {
        if (usedBytes + needed <= budgetBytes) break;
        auto& e = cache[key];
        glDeleteTextures(1, &e.texId);
        usedBytes -= e.vramBytes;
        cache.erase(key);
    }
}

void TextureCache::onFrameEnd(uint64_t frame) {
    for (auto& [k,v] : cache)
        if (v.lastUsed == 0) v.lastUsed = frame;
}

void TextureCache::destroyAll() {
    for (auto& [k,v] : cache) glDeleteTextures(1, &v.texId);
    cache.clear(); usedBytes = 0;
}

// ── ChunkVBOPacker ───────────────────────────────────────────────────────────
void ChunkVBOPacker::init(size_t vboSize) {
    _totalSize = vboSize;
    glGenBuffers(1, &_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vboSize, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool ChunkVBOPacker::alloc(int chunkId, size_t bytes, size_t& outOffset) {
    size_t cursor = 0;
    for (auto& r : regions) {
        size_t gap = r.offset - cursor;
        if (gap >= bytes) {
            outOffset = cursor;
            regions.push_back({cursor, bytes, chunkId});
            std::sort(regions.begin(), regions.end(),
                [](const Region& a, const Region& b){ return a.offset < b.offset; });
            return true;
        }
        cursor = r.offset + r.size;
    }
    if (cursor + bytes <= _totalSize) {
        outOffset = cursor;
        regions.push_back({cursor, bytes, chunkId});
        return true;
    }
    return false;
}

void ChunkVBOPacker::free(int chunkId) {
    regions.erase(std::remove_if(regions.begin(), regions.end(),
        [chunkId](const Region& r){ return r.chunkId == chunkId; }),
        regions.end());
}

void ChunkVBOPacker::defrag() {
    size_t cursor = 0;
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);
    for (auto& r : regions) {
        if (r.offset != cursor) {
            std::vector<uint8_t> tmp(r.size);
            glGetBufferSubData(GL_ARRAY_BUFFER, (GLintptr)r.offset,
                               (GLsizeiptr)r.size, tmp.data());
            glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)cursor,
                            (GLsizeiptr)r.size, tmp.data());
            r.offset = cursor;
        }
        cursor += r.size;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ChunkVBOPacker::destroy() {
    glDeleteBuffers(1, &_vbo);
    regions.clear();
}
