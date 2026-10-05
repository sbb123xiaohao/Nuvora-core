/* Exercise shared native icons through their public drawing API. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../user/ui_controls.h"

enum { GUARD_WORDS=16 };
struct guarded_pixels { u32 *allocation,*pixels,count; };
static const u32 icons[]={
    UI_ICON_SEARCH,UI_ICON_HOME,UI_ICON_FOLDER,UI_ICON_DOCUMENT,UI_ICON_IMAGE,
    UI_ICON_VIDEO,UI_ICON_MUSIC,UI_ICON_DOWNLOAD,UI_ICON_APP,UI_ICON_STORAGE,
    UI_ICON_BACK,UI_ICON_PLUS,UI_ICON_CLOSE,UI_ICON_NETWORK,UI_ICON_SPEAKER,
    UI_ICON_SHIELD,UI_ICON_SETTINGS,UI_ICON_PLAY,UI_ICON_PAUSE,UI_ICON_PREVIOUS,
    UI_ICON_NEXT,UI_ICON_REPEAT,UI_ICON_SHUFFLE,UI_ICON_MORE,UI_ICON_CHECK
};
static const u32 colors[]={UI_BLUE,UI_TEXT,UI_MUTED};
static u32 pixel_word(u32 format,u32 rgb) {
    /* Expected ABI channel order, independent of the rendering helper. */
    return format==NV_DISPLAY_RGBX8?((rgb&255)<<16)|(rgb&0xff00)|(rgb>>16):rgb;
}
static u32 guard_word(u32 index) { return 0xa371c520u+index*0x10203u; }
static struct guarded_pixels allocate_pixels(u32 count,u32 background) {
    struct guarded_pixels p={.count=count};
    p.allocation=malloc(((usize)count+2*GUARD_WORDS)*sizeof(u32));
    assert(p.allocation);p.pixels=p.allocation+GUARD_WORDS;
    for (u32 i=0;i<GUARD_WORDS;++i) {
        p.allocation[i]=guard_word(i);
        p.pixels[count+i]=guard_word(GUARD_WORDS+i);
    }
    for (u32 i=0;i<count;++i) p.pixels[i]=background;
    return p;
}
static void check_guards(const struct guarded_pixels *p) {
    for (u32 i=0;i<GUARD_WORDS;++i) {
        assert(p->allocation[i]==guard_word(i));
        assert(p->pixels[p->count+i]==guard_word(GUARD_WORDS+i));
    }
}
static void release_pixels(struct guarded_pixels *p) {
    check_guards(p);free(p->allocation);
}
static void icon_scene(struct nv_canvas *c,struct desktop_clip clip,u32 scale) {
    for (u32 i=0;i<ARRAY_LEN(icons);++i)
        ui_line_icon(c,clip,(i32)((5+(i%5)*33)*scale),
            (i32)((7+(i/5)*34)*scale),24*scale,icons[i],colors[i%ARRAY_LEN(colors)]);
}
static void tiled_and_clipped(u32 format,u32 scale) {
    u32 width=177*scale,height=185*scale,count=width*height;
    u32 background=pixel_word(format,UI_BG);
    struct guarded_pixels full=allocate_pixels(count,background);
    struct guarded_pixels assembled=allocate_pixels(count,background);
    struct guarded_pixels clipped=allocate_pixels(count,background);
    struct desktop_clip all_clip={0,0,width,height};
    struct nv_canvas all={full.pixels,width,0,height,format};
    icon_scene(&all,all_clip,scale);check_guards(&full);
    /* Each tile has its own allocation and guards. A write into an adjacent
     * screen row therefore cannot hide in a larger framebuffer allocation. */
    static const u32 row_counts[]={1,7,19,4,31,2,13};
    for (u32 y=0,part=0;y<height;++part) {
        u32 rows=MIN(row_counts[part%ARRAY_LEN(row_counts)],height-y);
        struct guarded_pixels tile=allocate_pixels(width*rows,background);
        struct nv_canvas c={tile.pixels,width,y,rows,format};
        icon_scene(&c,all_clip,scale);check_guards(&tile);
        memcpy(assembled.pixels+(usize)y*width,tile.pixels,(usize)rows*width*sizeof(u32));
        release_pixels(&tile);y+=rows;
    }
    assert(!memcmp(full.pixels,assembled.pixels,(usize)count*sizeof(u32)));
    /* Damage rectangles partition the screen at positions unrelated to icon
     * cells and strokes, including one-pixel-wide and one-pixel-high strips. */
    static const u32 columns[]={1,13,29,41,3,17};
    struct nv_canvas damage={clipped.pixels,width,0,height,format};
    for (u32 y=0,part=0;y<height;++part) {
        u32 rows=MIN(row_counts[part%ARRAY_LEN(row_counts)],height-y);
        for (u32 x=0,column=0;x<width;++column) {
            u32 span=MIN(columns[column%ARRAY_LEN(columns)],width-x);
            struct desktop_clip clip={x,y,x+span,y+rows};
            icon_scene(&damage,clip,scale);x+=span;
        }
        check_guards(&clipped);y+=rows;
    }
    assert(!memcmp(full.pixels,clipped.pixels,(usize)count*sizeof(u32)));
    for (u32 i=0;i<count;++i) clipped.pixels[i]=background;
    struct desktop_clip crop={11*scale,13*scale,89*scale,61*scale};
    icon_scene(&damage,crop,scale);
    for (u32 y=0;y<height;++y) for (u32 x=0;x<width;++x) {
        bool inside=x>=crop.left && x<crop.right && y>=crop.top && y<crop.bottom;
        assert(clipped.pixels[(usize)y*width+x]==
            (inside?full.pixels[(usize)y*width+x]:background));
    }
    release_pixels(&full);release_pixels(&assembled);release_pixels(&clipped);
}
static void exact_colors(void) {
    assert(UI_BLUE==0x1761f5 && UI_BG==0xf2f4f8);
    assert(UI_TEXT==0x172133 && UI_MUTED==0x8993a4);
    assert(pixel_word(NV_DISPLAY_RGBX8,UI_BLUE)==0xf56117);
    assert(pixel_word(NV_DISPLAY_RGBX8,UI_BG)==0xf8f4f2);
    for (u32 id=0;id<ARRAY_LEN(icons);++id) for (u32 color=0;color<ARRAY_LEN(colors);++color) {
        struct guarded_pixels bgr=allocate_pixels(40*40,UI_BG);
        struct guarded_pixels rgb=allocate_pixels(40*40,pixel_word(NV_DISPLAY_RGBX8,UI_BG));
        struct nv_canvas a={bgr.pixels,40,0,40,NV_DISPLAY_BGRX8};
        struct nv_canvas b={rgb.pixels,40,0,40,NV_DISPLAY_RGBX8};
        struct desktop_clip clip={0,0,40,40};
        ui_line_icon(&a,clip,8,8,24,icons[id],colors[color]);
        ui_line_icon(&b,clip,8,8,24,icons[id],colors[color]);
        u32 ink=0,opaque=0;
        for (u32 y=0;y<40;++y) for (u32 x=0;x<40;++x) {
            u32 at=y*40+x;
            assert(rgb.pixels[at]==pixel_word(NV_DISPLAY_RGBX8,bgr.pixels[at]));
            if (bgr.pixels[at]!=UI_BG) {
                assert(x>=8 && x<32 && y>=8 && y<32);++ink;
                opaque+=bgr.pixels[at]==colors[color];
            }
        }
        assert(ink && opaque);release_pixels(&bgr);release_pixels(&rgb);
    }
}
static void off_edge(void) {
    static const u32 sizes[]={0,1,3,8,24,48,96};
    static const i32 positions[][2]={{-40,-31},{-5,13},{17,-7},{26,21},{47,40},{-200,7},{200,9}};
    static const struct desktop_clip clips[]={
        {0,0,31,29},{3,4,23,19},{0,11,31,12},{15,0,16,29},
        {4,4,4,20},{6,8,25,8},{50,50,90,90},{20,20,10,10}
    };
    for (u32 format=NV_DISPLAY_BGRX8;format<=NV_DISPLAY_RGBX8;++format)
        for (u32 id=0;id<ARRAY_LEN(icons);++id)
            for (u32 size=0;size<ARRAY_LEN(sizes);++size)
                for (u32 pos=0;pos<ARRAY_LEN(positions);++pos)
                    for (u32 clip=0;clip<ARRAY_LEN(clips);++clip) {
                        u32 background=pixel_word(format,UI_BG);
                        struct guarded_pixels first=allocate_pixels(31*29,background);
                        struct guarded_pixels again=allocate_pixels(31*29,background);
                        struct nv_canvas a={first.pixels,31,0,29,format};
                        struct nv_canvas b={again.pixels,31,0,29,format};
                        ui_line_icon(&a,clips[clip],positions[pos][0],positions[pos][1],
                            sizes[size],icons[id],UI_BLUE);
                        ui_line_icon(&b,clips[clip],positions[pos][0],positions[pos][1],
                            sizes[size],icons[id],UI_BLUE);
                        assert(!memcmp(first.pixels,again.pixels,(usize)first.count*sizeof(u32)));
                        for (u32 y=0;y<29;++y) for (u32 x=0;x<31;++x)
                            if (!sizes[size] || x<clips[clip].left || x>=clips[clip].right ||
                                y<clips[clip].top || y>=clips[clip].bottom)
                                assert(first.pixels[y*31+x]==background);
                        release_pixels(&first);release_pixels(&again);
                    }
}
int main(void) {
    exact_colors();off_edge();
    for (u32 format=NV_DISPLAY_BGRX8;format<=NV_DISPLAY_RGBX8;++format)
        for (u32 scale=1;scale<=4;++scale) tiled_and_clipped(format,scale);
    puts("PASS native controls: line icons, exact RGBX/BGRX colors, arbitrary tiles/clips and guarded edge bounds");
    return 0;
}
