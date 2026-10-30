/* Exercise shared native icons through their public drawing API. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../user/ui_controls.h"

enum { GUARD_WORDS=16 };
struct guarded_pixels { u32 *allocation,*pixels,count; };
static const u32 icons[]={
    UI_ICON_TERMINAL,UI_ICON_SEARCH,UI_ICON_HOME,UI_ICON_FOLDER,UI_ICON_DOCUMENT,UI_ICON_IMAGE,
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
/* Compare the optimized exposed shadow against complete rounded layers.
 * Opaque cards hide the core; their cutouts must retain every shadow layer. */
static void card_scene(struct nv_canvas *c,struct desktop_clip clip,u32 scale,bool reference) {
    static const i32 rectangles[][5]={
        {-17,-9,67,51,14},{30,22,73,57,16},{102,71,58,43,12},{3,98,31,27,40}
    };
    for (u32 card=0;card<ARRAY_LEN(rectangles);++card) {
        i32 x=rectangles[card][0]*(i32)scale,y=rectangles[card][1]*(i32)scale;
        u32 w=rectangles[card][2]*scale,h=rectangles[card][3]*scale;
        u32 radius=MIN((u32)rectangles[card][4]*scale,MIN(w,h)/2);
        if (reference) for (i32 i=6;i>=1;--i)
            desktop_round_alpha(c,clip,x-i*(i32)scale,y+(3-i)*(i32)scale,
                w+2*i*scale,h+2*i*scale,radius+i*scale,0x52647f,6);
        else desktop_shadow(c,clip,x,y,w,h,radius,scale);
        desktop_round(c,clip,x,y,w,h,radius,card&1?UI_BLUE:UI_WHITE);
    }
}
static void rounded_shadows(u32 format,u32 scale) {
    u32 width=137*scale,height=119*scale,count=width*height;
    u32 background=pixel_word(format,0xadc5db);
    struct guarded_pixels actual=allocate_pixels(count,background);
    struct guarded_pixels reference=allocate_pixels(count,background);
    struct guarded_pixels tiled=allocate_pixels(count,background);
    struct guarded_pixels damage=allocate_pixels(count,background);
    struct nv_canvas a={actual.pixels,width,0,height,format};
    struct nv_canvas b={reference.pixels,width,0,height,format};
    struct nv_canvas d={damage.pixels,width,0,height,format};
    struct desktop_clip all={0,0,width,height};
    card_scene(&a,all,scale,false);card_scene(&b,all,scale,true);
    assert(!memcmp(actual.pixels,reference.pixels,(usize)count*sizeof(u32)));
    static const u32 spans[]={1,11,3,23,7};
    for (u32 y=0,part=0;y<height;++part) {
        u32 rows=MIN(spans[part%ARRAY_LEN(spans)],height-y);
        struct guarded_pixels tile=allocate_pixels(width*rows,background);
        struct nv_canvas c={tile.pixels,width,y,rows,format};
        card_scene(&c,all,scale,false);
        memcpy(tiled.pixels+(usize)y*width,tile.pixels,(usize)width*rows*sizeof(u32));
        release_pixels(&tile);
        for (u32 x=0,column=0;x<width;++column) {
            u32 columns=MIN(spans[column%ARRAY_LEN(spans)],width-x);
            struct desktop_clip crop={x,y,x+columns,y+rows};
            card_scene(&d,crop,scale,false);x+=columns;
        }
        y+=rows;
    }
    assert(!memcmp(actual.pixels,tiled.pixels,(usize)count*sizeof(u32)));
    assert(!memcmp(actual.pixels,damage.pixels,(usize)count*sizeof(u32)));
    release_pixels(&actual);release_pixels(&reference);
    release_pixels(&tiled);release_pixels(&damage);
}
/* Independent scalar oracle: visit every canvas pixel, derive circle coverage
 * directly, then blend each channel. Never call the optimized span/coverage or
 * blend helpers to form the expected image. Vary untouched high bytes too. */
static u32 oracle_round(u32 background,u32 format,u32 rgb,u32 opacity,
    u32 px,u32 py,u32 w,u32 h,u32 radius) {
    radius=MIN(radius,MIN(w,h)/2);
    u32 coverage=255;
    if (radius) {
        i64 dx=px<radius?(i64)2*px+1-2*radius:
            px>=w-radius?(i64)2*(px-(w-radius))+1:0;
        i64 dy=py<radius?(i64)2*py+1-2*radius:
            py>=h-radius?(i64)2*(py-(h-radius))+1:0;
        if (dx && dy) {
            i64 n=(i64)4*radius*radius-dx*dx-dy*dy+2*radius;
            coverage=n<=0?0:(u32)(MIN(n,(i64)4*radius)*255/(4*radius));
        }
    }
    u32 alpha=coverage*opacity/255;
    if (!alpha) return background;
    u32 color=pixel_word(format,rgb),out=0;
    for (u32 shift=0;shift<24;shift+=8)
        out|=((((color>>shift)&255)*alpha+((background>>shift)&255)*(255-alpha)+127)/255)<<shift;
    return out;
}
static u32 raster_random(u32 *state) { *state=*state*1664525u+1013904223u;return *state; }
static void rounded_spans(void) {
    const u32 radii[]={0,1,2,7,14,56,10000};
    const u32 opacities[]={0,1,6,127,220,254,255};
    u32 state=0x18d3529a;
    for (u32 sample=0;sample<8192;++sample) {
        u32 format=sample&1?NV_DISPLAY_RGBX8:NV_DISPLAY_BGRX8;
        u32 width=1+raster_random(&state)%73,y0=raster_random(&state)%37;
        u32 rows=1+raster_random(&state)%31;
        i32 x=(i32)(raster_random(&state)%131)-40;
        i32 y=(i32)(raster_random(&state)%131)-40;
        u32 w=raster_random(&state)%101,h=raster_random(&state)%101;
        u32 radius=radii[sample%ARRAY_LEN(radii)],opacity=opacities[(sample/7)%ARRAY_LEN(opacities)];
        struct desktop_clip clip={raster_random(&state)%100,raster_random(&state)%100,
            raster_random(&state)%100,raster_random(&state)%100};
        if (!(sample%4)) clip=(struct desktop_clip){0,0,width,y0+rows};
        struct guarded_pixels actual=allocate_pixels(width*rows,0);
        struct guarded_pixels expected=allocate_pixels(width*rows,0);
        for (u32 i=0;i<width*rows;++i) actual.pixels[i]=expected.pixels[i]=raster_random(&state);
        struct nv_canvas c={actual.pixels,width,y0,rows,format};
        u32 rgb=raster_random(&state)&0xffffff;
        desktop_round_alpha(&c,clip,x,y,w,h,radius,rgb,opacity);
        for (u32 row=0;row<rows;++row) for (u32 col=0;col<width;++col) {
            u32 py=y0+row,at=row*width+col;
            if (col<clip.left || col>=clip.right || py<clip.top || py>=clip.bottom ||
                (i64)col<x || (i64)py<y || (i64)col>=(i64)x+w || (i64)py>=(i64)y+h) continue;
            expected.pixels[at]=oracle_round(expected.pixels[at],format,rgb,opacity,
                (u32)((i64)col-x),(u32)((i64)py-y),w,h,radius);
        }
        assert(!memcmp(actual.pixels,expected.pixels,(usize)width*rows*sizeof(u32)));
        release_pixels(&actual);release_pixels(&expected);
    }
}
int main(void) {
    rounded_spans();
    exact_colors();off_edge();
    for (u32 format=NV_DISPLAY_BGRX8;format<=NV_DISPLAY_RGBX8;++format)
        for (u32 scale=1;scale<=4;++scale) {
            tiled_and_clipped(format,scale);rounded_shadows(format,scale);
        }
    puts("PASS native controls: 8192 independent rounded-raster oracle cases, line icons, rounded shadows vs complete layers, RGBX/BGRX, scales 1-4, arbitrary tiles/clips and guarded bounds");
    return 0;
}
