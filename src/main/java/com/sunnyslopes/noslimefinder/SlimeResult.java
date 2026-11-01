package com.sunnyslopes.noslimefinder;

import java.util.Locale;

/** One AFK point result. */
public final class SlimeResult {
    public final int x;
    public final int z;
    public final int area;
    public final int slime;
    public final int chunks;
    public final int avoidX;
    public final int avoidY;

    public SlimeResult(int x, int z, int area, int slime, int chunks, int avoidX, int avoidY) {
        this.x = x;
        this.z = z;
        this.area = area;
        this.slime = slime;
        this.chunks = chunks;
        this.avoidX = avoidX;
        this.avoidY = avoidY;
    }

    public long distanceSq() {
        return (long) x * x + (long) z * z;
    }

    public String toLine() {
        return String.format(Locale.ROOT,
            "/tp %d -64 %d area=%d slime=%d chunks=%d avoid=%d+%d",
            x, z, area, slime, chunks, avoidX, avoidY);
    }
}
