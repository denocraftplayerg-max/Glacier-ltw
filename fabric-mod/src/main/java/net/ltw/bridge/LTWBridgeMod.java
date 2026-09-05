package net.ltw.bridge;

import net.fabricmc.api.ClientModInitializer;

public class LTWBridgeMod implements ClientModInitializer {
    @Override
    public void onInitializeClient() {
        System.out.println("[LTW-Bridge] Initializing GPU-driven culling bridge...");
        LTWBridge.tryLoad();
        if (LTWBridge.isAvailable()) {
            System.out.println("[LTW-Bridge] Ready. Will feed chunk positions + frustum to LTW.");
        } else {
            System.out.println("[LTW-Bridge] Disabled (LTW not loaded). Culling will use fallback.");
        }
    }
}
