/* Exercise the actual credential editor and tiled account renderer. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define NV_RUNTIME_H
#include <nv/abi.h>
#include <nv/string.h>
static int account_call(u32 op,void *data) { (void)op;(void)data;return -NV_ENODEV; }
#include "../user/account_client.h"
static void typing(void) {
    struct account_ui a={0};account_page(&a,ACCOUNT_CREATE);
    const char *name="alice";for (;*name;++name) account_client_key(&a,*name,0);
    assert(!strcmp(a.fields[0],"alice"));
    account_client_key(&a,NV_KEY_HOME,0);account_client_key(&a,'z',0);
    account_client_key(&a,NV_KEY_DELETE,0);assert(!strcmp(a.fields[0],"zlice"));
    account_client_key(&a,'a',NV_KEY_CTRL);account_client_key(&a,'b',0);assert(!strcmp(a.fields[0],"b"));
    account_client_key(&a,'\t',0);account_client_key(&a,'\t',0);
    const char *password="long password 123";for (;*password;++password) account_client_key(&a,*password,0);
    assert(account_password_field(a.page,a.focus) && !a.show_password);
    account_client_key(&a,'\t',NV_KEY_SHIFT);assert(a.focus==1);
    account_page(&a,ACCOUNT_LIST);assert(!*a.fields[2]);
    a.count=1;a.profiles[0].uid=1000;strlcpy(a.profiles[0].name,"alice",32);
    a.info.uid=1000;a.info.role=1;
    account_page(&a,ACCOUNT_REMOVE);assert(!a.focus);
    account_client_key(&a,'\n',0);assert(a.page==ACCOUNT_LIST); /* Enter cancels deletion. */
    a.info.flags=NV_AUTH_LOCKED;strlcpy(a.info.name,"alice",32);
    account_page(&a,ACCOUNT_LOGIN);account_client_key(&a,'\t',NV_KEY_SHIFT);
    assert(a.focus==1 && !strcmp(a.fields[0],"alice"));
    account_client_key(&a,'l',NV_KEY_META);assert(!*a.fields[1]);
}
static void render(u32 width,u32 height,u32 scale,u32 format) {
    u32 count=width*height;
    u32 *full=malloc((count+2)*4),*tiled=malloc((count+2)*4);assert(full && tiled);
    full[0]=tiled[0]=0x12345678;full[count+1]=tiled[count+1]=0x87654321;
    struct account_ui a={.gate=true,.count=3,.info={.role=1,.uid=1000,.flags=NV_AUTH_PERSISTENT}};
    for (u32 i=0;i<3;++i) { a.profiles[i].uid=1000+i;strlcpy(a.profiles[i].name,"local-user",32);
        strlcpy(a.profiles[i].display_name,"Display name 0123456789",64); }
    struct desktop_clip clip={0,0,width,height};
    for (u32 page=ACCOUNT_WELCOME;page<=ACCOUNT_FATAL;++page) {
        account_page(&a,page);
        strlcpy(a.fields[0],"user0123",sizeof(a.fields[0]));a.positions[0]=8;
        strlcpy(a.fields[2],"a very long password 123",sizeof(a.fields[2]));
        struct account_rect r=account_gate_rect(width/scale,height/scale,&a);
        for (u32 busy=0;busy<2;++busy) {
            a.busy=busy;a.progress=(struct nv_account_progress){.completed=123456,.total=600000};
            memset(full+1,0x29,count*4);memset(tiled+1,0x29,count*4);
            struct nv_canvas all={full+1,width,0,height,format};
            account_ui_render(&all,clip,r.x*scale,r.y*scale,r.w,r.h,scale,&a);
            for (u32 y=0;y<height;y+=13) {
                struct nv_canvas part={tiled+1+(uptr)y*width,width,y,MIN(13u,height-y),format};
                account_ui_render(&part,clip,r.x*scale,r.y*scale,r.w,r.h,scale,&a);
            }
            assert(!memcmp(full,tiled,(count+2)*4));assert(full[0]==0x12345678 && full[count+1]==0x87654321);
            if (!busy && account_fields(page)) {
                struct account_rect f=account_field_rect(r.w,0);
                assert(account_ui_hit(r.w,r.h,&a,f.x+3,f.y+3)==ACCOUNT_HIT_FIELD);
            }
        }
    }
    free(full);free(tiled);
}
int main(void) {
    typing();
    for (u32 format=NV_DISPLAY_BGRX8;format<=NV_DISPLAY_RGBX8;++format) {
        render(640,480,1,format);render(1024,768,1,format);render(1280,800,1,format);
        render(1600,900,2,format);render(1920,1080,2,format);
    }
    assert(desktop_text_width("iii",3,1)<desktop_text_width("WWW",3,1));
    puts("PASS account UI: password masking, input editing, lock identity, safe deletion and tiled proportional-font rendering");
}
