#include <assert.h>
#include <stdio.h>
#include "../user/desktop_shell.h"
#include "../user/desktop.c"

static void search(void) {
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
    files_move(NV_KEY_RIGHT);assert(selected==1 && !scroll);
    files_move(NV_KEY_DOWN);assert(selected==4 && !scroll);
    files_move(NV_KEY_DOWN);assert(selected==7 && scroll==3);
    files_move(NV_KEY_LEFT);assert(selected==6 && scroll==3);
    files_move(NV_KEY_PGDN);assert(selected==9 && scroll==6);
    windows[DESKTOP_FILES].w=340;scroll_to_selection();assert(selected==9 && scroll==8);
    file_view=1;scroll_to_selection();assert(selected==9 && scroll==5);
    count=0;files_move(NV_KEY_UP);assert(!selected && !scroll);
    settings_tab=SETTINGS_SYSTEM;account.info.role=0;
    assert(settings_activate(SETTINGS_ACTION+1)==-NV_EACCESS);
    settings_tab=SETTINGS_NETWORK;assert(settings_activate(SETTINGS_ACTION+1)==-NV_EACCESS);
    settings_tab=SETTINGS_SOUND;audio_ready=false;
    assert(settings_activate(SETTINGS_ACTION)==-NV_ENODEV);
}
int main(void) {
    search(); switching(); placement(); editor_navigation(); spaces_model();
    puts("PASS desktop shell: launcher/MRU/tiling/editor, Spaces filter/sort/grid, legacy/new preferences and settings roles");
    return 0;
}
