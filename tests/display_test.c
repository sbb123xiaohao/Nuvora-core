/* Rasterize the real compositor in tiles and check its controls and bounds. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../user/desktop_ui.h"

static u32 *guarded(u32 count) {
    u32 *p=malloc(((usize)count+2)*sizeof(u32));
    assert(p);
    p[0]=0x9173ace4; p[count+1]=0x27607845;
    return p;
}
static void preview(const char *name, const u32 *pixels, u32 width, u32 height) {
    if (!name) return;
    FILE *f=fopen(name,"wb"); assert(f);
    fprintf(f,"P6\n%u %u\n255\n",width,height);
    for (u32 i=0;i<width*height;++i) {
        u32 color=pixels[i];
        u8 rgb[]={(u8)(color>>16),(u8)(color>>8),(u8)color};
        assert(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);
}
static void case_render(u32 width,u32 height,u32 format,u32 tile_rows,bool all_open) {
    u32 s=desktop_scale(width,height),sw=width/s,sh=height/s;
    struct desktop_window windows[DESKTOP_WINDOW_COUNT]={
        {.x=125,.y=42,.w=MIN(520u,sw-145),.h=MIN(350u,sh-83),.open=true},
        {.x=140,.y=62,.w=MIN(485u,sw-155),.h=MIN(310u,sh-100),.open=all_open},
        {.x=150,.y=86,.w=MIN(475u,sw-160),.h=MIN(250u,sh-126),.open=all_open}};
    u8 order[DESKTOP_WINDOW_COUNT];
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) order[i]=(u8)i;
    order[0]=DESKTOP_TERMINAL; order[1]=DESKTOP_EDITOR; order[2]=DESKTOP_FILES;
    struct nv_dirent64 files[24]={0};
    for (u32 i=0;i<ARRAY_LEN(files);++i) {
        files[i].kind=i&1?NV_FILE:NV_DIR;
        files[i].size=i*101;
        strlcpy(files[i].name,i&1?"a long document name.nvd":"Folder with a long name",
                sizeof(files[i].name));
    }
    files[18].kind=NV_FILE; files[18].size=10ull*1024*1024*1024;
    strlcpy(files[18].name,"track.mp3",sizeof(files[18].name));
    files[19].size=NV_FILE_MAX64;
    strlcpy(files[19].name,"movie.mpg",sizeof(files[19].name));
    assert(!strcmp(desktop_kind(&files[18]),"MP3 audio"));
    assert(!strcmp(desktop_kind(&files[19]),"MPEG video"));
    char lines[64][128]={{0}};
    strlcpy(lines[0],"Nuvora terminal. Type help.",sizeof(lines[0]));
    strlcpy(lines[1],"C:/ :: ls",sizeof(lines[1]));
    strlcpy(lines[2],"[dir] home",sizeof(lines[2]));
    const char *sample="An editable text window\nSecond line with 0123456789\n";
    struct desktop_launcher launcher={0}; desktop_search(&launcher);
    struct desktop_view v={.path="/home",.message="",.drive="4 drives",
        .entries=files,.count=24,.selected=4,.scroll=4,.volumes=4,
        .pointer=true,.pointer_x=width/2,.pointer_y=height/2,
        .audio_ready=true,.volume_percent=72,
        .launcher=&launcher,
        .windows=windows,.order=order,.active=DESKTOP_FILES,
        .shortcut_selected=0xffffffffu,
        .editor_path="/home/note.txt",.editor_text=sample,.editor_length=strlen(sample),
        .editor_cursor=12,.editor_dirty=true,.editor_input="/home/note.txt",
        .terminal_lines=(const char (*)[128])lines,.terminal_count=3,
        .terminal_input="ping 10.0.2.2"};
    u32 n=width*height;
    u32 *full=guarded(n),*tiled=guarded(n);
    struct nv_canvas all={full+1,width,0,height,format};
    if (!all_open) {
        windows[DESKTOP_FILES].open=false;
        v.active=DESKTOP_WINDOW_COUNT;
        desktop_render(&all,height,&v);
        for (u32 y=0;y<height;y+=tile_rows) {
            struct nv_canvas tile={tiled+1+y*width,width,y,MIN(tile_rows,height-y),format};
            desktop_render(&tile,height,&v);
        }
        assert(!memcmp(full+1,tiled+1,(usize)n*sizeof(u32)));
        assert(full[0]==0x9173ace4 && full[n+1]==0x27607845);
        assert(tiled[0]==0x9173ace4 && tiled[n+1]==0x27607845);
        struct desktop_hit home_hit=desktop_hit(width,height,&v,45*s,48*s);
        assert(home_hit.kind==DESKTOP_HIT_SHORTCUT && home_hit.index==0);
        home_hit=desktop_hit(width,height,&v,45*s,(46+3*78+20)*s);
        assert(home_hit.kind==DESKTOP_HIT_SHORTCUT && home_hit.index==3);
        home_hit=desktop_hit(width,height,&v,(sw-40)*s,60*s);
        assert(home_hit.kind==DESKTOP_HIT_NONE);
        v.message="Media: audio device unavailable";
        desktop_render(&all,height,&v);
        assert(full[1+(sh-50)*s*width+17*s]==nv_display_rgb(format,0xb98866));
        v.message="";
        desktop_render(&all,height,&v);
        if (format==NV_DISPLAY_BGRX8) {
            const char *name=width==640 && height==480?"NV_HOME_640_PREVIEW":
                width==1024 && height==768?"NV_HOME_1024_PREVIEW":
                width==1280 && height==800?"NV_DESKTOP_PREVIEW":
                width==1920 && height==1080?"NV_HOME_1920_PREVIEW":NULL;
            if (name) preview(getenv(name),full+1,width,height);
        }
        if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8) {
            v.menu=true;
            desktop_render(&all,height,&v);
            preview(getenv("NV_HOME_START_PREVIEW"),full+1,width,height);
            v.menu=false;
        }
        windows[DESKTOP_FILES].open=true;
        v.active=DESKTOP_FILES;
        v.message="Ready";
    }
    desktop_render(&all,height,&v);
    for (u32 y=0;y<height;y+=tile_rows) {
        struct nv_canvas tile={tiled+1+y*width,width,y,MIN(tile_rows,height-y),format};
        desktop_render(&tile,height,&v);
    }
    assert(!memcmp(full+1,tiled+1,(usize)n*sizeof(u32)));
    assert(full[0]==0x9173ace4 && full[n+1]==0x27607845);
    assert(tiled[0]==0x9173ace4 && tiled[n+1]==0x27607845);
    u32 previous_x=v.pointer_x, previous_y=v.pointer_y;
    v.pointer_x+=27*s; v.pointer_y+=18*s;
    desktop_render(&all,height,&v);
    struct nv_canvas repair={tiled+1,width,0,height,format};
    struct desktop_clip old_cursor={previous_x,previous_y,previous_x+8*s,previous_y+10*s};
    struct desktop_clip new_cursor={v.pointer_x,v.pointer_y,v.pointer_x+8*s,v.pointer_y+10*s};
    desktop_render_clip(&repair,height,&v,old_cursor);
    desktop_render_clip(&repair,height,&v,new_cursor);
    assert(!memcmp(full+1,tiled+1,(usize)n*sizeof(u32)));
    v.volume_open=true; v.volume_percent=37;
    struct desktop_hit volume_hit=desktop_hit(width,height,&v,
        (sw-100)*s,(sh-62)*s);
    if (sw>=640) assert(volume_hit.kind==DESKTOP_HIT_VOLUME_SLIDER);
    if (width==1280 && height==800 && !all_open && format==NV_DISPLAY_BGRX8) {
        desktop_render(&all,height,&v);
        preview(getenv("NV_VOLUME_PREVIEW"),full+1,width,height);
    }
    v.volume_open=false;
    struct desktop_hit hit=desktop_hit(width,height,&v,(desktop_dock_x(sw,&v)+25)*s,height-15*s);
    assert(hit.kind==DESKTOP_HIT_START);
    hit=desktop_hit(width,height,&v,(desktop_dock_x(sw,&v)+125)*s,height-15*s);
    assert(hit.kind==DESKTOP_HIT_TASK && hit.index==DESKTOP_FILES);
    hit=desktop_hit(width,height,&v,(windows[0].x+windows[0].w-12)*s,
                    (windows[0].y+12)*s);
    assert(hit.kind==DESKTOP_HIT_CLOSE && hit.window==DESKTOP_FILES);
    hit=desktop_hit(width,height,&v,(windows[0].x+windows[0].w-43)*s,
                    (windows[0].y+12)*s);
    assert(hit.kind==DESKTOP_HIT_MAXIMIZE);
    hit=desktop_hit(width,height,&v,(windows[0].x+windows[0].w-130)*s,
                    (windows[0].y+43)*s);
    assert(hit.kind==DESKTOP_HIT_FILE_NEW_FOLDER);
    hit=desktop_hit(width,height,&v,(windows[0].x+windows[0].w-60)*s,
                    (windows[0].y+43)*s);
    assert(hit.kind==DESKTOP_HIT_FILE_NEW_TEXT);
    if (!all_open) {
        struct files_layout g=desktop_files_layout(&windows[0],v.file_view);
        hit=desktop_hit(width,height,&v,(windows[0].x+g.left+8)*s,
                        (windows[0].y+g.top+14)*s);
        assert(hit.kind==DESKTOP_HIT_FILE && hit.index==4);
        hit=desktop_hit(width,height,&v,(windows[0].x+20)*s,
                        (windows[0].y+103+36)*s);
        assert(hit.kind==DESKTOP_HIT_PLACE && hit.index==1);
    }
    const char *path=all_open?getenv("NV_WINDOWS_PREVIEW"):getenv("NV_FILES_PREVIEW");
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8)
        preview(path,full+1,width,height);
    v.menu=true;
    struct desktop_rect menu=desktop_menu_rect(sw,sh);
    hit=desktop_hit(width,height,&v,(menu.x+40)*s,(menu.y+96+34*2+8)*s);
    assert(hit.kind==DESKTOP_HIT_MENU && hit.index==2);
    desktop_render(&all,height,&v);
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8 && all_open)
        preview(getenv("NV_START_PREVIEW"),full+1,width,height);
    v.menu=false;
    v.file_mode=DESKTOP_FILE_DELETE;
    v.file_input="sample.txt"; v.file_target="sample.txt";
    hit=desktop_hit(width,height,&v,(windows[0].x+48)*s,
                    (windows[0].y+MAX(40u,windows[0].h/2-48)+76)*s);
    assert(hit.kind==DESKTOP_HIT_FILE_DIALOG && hit.index==1);
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8 && !all_open) {
        desktop_render(&all,height,&v);
        preview(getenv("NV_DELETE_PREVIEW"),full+1,width,height);
    }
    v.file_mode=DESKTOP_FILE_NORMAL;
    v.active=DESKTOP_EDITOR;
    windows[DESKTOP_EDITOR].open=true;
    windows[DESKTOP_TERMINAL].open=false;
    order[0]=DESKTOP_TERMINAL; order[1]=DESKTOP_FILES; order[2]=DESKTOP_EDITOR;
    v.editor_mode=DESKTOP_EDIT_CLOSE;
    hit=desktop_hit(width,height,&v,(windows[1].x+48)*s,
                    (windows[1].y+MAX(66u,windows[1].h/2-38)+53)*s);
    assert(hit.kind==DESKTOP_HIT_EDITOR_DIALOG && hit.index==1);
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8 && all_open) {
        desktop_render(&all,height,&v);
        preview(getenv("NV_EDITOR_PREVIEW"),full+1,width,height);
    }
    v.editor_mode=DESKTOP_EDIT_NORMAL;
    v.active=DESKTOP_TERMINAL;
    windows[DESKTOP_TERMINAL].open=true;
    order[0]=DESKTOP_EDITOR; order[1]=DESKTOP_FILES; order[2]=DESKTOP_TERMINAL;
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8 && all_open) {
        desktop_render(&all,height,&v);
        preview(getenv("NV_TERMINAL_PREVIEW"),full+1,width,height);
    }
    windows[DESKTOP_EDITOR].minimized=true;
    windows[DESKTOP_TERMINAL].minimized=true;
    hit=desktop_hit(width,height,&v,(windows[0].x+130)*s,
                    (windows[0].y+90)*s);
    assert(hit.window==DESKTOP_FILES);
    free(full); free(tiled);
}
static void native_render(u32 width,u32 height,u32 format) {
    u32 s=desktop_scale(width,height),sw=width/s,sh=height/s;
    struct desktop_window windows[DESKTOP_WINDOW_COUNT]={0};
    u8 order[DESKTOP_WINDOW_COUNT];
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) order[i]=(u8)i;
    u32 count=161*99;
    u32 *source=guarded(count);
    for (u32 i=0;i<count;++i) source[i+1]=0x402020u+i; /* Window ABI is BGRX. */
    windows[DESKTOP_MEDIA]=(struct desktop_window){.x=100,.y=44,.w=MIN(420u,sw-112),
        .h=MIN(250u,sh-86),.client=17,.open=true,.pixels=source+1,
        .pixel_width=161,.pixel_height=99};
    strlcpy(windows[DESKTOP_MEDIA].title,"Media - actual application pixels",64);
    struct desktop_view v={.path="/home",.message="",.drive="1 drive",.windows=windows,
        .order=order,.active=DESKTOP_MEDIA,.shortcut_selected=~0u,
        .pointer=true,.pointer_x=260*s,.pointer_y=150*s};
    u32 n=width*height;
    u32 *full=guarded(n),*tiled=guarded(n);
    struct nv_canvas c={full+1,width,0,height,format};
    desktop_render(&c,height,&v);
    u32 sx=20*161/((windows[DESKTOP_MEDIA].w-2)*s);
    u32 sy=20*99/((windows[DESKTOP_MEDIA].h-29)*s);
    assert(full[1+((44+28)*s+20)*width+101*s+20]==nv_display_rgb(format,source[1+sy*161+sx]));
    for (u32 y=0;y<height;y+=31) {
        struct nv_canvas part={tiled+1+(uptr)y*width,width,y,MIN(31u,height-y),format};
        desktop_render(&part,height,&v);
    }
    assert(!memcmp(full+1,tiled+1,(usize)n*4));
    struct desktop_hit hit=desktop_hit(width,height,&v,220*s,120*s);
    assert(hit.kind==DESKTOP_HIT_CLIENT && hit.window==DESKTOP_MEDIA);
    hit=desktop_hit(width,height,&v,101*s,140*s);
    assert(hit.kind==DESKTOP_HIT_RESIZE && hit.index==1);
    /* A clip which intersects only the frame must never underflow the pixel
     * copy width. Repairing the old/new pointer over app content is exact. */
    c.pixels=tiled+1;
    struct desktop_clip frame={100*s,44*s,101*s,300*s};
    desktop_render_clip(&c,height,&v,frame);
    u32 oldx=v.pointer_x,oldy=v.pointer_y;
    v.pointer_x=300*s;v.pointer_y=170*s;
    struct nv_canvas expected={full+1,width,0,height,format};
    desktop_render(&expected,height,&v);
    struct desktop_clip old={oldx,oldy,oldx+8*s,oldy+10*s};
    struct desktop_clip fresh={v.pointer_x,v.pointer_y,v.pointer_x+8*s,v.pointer_y+10*s};
    desktop_render_clip(&c,height,&v,old);desktop_render_clip(&c,height,&v,fresh);
    assert(!memcmp(full+1,tiled+1,(usize)n*4));
    struct nv_surface text={.cursor=166};
    for (u32 i=0;i<2000;++i) text.cells[i]=0x1700u|(u8)("0123456789"[i%10]);
    windows[DESKTOP_MEDIA].text=&text;
    desktop_render(&expected,height,&v);
    for (u32 y=0;y<height;y+=31) {
        struct nv_canvas part={tiled+1+(uptr)y*width,width,y,MIN(31u,height-y),format};
        desktop_render(&part,height,&v);
    }
    assert(!memcmp(full+1,tiled+1,(usize)n*4));
    for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) {
        windows[i]=windows[DESKTOP_MEDIA]; windows[i].client=i+1;
        strlcpy(windows[i].title,"Native window",64);
    }
    windows[4].minimized=true;
    v.overview=true; v.overview_selected=10; v.pointer=false;
    for (u32 page=0;page<2;++page) {
        if (page) v.overview_selected=0;
        desktop_render(&expected,height,&v);
        for (u32 y=0;y<height;y+=31) {
            struct nv_canvas part={tiled+1+(uptr)y*width,width,y,MIN(31u,height-y),format};
            desktop_render(&part,height,&v);
        }
        assert(!memcmp(full+1,tiled+1,(usize)n*4));
        u32 size=desktop_overview_page_size(sw),first=page?(DESKTOP_WINDOW_COUNT-1)/size*size:0;
        u32 visible=MIN(size,DESKTOP_WINDOW_COUNT-first);
        struct desktop_rect r=desktop_overview_rect(sw,sh,visible,0);
        hit=desktop_hit(width,height,&v,(r.x+12)*s,(r.y+12)*s);
        assert(hit.kind==DESKTOP_HIT_OVERVIEW && hit.index==DESKTOP_WINDOW_COUNT-1-first);
        hit=desktop_hit(width,height,&v,(sw-40)*s,40*s);
        assert(hit.kind==DESKTOP_HIT_NONE && hit.window==DESKTOP_WINDOW_COUNT);
    }
    v.overview=false;
    struct desktop_switcher switcher={0}; desktop_switch_begin(&switcher,windows,order,10,1);
    v.switcher=&switcher;
    desktop_render(&expected,height,&v);
    for (u32 y=0;y<height;y+=31) {
        struct nv_canvas part={tiled+1+(uptr)y*width,width,y,MIN(31u,height-y),format};
        desktop_render(&part,height,&v);
    }
    assert(!memcmp(full+1,tiled+1,(usize)n*4));
    hit=desktop_hit(width,height,&v,125*s,height-15*s);
    assert(hit.kind==DESKTOP_HIT_NONE && hit.window==DESKTOP_WINDOW_COUNT);
    switcher.open=false; v.snap_preview=DESKTOP_TILE_RIGHT;
    desktop_render(&expected,height,&v);
    assert(full[1+(sw/2)*s]==nv_display_rgb(format,0x91b2f4));
    assert(full[0]==0x9173ace4 && full[n+1]==0x27607845);
    assert(tiled[0]==0x9173ace4 && tiled[n+1]==0x27607845);
    assert(source[0]==0x9173ace4 && source[count+1]==0x27607845);
    free(source);free(full);free(tiled);
}
static void settings_render(u32 width,u32 height,u32 format) {
    u32 s=desktop_scale(width,height),sw=width/s,sh=height/s,n=width*height;
    struct desktop_window windows[DESKTOP_WINDOW_COUNT]={0};
    windows[DESKTOP_SETTINGS]=(struct desktop_window){.x=20,.y=20,.w=MIN(720u,sw-40),
        .h=MIN(480u,sh-76),.open=true};
    u8 order[DESKTOP_WINDOW_COUNT];for (u32 i=0;i<DESKTOP_WINDOW_COUNT;++i) order[i]=(u8)i;
    struct account_ui account={0};account.info.role=NV_ACCOUNT_ADMIN;
    struct nv_net_info network={.index=2,.state=NV_NET_ONLINE,.type=NV_NET_USB_BRIDGE,
        .ip=0x0a00020f,.mask=0xffffff00,.gateway=0x0a000202,.dns=0x0a000203};
    struct desktop_view v={.windows=windows,.order=order,.active=DESKTOP_SETTINGS,
        .path="/home",.drive="1 volume",.message="",.account=&account,
        .system_memory="256MiB memory",.system_free="190MiB free / 3 tasks",
        .audio_ready=true,.volume_percent=63,.idle_minutes=5,.network=&network,.network_present=true};
    u32 *full=guarded(n),*tiled=guarded(n);
    struct nv_canvas all={full+1,width,0,height,format};
    const struct desktop_window *w=&windows[DESKTOP_SETTINGS];
    for (u32 tab=0;tab<SETTINGS_TABS;++tab) {
        v.settings_tab=tab;
        for (u32 state=0;state<2;++state) {
            v.audio_ready=v.network_present=!state;account.info.role=state?0:NV_ACCOUNT_ADMIN;
            desktop_render(&all,height,&v);
            for (u32 y=0;y<height;y+=31) {
                struct nv_canvas part={tiled+1+(uptr)y*width,width,y,MIN(31u,height-y),format};
                desktop_render(&part,height,&v);
            }
            assert(!memcmp(full+1,tiled+1,(usize)n*4));
            assert(full[0]==0x9173ace4 && full[n+1]==0x27607845);
            assert(tiled[0]==0x9173ace4 && tiled[n+1]==0x27607845);
            for (u32 i=0;i<desktop_settings_choices(tab);++i) {
                struct desktop_rect r=desktop_settings_rect(w->w-2,w->h-29,tab,SETTINGS_ACTION+i);
                struct desktop_hit hit=desktop_hit(width,height,&v,(w->x+1+r.x+r.w/2)*s,
                    (w->y+28+r.y+r.h/2)*s);
                assert(hit.kind==DESKTOP_HIT_SETTINGS && hit.index==SETTINGS_ACTION+i);
            }
            if (tab==SETTINGS_SOUND && !state) for (u32 percent=0;percent<=100;percent+=50) {
                struct desktop_hit hit=desktop_hit(width,height,&v,
                    (w->x+155+(w->w-180)*percent/100)*s,(w->y+28+223)*s);
                assert(hit.kind==DESKTOP_HIT_SETTINGS_VOLUME && hit.index==percent);
            }
        }
    }
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8) {
        v.settings_tab=SETTINGS_APPEARANCE;
        desktop_render(&all,height,&v);preview(getenv("NV_SETTINGS_PREVIEW"),full+1,width,height);
    }
    free(full);free(tiled);
}

int main(void) {
    settings_render(640,480,NV_DISPLAY_BGRX8);
    settings_render(1280,800,NV_DISPLAY_BGRX8);
    settings_render(1920,1080,NV_DISPLAY_RGBX8);
    native_render(640,480,NV_DISPLAY_BGRX8);
    native_render(1024,768,NV_DISPLAY_BGRX8);
    native_render(1280,800,NV_DISPLAY_RGBX8);
    assert(desktop_scale(1600,900)==2);
    assert(desktop_scale(1920,1080)==2);
    assert(desktop_scale(2560,1000)==2);
    assert(desktop_scale(2560,1440)==2);
    assert(desktop_scale(2880,1800)==3);
    assert(desktop_scale(3840,2160)==4);
    assert(desktop_pointer_axis(50,100,100,false)==99);
    assert(desktop_pointer_axis(50,-100,100,false)==0);
    assert(desktop_pointer_axis(50,16384,1280,true)==639);
    assert(desktop_pointer_axis(50,32767,1280,true)==1279);
    char size[32]; desktop_size(size,10ull*1024*1024*1024);
    assert(!strcmp(size,"10GiB"));
    for (u32 format=NV_DISPLAY_BGRX8;format<=NV_DISPLAY_RGBX8;++format) {
        case_render(640,480,format,7,false);
        case_render(1024,768,format,41,false);
        case_render(1280,800,format,137,false);
        case_render(1280,800,format,137,true);
        case_render(1600,900,format,23,true);
        case_render(1920,1080,format,43,false);
        case_render(1920,1080,format,43,true);
        case_render(2560,720,format,31,true);
        case_render(2560,1000,format,47,true);
        case_render(2560,1440,format,193,true);
    }
    puts("PASS display: tiled compositor, windows, controls, menu and editor dialog");
}
