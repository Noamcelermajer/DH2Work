#include <stdio.h>
#include <string.h>
#include "../storm_path_repair.h"

static int check(const char* name,const char* path,const char* expected) {
    char out[1024];
    memset(out,0x5a,sizeof out);
    int matched=dh2_repeated_cache_root(path,out,sizeof out);
    int passed=expected ? matched && !strcmp(out,expected) : !matched;
    if(!passed)fprintf(stderr,"FAIL %s: matched=%d output=%s\n",name,matched,matched?out:"<none>");
    else printf("PASS %s\n",name);
    return passed;
}

static int check_texture(const char* name,const char* path,const char* expected) {
    char out[1024];
    memset(out,0x5a,sizeof out);
    int matched=dh2_qata_texture_path(path,out,sizeof out);
    int passed=expected ? matched && !strcmp(out,expected) : !matched;
    if(!passed)fprintf(stderr,"FAIL %s: matched=%d output=%s\n",name,matched,matched?out:"<none>");
    else printf("PASS %s\n",name);
    return passed;
}

int main(void) {
    const char* root="/storage/emulated/0/Android/data/com.gameloft.android.GAND.GloftD2SS/files/";
    const char* model="data/3d/characters/prince/prince_modular.bdae";
    char original[1024],expected[1024],wrong[1024],long_path[1100];
    snprintf(original,sizeof original,"%s/storage/emulated/0/android/data/com.gameloft.android.gand.gloftd2ss/files/%s",root,model);
    snprintf(expected,sizeof expected,"%s%s",root,model);
    snprintf(wrong,sizeof wrong,"%s/storage/emulated/0/android/data/another.package/files/%s",root,model);
    memset(long_path,'a',sizeof long_path-1);long_path[sizeof long_path-1]=0;
    int ok=1;
    ok&=check("emulator repeated model root",original,expected);
    const char* wrapper="/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/";
    snprintf(original,sizeof original,"%s/storage/emulated/0/android/data/local.dh2.fold7/files/plugins/com.gameloft.android.gand.gloftd2ss/%s",wrapper,model);
    snprintf(expected,sizeof expected,"%s%s",wrapper,model);
    ok&=check("standalone wrapper repeated model root",original,expected);
    snprintf(wrong,sizeof wrong,"%s/storage/emulated/0/android/data/local.dh2.fold7/files/plugins/another.package/%s",wrapper,model);
    ok&=check("wrapper different repeated guest",wrong,0);
    ok&=check("wrapper other plugin", "/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/other.package//storage/emulated/0/android/data/local.dh2.fold7/files/plugins/other.package/data/a",0);
    ok&=check("wrapper nested directory", "/storage/emulated/0/Android/data/local.dh2.fold7/files/plugins/com.gameloft.android.GAND.GloftD2SS/nested//storage/emulated/0/android/data/local.dh2.fold7/files/plugins/com.gameloft.android.gand.gloftd2ss/nested/data/a",0);
    ok&=check("normal absolute asset",expected,0);
    ok&=check("relative asset",model,0);
    ok&=check("cache directory",root,0);
    ok&=check("different package",wrong,0);
    ok&=check("repeated root without child","/storage/emulated/0/Android/data/p/files//storage/emulated/0/android/data/p/files/",0);
    ok&=check("unrelated double slash","/storage/emulated/0/Android/data/p/files//other/asset",0);
    ok&=check("unexpected root kind","/storage/emulated/0/Android/data/p/cache//storage/emulated/0/android/data/p/cache/data/a",0);
    ok&=check("too long",long_path,0);
    char narrow[8];
    ok&=!dh2_repeated_cache_root(original,narrow,sizeof narrow);
    ok&=dh2_read_only_mode("rb") && dh2_read_only_mode("r") &&
         !dh2_read_only_mode("r+") && !dh2_read_only_mode("rb+") &&
         !dh2_read_only_mode("wb") && !dh2_read_only_mode(0);
    const char* atlases[]={
        "atlas_skinned_characters_animdecor_gameobjects_001.tga",
        "atlas_modular_character_01.tga",
        "atlas_weapons_dh2.tga",
        "atlas_fx_particles_002.tga"
    };
    for(unsigned j=0;j<sizeof atlases/sizeof atlases[0];j++) {
        snprintf(original,sizeof original,"%sqata/3d/textures/%s",root,atlases[j]);
        snprintf(expected,sizeof expected,"%sdata/3d/textures/%s",root,atlases[j]);
        ok&=check_texture(atlases[j],original,expected);
    }
    ok&=check_texture("normal texture",expected,0);
    ok&=check_texture("relative qata","qata/3d/textures/atlas_fx_particles_002.tga",0);
    ok&=check_texture("other qata resource","/storage/emulated/0/Android/data/p/files/qata/3d/models/a.tga",0);
    ok&=check_texture("nested texture","/storage/emulated/0/Android/data/p/files/qata/3d/textures/dir/a.tga",0);
    ok&=check_texture("other extension","/storage/emulated/0/Android/data/p/files/qata/3d/textures/a.png",0);
    ok&=check_texture("texture directory","/storage/emulated/0/Android/data/p/files/qata/3d/textures/",0);
    ok&=check_texture("other root","/tmp/qata/3d/textures/a.tga",0);
    ok&=!dh2_qata_texture_path(original,narrow,sizeof narrow);
    if(!ok)return 1;
    puts("PASS repeated cache root checks");
    return 0;
}
