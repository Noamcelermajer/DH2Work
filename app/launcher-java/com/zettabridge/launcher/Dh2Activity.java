package com.zettabridge.launcher;

import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.*;

/** Single-game setup, local cache import and diagnostics for the user's DH2 APK. */
public class Dh2Activity extends Activity {
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private TextView status,location;
    private Button play,importButton;
    private static final int PICK=21, EXPORT=22;
    private static final String LEGACY_REVISION="dh2-fold7-test5-diagnostics";
    private static final String BUNDLED_CACHE_MARKER=".dh2-bundled-cache.sha256";
    private File dataRoot() { return new File(getExternalFilesDir(null),"plugins/"+CacheArchive.GAME); }
    private boolean cacheReady() {
        File root=dataRoot();
        return new File(root,"data").isDirectory() && new File(root,"shaders.pak").isFile();
    }
    private boolean hasExistingCacheFiles() throws IOException {
        File root=dataRoot();
        if (!root.exists()) return false;
        File[] files=root.listFiles();
        if (files==null) throw new IOException("Cannot inspect existing cache");
        for (File file:files)
            if (!file.getName().equals("dh2-options.json")) return true;
        return false;
    }
    private String bundledCacheHash() throws IOException {
        try (BufferedReader in=new BufferedReader(new InputStreamReader(getAssets().open("dh2/cache.sha256"),StandardCharsets.US_ASCII))) {
            String hash=in.readLine();
            if (hash==null || !hash.trim().matches("[0-9a-f]{64}")) throw new IOException("Invalid bundled cache revision");
            return hash.trim();
        } catch (FileNotFoundException absent) {
            return null;
        }
    }
    private boolean bundledCacheInstalled() throws IOException {
        String hash=bundledCacheHash();
        if (hash==null || !cacheReady()) return false;
        File marker=new File(dataRoot(),BUNDLED_CACHE_MARKER);
        if (!marker.isFile()) return false;
        try (BufferedReader in=new BufferedReader(new InputStreamReader(new FileInputStream(marker),StandardCharsets.US_ASCII))) {
            return hash.equals(in.readLine());
        }
    }
    private String bundledGuestRevision() throws IOException {
        try (BufferedReader in=new BufferedReader(new InputStreamReader(getAssets().open("dh2/guest.sha256"),StandardCharsets.US_ASCII))) {
            String revision=in.readLine();
            if (revision == null) throw new IOException("Missing bundled guest revision");
            revision=revision.trim();
            if (!revision.matches("[0-9a-f]{64}")) throw new IOException("Invalid bundled guest revision");
            return revision;
        } catch (FileNotFoundException absent) {
            return LEGACY_REVISION;
        }
    }
    private boolean importBundledCache() throws IOException {
        InputStream bundled;
        try { bundled=getAssets().open("dh2/cache.zip"); }
        catch (FileNotFoundException absent) { return false; }
        try (InputStream in=bundled) {
            String hash=bundledCacheHash();
            if (hash==null) throw new IOException("Bundled cache revision is missing");
            final long[] last={0};
            CacheArchive.install(in,dataRoot(),(bytes,files)->{
                long now=SystemClock.elapsedRealtime();
                if(now-last[0]>500){last[0]=now;runOnUiThread(()->status.setText("Installing bundled cache: "+files+" files, "+(bytes/(1024*1024))+" MiB"));}
            });
            if (!cacheReady()) throw new IOException("Bundled cache is missing required game files");
            java.nio.file.Files.write(new File(dataRoot(),BUNDLED_CACHE_MARKER).toPath(),(hash+"\n").getBytes(StandardCharsets.US_ASCII));
            return true;
        }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        Diagnostics.installCrashRecorder(this);
        LinearLayout panel=new LinearLayout(this); panel.setOrientation(1); panel.setPadding(32,32,32,32);panel.setFitsSystemWindows(true);
        TextView title=new TextView(this);title.setText("Dungeon Hunter 2");title.setTextSize(26);panel.addView(title);
        TextView note=new TextView(this);note.setText("Game setup and crash diagnostics\n\nThe bundled cache installs on first launch when available. You can also import a cache ZIP. After a crash, reopen this screen and export diagnostics.");panel.addView(note);
        status=new TextView(this);status.setPadding(0,24,0,24);panel.addView(status);
        importButton=new Button(this);importButton.setText("Import cache ZIP");importButton.setOnClickListener(v -> pick());panel.addView(importButton);
        play=new Button(this);play.setText("Launch game");play.setOnClickListener(v -> launchGame());panel.addView(play);
        Button report=new Button(this);report.setText("View / share diagnostic report");report.setOnClickListener(v -> showReport());panel.addView(report);
        Button export=new Button(this);export.setText("Export diagnostic ZIP");export.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("application/zip").putExtra(Intent.EXTRA_TITLE,"DH2-diagnostics.zip"),EXPORT));panel.addView(export);
        option(panel,"Prefer English (cache translations may override)","preferEnglish",true);
        option(panel,"Fit game to 16:9 (experimental)","fit16by9",true);
        option(panel,"Keep graphics context during cinematics","preserveContext",false);
        location=new TextView(this);location.setTextIsSelectable(true);location.setText("Cache destination:\n"+dataRoot().getAbsolutePath());panel.addView(location);
        TextView credit=new TextView(this);credit.setPadding(0,24,0,0);credit.setText("Uses ZettaBridge and Dynarmic for ARM32 translation. Private compatibility build; upstream notices are included.");panel.addView(credit);
        ScrollView scroll=new ScrollView(this);scroll.addView(panel);setContentView(scroll);
        setBusy(true,"Preparing game and ARM64 runtime...");
        worker.execute(() -> {
            try {
                String revision=bundledGuestRevision();
                if (!revision.equals(getPreferences(0).getString("prepared", "")) || PluginStore.find(this,CacheArchive.GAME)==null) {
                    stopGuest();
                    File temporary=new File(getCacheDir(),"dh2-bundled.apk");
                    try (InputStream in=getAssets().open("dh2/game.apk"); OutputStream out=new FileOutputStream(temporary)) {
                        byte[] b=new byte[65536];int n;while ((n=in.read(b))!=-1) out.write(b,0,n);
                    }
                    PluginRecord record=PluginStore.importApk(this,Uri.fromFile(temporary));
                    if (!CacheArchive.GAME.equals(record.packageName)) throw new IOException("Unexpected bundled package");
                    temporary.delete();
                    getPreferences(0).edit().putString("prepared",revision).apply();
                }
                boolean ready=cacheReady();
                boolean existing=hasExistingCacheFiles();
                if (!ready && !existing && importBundledCache()) ready=cacheReady();
                if (ready) getPreferences(0).edit().putBoolean("cacheImported",true).apply();
                configurePath();
                final boolean cacheAvailable=ready;
                final boolean bundledInstalled=bundledCacheInstalled();
                final boolean partialCache=existing && !cacheAvailable;
                runOnUiThread(() -> setBusy(false,bundledInstalled?"Ready. Bundled cache import completed; launch the game.":
                    cacheAvailable?"Cache files found; completeness not verified. You can launch or import a cache ZIP.":
                    partialCache?"Existing cache files are incomplete; import a complete cache ZIP.":
                    "Ready. Import the cache before the first launch."));
            } catch (Throwable e) { failed("Preparation failed",e); }
        });
    }

    private void option(LinearLayout panel,String title,String key,boolean initial){
        CheckBox box=new CheckBox(this);box.setText(title);box.setChecked(getPreferences(0).getBoolean(key,initial));
        box.setOnCheckedChangeListener((button,value)->getPreferences(0).edit().putBoolean(key,value).apply());panel.addView(box);
    }
    private void configurePath() throws IOException {
        File root=dataRoot();if (!root.isDirectory() && !root.mkdirs()) throw new IOException("External game storage is unavailable");
        String options="{\"preferEnglish\":"+getPreferences(0).getBoolean("preferEnglish",true)+",\"fit16by9\":"+getPreferences(0).getBoolean("fit16by9",true)+",\"preserveContext\":"+getPreferences(0).getBoolean("preserveContext",false)+"}";
        java.nio.file.Files.write(new File(root,"dh2-options.json").toPath(),options.getBytes(StandardCharsets.UTF_8));
        getSharedPreferences(CacheArchive.GAME+"__DungeonHunter2Prefs",0).edit().putString("SDFolder",root.getAbsolutePath()).commit();
    }
    private void setBusy(boolean busy,String text) { play.setEnabled(!busy);importButton.setEnabled(!busy);status.setText(text); }
    private void pick() {
        Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        i.putExtra(Intent.EXTRA_MIME_TYPES,new String[]{"application/zip","application/x-zip-compressed","application/octet-stream"});startActivityForResult(i,PICK);
    }
    @Override protected void onActivityResult(int request,int result,Intent data) {
        super.onActivityResult(request,result,data);
        if(request==EXPORT && result==RESULT_OK && data!=null && data.getData()!=null){
            Uri destination=data.getData();setBusy(true,"Saving diagnostics...");worker.execute(()->{
                try(OutputStream out=getContentResolver().openOutputStream(destination)){if(out==null)throw new IOException("Cannot write selected document");Dh2Diagnostics.export(this,out);runOnUiThread(()->setBusy(false,"Diagnostic ZIP saved. Attach it to this conversation."));}
                catch(Exception e){failed("Export failed",e);}
            });return;
        }
        if (request!=PICK || result!=RESULT_OK || data==null || data.getData()==null) return;
        Uri selected=data.getData();stopGuest();setBusy(true,"Importing cache. Keep this screen open...");
        worker.execute(() -> {
            try (InputStream in=getContentResolver().openInputStream(selected)) {
                if (in==null) throw new IOException("Cannot open selected archive");
                final long[] last={0};
                CacheArchive.Result r=CacheArchive.install(in,dataRoot(),(bytes,files)->{
                    long now=SystemClock.elapsedRealtime();if(now-last[0]>500){last[0]=now;runOnUiThread(()->status.setText("Importing: "+files+" files, "+(bytes/(1024*1024))+" MiB"));}
                });
                java.nio.file.Files.deleteIfExists(new File(dataRoot(),BUNDLED_CACHE_MARKER).toPath());
                configurePath();getPreferences(0).edit().putBoolean("cacheImported",true).apply();
                runOnUiThread(()->setBusy(false,"Imported "+r.files+" files ("+(r.bytes/(1024*1024))+" MiB). Completeness not verified; ready to try launch."));
            } catch(Exception e) { failed("Cache import failed",e); }
        });
    }
    private void launchGame() {
        try {
            long page=android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE);
            if(page!=4096)throw new IOException("This translation runtime currently requires 4096-byte memory pages. This phone reports "+page+". Please share the diagnostic report.");
            setBusy(true,"Starting a fresh diagnostic run...");
            worker.execute(()->{
                try{stopGuest();configurePath();
                    LanguagePreference.Result language;
                    try { language=LanguagePreference.apply(dataRoot(),getPreferences(0).getBoolean("preferEnglish",true)); }
                    catch (IOException | SecurityException e) { language=LanguagePreference.Result.UNRECOGNIZED; android.util.Log.w("DH2", "English preference could not be applied", e); }
                    Dh2Diagnostics.collectExits(this);Dh2Diagnostics.begin(this);
                    java.nio.file.Files.write(new File(getExternalFilesDir(null),"dh2-session.txt").toPath(),
                        ("English preference: "+language+"\n").getBytes(StandardCharsets.UTF_8),
                        java.nio.file.StandardOpenOption.APPEND);
                    final boolean languageFailed=language==LanguagePreference.Result.UNRECOGNIZED;
                    runOnUiThread(()->{setBusy(false,languageFailed?
                        "Saved language setting was not recognized; the game may keep its existing language. Export diagnostics for details.":
                        "Logs are saved automatically. Export the diagnostic ZIP after the test.");
                        startActivity(PluginSwitchActivity.intent(this,CacheArchive.GAME));});
                }catch(Exception e){failed("Launch failed",e);}
            });
        }
        catch(Exception e){failed("Launch failed",e);}
    }
    private void stopGuest() {
        ActivityManager am=(ActivityManager)getSystemService(ACTIVITY_SERVICE);
        java.util.List<ActivityManager.RunningAppProcessInfo> list=am.getRunningAppProcesses();
        if(list!=null)for(ActivityManager.RunningAppProcessInfo p:list)
            if(p.uid==android.os.Process.myUid() && p.processName.equals(getPackageName()+":guest")) android.os.Process.killProcess(p.pid);
    }
    private void failed(String label,Throwable e) {
        Diagnostics.report(this,label,e,false);
        runOnUiThread(()->setBusy(false,label+": "+e.getMessage()+"\nUse the diagnostic report for details."));
    }
    private void showReport() {
        StringBuilder b=new StringBuilder("DH2 compatibility build\nModel: "+Build.MODEL+"\nAndroid: "+Build.VERSION.RELEASE+"\nABIs: "+java.util.Arrays.toString(Build.SUPPORTED_ABIS)+"\nPage size: "+android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE)+"\nCache: "+dataRoot()+"\n\n");
        File media=new File(dataRoot(),"dh2-media-status.txt");
        try{b.append("dh2-media-status.txt:\n").append(new String(java.nio.file.Files.readAllBytes(media.toPath()),StandardCharsets.UTF_8)).append("\n");}
        catch(IOException ignored){b.append("No media query report yet.\n\n");}
        for(String name:new String[]{"zb-runtime-report.txt","zb-errors.txt","dh2-exits.txt","dh2-session.txt"}){
            File f=new File(getExternalFilesDir(null),name);b.append(name).append(":\n");
            b.append(Dh2Diagnostics.tail(f,120000)).append("\n");
        }
        b.append("\ndh2-events.txt:\n").append(Dh2Diagnostics.tail(new File(dataRoot(),"dh2-events.txt"),40000));
        b.append("\nUse Export diagnostic ZIP for full logs and system exit traces.\n");
        String text=b.toString();TextView view=new TextView(this);view.setText(text);view.setTextIsSelectable(true);view.setPadding(24,12,24,12);
        ScrollView scroll=new ScrollView(this);scroll.addView(view);
        new AlertDialog.Builder(this).setTitle("Diagnostic report").setView(scroll).setPositiveButton("Share",(d,w)->startActivity(Intent.createChooser(new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT,text),"Share report"))).setNegativeButton("Close",null).show();
    }
    @Override protected void onResume(){super.onResume();if(!worker.isShutdown())worker.execute(()->{try{Dh2Diagnostics.collectExits(this);}catch(IOException ignored){}});}
    @Override protected void onDestroy(){worker.shutdownNow();super.onDestroy();}
}
