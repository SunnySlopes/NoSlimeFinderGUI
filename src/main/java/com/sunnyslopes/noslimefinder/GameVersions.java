package com.sunnyslopes.noslimefinder;

import java.util.List;

/**
 * Version choices. Merged entries keep one cubiomes representative and show
 * the version range in the label (e.g. "1.21.5~26.1").
 */
public final class GameVersions {

    public record Option(String label, String mc) {
        @Override public String toString() { return label; }
    }

    /** Fixed group list; no "show all versions" option. */
    public static final List<Option> GROUPS = List.of(
        new Option("1.18", "1.18"),
        new Option("1.19", "1.19.2"),
        new Option("1.20", "1.19.4"),
        new Option("1.21~1.21.3", "1.21.3"),
        new Option("1.21.4", "1.21.4"),
        new Option("1.21.5~26.1", "26.1"),
        new Option("26.2", "26.2"),
        new Option("26.3", "26.3")
    );

    public static final String DEFAULT_LABEL = "26.3";

    public static Option find(String label) {
        if (label == null) return null;
        for (Option o : GROUPS) {
            if (o.label().equals(label)) return o;
        }
        return null;
    }

    public static String mcForLabel(String label) {
        Option o = find(label);
        return o == null ? "26.1" : o.mc();
    }

    private GameVersions() {}
}
