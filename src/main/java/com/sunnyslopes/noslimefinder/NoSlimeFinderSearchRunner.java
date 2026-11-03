package com.sunnyslopes.noslimefinder;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/** Runs one NoSlimeFinder search and polls native progress. */
public class NoSlimeFinderSearchRunner {

    public static final int DEFAULT_MAX_RESULTS = 100;
    private static final long PROGRESS_POLL_MS = 100L;

    public record ProgressInfo(int stage, long current, long total, double percent,
                               long elapsedMs, long remainingMs, boolean done,
                               boolean paused) {}

    private static final Object LOCK = new Object();
    private static volatile NoSlimeFinderSearchRunner active;

    private volatile boolean running = false;
    private volatile boolean paused = false;
    private volatile boolean stopRequested = false;
    private volatile long runStartMs = 0;
    private volatile long pausedTotalMs = 0;
    private volatile long pauseStartMs = 0;

    public static boolean isAnySearchActive() {
        synchronized (LOCK) {
            return active != null;
        }
    }

    public void pause() {
        if (running && !paused) {
            paused = true;
            pauseStartMs = System.currentTimeMillis();
            try {
                NoSlimeFinderBridge.pause();
            } catch (Throwable ignored) {
            }
        }
    }

    public void resume() {
        if (paused) {
            if (pauseStartMs > 0) {
                pausedTotalMs += System.currentTimeMillis() - pauseStartMs;
                pauseStartMs = 0;
            }
            paused = false;
            try {
                NoSlimeFinderBridge.resume();
            } catch (Throwable ignored) {
            }
        }
    }

    public void stop() {
        stopRequested = true;
        try {
            NoSlimeFinderBridge.stop();
        } catch (Throwable ignored) {
        }
    }

    private long elapsedMs() {
        long base = System.currentTimeMillis() - runStartMs;
        long pausedMs = pausedTotalMs;
        if (pauseStartMs > 0) pausedMs += System.currentTimeMillis() - pauseStartMs;
        return Math.max(0, base - pausedMs);
    }

    public boolean startSearch(long seed, int minX, int maxX, int minZ, int maxZ,
                               int threads, String mcVersion, int biomeY,
                               int[] avoidBiomeIds, int maxResults, int minArea,
                               Consumer<ProgressInfo> progressCallback,
                               Consumer<List<SlimeResult>> resultCallback,
                               Consumer<String> errorCallback) {
        synchronized (LOCK) {
            if (active != null) return false;
            active = this;
        }
        running = true;
        paused = false;
        stopRequested = false;
        pausedTotalMs = 0;
        pauseStartMs = 0;
        runStartMs = System.currentTimeMillis();

        Thread worker = new Thread(() -> runSearch(seed, minX, maxX, minZ, maxZ,
            threads, mcVersion, biomeY, avoidBiomeIds, maxResults, minArea,
            progressCallback, resultCallback, errorCallback), "noslimefinder-search");
        worker.setDaemon(true);
        worker.start();
        return true;
    }

    private void runSearch(long seed, int minX, int maxX, int minZ, int maxZ,
                           int threads, String mcVersion, int biomeY,
                           int[] avoidBiomeIds, int maxResults, int minArea,
                           Consumer<ProgressInfo> progressCallback,
                           Consumer<List<SlimeResult>> resultCallback,
                           Consumer<String> errorCallback) {
        final long[] stageStart = {0L};
        final int[] lastStage = {-1};
        final long[] lastCurrent = {0L};
        final long[] lastTotal = {0L};

        ScheduledExecutorService poller = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "noslimefinder-progress");
            t.setDaemon(true);
            return t;
        });

        poller.scheduleAtFixedRate(() -> {
            if (!running || active != NoSlimeFinderSearchRunner.this) return;
            try {
                long[] p = NoSlimeFinderBridge.getSearchProgress();
                if (p == null || p.length < 6) return;
                int stage = (int) p[0];
                boolean done = p[1] != 0;
                boolean isPaused = p[2] != 0;
                long current = p[4];
                long total = p[5];
                long elapsed = elapsedMs();

                if (stage != lastStage[0]) {
                    lastStage[0] = stage;
                    stageStart[0] = elapsed;
                }
                lastCurrent[0] = current;
                lastTotal[0] = total;

                double percent = total > 0 ? Math.min(100.0, current * 100.0 / total) : 0.0;
                if (done) percent = 100.0;

                long remaining = 0L;
                long stageElapsed = Math.max(0L, elapsed - stageStart[0]);
                if (!done && !isPaused && total > current && current > 0 && stageElapsed > 0) {
                    double eta = stageElapsed * ((double) (total - current) / (double) current);
                    if (eta > 0 && eta < (double) Long.MAX_VALUE) remaining = (long) eta;
                }
                if (progressCallback != null) {
                    progressCallback.accept(new ProgressInfo(stage, current, total, percent,
                        elapsed, remaining, done, isPaused));
                }
            } catch (Throwable ignored) {
            }
        }, PROGRESS_POLL_MS, PROGRESS_POLL_MS, TimeUnit.MILLISECONDS);

        int[] raw = null;
        try {
            raw = NoSlimeFinderBridge.slimeSearch(seed, minX, maxX, minZ, maxZ,
                threads, mcVersion, biomeY, avoidBiomeIds, maxResults, minArea);
            if (raw != null && resultCallback != null) {
                List<SlimeResult> results = new ArrayList<>(raw.length / 7);
                for (int i = 0; i + 6 < raw.length; i += 7) {
                    results.add(new SlimeResult(raw[i], raw[i + 1], raw[i + 2],
                        raw[i + 3], raw[i + 4], raw[i + 5], raw[i + 6]));
                }
                resultCallback.accept(results);
            } else if (raw == null && !stopRequested && errorCallback != null) {
                String msg;
                try {
                    msg = NoSlimeFinderBridge.lastError();
                } catch (Throwable t) {
                    msg = t.getMessage();
                }
                if (msg == null || msg.isBlank()) msg = "native search failed";
                errorCallback.accept(msg);
            }
        } catch (UnsatisfiedLinkError e) {
            if (errorCallback != null) {
                errorCallback.accept("Native library not loaded. Initialize the "
                    + "NoSlimeFinderBase submodule and build with jni/build-jni.bat. "
                    + e.getMessage());
            }
        } finally {
            poller.shutdownNow();
            try {
                poller.awaitTermination(2, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            if (pauseStartMs > 0) {
                pausedTotalMs += System.currentTimeMillis() - pauseStartMs;
                pauseStartMs = 0;
            }
            running = false;
            paused = false;
            synchronized (LOCK) {
                if (active == this) active = null;
            }
            if (progressCallback != null) {
                long total = Math.max(1L, Math.max(lastTotal[0], lastCurrent[0]));
                progressCallback.accept(new ProgressInfo(stopRequested ? 6 : 5, total, total,
                    stopRequested ? 0.0 : 100.0, elapsedMs(), 0L, true, false));
            }
        }
    }

    public NoSlimeFinderSearchRunner() {}
}
