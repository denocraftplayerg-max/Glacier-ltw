package com.glacier.mixin;

import com.glacier.client.EntityRenderOptimizer;
import net.minecraft.client.renderer.culling.Frustum;
import net.minecraft.client.renderer.entity.EntityRenderDispatcher;
import net.minecraft.world.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(EntityRenderDispatcher.class)
public abstract class EntityRenderDispatcherMixin {

    @Inject(
        method = "shouldRender(Lnet/minecraft/world/entity/Entity;Lnet/minecraft/client/renderer/culling/Frustum;DDD)Z",
        at = @At("HEAD"),
        cancellable = true
    )
    private void glacier$cullEntityByDistance(
            Entity entity,
            Frustum frustum,
            double camX,
            double camY,
            double camZ,
            CallbackInfoReturnable<Boolean> cir) {

        if (!EntityRenderOptimizer.withinDistance(entity, camX, camY, camZ)) {
            cir.setReturnValue(false);
        }
    }
}
