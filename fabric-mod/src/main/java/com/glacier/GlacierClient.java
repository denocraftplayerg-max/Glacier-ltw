package com.glacier;

import net.fabricmc.api.ClientModInitializer;

public final class GlacierClient implements ClientModInitializer {

    @Override
    public void onInitializeClient() {
        GlacierRuntime.initializeClient();
    }
}
