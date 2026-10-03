package local.dh2.compat;

import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.database.sqlite.SQLiteException;
import android.provider.MediaStore;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Optional personal playlists must not prevent the game from starting. */
public final class MediaQueries {
    public static Cursor queryPlaylists(Context context) {
        // These are the only columns consumed by MediaPlayList.a(). A literal
        // "*" in a projection is rejected by modern MediaProvider.
        String[] columns = {"_id", "name"};
        MatrixCursor snapshot = new MatrixCursor(columns);
        try (Cursor source = context.getContentResolver().query(
                MediaStore.Audio.Playlists.EXTERNAL_CONTENT_URI,
                columns, null, null, null)) {
            if (source != null) {
                int id = source.getColumnIndexOrThrow("_id");
                int name = source.getColumnIndexOrThrow("name");
                while (source.moveToNext()) {
                    snapshot.addRow(new Object[]{source.getLong(id), source.getString(name)});
                }
            }
            record(context, "Optional media playlists: " + snapshot.getCount());
            return snapshot;
        } catch (SecurityException | IllegalArgumentException | SQLiteException unavailable) {
            // This is the user's external music library, not game audio assets.
            // Return a non-null empty cursor so the legacy constructor creates
            // empty name/id arrays and nativeInitplayer still runs normally.
            snapshot.close();
            Log.w("DH2Media", "Optional personal playlists unavailable", unavailable);
            record(context, "Optional media playlists unavailable: "
                    + unavailable.getClass().getSimpleName() + ": " + unavailable.getMessage());
            return new MatrixCursor(columns);
        }
    }

    private static void record(Context context, String message) {
        try {
            File root = context.getExternalFilesDir(null);
            if (root == null) return;
            try (FileOutputStream out = new FileOutputStream(new File(root, "dh2-media-status.txt"))) {
                out.write(("test2 " + System.currentTimeMillis() + "\n" + message + "\n")
                        .getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException | SecurityException ignored) {
            // Diagnostic storage is optional too.
        }
    }

    private MediaQueries() {}
}
