package com.glacier.memory;

public final class ReusableBuffers {

    private float[] floats;
    private int[] ints;

    public ReusableBuffers(int floatCapacity, int intCapacity) {
        floats = new float[floatCapacity];
        ints = new int[intCapacity];
    }

    public float[] floats(int required) {
        if (required > floats.length) {
            floats = new float[nextPowerOfTwo(required)];
        }
        return floats;
    }

    public int[] ints(int required) {
        if (required > ints.length) {
            ints = new int[nextPowerOfTwo(required)];
        }
        return ints;
    }

    private static int nextPowerOfTwo(int value) {
        if (value <= 1) return 1;
        return Integer.highestOneBit(value - 1) << 1;
    }
}
