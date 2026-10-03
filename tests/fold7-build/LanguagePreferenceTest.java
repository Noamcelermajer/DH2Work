package com.zettabridge.launcher;

import java.io.File;

/** Host entry point for the Python byte-preservation regression test. */
public final class LanguagePreferenceTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("root enabled");
        System.out.println(LanguagePreference.apply(new File(args[0]), Boolean.parseBoolean(args[1])));
    }
}
