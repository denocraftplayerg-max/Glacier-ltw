package com.glacier.client;

import com.glacier.config.GlacierConfig;

public final class ChunkRenderOptimizer {

    private static final LongSet DIRTY = new LongSet(512);
    private static long frame;

    private ChunkRenderOptimizer() {}

    public static void beginFrame() { frame++; }

    public static boolean markDirty(int chunkX, int chunkZ) {
        if (!GlacierConfig.CHUNK_DEDUPLICATION) return true;
        return DIRTY.add(pack(chunkX, chunkZ));
    }

    public static void clearDirty(int chunkX, int chunkZ) {
        DIRTY.remove(pack(chunkX, chunkZ));
    }

    public static boolean isDirty(int chunkX, int chunkZ) {
        return DIRTY.contains(pack(chunkX, chunkZ));
    }

    public static int pending() { return DIRTY.size(); }

    public static void clear() { DIRTY.clear(); }

    private static long pack(int x, int z) {
        return ((long) x << 32) ^ (z & 0xffffffffL);
    }
}
