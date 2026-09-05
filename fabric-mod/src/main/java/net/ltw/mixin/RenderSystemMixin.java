package net.ltw.mixin;

import com.mojang.blaze3d.shaders.ShaderSource;
import com.mojang.blaze3d.systems.RenderSystem;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(RenderSystem.class)
public class RenderSystemMixin {

    @Inject(method = "initRenderer", at = @At("HEAD"), remap = false)
    private static void ltw$boostThreadPriority(long window, int glVersion, boolean vSync,
                                                  ShaderSource shaderSource, boolean bl2, CallbackInfo ci) {
        Thread.currentThread().setPriority(Thread.NORM_PRIORITY + 2);
    }
}
