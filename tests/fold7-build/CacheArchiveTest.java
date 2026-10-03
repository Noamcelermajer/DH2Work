package com.zettabridge.launcher;
import java.io.*;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

public class CacheArchiveTest {
    static byte[] zip(String... names) throws Exception {
        ByteArrayOutputStream b=new ByteArrayOutputStream();
        try(ZipOutputStream z=new ZipOutputStream(b)){
            for(String name:names){z.putNextEntry(new ZipEntry(name));z.write(("asset:"+name).getBytes("UTF-8"));z.closeEntry();}
        }return b.toByteArray();
    }
    static void require(boolean ok,String label){if(!ok)throw new AssertionError(label);}
    public static void main(String[] args) throws Exception {
        Path temp=Files.createTempDirectory("dh2-cache-test-");
        String pkg=CacheArchive.GAME;int passed=0;
        for(String prefix:new String[]{"","wrapper/","Android/data/"+pkg+"/files/",pkg+"/files/","sdcard/Android/data/"+pkg+"/files/"}){
            File dest=temp.resolve("normal-"+passed).toFile();
            byte[] src=zip(prefix+"data/scene/test.xml",prefix+"shaders.pak");
            CacheArchive.Result r=CacheArchive.install(new ByteArrayInputStream(src),dest,null);
            require(r.files==2,"file count");
            require(Files.readString(new File(dest,"data/scene/test.xml").toPath()).equals("asset:"+prefix+"data/scene/test.xml"),"asset bytes");
            require(new File(dest,"shaders.pak").isFile(),"root selection");passed++;
        }
        File dest=temp.resolve("existing").toFile();dest.mkdirs();Files.writeString(new File(dest,"preserved.txt").toPath(),"old cache");
        for(byte[] invalid:new byte[][]{zip("../escape"),zip("/escape"),zip("C:/escape"),zip("foo/../../escape"),zip("main.102."+pkg+".obb"),"not a zip".getBytes()}){
            boolean failed=false;try{CacheArchive.install(new ByteArrayInputStream(invalid),dest,null);}catch(IOException e){failed=true;}
            require(failed,"invalid archive rejected");require(Files.readString(new File(dest,"preserved.txt").toPath()).equals("old cache"),"rollback");passed++;
        }
        boolean cancelled=false;
        try{CacheArchive.install(new ByteArrayInputStream(zip("data/a","data/b")),dest,(b,f)->{throw new IOException("cancel");});}catch(IOException e){cancelled=true;}
        require(cancelled&&new File(dest,"preserved.txt").isFile(),"cancel preserves old cache");passed++;
        CacheArchive.install(new ByteArrayInputStream(zip("data/new")),dest,null);
        require(new File(dest,"data/new").isFile()&&!new File(dest,"preserved.txt").exists(),"successful replacement");passed++;
        require(!temp.resolve("escape").toFile().exists(),"no traversal write");
        System.out.println("CacheArchiveTest PASS: "+passed+" cases (layouts, exact bytes, invalid paths, invalid ZIP, OBB refusal, cancellation, replacement)");
    }
}
