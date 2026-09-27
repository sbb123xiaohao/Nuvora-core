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
    u8 order[]={DESKTOP_TERMINAL,DESKTOP_EDITOR,DESKTOP_FILES};
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
    struct desktop_view v={.path="/home",.message="",.drive="4 drives",
        .entries=files,.count=24,.selected=4,.scroll=2,.volumes=4,
        .pointer=true,.pointer_x=width/2,.pointer_y=height/2,
        .audio_ready=true,.volume_percent=72,
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
        home_hit=desktop_hit(width,height,&v,45*s,(28+3*70+20)*s);
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
    struct desktop_hit hit=desktop_hit(width,height,&v,25*s,height-15*s);
    assert(hit.kind==DESKTOP_HIT_START);
    hit=desktop_hit(width,height,&v,100*s,height-15*s);
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
        hit=desktop_hit(width,height,&v,(windows[0].x+130)*s,
                        (windows[0].y+90+(4-2)*18)*s);
        assert(hit.kind==DESKTOP_HIT_FILE && hit.index==4);
        hit=desktop_hit(width,height,&v,(windows[0].x+20)*s,
                        (windows[0].y+88+22)*s);
        assert(hit.kind==DESKTOP_HIT_PLACE && hit.index==1);
    }
    const char *path=all_open?getenv("NV_WINDOWS_PREVIEW"):getenv("NV_FILES_PREVIEW");
    if (width==1280 && height==800 && format==NV_DISPLAY_BGRX8)
        preview(path,full+1,width,height);
    v.menu=true;
    hit=desktop_hit(width,height,&v,40*s,(sh-220+50+25*2+8)*s);
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
int main(void) {
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
