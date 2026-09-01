package net.ltw.mixin;

import net.ltw.bridge.LTWBridge;
import net.minecraft.core.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Mixin on SectionRenderDispatcher$RenderSection.
 *
 * client.txt verified names:
 *   setSectionNode(long) -> c         [was: setOrigin - NOT in mapping]
 *   getRenderOrigin() -> f            [was: getOrigin - NOT in mapping]
 */
@Mixin(targets = "net.minecraft.client.renderer.chunk.SectionRenderDispatcher$RenderSection")
public abstract class SectionRenderDispatcherMixin {

    // getRenderOrigin() -> f in client.txt
    @Shadow public abstract BlockPos getRenderOrigin();

    // setSectionNode(long) -> c in client.txt
    @Inject(method = "setSectionNode", at = @At("RETURN"))
    private void ltw$onSetSectionNode(long sectionNode, CallbackInfo ci) {
        if (!LTWBridge.isAvailable()) return;
        BlockPos origin = getRenderOrigin();
        if (origin == null) return;
        int sectionId = System.identityHashCode(this);
        LTWBridge.registerChunkPosition(sectionId, (float) origin.getX(), (float) origin.getY(), (float) origin.getZ());
    }
}
