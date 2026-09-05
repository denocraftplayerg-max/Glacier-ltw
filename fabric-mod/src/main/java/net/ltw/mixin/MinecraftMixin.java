package net.ltw.mixin;

import net.ltw.bridge.LTWBridge;
import net.minecraft.client.GraphicsPreset;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Options;
import net.minecraft.client.main.GameConfig;
import org.objectweb.asm.Opcodes;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Minecraft.class)
public class MinecraftMixin {

    @Shadow public boolean noRender;
    @Shadow @Final public Options options;

    // NOTE: Thread.yield() in runTick causes stalls on mobile; we cancel the
    // surrounding sleep by injecting at the INVOKE opcode target in Minecraft bytecode.
    // Using INVOKE on Thread.yield with the correct obfuscated name from mapping.
    @Redirect(
        method = "runTick",
        at = @At(
            value = "INVOKE",
            target = "Ljava/lang/Thread;yield()V",
            remap = false
        )
    )
    private void ltw$removeThreadYield() {
        // Suppress Thread.yield() to prevent mobile GPU pipeline stalls
    }

    @Inject(method = "<init>", at = @At("RETURN"))
    private void ltw$forceGraphicsMode(GameConfig gameConfig, CallbackInfo ci) {
        var graphicsModeOption = this.options.graphicsPreset();
        if (graphicsModeOption.get() == GraphicsPreset.FABULOUS) {
            System.out.println("[LTW-Bridge] Fabulous graphics not supported, forcing Fancy.");
            graphicsModeOption.set(GraphicsPreset.FANCY);
        }

        if (this.options.improvedTransparency().get()) {
            System.out.println("[LTW-Bridge] Improved transparency not supported, disabling.");
            this.options.improvedTransparency().set(false);
        }
    }

    @Redirect(method = "setScreen", at = @At(value = "FIELD", target = "Lnet/minecraft/client/Minecraft;noRender:Z", opcode = Opcodes.PUTFIELD))
    private void ltw$keepNoRender(Minecraft instance, boolean value) {
    }
}
