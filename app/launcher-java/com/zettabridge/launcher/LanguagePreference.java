package com.zettabridge.launcher;

import java.io.File;
import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.util.Arrays;

/** Changes only the language option in the owner's known DH2 settings format. */
final class LanguagePreference {
    static final String SETTINGS = "dh2_settings.savegame";
    static final String BACKUP = "dh2_settings.savegame.before-prefer-english.bak";
    private static final String[] KEYS = {
        "AAA_DONT_DELETE", "AllFaeries", "AllLevels", "AutoOrientation",
        "AutoTransmute", "DPad", "ForwardCam", "GOD", "GOD_MANA",
        "GiveGold", "HUDStyle", "Language", "LevelUp", "OneShotKill",
        "VolumeFX", "VolumeMusic"
    };
    private static final int MAX_SETTINGS_BYTES = 65536;

    enum Result { DISABLED, ABSENT, ALREADY_ENGLISH, UPDATED, UNRECOGNIZED }

    static Result apply(File root, boolean preferEnglish) throws IOException {
        if (!preferEnglish) return Result.DISABLED;
        Path settings = new File(root, SETTINGS).toPath();
        if (!Files.exists(settings)) return Result.ABSENT;
        if (Files.isSymbolicLink(settings) || !Files.isRegularFile(settings)
                || Files.size(settings) > MAX_SETTINGS_BYTES) return Result.UNRECOGNIZED;
        byte[] before = Files.readAllBytes(settings);
        int offset = languageValueOffset(before);
        if (offset < 0) return Result.UNRECOGNIZED;
        int current = readInt(before, offset);
        if (current == 0) return Result.ALREADY_ENGLISH;
        if (current < -1 || current > 8) return Result.UNRECOGNIZED;

        Path backup = new File(root, BACKUP).toPath();
        boolean createdBackup = false;
        if (!Files.exists(backup)) {
            // CREATE_NEW keeps an earlier owner's backup intact. Verify the backup
            // before changing the settings, including after a partial write.
            try {
                Files.write(backup, before, StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE);
                createdBackup = true;
            } catch (java.nio.file.FileAlreadyExistsException concurrent) {
                // Check the file that won the race below.
            }
        }
        if (Files.isSymbolicLink(backup) || !Files.isRegularFile(backup)
                || Files.size(backup) > MAX_SETTINGS_BYTES) {
            return Result.UNRECOGNIZED;
        }
        byte[] originalBackup = Files.readAllBytes(backup);
        if (languageValueOffset(originalBackup) < 0
                || (createdBackup && !Arrays.equals(originalBackup, before))) return Result.UNRECOGNIZED;

        byte[] after = before.clone();
        writeInt(after, offset, 0);
        Path replacement = Files.createTempFile(root.toPath(), ".dh2-language-", ".tmp");
        try {
            Files.write(replacement, after);
            // The temporary file and settings are in the same directory. Refuse
            // a filesystem that cannot replace the file atomically.
            try {
                Files.move(replacement, settings, StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } catch (AtomicMoveNotSupportedException unsupported) {
                return Result.UNRECOGNIZED;
            }
        } finally {
            Files.deleteIfExists(replacement);
        }
        return Result.UPDATED;
    }

    static int languageValueOffset(byte[] bytes) {
        if (bytes.length < 4 || bytes.length > MAX_SETTINGS_BYTES) return -1;
        int count = readInt(bytes, 0);
        if (count != KEYS.length) return -1;
        int at = 4, language = -1;
        for (String expected : KEYS) {
            if (at > bytes.length - 4) return -1;
            int length = readInt(bytes, at);
            at += 4;
            if (length != expected.length() || at > bytes.length - length - 4) return -1;
            byte[] key = expected.getBytes(java.nio.charset.StandardCharsets.US_ASCII);
            if (!Arrays.equals(key, Arrays.copyOfRange(bytes, at, at + length))) return -1;
            at += length;
            if ("Language".equals(expected)) language = at;
            at += 4;
        }
        // The native __saveTutorials writer appends 14 one-byte flags after
        // __saveOptions. Preserve those and all unrelated option bytes.
        return at == bytes.length - 14 ? language : -1;
    }

    private static int readInt(byte[] bytes, int at) {
        return (bytes[at] & 255) | (bytes[at + 1] & 255) << 8
                | (bytes[at + 2] & 255) << 16 | (bytes[at + 3] & 255) << 24;
    }

    private static void writeInt(byte[] bytes, int at, int value) {
        bytes[at] = (byte) value;
        bytes[at + 1] = (byte) (value >>> 8);
        bytes[at + 2] = (byte) (value >>> 16);
        bytes[at + 3] = (byte) (value >>> 24);
    }

    private LanguagePreference() {}
}
