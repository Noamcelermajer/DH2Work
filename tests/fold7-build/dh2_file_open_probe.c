/* Original CFileSystem::open and createReadFile, original or repaired Storm. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <jni.h>
int main(int argc,char **argv) {
 setvbuf(stdout,0,_IONBF,0);
 if(argc!=5 && argc!=6)return 2;
 void *dl=dlopen("libdl_android.so",RTLD_NOW);
 void(*target)(unsigned)=dlsym(dl,"android_set_application_target_sdk_version");target(24);
 if(!dlopen("libzbcompat.so",RTLD_NOW|RTLD_GLOBAL)){puts(dlerror());return 1;}
 char lib[1024];snprintf(lib,sizeof lib,"%s/libDungeonHunter2.so",argv[1]);
 void *h=dlopen(lib,RTLD_NOW|RTLD_GLOBAL);if(!h){puts(dlerror());return 1;}
 if(atoi(argv[2])) {
  snprintf(lib,sizeof lib,"%s/libStormGLOFT.so",argv[1]);
  void *s=dlopen(lib,RTLD_NOW|RTLD_GLOBAL);if(!s){puts(dlerror());return 1;}
  static const struct JNIInvokeInterface table={0};JavaVM vm=&table;
  jint(*start)(JavaVM*,void*)=dlsym(s,"JNI_OnLoad");start(&vm,0);
 }
 void *(*openFile)(void**,const char*,const char*)=dlsym(h,"_ZN6glitch2io11CFileSystem4openEPKcS3_");
 if(argc==6) {
  char *root=dlsym(h,"_ZN6glitch2io11CFileSystem16WorkingDirectoryE");
  if(!root || strlen(argv[5])>=1024)return 6;
  strcpy(root,argv[5]);printf("WorkingDirectory=%s\n",root);
 }
 void *result=0;
 printf("CFileSystem::open input=%s patched=%s\n",argv[3],argv[2]);
 errno=0;openFile(&result,argv[3],"rb");
 int err=errno;
 printf("CFileSystem::open returned=%s errno=%d\n",result?"file":"null",err);
 if((result!=0)!=atoi(argv[4]))return 3;
 if(result) {
  uint32_t *obj=result;FILE *f=(FILE*)obj[1];char b[32]={0};
  if(fread(b,1,5,f)!=5 || memcmp(b,"probe",5))return 4;
  puts("PASS original FILE content intact");
  void(*drop)(void*)=dlsym(h,"_ZN6glitch13ISharedObjectINS_2io5CFileEE4dropEv");
  (void)drop; /* process exit cleans isolated probe state */
 } else {
  void *(*readFile)(const char*)=dlsym(h,"_ZN6glitch2io14createReadFileEPKc");
  if(!readFile || readFile(argv[3]))return 5;
  puts("PASS original createReadFile returns null for invalid input");
 }
 puts("PASS file-open contract");return 0;
}
