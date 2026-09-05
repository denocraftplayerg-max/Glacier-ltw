package net.ltw.render.frustum;

import net.minecraft.world.phys.AABB;
import org.joml.FrustumIntersection;
import org.joml.Matrix4f;
import org.joml.Vector4f;

public class VFrustum {
    private Vector4f viewVector = new Vector4f();
    private double camX;
    private double camY;
    private double camZ;

    private final FrustumIntersection frustum = new FrustumIntersection();
    private final Matrix4f matrix = new Matrix4f();

    public void setCamOffset(double camX, double camY, double camZ) {
        this.camX = camX;
        this.camY = camY;
        this.camZ = camZ;
    }

    public void calculateFrustum(Matrix4f modelViewMatrix, Matrix4f projMatrix) {
        projMatrix.mul(modelViewMatrix, this.matrix);
        this.frustum.set(this.matrix, false);
        this.viewVector = this.matrix.transformTranspose(new Vector4f(0.0F, 0.0F, 1.0F, 0.0F));
    }

    public boolean isVisible(AABB aabb) {
        return this.frustum.testAab(
                (float)(aabb.minX - this.camX), (float)(aabb.minY - this.camY), (float)(aabb.minZ - this.camZ),
                (float)(aabb.maxX - this.camX), (float)(aabb.maxY - this.camY), (float)(aabb.maxZ - this.camZ));
    }

    public boolean isVisible(double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
        return this.frustum.testAab(
                (float)(minX - this.camX), (float)(minY - this.camY), (float)(minZ - this.camZ),
                (float)(maxX - this.camX), (float)(maxY - this.camY), (float)(maxZ - this.camZ));
    }

    public int intersectAab(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
        return this.frustum.intersectAab(minX, minY, minZ, maxX, maxY, maxZ);
    }

    public Vector4f getViewVector() { return viewVector; }
}
