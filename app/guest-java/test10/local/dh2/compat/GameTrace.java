package local.dh2.compat;

import android.app.*;
import android.content.*;
import android.os.*;
import android.view.*;
import android.widget.FrameLayout;
import android.opengl.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.egl.EGLConfig;

/** Per-session lifecycle and frame milestones. Rendering remains on the original GL thread. */
public final class GameTrace {
    private static Context context;
    private static File events;
    private static boolean preserve,fit,english;
    public static void initialize(Context c){
        context=c;events=new File(c.getExternalFilesDir(null),"dh2-events.txt");
        try {
            org.json.JSONObject options=new org.json.JSONObject(new String(java.nio.file.Files.readAllBytes(new File(c.getExternalFilesDir(null),"dh2-options.json").toPath()),StandardCharsets.UTF_8));
            preserve=options.optBoolean("preserveContext",false);fit=options.optBoolean("fit16by9",false);english=options.optBoolean("preferEnglish",true);
        }catch(Exception e){preserve=false;fit=false;english=true;}
        event("start locale="+Locale.getDefault()+" preferEnglish="+english+" fit16by9="+fit+" preserveContext="+preserve);
        for(String name:new String[]{"character_properties_pystructnames.bin","character_properties.bin","character_models_dictionary_pyarraynames.bin"}){
            File file=new File(c.getExternalFilesDir(null),"data/PyData/"+name);
            if(!file.isFile())file=new File(c.getExternalFilesDir(null),"data/pydata/"+name);
            event("asset probe="+file.getName()+" exists="+file.isFile()+" bytes="+file.length());
            if(file.isFile() && file.length()<=1048576){
                try {java.security.MessageDigest digest=java.security.MessageDigest.getInstance("SHA-256");
                    try(InputStream in=new FileInputStream(file)){byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1)digest.update(b,0,n);}
                    StringBuilder hash=new StringBuilder();for(byte b:digest.digest())hash.append(String.format(Locale.US,"%02x",b&255));event("asset sha256="+file.getName()+" "+hash);
                }catch(Exception ex){event("asset probe error="+ex);}
            }
        }
        File model=new File(c.getExternalFilesDir(null),"data/3d/characters/prince/prince_modular.bdae");
        event("model probe="+model.getAbsolutePath()+" exists="+model.isFile()+" bytes="+model.length());
        if(model.isFile())try(InputStream in=new FileInputStream(model)){
            byte[] header=new byte[32];int n=in.read(header);StringBuilder hex=new StringBuilder();
            for(int i=0;i<n;i++)hex.append(String.format(Locale.US,"%02x",header[i]&255));
            event("model first32="+hex);
        }catch(IOException ex){event("model probe error="+ex);}
        if(c instanceof Application)((Application)c).registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks(){
            public void onActivityCreated(Activity a,Bundle b){event(a.getClass().getSimpleName()+" created");}
            public void onActivityStarted(Activity a){event(a.getClass().getSimpleName()+" started");}
            public void onActivityResumed(Activity a){event(a.getClass().getSimpleName()+" resumed");}
            public void onActivityPaused(Activity a){event(a.getClass().getSimpleName()+" paused");}
            public void onActivityStopped(Activity a){event(a.getClass().getSimpleName()+" stopped");}
            public void onActivityDestroyed(Activity a){event(a.getClass().getSimpleName()+" destroyed");}
            public void onActivitySaveInstanceState(Activity a,Bundle b){event(a.getClass().getSimpleName()+" saved state");}
        });
    }
    public static synchronized void event(String text){
        if(events==null)return;
        try {
            if(events.length()>512*1024){File old=new File(events.getParentFile(),"dh2-events-previous.txt");old.delete();events.renameTo(old);}
            try(FileOutputStream out=new FileOutputStream(events,true)){out.write((System.currentTimeMillis()+" tid="+android.os.Process.myTid()+" "+text+"\n").getBytes(StandardCharsets.UTF_8));}
        }catch(IOException ignored){}
    }
    public static int phoneLanguage(int original){event("phone language original="+original+" returned="+(english?0:original));return english?0:original;}
    private static int[] initialPhoneSize(int originalWidth,int originalHeight){
        if(!fit || originalWidth<=0 || originalHeight<=0)return new int[]{originalWidth,originalHeight};
        int width=Math.max(originalWidth,originalHeight),height=Math.min(originalWidth,originalHeight);
        int fittedHeight=(int)((long)width*9/16),fittedWidth=width;
        if(fittedHeight>height){fittedHeight=height;fittedWidth=(int)((long)height*16/9);}
        return new int[]{fittedWidth,fittedHeight};
    }
    public static int initialPhoneWidth(int width,int height){
        int[] fitted=initialPhoneSize(width,height);
        event("initial phone="+width+"x"+height+" fitted="+fitted[0]+"x"+fitted[1]);
        return fitted[0];
    }
    public static int initialPhoneHeight(int width,int height){return initialPhoneSize(width,height)[1];}
    public static void installContent(Activity activity,View view){
        if(!fit){activity.setContentView(view);return;}
        FrameLayout frame=new FrameLayout(activity){
            @Override protected void onMeasure(int widthSpec,int heightSpec){
                int width=MeasureSpec.getSize(widthSpec),height=MeasureSpec.getSize(heightSpec);
                int w=width,h=(int)((long)width*9/16);
                if(h>height){h=height;w=(int)((long)height*16/9);}
                FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)view.getLayoutParams();p.width=w;p.height=h;
                super.onMeasure(widthSpec,heightSpec);
            }
        };
        frame.setBackgroundColor(0xff000000);frame.addView(view,new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER));activity.setContentView(frame);
    }
    public static GLSurfaceView.Renderer wrap(GLSurfaceView view,GLSurfaceView.Renderer renderer){
        view.setPreserveEGLContextOnPause(preserve);
        return new GLSurfaceView.Renderer(){
            long frames,started=SystemClock.elapsedRealtime();int surfaces;
            public void onSurfaceCreated(GL10 gl,EGLConfig config){
                event("surface created #"+(++surfaces)+" EGL="+EGL14.eglGetCurrentContext()+" GL="+gl.glGetString(GL10.GL_VERSION)+" renderer="+gl.glGetString(GL10.GL_RENDERER));
                renderer.onSurfaceCreated(gl,config);event("surface initialization returned");
            }
            public void onSurfaceChanged(GL10 gl,int w,int h){
                event("surface size="+w+"x"+h+" view="+view.getWidth()+"x"+view.getHeight());
                renderer.onSurfaceChanged(gl,w,h);
                // The legacy renderer can retain the portrait GL viewport set
                // during onSurfaceCreated even after its resize callback.
                if(fit && w>0 && h>0){GLES20.glViewport(0,0,w,h);event("fit viewport="+w+"x"+h);}
                event("surface resize returned");
            }
            public void onDrawFrame(GL10 gl){
                renderer.onDrawFrame(gl);frames++;
                if(frames==1 || frames%120==0)event("render returned frames="+frames+" elapsedMs="+(SystemClock.elapsedRealtime()-started));
            }
        };
    }
    public static boolean fitEnabled(){return fit;}
    private GameTrace(){}
}
