package com.glacier.mixin;

import com.glacier.client.RenderFrame;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Camera;
import net.minecraft.client.renderer.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Minecraft.class)
public abstract class MinecraftMixin {

    @Unique
    private final RenderFrame glacier$frame = new RenderFrame();

    @Inject(method = "runTick", at = @At("HEAD"))
    private void glacier$beginFrame(boolean renderLevel, CallbackInfo ci) {
        Minecraft minecraft = (Minecraft) (Object) this;
        GameRenderer gameRenderer = minecraft.gameRenderer;
        if (gameRenderer == null) return;
        Camera camera = gameRenderer.getMainCamera();
        var position = camera.getPosition();
        glacier$frame.begin(
                position.x, position.y, position.z,
                camera.getYRot(), camera.getXRot());
    }
}
