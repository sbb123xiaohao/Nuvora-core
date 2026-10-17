#ifndef NV_TEXT_WINDOW_H
#define NV_TEXT_WINDOW_H
#include "window_client.h"
#include "gui_text.h"
#include <nv/terminal.h>

static const u32 gui_text_palette[] = {0x15222b,0x244755,0x397266,0x397785,
    0x9b4b40,0x795477,0xa0784c,0xe1e6e4,0x738b96,0x4d94b0,
    0x7fb38e,0x84bbbf,0xdb8070,0xb493b8,0xe4c591,0xfaf9f5};
static inline void gui_cells_render(struct nv_canvas *c, struct desktop_clip clip,
    u32 x, u32 y, u32 width, u32 height, const u16 *cells, u32 columns, u32 rows,
    u32 cursor, u32 scale, u32 ox, u32 oy) {
    u32 cell_w=6*scale,cell_h=scale==1?13:12*scale;
    desktop_box(c,clip,x,y,width,height,gui_text_palette[0]);
    clip.left=MAX(clip.left,x); clip.top=MAX(clip.top,y);
    clip.right=MIN(clip.right,x+width); clip.bottom=MIN(clip.bottom,y+height);
    for (u32 row=0;row<rows;++row) {
        u32 py=oy+row*cell_h;
        if (py>=clip.bottom || py+cell_h<=clip.top ||
            py>=c->y0+c->rows || py+cell_h<=c->y0) continue;
        for (u32 col=0;col<columns;++col) {
            u16 cell=cells[row*columns+col]; u32 px=ox+col*cell_w;
            if (px>=clip.right || px+cell_w<=clip.left) continue;
            desktop_box(c,clip,px,py,cell_w,cell_h,gui_text_palette[(cell>>12)&15]);
            char ch[2]={(char)(cell&255),0};
            desktop_mono_text(c,clip,px,py,ch,1,scale,gui_text_palette[(cell>>8)&15]);
            if (cursor==row*columns+col)
                desktop_box(c,clip,px,py+cell_h-2*scale,cell_w,scale,gui_text_palette[7]);
        }
    }
}
/* Responsive Terminal grids start at a small, stable inset. Font size depends
 * on the display, not the current window, so maximize adds columns and rows. */
static inline void gui_terminal_render(struct nv_canvas *c, struct desktop_clip clip,
    u32 x, u32 y, u32 width, u32 height, const struct nv_terminal_view *text) {
    struct nv_terminal_layout l=nv_terminal_layout(width,height,text->scale);
    gui_cells_render(c,clip,x,y,width,height,text->cells,text->columns,text->rows,
                     text->cursor,l.scale,x+l.padding,y+l.padding);
}

/* Adapt the existing cell-based editor and shell to native pixel windows.
 * The 80x25 editing model remains unchanged; glyphs use the desktop AA font. */
static inline void gui_surface_render(struct nv_canvas *c, struct desktop_clip clip,
    u32 x, u32 y, u32 width, u32 height, const struct nv_surface *text) {
    u32 scale = MAX(1u, MIN(4u, MIN(width / (80 * 6), height / (25 * 12))));
    u32 cell_w = 6 * scale, cell_h = scale == 1 ? 13 : 12 * scale;
    u32 ox = x + (width > 80 * cell_w ? (width - 80 * cell_w) / 2 : 0);
    u32 oy = y + (height > 25 * cell_h ? (height - 25 * cell_h) / 2 : 0);
    gui_cells_render(c,clip,x,y,width,height,text->cells,80,25,text->cursor,scale,ox,oy);
}
struct text_window { struct app_window app; struct nv_display_info mode; u32 *tile, rows; };
static inline int text_window_open(struct text_window *w, const char *title) {
    int r = nv_display_info(&w->mode);
    if (r < 0) return r;
    r = app_window_open(&w->app, title, MIN(960u, w->mode.width - 40),
                         MIN(650u, w->mode.height - 100));
    if (r < 0) return r;
    w->tile = grow((NV_DISPLAY_MAX_COPY + NV_PAGE - 1) / NV_PAGE);
    if ((iptr)w->tile < 0) {
        nv_window_destroy(w->app.id); w->app.id = 0; return -NV_ENOMEM;
    }
    return 0;
}
static inline int text_window_show(struct text_window *w, const struct nv_surface *text) {
    int r = app_window_begin(&w->app);
    if (r == -NV_EAGAIN || w->app.closed) return 0;
    if (r < 0) return r;
    w->rows = NV_DISPLAY_MAX_COPY / (w->app.width * 4);
    for (u32 y = 0; y < w->app.height; y += w->rows) {
        struct nv_canvas c = {w->tile, w->app.width, y, MIN(w->rows, w->app.height-y), w->mode.format};
        struct desktop_clip clip = {0, y, w->app.width, y+c.rows};
        gui_surface_render(&c, clip, 0, 0, w->app.width, w->app.height, text);
        struct nv_display_present rect = {.y=y, .width=w->app.width, .height=c.rows, .stride=w->app.width*4, .pixels=(uptr)w->tile};
        r = app_window_present(&w->app, &rect);
        if (r == -NV_EAGAIN) return 0;
        if (r < 0) return r;
    }
    w->app.resized = false;
    r = nv_window_commit(w->app.id);
    return r == -NV_EAGAIN ? 0 : r;
}
#endif
