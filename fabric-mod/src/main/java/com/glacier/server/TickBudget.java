package com.glacier.server;

import com.glacier.config.GlacierConfig;

public final class TickBudget {

    private long startNs;

    TickBudget() {}

    public void begin() { startNs = System.nanoTime(); }

    public long elapsed() { return System.nanoTime() - startNs; }

    public boolean canRun(long estimatedCostNs) {
        return elapsed() + estimatedCostNs
                < GlacierConfig.TICK_BUDGET_NS - GlacierConfig.RESERVED_TICK_NS;
    }
}
