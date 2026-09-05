package com.glacier;

import net.ltw.bridge.LTWBridge;

public final class GlacierRuntime {

    private static boolean initialized;
    private static boolean clientInitialized;

    private GlacierRuntime() {
    }

    public static synchronized void initialize() {
        if (initialized) return;
        initialized = true;
    }

    public static synchronized void initializeClient() {
        if (!initialized) initialize();
        if (clientInitialized) return;
        LTWBridge.tryLoad();
        clientInitialized = true;
    }

    public static boolean isInitialized() { return initialized; }
    public static boolean isClientInitialized() { return clientInitialized; }
}

