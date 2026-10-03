/* Exercise original CFile constructor and basename search without a GPU. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
 setvbuf(stdout,0,_IONBF,0);
 if(argc<3)return 2;
 void *dl=dlopen("libdl_android.so",RTLD_NOW);
 void(*target)(unsigned)=dlsym(dl,"android_set_application_target_sdk_version");target(24);
 if(!dlopen("libzbcompat.so",RTLD_NOW|RTLD_GLOBAL)){puts(dlerror());return 1;}
 char lib[1024];snprintf(lib,sizeof lib,"%s/libDungeonHunter2.so",argv[1]);
 void *h=dlopen(lib,RTLD_NOW|RTLD_GLOBAL);if(!h){puts(dlerror());return 1;}
 if(argc>3){snprintf(lib,sizeof lib,"%s/libStormGLOFT.so",argv[1]);if(!dlopen(lib,RTLD_NOW|RTLD_GLOBAL)){puts(dlerror());return 1;}}
 void *(*ctor)(void*,FILE*,const char*,int)=dlsym(h,"_ZN6glitch2io5CFileC1EP7__sFILEPKcb");
 uint32_t obj[16]={0};
 printf("CFile construct: %s\n",argv[2]);
 ctor(obj,0,argv[2],0);
 printf("CFile success: begin=%08x end=%08x basename=%08x\n",obj[7],obj[6],obj[8]);
 if(obj[8])printf("basename=%s\n",(char*)obj[8]);
 return 0;
}
