#ifndef NV_GUI_TEXT_H
#define NV_GUI_TEXT_H
#include <nv/gfx.h>
#include <nv/string.h>
#include "desktop_font.h"
struct desktop_clip { u32 left, top, right, bottom; };
static inline u32 desktop_blend(u32 foreground,u32 background,u32 alpha) {
    u32 rest=255-alpha;
    u32 g=((((foreground>>8)&255)*alpha+((background>>8)&255)*rest+127)/255)<<8;
    /* Channels use exact /255 to preserve an opaque pixel in both formats. */
    u32 r=((((foreground>>16)&255)*alpha+((background>>16)&255)*rest+127)/255)<<16;
    u32 b=((foreground&255)*alpha+(background&255)*rest+127)/255;
    return r|g|b;
}
/* Signed bounds allow clipped shadows at the edge of the screen. The same
 * coverage calculation is used for full frames and arbitrary damage tiles. */
static inline u32 desktop_round_coverage(u32 px,u32 py,u32 w,u32 h,u32 radius) {
    if (px>=w || py>=h) return 0;
    radius=MIN(radius,MIN(w,h)/2);
    if (!radius) return 255;
    i64 cx=px<radius?2*(i64)px+1-2*radius:
        px>=w-radius?2*(i64)(px-(w-radius))+1:0;
    i64 cy=py<radius?2*(i64)py+1-2*radius:
        py>=h-radius?2*(i64)(py-(h-radius))+1:0;
    if (!cx || !cy) return 255;
    i64 coverage=(i64)radius*radius*4-cx*cx-cy*cy+2*radius;
    return coverage<=0?0:(u32)(MIN(coverage,(i64)4*radius)*255/(4*radius));
}
static inline void desktop_round_alpha(struct nv_canvas *c,struct desktop_clip clip,
    i32 x,i32 y,u32 w,u32 h,u32 radius,u32 rgb,u32 opacity) {
    if (!w || !h || !opacity) return;
    radius=MIN(radius,MIN(w,h)/2);
    i32 left=MAX(MAX(x,0),(i32)clip.left),right=MIN(MIN((i64)x+w,(i64)c->width),(i64)clip.right);
    i32 top=MAX(MAX(y,(i32)c->y0),(i32)clip.top);
    i32 bottom=MIN(MIN((i64)y+h,(i64)c->y0+c->rows),(i64)clip.bottom);
    u32 color=nv_display_rgb(c->format,rgb);
    for (i32 py=top;py<bottom;++py) {
        u32 *out=c->pixels+(usize)(py-(i32)c->y0)*c->width;
        for (i32 px=left;px<right;++px) {
            u32 alpha=desktop_round_coverage((u32)(px-x),(u32)(py-y),w,h,radius)*opacity/255;
            if (!alpha) continue;
            out[px]=alpha==255?color:desktop_blend(color,out[px],alpha);
        }
    }
}
static inline void desktop_round(struct nv_canvas *c,struct desktop_clip clip,
    i32 x,i32 y,u32 w,u32 h,u32 radius,u32 rgb) {
    desktop_round_alpha(c,clip,x,y,w,h,radius,rgb,255);
}
static inline void desktop_shadow(struct nv_canvas *c,struct desktop_clip clip,
    i32 x,i32 y,u32 w,u32 h,u32 radius,u32 scale) {
    if (!w || !h || !scale) return;
    radius=MIN(radius,MIN(w,h)/2);
    i32 left=MAX(MAX((i64)x-6*scale,0),(i64)clip.left);
    i32 right=MIN(MIN((i64)x+w+6*scale,(i64)c->width),(i64)clip.right);
    i32 top=MAX(MAX((i64)y-3*scale,(i64)c->y0),(i64)clip.top);
    i32 bottom=MIN(MIN((i64)y+h+9*scale,(i64)c->y0+c->rows),(i64)clip.bottom);
    u32 color=nv_display_rgb(c->format,0x52647f);
    for (i32 py=top;py<bottom;++py) {
        /* Visit exposed bands AND rounded cutouts; skip the covered core.
         * A rectangular exclusion left straight seams at every card corner. */
        bool within=py>=y && (i64)py<(i64)y+h;
        u32 inset=within && (py<y+(i64)radius || (i64)py>=(i64)y+h-radius)?radius:0;
        i32 core_left=within?MAX(left,MIN((i64)x+inset,(i64)right)):right;
        i32 core_right=within?MAX(left,MIN((i64)x+w-inset,(i64)right)):right;
        u32 *out=c->pixels+(usize)(py-(i32)c->y0)*c->width;
        for (i32 px=left;px<right;++px) {
            if (px>=core_left && px<core_right) { px=core_right-1;continue; }
            if (px>=x && py>=y && desktop_round_coverage(px-x,py-y,w,h,radius)==255) continue;
            u32 value=out[px];
            for (i32 i=6;i>=1;--i) {
                i64 dx=(i64)px-x+i*scale,dy=(i64)py-y-(3-i)*(i64)scale;
                if (dx<0 || dy<0) continue;
                u32 alpha=desktop_round_coverage((u32)dx,(u32)dy,w+2*i*scale,h+2*i*scale,
                    radius+i*scale)*6/255;
                if (alpha) value=desktop_blend(color,value,alpha);
            }
            out[px]=value;
        }
    }
}
static inline void desktop_box(struct nv_canvas *c, struct desktop_clip clip,
                        u32 x, u32 y, u32 w, u32 h, u32 rgb) {
    if (!w || !h || x >= clip.right || y >= clip.bottom) return;
    u32 left = MAX(x, clip.left), top = MAX(y, clip.top);
    u32 right = MIN(x + w, clip.right), bottom = MIN(y + h, clip.bottom);
    if (right > left && bottom > top) nv_gfx_fill(c, left, top, right - left, bottom - top, rgb);
}
struct desktop_face { const u8 *mask,*advances;const signed char *bearings;u32 stride,height; };
#define NV_FACE(kind,upper,scale) {&nv_font_##kind##_##scale[0][0],nv_font_##kind##_##scale##_advance,nv_font_##kind##_##scale##_bearing, \
    NV_FONT_##upper##_##scale##_WIDTH,NV_FONT_##upper##_##scale##_HEIGHT}
static inline const struct desktop_face *desktop_face(u32 kind,u32 scale) {
    static const struct desktop_face ui[]={NV_FACE(ui,UI,1),NV_FACE(ui,UI,2),NV_FACE(ui,UI,3),NV_FACE(ui,UI,4)};
    static const struct desktop_face mono[]={NV_FACE(mono,MONO,1),NV_FACE(mono,MONO,2),NV_FACE(mono,MONO,3),NV_FACE(mono,MONO,4)};
    static const struct desktop_face bold[]={NV_FACE(bold,BOLD,1),NV_FACE(bold,BOLD,2),NV_FACE(bold,BOLD,3),NV_FACE(bold,BOLD,4)};
    static const struct desktop_face editor[]={NV_FACE(editor,EDITOR,1),NV_FACE(editor,EDITOR,2),NV_FACE(editor,EDITOR,3),NV_FACE(editor,EDITOR,4)};
    u32 index=MAX(1u,MIN(scale,4u))-1;
    return kind==1?mono+index:kind==2?bold+index:kind==3?editor+index:ui+index;
}
#undef NV_FACE
static inline u32 desktop_glyph(u8 ch) { return (ch>=32 && ch<=126?ch:'?')-32u; }
static inline u32 desktop_text_width(const char *value,u32 count,u32 scale) {
    const struct desktop_face *f=desktop_face(0,scale);u32 width=0;
    for (u32 i=0;i<count && value[i];++i) width+=f->advances[desktop_glyph((u8)value[i])];
    return width;
}
static inline void desktop_text_render(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 count,u32 rgb,const struct desktop_face *f) {
    /* Most text is outside a small damage tile. Reject entire lines before
     * visiting glyphs; intersect scanlines and columns once per glyph. */
    u32 top=MAX(MAX(y,clip.top),c->y0),bottom=MIN(MIN(y+f->height,clip.bottom),c->y0+c->rows);
    if (top>=bottom || x>=clip.right) return;
    u32 color=nv_display_rgb(c->format,rgb),gx=x;
    for (u32 ch=0;ch<count && value[ch];++ch) {
        u32 glyph=desktop_glyph((u8)value[ch]);
        i64 ink=(i64)gx+f->bearings[glyph];
        if (ink>=clip.right || ink>=c->width) break;
        const u8 *mask=f->mask+(usize)glyph*f->stride*f->height;
        u32 left=(u32)MAX(ink,(i64)clip.left),right=(u32)MIN(MIN(ink+f->stride,(i64)clip.right),(i64)c->width);
        for (u32 py=top;py<bottom;++py) {
            const u8 *row=mask+(py-y)*f->stride;
            u32 *out=c->pixels+(py-c->y0)*c->width;
            for (u32 px=left;px<right;++px) {
                u32 alpha=row[(i64)px-ink];
                if (!alpha) continue;
                if (alpha==255) { out[px]=color;continue; }
                u32 background=out[px],rest=255-alpha;
                u32 b=((color&255)*alpha+(background&255)*rest+127)/255;
                u32 g=(((color>>8)&255)*alpha+((background>>8)&255)*rest+127)/255;
                u32 r=(((color>>16)&255)*alpha+((background>>16)&255)*rest+127)/255;
                out[px]=(r<<16)|(g<<8)|b;
            }
        }
        gx+=f->advances[glyph];
    }
}
/* Keep face selection outside the rasterizer so --gc-sections can discard
 * atlases a particular app never uses. Terminal carries only its mono face. */
static inline void desktop_text_kind(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 count,u32 scale,u32 rgb,u32 kind) {
    desktop_text_render(c,clip,x,y,value,count,rgb,desktop_face(kind,scale));
}
static inline void desktop_text(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 count,u32 scale,u32 rgb) {
    desktop_text_kind(c,clip,x,y,value,count,scale,rgb,0);
}
static inline void desktop_mono_text(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 count,u32 scale,u32 rgb) {
    desktop_text_kind(c,clip,x,y,value,count,scale,rgb,1);
}
static inline void desktop_bold_text(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 count,u32 scale,u32 rgb) {
    desktop_text_kind(c,clip,x,y,value,count,scale,rgb,2);
}
static inline void desktop_editor_text(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 count,u32 scale,u32 rgb) {
    desktop_text_kind(c,clip,x,y,value,count,scale,rgb,3);
}
static inline void desktop_label(struct nv_canvas *c, struct desktop_clip clip,
                          u32 x, u32 y, const char *value, u32 max, u32 s, u32 rgb) {
    u32 budget=max*6*s,n=(u32)strlen(value),width=desktop_text_width(value,n,s);
    clip.right=MIN(clip.right,x+budget);
    if (width<=budget) desktop_text(c,clip,x,y,value,n,s,rgb);
    else {
        u32 ellipsis=desktop_text_width("...",3,s),used=0,length=0;
        const struct desktop_face *f=desktop_face(0,s);
        while (length<n && used+f->advances[desktop_glyph((u8)value[length])]+ellipsis<=budget)
            used+=f->advances[desktop_glyph((u8)value[length++])];
        desktop_text(c,clip,x,y,value,length,s,rgb);
        desktop_text(c,clip,x+used,y,"...",3,s,rgb);
    }
}
static inline void desktop_mono_label(struct nv_canvas *c,struct desktop_clip clip,
    u32 x,u32 y,const char *value,u32 max,u32 s,u32 rgb) {
    u32 n=(u32)strnlen(value,max+1);
    if (n<=max) desktop_mono_text(c,clip,x,y,value,n,s,rgb);
    else if (max>=3) {
        desktop_mono_text(c,clip,x,y,value,max-3,s,rgb);
        desktop_mono_text(c,clip,x+(max-3)*6*s,y,"...",3,s,rgb);
    }
}
#endif
