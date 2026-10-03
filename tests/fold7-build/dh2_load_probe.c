/* Runs the supplied engine through the same ARM32 -> ARM64 runtime as the APK.
 * Loading libraries and calling pure functions does not test JNI, graphics or gameplay.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <jni.h>

int main(int argc,char**argv) {
    setvbuf(stdout,0,_IONBF,0);
    if(argc!=2){fprintf(stderr,"usage: dh2_load_probe <guest library directory>\n");return 2;}
    void* dl=dlopen("libdl_android.so",RTLD_NOW);
    void(*target)(unsigned)=dl?(void(*)(unsigned))dlsym(dl,"android_set_application_target_sdk_version"):0;
    if(!target){fprintf(stderr,"target SDK API unavailable\n");return 1;}target(24);
    if(!dlopen("libzbcompat.so",RTLD_NOW|RTLD_GLOBAL)){fprintf(stderr,"compat: %s\n",dlerror());return 1;}
    const char* names[]={"DungeonHunter2","StormGLOFT","nativeinterface"};
    void* handles[3]={0};
    for(int i=0;i<3;i++){
        char path[1024];snprintf(path,sizeof path,"%s/lib%s.so",argv[1],names[i]);
        printf("Loading %s...\n",names[i]);handles[i]=dlopen(path,RTLD_NOW|RTLD_GLOBAL);
        if(!handles[i]){fprintf(stderr,"FAIL %s: %s\n",names[i],dlerror());return 1;}
        printf("PASS loaded %s\n",names[i]);
    }
    /* These Lua routines are local symbols. The input library is SHA-256 pinned
       by the runner; recover the load bias from an exported symbol. */
    Dl_info info;
    if(!dladdr(dlsym(handles[0],"JNI_OnLoad"),&info)){puts("FAIL engine load bias");return 1;}
    uintptr_t base=(uintptr_t)info.dli_fbase;
    int(*log2fn)(unsigned)=(int(*)(unsigned))(base+0x854854);
    unsigned(*int2fb)(unsigned)=(unsigned(*)(unsigned))(base+0x854800);
    unsigned(*fb2int)(unsigned)=(unsigned(*)(unsigned))(base+0x854838);
    for(unsigned i=0;i<4096;i++){
        int want=-1;for(unsigned x=i;x;x>>=1)want++;
        if(log2fn(i)!=want){printf("FAIL log2 %u\n",i);return 1;}
        unsigned rounded=fb2int(int2fb(i));
        if(rounded<i){printf("FAIL rounding %u\n",i);return 1;}
    }
    puts("PASS original engine log2 and integer encoding: 4096 inputs");
    /* The engine entry point retains JavaVM and sets native audio parameters.
       Storm ignores JavaVM and installs native hooks. This inert VM tests those
       entry points, but does not stand in for Android ART. */
    unsigned original_inline[4];memcpy(original_inline,(void*)(base+0x530c50),sizeof original_inline);
    static const struct JNIInvokeInterface vm_functions={0};
    JavaVM vm=&vm_functions;
    for(int i=0;i<2;i++){
        jint(*onload)(JavaVM*,void*)=(jint(*)(JavaVM*,void*))dlsym(handles[i],"JNI_OnLoad");
        if(!onload){printf("FAIL JNI_OnLoad missing: %s\n",names[i]);return 1;}
        printf("Calling %s JNI_OnLoad with inert VM...\n",names[i]);
        jint version=onload(&vm,0);
        if(version!=JNI_VERSION_1_4&&version!=JNI_VERSION_1_6){printf("FAIL JNI version 0x%x\n",version);return 1;}
        printf("PASS %s JNI_OnLoad: 0x%x\n",names[i],version);
    }
    void* shader=dlsym(handles[1],"_Z17my_glShaderSourcejiPKPKcPKi");
    void* getstring=dlsym(handles[1],"_Z14my_glGetStringj");
    if(!shader||*(void**)(base+0x994ff8)!=shader){puts("FAIL shader hook not installed");return 1;}
    if(!getstring||*(void**)(base+0x994c4c)!=getstring){puts("FAIL GL string hook not installed");return 1;}
    if(!memcmp(original_inline,(void*)(base+0x530c50),sizeof original_inline)){puts("FAIL inline hook not installed");return 1;}
    puts("PASS shader and GL-string GOT hooks and original inline hook are installed");
    puts("PASS native load probe; Android framework and gameplay remain untested");
    return 0;
}
