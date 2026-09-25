#ifndef NV_GFX_H
#define NV_GFX_H
#include <nv/sdk.h>
#include <nv/font5x7.h>
/* Tiny, optional software rasterizer. y0 is the first screen row held by the
 * caller's tile; callers may render full-screen coordinates into small tiles. */
struct nv_canvas {
    u32 *pixels;
    u32 width, y0, rows, format;
};
static inline void nv_gfx_fill(struct nv_canvas *c, u32 x, u32 y, u32 w, u32 h, u32 rgb) {
    if (!c->pixels || x >= c->width || y >= c->y0 + c->rows ||
        w == 0 || h == 0 || (u64)y + h <= c->y0) return;
    u32 left = x, right = x + MIN(w, c->width - x);
    u32 top = MAX(y, c->y0);
    u32 bottom = (u32)MIN((u64)y + h, (u64)c->y0 + c->rows);
    u32 color = nv_display_rgb(c->format, rgb);
    for (u32 py = top; py < bottom; ++py) {
        u32 *p = c->pixels + (py - c->y0) * c->width + left;
        for (u32 px = left; px < right; ++px) *p++ = color;
    }
}
static inline void nv_gfx_text(struct nv_canvas *c, u32 x, u32 y,
                               const char *s, u32 count, u32 scale, u32 rgb) {
    if (!scale) return;
    u32 color = nv_display_rgb(c->format, rgb);
    for (u32 ch = 0; ch < count && s[ch] && (u64)x + ch * 6u * scale < c->width; ++ch) {
        u8 glyph = (u8)s[ch];
        if (glyph < 32 || glyph > 126) glyph = '?';
        for (u32 row = 0; row < 7; ++row) {
            u8 bits = nv_font5x7[glyph - 32][row];
            for (u32 sy = 0; sy < scale; ++sy) {
                u64 py = (u64)y + row * scale + sy;
                if (py < c->y0 || py >= (u64)c->y0 + c->rows) continue;
                for (u32 col = 0; col < 5; ++col) {
                    if (!(bits & (16u >> col))) continue;
                    for (u32 sx = 0; sx < scale; ++sx) {
                        u64 px = (u64)x + ch * 6u * scale + col * scale + sx;
                        if (px < c->width) c->pixels[(py - c->y0) * c->width + px] = color;
                    }
                }
            }
        }
    }
}
static inline void nv_gfx_label(struct nv_canvas *c, u32 x, u32 y,
                                const char *s, u32 max_chars, u32 scale, u32 rgb) {
    u32 length = 0;
    while (s[length] && length < max_chars + 1) ++length;
    if (length <= max_chars) {
        nv_gfx_text(c, x, y, s, length, scale, rgb);
    } else if (max_chars >= 3) {
        nv_gfx_text(c, x, y, s, max_chars - 3, scale, rgb);
        nv_gfx_text(c, x + (max_chars - 3) * scale * 6, y, "...", 3, scale, rgb);
    }
}
#endif
