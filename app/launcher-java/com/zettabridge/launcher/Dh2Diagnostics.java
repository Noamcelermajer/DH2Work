package com.zettabridge.launcher;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

import java.util.zip.*;

/** Bounded, app-only diagnostic export. No root, broad storage or READ_LOGS permission. */
final class Dh2Diagnostics {
    private static final String[] REPORTS={"zb-runtime-report.txt","zb-errors.txt","dh2-logcat.txt","dh2-exits.txt","dh2-session.txt","dh2-logcat.txt.1","dh2-logcat.txt.2"};
    private static Process capture;
    private static File root(Context c){return c.getExternalFilesDir(null);}
    static synchronized void begin(Context c) throws IOException {
        if(capture!=null){capture.destroy();capture=null;}
        File dir=root(c);if(dir==null)throw new IOException("Diagnostic storage unavailable");
        File previous=new File(dir,"previous-run");previous.mkdirs();
        File[] stale=previous.listFiles();if(stale!=null)for(File f:stale)if(f.isFile())f.delete();
        for(String name:REPORTS){File f=new File(dir,name);if(f.isFile())Files.move(f.toPath(),new File(previous,name).toPath(),StandardCopyOption.REPLACE_EXISTING);}
        File game=new File(dir,"plugins/"+CacheArchive.GAME);
        for(String name:new String[]{"dh2-events.txt","dh2-events-previous.txt","dh2-media-status.txt"}){File f=new File(game,name);if(f.isFile())Files.move(f.toPath(),new File(previous,name).toPath(),StandardCopyOption.REPLACE_EXISTING);}
        String info="DH2 test5 session "+new Date()+"\nUID="+android.os.Process.myUid()+"\nlocale="+Locale.getDefault()+"\n";
        Files.write(new File(dir,"dh2-session.txt").toPath(),info.getBytes(StandardCharsets.UTF_8));
        // Android restricts unprivileged logcat to this app UID. --uid further narrows it.
        // Native guest logging also goes straight into the runtime report if logcat is denied.
        try {
            capture=new ProcessBuilder("logcat","-b","main","-b","system","-b","crash","-v","threadtime",
                "--uid="+android.os.Process.myUid(),"-T","1","-f",new File(dir,"dh2-logcat.txt").getAbsolutePath(),"-r","1024","-n","2")
                .redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.appendTo(new File(dir,"dh2-session.txt"))).start();
        }catch(IOException e){Files.write(new File(dir,"dh2-session.txt").toPath(),("logcat unavailable: "+e+"\n").getBytes(StandardCharsets.UTF_8),StandardOpenOption.APPEND);}
    }
    static void collectExits(Context c) throws IOException {
        if(Build.VERSION.SDK_INT<30)return;
        File dir=root(c);if(dir==null)return;
        StringBuilder text=new StringBuilder("Android process exit history (newest first)\n");
        try {
            ActivityManager am=(ActivityManager)c.getSystemService(Context.ACTIVITY_SERVICE);
            int traces=0;
            for(ApplicationExitInfo e:am.getHistoricalProcessExitReasons(c.getPackageName(),0,8)){
                text.append(new Date(e.getTimestamp())).append(" process=").append(e.getProcessName()).append(" pid=").append(e.getPid())
                    .append(" reason=").append(e.getReason()).append(" status=").append(e.getStatus()).append(" pssKiB=").append(e.getPss())
                    .append(" rssKiB=").append(e.getRss()).append(" description=").append(e.getDescription()).append('\n');
                if(traces>=2 || (e.getReason()!=ApplicationExitInfo.REASON_CRASH_NATIVE && e.getReason()!=ApplicationExitInfo.REASON_ANR))continue;
                String name="dh2-exit-"+e.getTimestamp()+".trace";
                try(InputStream in=e.getTraceInputStream()){
                    if(in!=null){File f=new File(dir,name);if(!f.isFile())try(OutputStream out=new FileOutputStream(f)){copy(in,out,4*1024*1024);}
                        text.append("trace=").append(name).append(" (native tombstone may be protobuf)\n");traces++;}
                    else text.append("trace unavailable\n");
                }catch(IOException ex){text.append("trace unavailable: ").append(ex).append('\n');}
            }
            File[] old=dir.listFiles((d,n)->n.startsWith("dh2-exit-")&&n.endsWith(".trace"));
            if(old!=null){Arrays.sort(old,Comparator.comparing(File::getName).reversed());for(int i=4;i<old.length;i++)old[i].delete();}
        }catch(RuntimeException e){text.append("History unavailable: ").append(e).append('\n');}
        Files.write(new File(dir,"dh2-exits.txt").toPath(),text.toString().getBytes(StandardCharsets.UTF_8));
    }
    static String tail(File f,int limit){
        try(RandomAccessFile in=new RandomAccessFile(f,"r")){long start=Math.max(0,in.length()-limit);in.seek(start);byte[] b=new byte[(int)(in.length()-start)];in.readFully(b);return new String(b,StandardCharsets.UTF_8);}
        catch(IOException e){return "No report recorded yet.\n";}
    }
    static void export(Context c,OutputStream destination) throws IOException {
        collectExits(c);
        try(ZipOutputStream zip=new ZipOutputStream(destination)){
            addReports(zip,root(c),"");addReports(zip,new File(root(c),"previous-run"),"previous-run/");
            File game=new File(root(c),"plugins/"+CacheArchive.GAME);
            for(String name:new String[]{"dh2-events.txt","dh2-events-previous.txt","dh2-media-status.txt"})add(zip,new File(game,name),name);
        }
    }
    private static void addReports(ZipOutputStream z,File dir,String prefix)throws IOException{
        File[] files=dir.listFiles((d,n)->n.equals("zb-runtime-report.txt")||n.equals("zb-errors.txt")||n.startsWith("dh2-") && (n.endsWith(".txt")||n.contains("logcat.txt.")||n.endsWith(".trace")));
        if(files!=null)for(File f:files)if(f.isFile())add(z,f,prefix+f.getName());
    }
    private static void add(ZipOutputStream z,File f,String name)throws IOException{
        if(!f.isFile())return;z.putNextEntry(new ZipEntry(name));try(InputStream in=new FileInputStream(f)){copy(in,z,4*1024*1024);}z.closeEntry();
    }
    private static void copy(InputStream in,OutputStream out,int limit)throws IOException{byte[] b=new byte[16384];int n;while(limit>0&&(n=in.read(b,0,Math.min(b.length,limit)))!=-1){out.write(b,0,n);limit-=n;}}
}
