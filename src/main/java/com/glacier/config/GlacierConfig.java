package com.glacier.config;

public final class GlacierConfig {

    private GlacierConfig() {}

    public static final boolean ENTITY_CULLING = true;
    public static final boolean PARTICLE_CULLING = true;
    public static final boolean CHUNK_PRIORITY = true;
    public static final boolean CHUNK_DEDUPLICATION = true;
    public static final boolean TICK_BUDGET = true;

    public static final int ENTITY_RENDER_DISTANCE = 128;
    public static final int PARTICLE_RENDER_DISTANCE = 96;

    public static final int MAX_ENTITY_SNAPSHOT = 4096;
    public static final int MAX_PARTICLE_SNAPSHOT = 8192;

    public static final int INITIAL_ENTITY_CAPACITY = 256;
    public static final int INITIAL_PARTICLE_CAPACITY = 512;

    public static final long TICK_BUDGET_NS = 50_000_000L;
    public static final long RESERVED_TICK_NS = 4_000_000L;
}
