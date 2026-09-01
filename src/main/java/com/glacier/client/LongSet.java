package com.glacier.client;

import java.util.Arrays;

public final class LongSet {

    private static final long EMPTY = Long.MIN_VALUE;

    private long[] table;
    private int size;
    private int mask;

    public LongSet(int capacity) {
        int n = 1;
        while (n < capacity * 2) n <<= 1;
        table = new long[n];
        Arrays.fill(table, EMPTY);
        mask = n - 1;
    }

    public boolean add(long value) {
        if (value == EMPTY) value++;
        int index = mix(value) & mask;
        while (true) {
            long current = table[index];
            if (current == EMPTY) { table[index] = value; size++; return true; }
            if (current == value) return false;
            index = (index + 1) & mask;
        }
    }

    public boolean contains(long value) {
        if (value == EMPTY) value++;
        int index = mix(value) & mask;
        while (true) {
            long current = table[index];
            if (current == EMPTY) return false;
            if (current == value) return true;
            index = (index + 1) & mask;
        }
    }

    public boolean remove(long value) {
        if (value == EMPTY) value++;
        int index = mix(value) & mask;
        while (true) {
            long current = table[index];
            if (current == EMPTY) return false;
            if (current == value) {
                table[index] = EMPTY;
                size--;
                rehashCluster(index);
                return true;
            }
            index = (index + 1) & mask;
        }
    }

    private void rehashCluster(int deleted) {
        int index = (deleted + 1) & mask;
        while (table[index] != EMPTY) {
            long value = table[index];
            table[index] = EMPTY;
            size--;
            add(value);
            index = (index + 1) & mask;
        }
    }

    public int size() { return size; }

    public void clear() {
        Arrays.fill(table, EMPTY);
        size = 0;
    }

    private static int mix(long x) {
        x ^= x >>> 33;
        x *= 0xff51afd7ed558ccdL;
        x ^= x >>> 33;
        x *= 0xc4ceb9fe1a85ec53L;
        x ^= x >>> 33;
        return (int) x;
    }
}
