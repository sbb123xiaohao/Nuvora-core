#ifndef NV_UI_THEME_H
#define NV_UI_THEME_H
#include "gui_text.h"
#include "ui_controls.h"
/* Wallpaper is generated at the display's native resolution. No bitmap
 * asset, floating point, GPU driver or network connection is required. */
static inline u32 desktop_glow(i32 x,i32 y,i32 cx,i32 cy,u32 radius) {
    i64 dx=x-cx,dy=y-cy;
    i64 strength=(i64)radius*radius-dx*dx-dy*dy;
    if (strength<=0) return 0;
    u32 a=(u32)(strength*255/((u64)radius*radius));
    return a*a/255;
}
static inline void desktop_wallpaper(struct nv_canvas *c,struct desktop_clip clip,
    u32 height,u32 theme,bool dim) {
    u32 left=MIN(clip.left,c->width),right=MIN(clip.right,c->width);
    u32 top=MAX(clip.top,c->y0),bottom=MIN(MIN(clip.bottom,height),c->y0+c->rows);
    static const u32 base[]={0xe8ebf1,0xe6edf1,0xede7ef};
    static const u32 blue[]={0xdbe7f7,0xcbdff1,0xdcd9f1};
    static const u32 mint[]={0xf8fbff,0xe1f3f3,0xf8edf1};
    static const u32 violet[]={0xebe5f7,0xdce6f6,0xe8daf0};
    theme=MIN(theme,2u);
    for (u32 py=top;py<bottom;++py) {
        i32 ny=(i32)((u64)py*1024/height);
        u32 *out=c->pixels+(usize)(py-c->y0)*c->width;
        for (u32 px=left;px<right;++px) {
            i32 nx=(i32)((u64)px*1024/c->width);
            u32 rgb=desktop_blend(blue[theme],base[theme],desktop_glow(nx,ny,270,730,830));
            rgb=desktop_blend(violet[theme],rgb,desktop_glow(nx,ny,1020,950,770));
            rgb=desktop_blend(mint[theme],rgb,desktop_glow(nx,ny,690,450,500)*3/4);
            if (dim) rgb=desktop_blend(0x0b1123,rgb,75);
            out[px]=nv_display_rgb(c->format,rgb);
        }
    }
}
static inline void desktop_icon(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,u32 size,u32 id) {
    static const u32 colors[]={0x568ced,0x72b5ad,0x52647f,0xba8aef,0x79a8c9,0x7b8aaf,0x7b8aaf};
    if (!size) return;
    u32 fg=0xf6faff,stroke=MAX(1u,size/20);
    desktop_round(c,clip,x,y,size,size,size/4,colors[MIN(id,6u)]);
    if (id==0) {
        /* Nuvora's connected spaces, with no opaque image margin or shadow. */
        ui_line(c,clip,x+size*3/10,y+size*3/10,x+size*7/10,y+size*4/10,stroke,fg);
        ui_line(c,clip,x+size*7/10,y+size*4/10,x+size*5/10,y+size*7/10,stroke,fg);
        desktop_round(c,clip,x+size/5,y+size/5,size/4,size/4,size/12,fg);
        desktop_round(c,clip,x+size*3/5,y+size*3/10,size/5,size/5,size/14,fg);
        desktop_round(c,clip,x+size*2/5,y+size*3/5,size/4,size/4,size/12,fg);
    } else if (id==2) {
        ui_line(c,clip,x+size/4,y+size*7/20,x+size*2/5,y+size/2,stroke,fg);
        ui_line(c,clip,x+size*2/5,y+size/2,x+size/4,y+size*13/20,stroke,fg);
        ui_line(c,clip,x+size/2,y+size*13/20,x+size*3/4,y+size*13/20,stroke,fg);
    } else {
        u32 icon=id==3?UI_ICON_MUSIC:id==5?UI_ICON_SHIELD:
                 id==6?UI_ICON_SETTINGS:UI_ICON_DOCUMENT;
        ui_line_icon(c,clip,x+size/5,y+size/5,size*3/5,icon,fg);
    }
}
#endif
