package local.dh2.compat;

import android.content.Context;
import java.io.File;

/** Resolve legacy absolute paths through the actual plugin Context. */
public final class GamePaths {
    private static final String ORIGINAL_DATA="/data/data/com.gameloft.android.GAND.GloftD2SS";
    private static final String ORIGINAL_EXTERNAL="/sdcard/Android/data/com.gameloft.android.GAND.GloftD2SS/files";
    private static volatile Context context;
    public static void initialize(Context value) {
        context=value;
        GameTrace.initialize(value);
        File external=value.getExternalFilesDir(null);
        if(external!=null) {
            external.mkdirs();
            value.getSharedPreferences("DungeonHunter2Prefs",0).edit().putString("SDFolder",external.getAbsolutePath()).commit();
        }
        new File(value.getFilesDir().getParentFile(),"prefs").mkdirs();
    }
    public static String resolve(String path) {
        Context c=context;
        if(c==null) throw new IllegalStateException("Game storage was used before Application initialization");
        if(path.equals(ORIGINAL_EXTERNAL) || path.startsWith(ORIGINAL_EXTERNAL+"/")) {
            File external=c.getExternalFilesDir(null);
            if(external==null) throw new IllegalStateException("External game storage is unavailable");
            return external.getAbsolutePath()+path.substring(ORIGINAL_EXTERNAL.length());
        }
        if(path.equals(ORIGINAL_DATA) || path.startsWith(ORIGINAL_DATA+"/"))
            return c.getFilesDir().getParent()+path.substring(ORIGINAL_DATA.length());
        return path;
    }
    private GamePaths() {}
}
