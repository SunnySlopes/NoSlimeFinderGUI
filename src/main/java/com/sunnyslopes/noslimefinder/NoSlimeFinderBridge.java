package com.sunnyslopes.noslimefinder;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/** JNI bridge to the NoSlimeFinder native library. */
public final class NoSlimeFinderBridge {

    private static boolean loaded = false;

    static {
        loadNativeLibrary();
    }

    private static void loadNativeLibrary() {
        if (loaded) return;
        try {
            System.loadLibrary("libNoSlimeFinderLibJ");
            loaded = true;
            return;
        } catch (UnsatisfiedLinkError first) {
            if (tryLoadFromBundledResources()) {
                loaded = true;
                return;
            }
            Path base = Paths.get(System.getProperty("user.dir", "."));
            List<String> names = nativeLibNames();
            Path[] candidates = {
                base.resolve("native").resolve("windows"),
                base.resolve("jni").resolve("native").resolve("jni").resolve("build").resolve("native"),
                base.resolve("build").resolve("libs").resolve("native").resolve("windows"),
            };
            for (Path dir : candidates) {
                for (String name : names) {
                    File f = dir.resolve(name).toFile();
                    if (f.exists()) {
                        System.load(f.getAbsolutePath());
                        loaded = true;
                        return;
                    }
                }
            }
            throw new UnsatisfiedLinkError(
                "NoSlimeFinder native library not found. Build with jni/build-jni.bat. "
                + first.getMessage());
        }
    }

    private static boolean tryLoadFromBundledResources() {
        String os = System.getProperty("os.name", "").toLowerCase();
        String folder;
        if (os.contains("win")) folder = "windows";
        else if (os.contains("mac")) folder = "macos";
        else if (os.contains("nix") || os.contains("nux") || os.contains("aix") || os.contains("linux"))
            folder = "linux";
        else
            return false;

        for (String name : nativeLibNames()) {
            String resource = "/native/" + folder + "/" + name;
            try (InputStream is = NoSlimeFinderBridge.class.getResourceAsStream(resource)) {
                if (is == null) continue;
                Path dir = Files.createTempDirectory("noslimefinder-native-");
                Path lib = dir.resolve(name);
                Files.copy(is, lib, StandardCopyOption.REPLACE_EXISTING);
                lib.toFile().deleteOnExit();
                dir.toFile().deleteOnExit();
                System.load(lib.toAbsolutePath().toString());
                return true;
            } catch (IOException | UnsatisfiedLinkError ignored) {
                // try the next candidate
            }
        }
        return false;
    }

    private static List<String> nativeLibNames() {
        String os = System.getProperty("os.name", "").toLowerCase();
        List<String> names = new ArrayList<>();
        if (os.contains("win")) {
            names.add("libNoSlimeFinderLibJ.dll");
            names.add("NoSlimeFinderLibJ.dll");
        } else if (os.contains("mac")) {
            names.add("libNoSlimeFinderLibJ.dylib");
        } else {
            names.add("libNoSlimeFinderLibJ.so");
        }
        return names;
    }

    /**
     * Runs synchronously. Returns a flat array of 7 ints per result:
     * [x, z, area, slime, chunks, avoidX, avoidY].
     * Returns null when stopped or on error (see {@link #lastError()}).
     */
    public static native int[] slimeSearch(long seed, int minX, int maxX,
        int minZ, int maxZ, int threads, String mcVersion, int biomeY,
        int[] avoidBiomeIds, int maxResults, int minArea);

    /** Returns entries "id|name", ordered by biome id descending. */
    public static native String[] listBiomes(String mcVersion);

    /** [stage, done, paused, error, current, total, elapsedMs, resultCount] */
    public static native long[] getSearchProgress();

    public static native boolean pause();
    public static native boolean resume();
    public static native boolean stop();
    public static native String lastError();

    private NoSlimeFinderBridge() {}
}
