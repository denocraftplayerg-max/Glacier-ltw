package net.ltw.mixin;

import com.mojang.blaze3d.resource.GraphicsResourceAllocator;
import com.mojang.blaze3d.buffers.GpuBufferSlice;
import net.ltw.bridge.LTWBridge;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.culling.Frustum;
import org.joml.Matrix4f;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Hooks into LevelRenderer.renderLevel for frame synchronization.
 * Signature matched to client.txt:
 *   479:616:void renderLevel(GraphicsResourceAllocator, DeltaTracker, boolean,
 *       Camera, Matrix4f, Matrix4f, Matrix4f, GpuBufferSlice, Vector4f, boolean) -> a
 */
@Mixin(LevelRenderer.class)
public class LevelRendererMixin {

    @Shadow private ClientLevel level;

    @Inject(method = "renderLevel", at = @At("HEAD"))
    private void ltw$onRenderLevelStart(
        GraphicsResourceAllocator allocator,
        DeltaTracker deltaTracker,
        boolean renderBlockOutline,
        Camera camera,
        Matrix4f modelViewMatrix,
        Matrix4f projectionMatrix,
        Matrix4f frustumMatrix,
        GpuBufferSlice lightBuffer,
        Vector4f fogColor,
        boolean isFoggy,
        CallbackInfo ci
    ) {
        if (!LTWBridge.isAvailable()) return;
        // Frame synchronization: clear previous frame's chunk table
        LTWBridge.clearChunkPositions();

        if (camera != null) {
            var pos = camera.getPosition();
            float partialTick = deltaTracker.getGameTimeDeltaPartialTick(true);
            float timeOfDay = (this.level != null) ? this.level.getTimeOfDay(partialTick) : 0.5f;
            float sunAngle = timeOfDay * ((float) Math.PI * 2.0F);
            float sunX = -(float) Math.sin(sunAngle);
            float sunY = (float) Math.cos(sunAngle);
            LTWBridge.uploadSunAndCamera(sunX, sunY, 0.0f, timeOfDay, (float) pos.x, (float) pos.y, (float) pos.z);
        }
    }
}
