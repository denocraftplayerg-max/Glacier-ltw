package com.glacier.client;

import com.glacier.config.GlacierConfig;
import net.minecraft.world.entity.Entity;

public final class EntityRenderOptimizer {

    private static final double MAX_DISTANCE_SQ =
            (double) GlacierConfig.ENTITY_RENDER_DISTANCE
                    * GlacierConfig.ENTITY_RENDER_DISTANCE;

    private EntityRenderOptimizer() {}

    public static boolean withinDistance(Entity entity,
            double cameraX, double cameraY, double cameraZ) {
        double dx = entity.getX() - cameraX;
        double dy = entity.getY() - cameraY;
        double dz = entity.getZ() - cameraZ;
        return dx * dx + dy * dy + dz * dz <= MAX_DISTANCE_SQ;
    }
}
