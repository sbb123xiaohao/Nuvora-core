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
    static const u32 base[]={0x101f29,0x152237,0x2a202a};
    static const u32 glow[]={0x28594e,0x304f72,0x70483f};
    static const u32 ribbon[]={0x80bfa9,0x8bb7de,0xdca58b};
    theme=MIN(theme,2u);
    for (u32 py=top;py<bottom;++py) {
        i32 ny=(i32)((u64)py*1024/height);
        u32 *out=c->pixels+(usize)(py-c->y0)*c->width;
        for (u32 px=left;px<right;++px) {
            i32 nx=(i32)((u64)px*1024/c->width);
            u32 rgb=desktop_blend(glow[theme],base[theme],desktop_glow(nx,ny,1020,760,1050));
            /* Tilted orbital bands use squared distances, without floating
             * point, bitmap assets or resolution-dependent seams. */
            i64 dx=nx-1000+(ny-600)/3,dy=ny-630;
            u64 orbit=(u64)(dx*dx+dy*dy*2);
            u32 fade=(u32)MAX(0,MIN(nx-370,255));
            if (orbit>105000 && orbit<230000) {
                u32 edge=(u32)MIN(orbit-105000,230000-orbit);
                rgb=desktop_blend(ribbon[theme],rgb,MIN(edge/400,105u)*fade/255);
            }
            if (orbit>265000 && orbit<345000) {
                u32 edge=(u32)MIN(orbit-265000,345000-orbit);
                rgb=desktop_blend(ribbon[theme],rgb,MIN(edge/500,54u)*fade/255);
            }
            if (orbit>70000 && orbit<540000 && orbit%42000<1200)
                rgb=desktop_blend(ribbon[theme],rgb,28*fade/255);
            if (dim) rgb=desktop_blend(0x08101c,rgb,90);
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
