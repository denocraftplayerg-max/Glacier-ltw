package com.glacier.client;

import com.glacier.config.GlacierConfig;
import net.minecraft.client.particle.Particle;

public final class ParticleOptimizer {

    private static final double MAX_DISTANCE_SQ =
            (double) GlacierConfig.PARTICLE_RENDER_DISTANCE
                    * GlacierConfig.PARTICLE_RENDER_DISTANCE;

    private ParticleOptimizer() {}

    public static boolean withinDistance(Particle particle,
            double cameraX, double cameraY, double cameraZ) {
        double dx = particle.getX() - cameraX;
        double dy = particle.getY() - cameraY;
        double dz = particle.getZ() - cameraZ;
        return dx * dx + dy * dy + dz * dz <= MAX_DISTANCE_SQ;
    }
}
