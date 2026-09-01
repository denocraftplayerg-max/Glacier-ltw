package com.glacier.server;

public final class TickController {

    private static final TickBudget BUDGET = new TickBudget();

    private TickController() {}

    public static void beginTick() { BUDGET.begin(); }

    public static boolean canRun(long estimatedCostNs) {
        return BUDGET.canRun(estimatedCostNs);
    }

    public static long elapsed() { return BUDGET.elapsed(); }
}
