/* Reconstructed equivalent of Storm's hook_import_function, using public ELF
 * data instead of the removed private bionic soinfo layout. Exact DH2 pair only.
 */
#include <elf.h>
#include <stdint.h>
#include <stddef.h>
#include "storm_path_repair.h"

extern void* storm_dlopen(const char*,int);
extern void* storm_dlsym(void*,const char*);
extern void* storm_fopen(const char*,const char*);
extern int storm_puts(const char*);
extern int* storm_errno(void);
extern int storm_log(int,const char*,const char*,...);

static void dh2_note(const char* prefix,const char* value) {
    char line[640];unsigned n=0;
    while(*prefix && n<100)line[n++]=*prefix++;
    for(unsigned i=0;value && value[i] && i<512;i++)line[n++]=value[i];
    line[n++]='\n';line[n]=0;
    storm_log(5,"DH2FileGuard","%s",line);
    /* Test 3 showed that abort may precede stdout flush. fd 2 is captured by
     * the bridge independently of the Android logd socket and its filtering. */
    int (*write_fn)(int,const void*,unsigned)=
        (int(*)(int,const void*,unsigned))storm_dlsym((void*)(uintptr_t)0xffffffffu,"write");
    if(write_fn)write_fn(2,line,n);
}


/* DH2 CFile uses string::at(last_separator + 1). A trailing separator
 * therefore aborts after fopen has accepted a directory. Treat such input as
 * a failed FILE open at the engine import boundary, preserving its null path.
 * Directory enumeration via opendir/openat is deliberately unaffected. */
__attribute__((visibility("hidden")))
void* dh2_fopen_guard(const char* path,const char* mode) {
    if(path) {
        const char* end=path;while(*end)++end;
        if(end!=path && (end[-1]=='/' || end[-1]=='\\')) {
            dh2_note("DH2FileGuard rejected directory-shaped path: ",path);
            *storm_errno()=21; /* EISDIR in ARM Linux/bionic. */
            return 0;
        }
    }
    void* result=storm_fopen(path,mode);
    int saved_errno=*storm_errno();
    if(!result && dh2_read_only_mode(mode)) {
        char recovered[1024];
        if(dh2_repeated_cache_root(path,recovered,sizeof recovered)) {
            result=storm_fopen(recovered,mode);
            if(result) {
                saved_errno=*storm_errno();
                dh2_note("DH2FileGuard recovered repeated root: ",recovered);
            } else *storm_errno()=saved_errno;
        }
        if(!result && dh2_qata_texture_path(path,recovered,sizeof recovered)) {
            result=storm_fopen(recovered,mode);
            if(result) {
                saved_errno=*storm_errno();
                dh2_note("DH2FileGuard recovered texture path: ",recovered);
            } else *storm_errno()=saved_errno;
        }
    }
    /* Record the asset whose failed reopen immediately preceded Test 4's
     * COnDemandReader null dispatch. Do not flood logs for all engine opens. */
    const char* basename=path;
    if(path)for(const char* p=path;*p;++p)if(*p=='/' || *p=='\\')basename=p+1;
    const char* expected="prince_modular.bdae";
    const char* match=basename;
    if(match){while(*match && *match==*expected){++match;++expected;}
        if(!*match && !*expected)dh2_note(result?"DH2Model opened: ":"DH2Model open failed: ",path);}
    *storm_errno()=saved_errno;
    return result;
}

/* The engine's no-exceptions STL prints a reason with puts before aborting.
 * Keep that reason even when stdout is buffered and the process dies. */
__attribute__((visibility("hidden")))
int dh2_puts_log(const char* text) {
    int saved_errno=*storm_errno();
    dh2_note("DH2Engine: ",text);
    *storm_errno()=saved_errno;
    return storm_puts(text);
}

static int same(const char* a,const char* b) {
    while(*a && *a==*b){a++;b++;}return *a==*b;
}

__attribute__((visibility("default")))
void* storm_import_fix(const char* library,const char* name,void* replacement) {
    if(!library||!name||!replacement)return 0;
    void* handle=storm_dlopen(library,1);
    if(!handle)return 0;
    uintptr_t anchor=(uintptr_t)storm_dlsym(handle,"JNI_OnLoad");
    if(!anchor)return 0;
    uintptr_t base=anchor-0x0053224c;
    const Elf32_Ehdr* eh=(const Elf32_Ehdr*)base;
    if(eh->e_ident[0]!=0x7f||eh->e_ident[1]!='E'||eh->e_ident[2]!='L'||eh->e_ident[3]!='F'||eh->e_machine!=EM_ARM)return 0;
    const Elf32_Phdr* ph=(const Elf32_Phdr*)(base+eh->e_phoff);
    const Elf32_Dyn* dynamic=0;size_t count=0;
    for(unsigned i=0;i<eh->e_phnum;i++)if(ph[i].p_type==PT_DYNAMIC){dynamic=(const Elf32_Dyn*)(base+ph[i].p_vaddr);count=ph[i].p_memsz/sizeof(Elf32_Dyn);break;}
    if(!dynamic)return 0;
    const Elf32_Sym* symbols=0;const char* strings=0;const Elf32_Rel* relocs=0;size_t bytes=0;
    for(size_t i=0;i<count && dynamic[i].d_tag!=DT_NULL;i++){
        uintptr_t value=dynamic[i].d_un.d_ptr;
        switch(dynamic[i].d_tag){
            case DT_SYMTAB:symbols=(const Elf32_Sym*)(base+value);break;
            case DT_STRTAB:strings=(const char*)(base+value);break;
            case DT_JMPREL:relocs=(const Elf32_Rel*)(base+value);break;
            case DT_PLTRELSZ:bytes=value;break;
        }
    }
    if(!symbols||!strings||!relocs||bytes>1024*1024)return 0;
    void* previous=0;
    for(size_t i=0;i<bytes/sizeof(Elf32_Rel);i++){
        if(ELF32_R_TYPE(relocs[i].r_info)!=R_ARM_JUMP_SLOT)continue;
        const Elf32_Sym* symbol=&symbols[ELF32_R_SYM(relocs[i].r_info)];
        const char* imported=strings+symbol->st_name;
        void** slot=(void**)(base+relocs[i].r_offset);
        if(same(imported,"fopen"))*slot=(void*)dh2_fopen_guard;
        if(same(imported,"puts"))*slot=(void*)dh2_puts_log;
        if(same(imported,name)){
            /* The pinned DH2 ELF has no GNU_RELRO segment; its GOT is writable. */
            previous=*slot;*slot=replacement;
        }
    }
    return previous;
}
