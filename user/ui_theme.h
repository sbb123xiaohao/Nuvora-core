#ifndef NV_UI_THEME_H
#define NV_UI_THEME_H
#include "gui_text.h"
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
    static const u32 base[]={0x101831,0x0c2136,0x21172e};
    static const u32 blue[]={0x286ab2,0x167a98,0x864c83};
    static const u32 mint[]={0x498b9f,0x2ca6a0,0xb37786};
    static const u32 violet[]={0x674f9f,0x294c8c,0x6c478e};
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
    u32 unit=MAX(1u,size/40),fg=0xf6faff;
    desktop_round(c,clip,x,y,size,size,size/4,colors[MIN(id,6u)]);
    if (id==0) {
        desktop_round(c,clip,x+12*unit,y+18*unit,17*unit,3*unit,unit,0xe2eeff);
        desktop_round(c,clip,x+18*unit,y+12*unit,3*unit,16*unit,unit,0xe2eeff);
        desktop_round(c,clip,x+7*unit,y+7*unit,12*unit,12*unit,4*unit,0xffffff);
        desktop_round(c,clip,x+23*unit,y+12*unit,10*unit,10*unit,4*unit,0xe2eeff);
        desktop_round(c,clip,x+13*unit,y+25*unit,10*unit,10*unit,4*unit,0xe2eeff);
    } else if (id==1 || id==4) {
        desktop_round(c,clip,x+10*unit,y+7*unit,20*unit,27*unit,3*unit,fg);
        for (u32 row=0;row<3;++row)
            desktop_round(c,clip,x+14*unit,y+(14+5*row)*unit,(row==2?8:12)*unit,2*unit,unit,0x72a1ae);
    } else if (id==2) {
        desktop_round(c,clip,x+6*unit,y+10*unit,28*unit,22*unit,3*unit,0x202d43);
        for (u32 row=0;row<7;++row) {
            u32 offset=row<4?row:6-row;
            desktop_round(c,clip,x+(11+offset)*unit,y+(16+row)*unit,2*unit,unit,unit,fg);
        }
        desktop_round(c,clip,x+21*unit,y+23*unit,7*unit,2*unit,unit,0xa9c6fc);
    } else if (id==3) {
        for (u32 row=0;row<18;++row) {
            u32 extent=row<9?row:17-row;
            desktop_round(c,clip,x+15*unit,y+(11+row)*unit,(extent+2)*unit,unit,unit,fg);
        }
    } else if (id==6) {
        desktop_round(c,clip,x+9*unit,y+9*unit,22*unit,22*unit,11*unit,fg);
        desktop_round(c,clip,x+15*unit,y+15*unit,10*unit,10*unit,5*unit,0x7b8aaf);
        desktop_round(c,clip,x+18*unit,y+5*unit,4*unit,8*unit,unit,fg);
        desktop_round(c,clip,x+18*unit,y+27*unit,4*unit,8*unit,unit,fg);
        desktop_round(c,clip,x+5*unit,y+18*unit,8*unit,4*unit,unit,fg);
        desktop_round(c,clip,x+27*unit,y+18*unit,8*unit,4*unit,unit,fg);
    } else {
        desktop_round(c,clip,x+14*unit,y+7*unit,12*unit,12*unit,6*unit,fg);
        desktop_round(c,clip,x+9*unit,y+22*unit,22*unit,11*unit,5*unit,fg);
    }
}
#endif
