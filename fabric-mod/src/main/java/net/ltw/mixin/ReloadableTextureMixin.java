package net.ltw.mixin;

import com.mojang.blaze3d.opengl.GlTexture;
import com.mojang.blaze3d.textures.GpuTexture;
import net.ltw.bridge.LTWBridge;
import net.minecraft.client.renderer.texture.AbstractTexture;
import net.minecraft.client.renderer.texture.ReloadableTexture;
import net.minecraft.resources.Identifier;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Intercepts ReloadableTexture.doLoad(NativeImage) at RETURN.
 * After the PNG is uploaded to GPU, tries to overwrite the texture
 * data with ASTC via glCompressedTexImage2D.
 *
 * Non-destructive: if the .astc file doesn't exist or glCompressedTexImage2D
 * fails (e.g. immutable storage), the PNG pixels already on GPU remain.
 *
 * This handles individual textures from resourcepacks, NOT the atlas.
 * The atlas is handled separately by TextureAtlasMixin.
 *
 * ResourceId is e.g. "minecraft:textures/block/stone.png"
 * C++ converts to "/sdcard/minecraft_astc/textures/block/stone.astc"
 */
@Mixin(ReloadableTexture.class)
public abstract class ReloadableTextureMixin extends AbstractTexture {

    @Shadow
    private Identifier resourceId;

    @Inject(method = "doLoad", at = @At("RETURN"))
    private void ltw$injectAstcTexture(CallbackInfo ci) {
        if (!LTWBridge.isAvailable()) return;
        if (this.resourceId == null) return;

        GpuTexture gpuTexture = this.getTexture();
        if (!(gpuTexture instanceof GlTexture)) return;
        int glId = ((GlTexture) gpuTexture).glId();

        // Try ASTC overwrite. If it fails, PNG data remains on GPU.
        LTWBridge.loadExternalAstc(this.resourceId.toString(), glId);
    }
}
