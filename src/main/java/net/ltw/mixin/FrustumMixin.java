package net.ltw.mixin;

import net.ltw.bridge.FrustumExtractor;
import net.ltw.bridge.LTWBridge;
import net.ltw.interfaces.FrustumMixed;
import net.ltw.render.frustum.VFrustum;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.world.phys.AABB;
import org.joml.Matrix4f;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Frustum.class)
public class FrustumMixin implements FrustumMixed {

    @Shadow private double camX;
    @Shadow private double camY;
    @Shadow private double camZ;
    @Shadow @Final private Matrix4f matrix;
    @Shadow private Vector4f viewVector;

    @Unique private final VFrustum vFrustum = new VFrustum();
    @Unique private final Vector4f ltw$forward = new Vector4f(0.0F, 0.0F, 1.0F, 0.0F);

    @Inject(method = "calculateFrustum", at = @At("HEAD"))
    private void ltw$calculateFrustum(Matrix4f modelView, Matrix4f projection, CallbackInfo ci) {
        this.vFrustum.calculateFrustum(modelView, projection);
        this.ltw$forward.set(0.0F, 0.0F, 1.0F, 0.0F);
        this.viewVector = this.matrix.transformTranspose(this.ltw$forward);

        if (LTWBridge.isAvailable()) {
            try {
                float[] planes = FrustumExtractor.extract(modelView, projection);
                if (planes != null) {
                    LTWBridge.updateFrustumPlanes(planes);
                }
            } catch (Throwable t) {
            }
        }
    }

    @Inject(method = "prepare", at = @At("RETURN"))
    public void ltw$prepareReturn(double d, double e, double f, CallbackInfo ci) {
        this.vFrustum.setCamOffset(this.camX, this.camY, this.camZ);
    }

    @Redirect(method = "isVisible(Lnet/minecraft/world/phys/AABB;)Z", at = @At(value = "INVOKE", target = "Lnet/minecraft/client/renderer/culling/Frustum;cubeInFrustum(DDDDDD)I"))
    private int ltw$isVisibleRedirect(Frustum instance, double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
        return this.vFrustum.isVisible(minX, minY, minZ, maxX, maxY, maxZ) ? 1 : 0;
    }

    @Override
    public VFrustum customFrustum() {
        return vFrustum;
    }
}
