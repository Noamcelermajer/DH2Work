package com.zettabridge.launcher;

import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/** Local cache extraction, shared by the Android UI and host regression tests. */
public final class CacheArchive {
    public static final String GAME = "com.gameloft.android.GAND.GloftD2SS";
    public interface Progress { void update(long bytes, int files) throws IOException; }
    public static final class Result {
        public final long bytes; public final int files;
        Result(long b, int f) { bytes=b; files=f; }
    }
    private CacheArchive() {}

    public static Result install(InputStream source, File destination, Progress progress) throws IOException {
        File parent = destination.getCanonicalFile().getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create cache directory");
        File stage = Files.createTempDirectory(parent.toPath(), ".dh2-import-").toFile();
        File backup = new File(parent, ".dh2-previous-" + UUID.randomUUID());
        boolean movedOld = false, published = false;
        long bytes = 0; int files = 0, entries = 0;
        Set<String> names = new HashSet<>();
        try {
            String prefix = stage.getCanonicalPath() + File.separator;
            byte[] buffer = new byte[256 * 1024];
            try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(source))) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (++entries > 200000) throw new IOException("Too many archive entries");
                    String name = entry.getName().replace('\\', '/');
                    if (name.startsWith("/") || name.indexOf(':') >= 0 || name.indexOf('\0') >= 0)
                        throw new IOException("Unsafe archive path");
                    for (String part : name.split("/")) if (part.equals("..")) throw new IOException("Unsafe archive path");
                    File output = new File(stage, name).getCanonicalFile();
                    if (!output.getPath().startsWith(prefix)) throw new IOException("Archive path escapes destination");
                    if (!names.add(output.getPath())) throw new IOException("Duplicate archive entry: " + name);
                    if (entry.isDirectory()) {
                        if (!output.isDirectory() && !output.mkdirs()) throw new IOException("Cannot create " + name);
                    } else {
                        if (name.toLowerCase(Locale.ROOT).endsWith(".obb"))
                            throw new IOException("This archive contains OBB files; this importer expects extracted game data");
                        File dir=output.getParentFile();
                        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("Cannot create asset directory");
                        try (OutputStream out = new BufferedOutputStream(new FileOutputStream(output))) {
                            int n;
                            while ((n=zip.read(buffer)) != -1) {
                                bytes += n;
                                if (bytes > 20L*1024*1024*1024) throw new IOException("Cache exceeds 20 GiB extraction limit");
                                if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled");
                                out.write(buffer,0,n);
                            }
                        }
                        files++;
                        if (progress != null) progress.update(bytes,files);
                    }
                    zip.closeEntry();
                }
            }
            if (files == 0) throw new IOException("No game files found; select a complete ZIP archive");
            File root=assetRoot(stage);
            if (destination.exists()) {
                if (!destination.renameTo(backup)) throw new IOException("Cannot preserve previous cache");
                movedOld=true;
            }
            if (!root.renameTo(destination)) throw new IOException("Cannot publish imported cache");
            published=true;
            return new Result(bytes,files);
        } finally {
            if (movedOld && !published && !backup.renameTo(destination))
                throw new IOException("Previous cache preserved at " + backup + "; automatic restoration failed");
            delete(stage);
            if (published) delete(backup);
        }
    }

    static File assetRoot(File stage) throws IOException {
        File current=stage;
        for (int depth=0; depth<12; depth++) {
            File explicit=new File(current,"Android/data/"+GAME+"/files");
            if (explicit.isDirectory()) return explicit;
            explicit=new File(current,GAME+"/files");
            if (explicit.isDirectory()) return explicit;
            if (current.getName().equals(GAME) && new File(current,"files").isDirectory())
                return new File(current,"files");
            if (new File(current,"data").isDirectory() || new File(current,"shaders.pak").isFile()) return current;
            File[] children=current.listFiles(f -> !f.getName().equals("__MACOSX") && !f.getName().equals(".DS_Store"));
            if (children == null) throw new IOException("Cannot inspect imported cache");
            if (children.length==1 && children[0].isDirectory()) current=children[0];
            else return current;
        }
        throw new IOException("Cache has too many wrapper directories");
    }

    private static void delete(File path) {
        if (path == null || !path.exists()) return;
        File[] children=path.listFiles();
        if (children != null) for (File child:children) delete(child);
        path.delete();
    }
}
