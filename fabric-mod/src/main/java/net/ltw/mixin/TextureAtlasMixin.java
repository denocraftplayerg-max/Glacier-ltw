package net.ltw.mixin;

import com.mojang.blaze3d.opengl.GlTexture;
import com.mojang.blaze3d.platform.NativeImage;
import com.mojang.blaze3d.textures.GpuTexture;
import net.ltw.bridge.LTWBridge;
import net.ltw.texture.ResourcePackTracker;
import net.minecraft.client.renderer.texture.AbstractTexture;
import net.minecraft.client.renderer.texture.TextureAtlas;
import net.minecraft.resources.Identifier;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL30;
import org.lwjgl.system.MemoryUtil;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Phase 1 — ASTC injection (vanilla only):
 *   ASTC exists -> inject via glCompressedTexImage2D, cancel PNG upload.
 *
 * Phase 2 — Capture (resourcepack active):
 *   PNG used normally. After upload, atlas read back from GPU,
 *   saved as PNG to /sdcard/minecraft_astc/captured/<pack_key>/<atlas>.png
 *   ONCE per resourcepack combination.
 */
@Mixin(TextureAtlas.class)
public abstract class TextureAtlasMixin extends AbstractTexture {

    @Shadow private Identifier location;
    @Shadow private int width;
    @Shadow private int height;

    @Unique private boolean ltw$astcInjected = false;

    @Inject(method = "uploadInitialContents", at = @At("HEAD"), cancellable = true)
    private void ltw$tryInjectAstc(CallbackInfo ci) {
        ltw$astcInjected = false;
        ResourcePackTracker.onAtlasUpload();

        // Only try ASTC when vanilla-only
        if (!ResourcePackTracker.isVanillaOnly()) return;

        if (!LTWBridge.isAvailable() || this.location == null) return;

        GpuTexture gpuTexture = this.getTexture();
        if (!(gpuTexture instanceof GlTexture)) return;
        int glId = ((GlTexture) gpuTexture).glId();

        if (LTWBridge.loadExternalAstc(this.location.toString(), glId)) {
            ltw$astcInjected = true;
            ci.cancel();
        }
    }

    @Inject(method = "uploadInitialContents", at = @At("RETURN"))
    private void ltw$captureAtlasPng(CallbackInfo ci) {
        if (ltw$astcInjected) return;
        if (!ResourcePackTracker.isCaptureMode()) return;
        if (this.location == null || this.width <= 0 || this.height <= 0) return;

        try {
            captureFromGpu();
        } catch (Throwable t) {
            System.out.println("[LTW-Bridge] Atlas capture failed: " + t.getMessage());
        }
    }

    @Unique
    private void captureFromGpu() {
        GpuTexture gpuTexture = this.getTexture();
        if (!(gpuTexture instanceof GlTexture)) return;
        int glId = ((GlTexture) gpuTexture).glId();
        int w = this.width;
        int h = this.height;

        String atlasName = flattenName(this.location.toString());
        Path capturePath = Path.of(ResourcePackTracker.getCaptureDir() + "/" + atlasName + ".png");

        if (Files.exists(capturePath)) return; // guardar 1 vez

        try { Files.createDirectories(capturePath.getParent()); } catch (Throwable ignored) {}

        // FBO + glReadPixels to read atlas from GPU
        int fbo = GL30.glGenFramebuffers();
        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, fbo);
        GL30.glFramebufferTexture2D(GL30.GL_FRAMEBUFFER, GL30.GL_COLOR_ATTACHMENT0,
                                    GL11.GL_TEXTURE_2D, glId, 0);

        if (GL30.glCheckFramebufferStatus(GL30.GL_FRAMEBUFFER) != GL30.GL_FRAMEBUFFER_COMPLETE) {
            GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, 0);
            GL30.glDeleteFramebuffers(fbo);
            return;
        }

        ByteBuffer rawBuffer = MemoryUtil.memAlloc(w * h * 4);
        GL11.glReadPixels(0, 0, w, h, GL11.GL_RGBA, GL11.GL_UNSIGNED_BYTE, rawBuffer);

        // Flip vertically (GL bottom-to-top -> NativeImage top-to-bottom)
        NativeImage image = new NativeImage(NativeImage.Format.RGBA, w, h, false);
        long dst = ((NativeImageAccessor) (Object) image).ltw$getPixels();

        for (int y = 0; y < h; y++) {
            long srcPtr = MemoryUtil.memAddress(rawBuffer) + (long)(h - 1 - y) * w * 4;
            long dstPtr = dst + (long) y * w * 4;
            MemoryUtil.memCopy(srcPtr, dstPtr, (long) w * 4);
        }

        GL30.glBindFramebuffer(GL30.GL_FRAMEBUFFER, 0);
        GL30.glDeleteFramebuffers(fbo);
        MemoryUtil.memFree(rawBuffer);

        try {
            image.writeToFile(capturePath);
            System.out.println("[LTW-Bridge] Captured: " + atlasName + " (" + w + "x" + h + ") -> " + capturePath);
        } catch (java.io.IOException e) {
            System.err.println("[LTW-Bridge] Failed to write captured atlas: " + e.getMessage());
        } finally {
            image.close();
        }
    }

    @Unique
    private static String flattenName(String identifier) {
        String s = identifier.replace(":", "_").replace("/", "_");
        if (s.endsWith(".png")) s = s.substring(0, s.length() - 4);
        return s;
    }
}
