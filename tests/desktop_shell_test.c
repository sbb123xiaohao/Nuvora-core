#include <assert.h>
#include <stdio.h>
#include <nv/sdk.h>
#include "../user/desktop_shell.h"
static int pin_open_error,pin_close_error,pin_replace_error,pin_sync_error,pin_info_error;
static bool pin_short_write;
static u32 pin_opens,pin_syncs,pin_commits,pin_removes,pin_committed_mask;
static struct desktop_pins pin_pending;
static iptr launcher_test_syscall(u32 op,uptr a,uptr b,uptr c) {
    if (op==NV_OPEN) {
        assert(!strcmp((const char *)a,"/home/users/alice/.apps-new"));
        assert(b==(NV_WRITE|NV_CREATE|NV_TRUNC));++pin_opens;
        return pin_open_error?pin_open_error:3;
    }
    if (op==NV_EMIT) {
        assert(a==3 && c==sizeof(pin_pending));memcpy(&pin_pending,(void *)b,c);
        return pin_short_write?4:(iptr)c;
    }
    if (op==NV_CLOSE) { assert(a==3);return pin_close_error; }
    if (op==NV_REPLACE) {
        assert(!strcmp((const char *)a,"/home/users/alice/.apps-new"));
        assert(!strcmp((const char *)b,"/home/users/alice/.apps"));
        if (pin_replace_error) return pin_replace_error;
        assert(desktop_pins_valid(&pin_pending));pin_committed_mask=pin_pending.mask;++pin_commits;return 0;
    }
    if (op==NV_REMOVE) { ++pin_removes;return 0; }
    if (op==NV_INFO) {
        if (pin_info_error) return pin_info_error;
        *(struct nv_info *)a=(struct nv_info){.disk_present=1};return 0;
    }
    if (op==NV_CONTROL) { assert(a==NV_CTL_SYNC);++pin_syncs;return pin_sync_error; }
    /* Keep the ordinary SDK semantics for unrelated, discarded desktop code. */
    return nv_syscall(op,a,b,c);
}
#define nv_syscall launcher_test_syscall
#include "../user/desktop.c"

static void search(void) {
    assert(desktop_client_app("Music")==DESKTOP_MEDIA);
    assert(desktop_client_app("Media")==DESKTOP_MEDIA);
    assert(desktop_client_app("Terminal")==DESKTOP_TERMINAL);
    assert(desktop_client_app("Folio")==DESKTOP_WINDOW_COUNT);
    assert(desktop_client_app(NULL)==DESKTOP_WINDOW_COUNT);
    struct desktop_launcher l={0}; desktop_search(&l);
    assert(l.count==7 && l.selected==0);
    strlcpy(l.query,"  MP3 VIDEO  ",sizeof(l.query)); desktop_search(&l);
    assert(l.count==1 && l.ids[0]==DESKTOP_MEDIA);
    strlcpy(l.query,"TeRm",sizeof(l.query)); desktop_search(&l);
    assert(l.count==1 && l.ids[0]==DESKTOP_TERMINAL);
    strlcpy(l.query,"text edit",sizeof(l.query)); desktop_search(&l);
    assert(l.count==1 && l.ids[0]==DESKTOP_EDITOR);
    strlcpy(l.query,"files video",sizeof(l.query)); desktop_search(&l);
    assert(!l.count && !l.selected);
    strlcpy(l.query,"\";reboot",sizeof(l.query)); desktop_search(&l);
    assert(!l.count); /* Search terms select registered apps, never commands. */
    memset(l.query,'x',sizeof(l.query)-1); l.query[sizeof(l.query)-1]=0;
    desktop_search(&l); assert(!l.count);
    strlcpy(l.query,"password",sizeof(l.query));desktop_search(&l);assert(l.count==1 && l.ids[0]==5);
    l.query[0]=0; desktop_search(&l); assert(l.count==7);
    desktop_launcher_move(&l,3);assert(l.selected==3);
    desktop_launcher_move(&l,1);assert(l.selected==4);
    desktop_launcher_move(&l,-3);assert(l.selected==1);
    desktop_launcher_move(&l,-1);assert(l.selected==0);
    desktop_launcher_move(&l,99);assert(l.selected==6);
    desktop_launcher_move(&l,-99);assert(l.selected==0);
    l.count=0;desktop_launcher_move(&l,3);assert(l.selected==0);
}
static void notices(void) {
    strlcpy(message,"Mounted drives saved.",sizeof(message));message_tick=100;message_error=true;
    assert(!note_expire(699) && *message && message_error);
    assert(note_expire(700) && !*message && !message_error);
    assert(!note_expire(701));
    strlcpy(message,"Fresh result",sizeof(message));message_tick=700;
    assert(!note_expire(1299) && *message);assert(note_expire(1300));
    strlcpy(message,"Wrap",sizeof(message));message_tick=~(u64)0-199;
    assert(!note_expire(399) && *message);assert(note_expire(400) && !*message);
}
static void launcher_gestures(void) {
    struct desktop_app_press p={0};
    assert(!desktop_app_press_end(&p,0,20,20));
    desktop_app_press_begin(&p,3,100,120);
    assert(p.active && !p.moved); /* Down alone must never start an app. */
    assert(desktop_app_press_end(&p,3,102,122) && !p.active);
    desktop_app_press_begin(&p,3,100,120);
    assert(!desktop_app_press_end(&p,4,100,120));
    desktop_app_press_begin(&p,3,100,120);
    desktop_app_press_move(&p,130,120);desktop_app_press_move(&p,100,120);
    assert(!desktop_app_press_end(&p,3,100,120)); /* Returning cannot turn a drag into a click. */
    desktop_app_press_begin(&p,3,0,0);
    assert(!desktop_app_press_end(&p,3,0xffffffffu,0));
    struct desktop_pins pins={0x3150414e,0x7f};assert(desktop_pins_valid(&pins));
    pins.mask=0x80;assert(!desktop_pins_valid(&pins));
    pins.mask=0;pins.magic=0;assert(!desktop_pins_valid(&pins));
    struct desktop_launcher l={.pinned=(1u<<6)|(1u<<3)};
    desktop_search(&l);assert(l.count==7 && l.ids[0]==3 && l.ids[1]==6 && l.ids[2]==0);
    strlcpy(l.query,"files",sizeof(l.query));desktop_search(&l);
    assert(l.count==1 && l.ids[0]==0);
    struct desktop_window w={.client=10};strlcpy(w.title,"Terminal",sizeof(w.title));
    assert(desktop_registered_app(&w,DESKTOP_TERMINAL)==ARRAY_LEN(desktop_apps));
    w.app=5;assert(desktop_registered_app(&w,DESKTOP_CLIENT_FIRST)==4);
    w.app=99;assert(desktop_registered_app(&w,DESKTOP_TERMINAL)==ARRAY_LEN(desktop_apps));
    w.client=0;assert(desktop_registered_app(&w,DESKTOP_ACCOUNTS)==5);
    assert(desktop_registered_app(&w,DESKTOP_SETTINGS)==6);
    struct desktop_window old={.x=12,.y=40,.w=300,.h=220,.maximized=true,.saved_w=240};
    w.client=10;w.generation=91;desktop_restore_geometry(&w,&old);
    assert(w.x==12 && w.y==40 && w.w==300 && w.h==220 && w.maximized && w.saved_w==240);
    assert(w.client==10 && w.generation==91); /* Never restore stale client buffers/generations. */
    w=(struct desktop_window){.x=140,.y=62,.w=485,.h=376};
    desktop_fit(&w,640,424);
    assert(w.x>=0 && w.y>=0 && (u32)w.x+w.w<=640 && (u32)w.y+w.h<=424);
    assert(w.w==485 && w.h==376); /* Keep the requested size when it fits. */
}
static void launcher_io(void) {
    strlcpy(account.info.home,"/home/users/alice",sizeof(account.info.home));launcher.pinned=0;
    assert(launcher_pin(99,true)==-NV_EINVAL && !pin_opens);
    assert(!launcher_pin(1,true) && launcher.pinned==2 && pin_committed_mask==2 && pin_syncs==1);
    assert(!launcher_pin(1,true) && pin_opens==1 && pin_syncs==2);
    pin_sync_error=-NV_EIO;
    assert(launcher_pin(2,true)==-NV_EIO && launcher.pinned==6 && pin_committed_mask==6);
    u32 opens=pin_opens,syncs=pin_syncs;pin_sync_error=0;
    assert(!launcher_pin(2,true) && pin_opens==opens && pin_syncs==syncs+1);
    for (u32 stage=0;stage<4;++stage) {
        pin_open_error=stage==0?-NV_EACCESS:0;pin_short_write=stage==1;
        pin_close_error=stage==2?-NV_EIO:0;pin_replace_error=stage==3?-NV_ENOSPC:0;
        u32 commits=pin_commits;syncs=pin_syncs;
        assert(launcher_pin(1,false)<0 && launcher.pinned==6 && pin_committed_mask==6);
        assert(pin_commits==commits && pin_syncs==syncs);
    }
    pin_open_error=pin_close_error=pin_replace_error=0;pin_short_write=false;
    assert(pin_removes==3);
    pin_info_error=-NV_EIO;syncs=pin_syncs;
    assert(launcher_pin(1,false)==-NV_EIO && launcher.pinned==4 && pin_syncs==syncs);
    pin_info_error=0;
    assert(!launcher_pin(1,false) && pin_syncs==syncs+1);
}
static void switching(void) {
    struct desktop_window w[DESKTOP_WINDOW_COUNT]={0};
    u8 order[DESKTOP_WINDOW_COUNT],ids[DESKTOP_WINDOW_COUNT];
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) order[i]=(u8)i;
    w[0].open=w[2].open=w[4].open=true; w[2].minimized=true;
    desktop_raise(order,2); desktop_raise(order,0); desktop_raise(order,4);
    assert(desktop_window_list(w,order,ids)==3 && ids[0]==4 && ids[1]==0 && ids[2]==2);
    struct desktop_switcher s={0};
    desktop_switch_begin(&s,w,order,4,1);
    assert(s.open && s.count==3 && s.ids[s.selected]==0);
    desktop_switch_step(&s,1); assert(s.ids[s.selected]==2);
    desktop_switch_step(&s,1); assert(s.ids[s.selected]==4);
    desktop_switch_step(&s,-1); assert(s.ids[s.selected]==2);
    desktop_raise(order,0); /* A new focus must not mutate this captured list. */
    assert(s.ids[0]==4 && s.ids[1]==0);
    w[4].open=false; desktop_switch_prune(&s,w);
    assert(s.count==2 && s.ids[s.selected]==2);
    w[2].open=false; desktop_switch_prune(&s,w);
    assert(s.count==1 && s.ids[s.selected]==0);
    w[0].open=false; desktop_switch_prune(&s,w); assert(!s.open && !s.count);
    w[2].open=true; desktop_switch_begin(&s,w,order,DESKTOP_WINDOW_COUNT,1);
    assert(s.open && s.ids[s.selected]==2);
    desktop_switch_step(&s,-1); assert(s.selected==0);
}
static void placement(void) {
    const u32 widths[]={500,640,641,960};
    for (u32 i=0;i<ARRAY_LEN(widths);++i) {
        u32 sw=widths[i];
        struct desktop_window w={.x=17,.y=26,.w=340,.h=220};
        desktop_place(&w,sw,330,DESKTOP_TILE_LEFT);
        assert(w.x==0 && w.y==0 && w.w==sw/2 && w.h==330 && !w.maximized);
        desktop_place(&w,sw,330,DESKTOP_TILE_RIGHT);
        assert((u32)w.x==sw/2 && w.w==sw-sw/2 && (u32)w.x+w.w==sw);
        desktop_place(&w,sw,330,DESKTOP_TILE_MAX);
        assert(w.maximized && w.x==0 && w.w==sw && w.saved_w==340);
        desktop_place(&w,sw,330,DESKTOP_FLOATING);
        assert(!w.maximized && !w.tiled && w.x==17 && w.y==26 && w.w==340 && w.h==220);
        desktop_place(&w,sw,330,DESKTOP_TILE_MAX);
        desktop_place(&w,320,190,DESKTOP_FLOATING);
        assert(w.x==0 && w.y==0 && w.w==320 && w.h==190);
        assert(desktop_snap_target(sw,0,80)==DESKTOP_TILE_LEFT);
        assert(desktop_snap_target(sw,sw-1,80)==DESKTOP_TILE_RIGHT);
        assert(desktop_snap_target(sw,sw/2,0)==DESKTOP_TILE_MAX);
        assert(desktop_snap_target(sw,sw/2,80)==DESKTOP_FLOATING);
        for (u32 count=1;count<=desktop_overview_page_size(sw);++count) for (u32 n=0;n<count;++n) {
            struct desktop_rect r=desktop_overview_rect(sw,350,count,n);
            assert(r.x+r.w<=sw-24 && r.y+r.h<350-65 && r.w>=140 && r.h>=80);
        }
    }
}
static void resize_bounds(void) {
    struct desktop_window w={.x=320,.y=0,.w=320,.h=424,.tiled=DESKTOP_TILE_RIGHT};
    desktop_resize(&w,2,640,424,340,310,-30,0);
    assert(w.x==320 && w.w==320 && w.h==424 && !w.tiled);
    w=(struct desktop_window){.x=140,.y=300,.w=400,.h=124};
    desktop_resize(&w,8,640,424,300,310,0,-20);
    assert(w.y==300 && w.h==124);
    w=(struct desktop_window){.x=100,.y=50,.w=400,.h=300};
    desktop_resize(&w,10,640,424,300,160,-40,-20);
    assert(w.x==100 && w.y==50 && w.w==360 && w.h==280);
    const struct desktop_window frames[]={
        {.x=0,.y=0,.w=640,.h=424}, {.x=320,.y=212,.w=320,.h=212},
        {.x=500,.y=300,.w=140,.h=124}, {.x=50,.y=40,.w=420,.h=310}};
    const i32 deltas[]={-700,-40,0,30,700};
    for (u32 i=0;i<ARRAY_LEN(frames);++i) for (u32 edges=1;edges<16;++edges)
        for (u32 dx=0;dx<ARRAY_LEN(deltas);++dx) for (u32 dy=0;dy<ARRAY_LEN(deltas);++dy) {
            w=frames[i];desktop_resize(&w,edges,640,424,482,354,deltas[dx],deltas[dy]);
            assert(w.x>=0 && w.y>=0 && w.w && w.h);
            assert((u32)w.x+w.w<=640 && (u32)w.y+w.h<=424);
        }
}
static void editor_navigation(void) {
    /* A 340-pixel editor has 38 visible columns. Exercise the actual key and
     * pointer paths so keyboard navigation agrees with the rendered rows. */
    mode.width=1024; mode.height=768;
    windows[DESKTOP_EDITOR]=(struct desktop_window){.w=340,.h=220};
    memset(editor_text,'x',90); editor_text[90]=0;
    editor_length=90; editor_cursor=45; editor_scroll=0;
    editor_mode=DESKTOP_EDIT_NORMAL; editor_dirty=false;
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==7);
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==45);
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==83);
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==83);
    assert(!editor_dirty && editor_length==90 && editor_text[90]==0);

    /* A short explicit line clamps the column; a full-width line followed by
     * a newline includes the same empty visual row as the renderer. */
    memset(editor_text,'a',38);
    memcpy(editor_text+38,"\nabc\n0123456789",15);
    editor_length=53; editor_text[editor_length]=0;
    editor_cursor=7;
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==38);
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==39);
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==43);
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==39);
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==38);
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==0);
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==0);
    editor_cursor=42;
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==46);
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==42);

    /* The logical window width is unchanged at 2x UI scale. */
    mode.width=1920; mode.height=1080;
    memset(editor_text,'x',90); editor_text[90]=0; editor_length=90;
    editor_cursor=45; editor_scroll=0;
    assert(editor_key(NV_KEY_UP,0)==0 && editor_cursor==7);
    assert(editor_key(NV_KEY_DOWN,0)==0 && editor_cursor==45);
    assert(editor_cursor_at(19+7*DESKTOP_EDITOR_CELL,73+DESKTOP_EDITOR_LINE)==45);
}
static void spaces_model(void) {
    struct nv_dirent64 items[6]={
        {.name="zeta.txt",.kind=NV_FILE,.size=20},
        {.name="Work",.kind=NV_DIR},
        {.name="alpha.MP3",.kind=NV_FILE,.size=~(u64)0},
        {.name="notes.md",.kind=NV_FILE,.size=1ull<<40},
        {.name="Archive",.kind=NV_DIR},
        {.name="Beta.mpg",.kind=NV_FILE,.size=10}};
    assert(files_matches("Report-Final.MD","FINAL.md"));
    assert(files_matches("file","") && !files_matches("file","files"));
    assert(!files_matches("", "x") && files_matches("aAa","AA"));
    files_sort(items,6,FILE_SORT_NAME);
    assert(!strcmp(items[0].name,"Archive") && !strcmp(items[1].name,"Work"));
    assert(!strcmp(items[2].name,"alpha.MP3") && !strcmp(items[5].name,"zeta.txt"));
    files_sort(items,6,FILE_SORT_KIND);
    assert(!strcmp(items[2].name,"notes.md") && !strcmp(items[3].name,"zeta.txt"));
    assert(!strcmp(items[4].name,"alpha.MP3") && !strcmp(items[5].name,"Beta.mpg"));
    files_sort(items,6,FILE_SORT_SIZE);
    assert(items[2].size==~(u64)0 && items[3].size==(1ull<<40));
    assert(suffix("DOCUMENT.TXT",".txt") && !suffix("not.txtx",".txt"));
    assert(!strcmp(desktop_kind(&items[2]),"MP3 audio"));

    struct desktop_preferences p={0x3155494e,2,15,0};
    assert(files_preferences_valid(&p));p.options=1;assert(!files_preferences_valid(&p));
    p.magic=0x3255494e;p.options=73|(1u<<7)|(2u<<8)|1024u;
    assert(files_preferences_valid(&p));
    p.options|=1u<<11;assert(!files_preferences_valid(&p));
    p.options=101;assert(!files_preferences_valid(&p));
    p.options=3u<<8;assert(!files_preferences_valid(&p));
    p.options=100;p.idle_minutes=0;assert(!files_preferences_valid(&p));
    p.idle_minutes=5;p.theme=3;assert(!files_preferences_valid(&p));
    p.theme=1;p.magic=0;assert(!files_preferences_valid(&p));

    windows[DESKTOP_FILES]=(struct desktop_window){.w=640,.h=420};
    count=10;selected=scroll=0;file_view=0;
    struct files_layout grid=desktop_files_layout(&windows[DESKTOP_FILES],file_view);
    files_move(NV_KEY_RIGHT);assert(selected==1 && !scroll);
    files_move(NV_KEY_DOWN);assert(selected==1+grid.columns);
    files_move(NV_KEY_DOWN);assert(selected==1+2*grid.columns);
    files_move(NV_KEY_LEFT);assert(selected==2*grid.columns);
    for (u32 i=0;i<3;++i) files_move(NV_KEY_PGDN);
    assert(selected==9 && scroll%grid.columns==0 && selected>=scroll &&
        selected<scroll+grid.columns*grid.rows);
    windows[DESKTOP_FILES].w=340;scroll_to_selection();
    grid=desktop_files_layout(&windows[DESKTOP_FILES],file_view);
    assert(selected==9 && selected>=scroll && selected<scroll+grid.columns*grid.rows);
    file_view=1;scroll_to_selection();grid=desktop_files_layout(&windows[DESKTOP_FILES],file_view);
    assert(grid.columns==1 && selected==9 && selected>=scroll && selected<scroll+grid.rows);
    count=0;files_move(NV_KEY_UP);assert(!selected && !scroll);
    settings_tab=SETTINGS_SYSTEM;account.info.role=0;
    assert(settings_activate(SETTINGS_ACTION+1)==-NV_EACCESS);
    settings_tab=SETTINGS_NETWORK;assert(settings_activate(SETTINGS_ACTION+1)==-NV_EACCESS);
    settings_tab=SETTINGS_SOUND;audio_ready=false;
    assert(settings_activate(SETTINGS_ACTION)==-NV_ENODEV);
}
int main(void) {
    search(); notices(); launcher_gestures(); launcher_io(); switching(); placement(); resize_bounds(); editor_navigation(); spaces_model();
    puts("PASS desktop shell: release/cancel gestures, private pins and failed-write/sync retry, MRU/tiling/editor, Spaces filter/sort/grid, preferences and settings roles");
    return 0;
}
