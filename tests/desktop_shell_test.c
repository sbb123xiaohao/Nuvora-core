#include <assert.h>
#include <stdio.h>
#include "../user/desktop_shell.h"

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
int main(void) {
    search(); switching(); placement();
    puts("PASS desktop shell: search, stable MRU, minimized/closed windows, tiling and preview bounds");
    return 0;
}
