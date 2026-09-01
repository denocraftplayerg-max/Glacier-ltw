package com.glacier;

import net.fabricmc.api.ModInitializer;

public final class Glacier implements ModInitializer {

    public static final String MOD_ID = "glacier_optimizer";

    @Override
    public void onInitialize() {
        GlacierRuntime.initialize();
    }
}
