#ifndef DH2_STORM_PATH_REPAIR_H
#define DH2_STORM_PATH_REPAIR_H

static unsigned char dh2_ascii_lower(unsigned char c) {
    return c>='A' && c<='Z' ? c+('a'-'A') : c;
}

static int dh2_read_only_mode(const char* mode) {
    return mode && mode[0]=='r' &&
        (!mode[1] || (mode[1]=='b' && !mode[2]));
}

/* The direct guest keeps its cache under <package>/files/. The standalone
 * wrapper puts the same cache under <host>/files/plugins/<DH2 package>/.
 * Limit the second form to this guest so an arbitrary nested directory does
 * not become a candidate for the repeated-root retry. */
static int dh2_cache_root_suffix(const char* path,unsigned package_start,unsigned root_len) {
    unsigned p=package_start;
    while(p<root_len && path[p]!='/')++p;
    if(p==package_start || p==root_len)return 0;
    const char* files="/files/";
    for(unsigned j=0;j<7;j++)
        if(p+j>=root_len || dh2_ascii_lower(path[p+j])!=(unsigned char)files[j])return 0;
    p+=7;
    if(p==root_len)return 1;
    const char* plugin="plugins/com.gameloft.android.gand.gloftd2ss/";
    unsigned j=0;
    for(;plugin[j];j++)
        if(p+j>=root_len || dh2_ascii_lower(path[p+j])!=(unsigned char)plugin[j])return 0;
    p+=j;
    return p==root_len;
}

/* Deferred model names can contain the cache root twice. The second copy is
 * lowercased by the original engine. Accept only the same Android cache root
 * repeated exactly, ignoring ASCII case, and retain its first spelling. */
static int dh2_repeated_cache_root(const char* path,char* out,unsigned cap) {
    const char* android_root="/storage/emulated/0/android/data/";
    unsigned i=0;
    if(!path || !out || !cap)return 0;
    while(android_root[i]) {
        if(!path[i] || dh2_ascii_lower(path[i])!=(unsigned char)android_root[i])return 0;
        ++i;
    }
    /* Bound every read to the output capacity. Long paths retain fopen's
     * ordinary failure instead of producing a truncated substitute path. */
    unsigned n=0;
    while(n<cap && path[n])++n;
    if(n==cap)return 0;
    unsigned split=0;
    for(unsigned p=i;p+1<n;p++)if(path[p]=='/' && path[p+1]=='/'){
        split=p;break;
    }
    unsigned root_len=split+1;
    if(!dh2_cache_root_suffix(path,i,root_len))return 0;
    unsigned second=split+1;
    if(second+root_len>=n)return 0; /* Require a child file after both roots. */
    for(unsigned j=0;j<root_len;j++)
        if(dh2_ascii_lower(path[j])!=dh2_ascii_lower(path[second+j]))return 0;
    const char* child=path+second+root_len;
    if(*child=='/' || *child=='\\')return 0;
    unsigned child_len=n-(second+root_len);
    if(root_len+child_len>=cap)return 0;
    for(unsigned j=0;j<root_len;j++)out[j]=path[j];
    for(unsigned j=0;j<child_len;j++)out[root_len+j]=child[j];
    out[root_len+child_len]=0;
    return 1;
}

/* The emulator has requested several existing atlas textures using `qata`
 * where the supplied cache has `data`. Limit this retry to flat .tga files
 * in that one directory; do not rewrite other resources or write modes. */
static int dh2_qata_texture_path(const char* path,char* out,unsigned cap) {
    const char* android_root="/storage/emulated/0/android/data/";
    const char* suffix="/files/qata/3d/textures/";
    unsigned i=0;
    if(!path || !out || !cap)return 0;
    while(android_root[i]) {
        if(!path[i] || dh2_ascii_lower(path[i])!=(unsigned char)android_root[i])return 0;
        ++i;
    }
    unsigned package_start=i;
    while(i<cap && path[i] && path[i]!='/')++i;
    if(i==package_start || i==cap || !path[i])return 0;
    unsigned suffix_start=i;
    for(unsigned j=0;suffix[j];j++) {
        if(i>=cap || path[i++]!=suffix[j])return 0;
    }
    unsigned name_start=i;
    while(i<cap && path[i] && path[i]!='/' && path[i]!='\\')++i;
    if(i==cap || path[i] || i-name_start<5)return 0;
    if(path[i-4]!='.' || path[i-3]!='t' || path[i-2]!='g' || path[i-1]!='a')return 0;
    for(unsigned j=0;j<=i;j++)out[j]=path[j];
    out[suffix_start+7]='d'; /* /files/ is seven bytes; qata -> data. */
    return 1;
}

#endif
