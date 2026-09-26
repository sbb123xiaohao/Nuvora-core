#ifndef NV_MEDIA_UI_H
#define NV_MEDIA_UI_H
#include <nv/gfx.h>
#include <nv/string.h>
#include <stddef.h>
#define PLM_NO_STDIO
#include "../third_party/pl_mpeg.h"

struct media_view {
    const char *path, *message, *title;
    const struct nv_dirent *entries;
    u32 count, selected, scroll, seconds, peak_left, peak_right;
    bool playing, paused, video, audio_ready, pointer;
    u32 pointer_x, pointer_y;
    const plm_frame_t *frame;
};
enum { MEDIA_HIT_NONE, MEDIA_HIT_BACK, MEDIA_HIT_ITEM, MEDIA_HIT_PAUSE };
struct media_hit { u32 kind, index; };
static u32 media_scale(u32 width, u32 height) {
    return width >= 1440 && height >= 720 ? 3 : 2;
}
static u32 media_visible(u32 height, u32 s) {
    return height > 72 * s ? (height - 72 * s) / (16 * s) : 1;
}
static struct media_hit media_hit(u32 width, u32 height,
                                  const struct media_view *v, u32 x, u32 y) {
    u32 s = media_scale(width, height);
    if (x < 75 * s && y < 26 * s) return (struct media_hit){MEDIA_HIT_BACK, 0};
    if (v->playing) {
        if (y >= height - 31 * s) return (struct media_hit){MEDIA_HIT_PAUSE, 0};
        return (struct media_hit){MEDIA_HIT_NONE, 0};
    }
    u32 top = 54 * s, row = y >= top ? (y - top) / (16 * s) : ~0u;
    if (x >= 16 * s && x < width - 16 * s && row < media_visible(height, s) &&
        v->scroll + row < v->count)
        return (struct media_hit){MEDIA_HIT_ITEM, v->scroll + row};
    return (struct media_hit){MEDIA_HIT_NONE, 0};
}
static u32 media_rgb(i32 y, i32 cb, i32 cr) {
    i32 c = MAX(0, y - 16), d = cb - 128, e = cr - 128;
    i32 r = (298 * c + 409 * e + 128) >> 8;
    i32 g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    i32 b = (298 * c + 516 * d + 128) >> 8;
    return (u32)MIN(255, MAX(0, r)) << 16 |
           (u32)MIN(255, MAX(0, g)) << 8 |
           (u32)MIN(255, MAX(0, b));
}
static void media_picture(struct nv_canvas *c, u32 height, u32 s,
                          const plm_frame_t *frame) {
    if (!frame || !frame->width || !frame->height) return;
    u32 area_w = c->width - 24 * s, area_h = height - 92 * s;
    u32 w = MIN(area_w, (u32)((u64)area_h * frame->width / frame->height));
    u32 h = MIN(area_h, (u32)((u64)w * frame->height / frame->width));
    u32 left = (c->width - w) / 2, top = 46 * s + (area_h - h) / 2;
    if (!w || !h) return;
    for (u32 py = MAX(top, c->y0); py < MIN(top + h, c->y0 + c->rows); ++py) {
        u32 sy = (u32)((u64)(py - top) * frame->height / h);
        u32 *dst = c->pixels + (py - c->y0) * c->width + left;
        for (u32 px = 0; px < w; ++px) {
            u32 sx = (u32)((u64)px * frame->width / w);
            u32 chroma = (sy / 2) * frame->cb.width + sx / 2;
            u32 rgb = media_rgb(frame->y.data[sy * frame->y.width + sx],
                                frame->cb.data[chroma], frame->cr.data[chroma]);
            dst[px] = nv_display_rgb(c->format, rgb);
        }
    }
}
static void media_render(struct nv_canvas *c, u32 height, const struct media_view *v) {
    u32 s = media_scale(c->width, height);
    nv_gfx_fill(c, 0, 0, c->width, height, 0x19252b);
    nv_gfx_fill(c, 0, 0, 4 * s, height, 0xb9814c);
    nv_gfx_fill(c, 4 * s, 0, c->width - 4 * s, 26 * s, 0x25343c);
    nv_gfx_text(c, 16 * s, 9 * s, "< Library", 9, s, 0xdce6e7);
    nv_gfx_text(c, c->width - 48 * s, 9 * s, "MEDIA", 5, s, 0xb9814c);
    if (v->playing) {
        nv_gfx_label(c, 16 * s, 30 * s, v->title,
                     (c->width / s - 32) / 6, s, 0xf3f2ed);
        if (v->video) {
            nv_gfx_fill(c, 12 * s, 43 * s, c->width - 24 * s,
                        height - 87 * s, 0x0b1217);
            media_picture(c, height, s, v->frame);
        } else {
            nv_gfx_fill(c, 16 * s, 48 * s, c->width - 32 * s, height - 100 * s, 0x223039);
            u32 line_w = c->width - 56 * s, origin = 28 * s;
            u32 bars[] = {v->peak_left, v->peak_right};
            for (u32 i = 0; i < 2; ++i) {
                u32 y = height / 2 - 33 * s + i * 31 * s;
                nv_gfx_text(c, origin, y - 12 * s, i ? "RIGHT" : "LEFT",
                            i ? 5 : 4, s, 0xa6b7be);
                nv_gfx_fill(c, origin, y, line_w, 8 * s, 0x31424b);
                nv_gfx_fill(c, origin, y, (u32)((u64)line_w * MIN(bars[i], 32768u) / 32768u),
                            8 * s, 0xb9814c);
            }
        }
        nv_gfx_fill(c, 0, height - 36 * s, c->width, 36 * s, 0x25343c);
        nv_gfx_text(c, 16 * s, height - 25 * s,
                    v->paused ? "PLAY  Space" : "PAUSE Space",
                    v->paused ? 11 : 11, s, 0xf3f2ed);
        char clock[16], a[12], b[12];
        number(a, v->seconds / 60, 10); number(b, v->seconds % 60, 10);
        u32 n = 0;
        for (const char *p = a; *p && n < sizeof(clock) - 4; ++p) clock[n++] = *p;
        clock[n++] = ':';
        if (v->seconds % 60 < 10) clock[n++] = '0';
        for (const char *p = b; *p && n < sizeof(clock) - 1; ++p) clock[n++] = *p;
        clock[n] = 0;
        nv_gfx_text(c, c->width - 55 * s, height - 25 * s,
                    clock, n, s, 0xc4d2d4);
    } else {
        nv_gfx_label(c, 16 * s, 35 * s, v->path, (c->width / s - 32) / 6, s, 0xf3f2ed);
        u32 max = media_visible(height, s);
        for (u32 i = v->scroll; i < v->count && i - v->scroll < max; ++i) {
            u32 y = (54 + 16 * (i - v->scroll)) * s;
            if (i == v->selected) {
                nv_gfx_fill(c, 12 * s, y - 3 * s, c->width - 24 * s, 14 * s, 0x334953);
                nv_gfx_fill(c, 12 * s, y - 3 * s, 2 * s, 14 * s, 0xb9814c);
            }
            nv_gfx_text(c, 18 * s, y, v->entries[i].kind == NV_DIR ? "+" : ">",
                        1, s, 0xb9814c);
            nv_gfx_label(c, 31 * s, y, v->entries[i].name,
                         (c->width / s - 95) / 6, s, 0xe0e8e8);
            if (v->entries[i].kind == NV_FILE) {
                char size[16];
                number(size, v->entries[i].size / 1024, 10);
                u32 n = (u32)strlen(size);
                if (n + 2 < sizeof(size)) { size[n++] = 'K'; size[n] = 0; }
                nv_gfx_text(c, c->width - 55 * s, y, size, n, s, 0x9cacb4);
            }
        }
        if (!v->count) nv_gfx_text(c, 18 * s, 60 * s, "No media files here", 19, s, 0x9cacb4);
        nv_gfx_fill(c, 0, height - 23 * s, c->width, 23 * s, 0x25343c);
        nv_gfx_label(c, 16 * s, height - 16 * s,
                     *v->message ? v->message : "Enter Open    Backspace Parent    Esc Desktop",
                     (c->width / s - 32) / 6, s, *v->message ? 0xe5ad84 : 0xa6b7be);
    }
    if (v->pointer) {
        for (u32 i = 0; i < 9; ++i)
            nv_gfx_fill(c, v->pointer_x, v->pointer_y + i * s,
                        (i < 7 ? i + 1 : 9 - i) * s, s, 0xf3f2ed);
    }
}
#endif
