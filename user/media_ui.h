#ifndef NV_MEDIA_UI_H
#define NV_MEDIA_UI_H
#include "gui_text.h"
#include "ui_controls.h"
#include <nv/string.h>
#include <stddef.h>
#define PLM_NO_STDIO
#include "../third_party/pl_mpeg.h"

/* Volume aliases follow the filesystem's C: through F: root semantics. */
static inline bool media_absolute_path(const char *path) {
    if (!path || !*path) return false;
    if (*path == '/') return true;
    char drive = path[0] >= 'a' && path[0] <= 'z' ? path[0] - 'a' + 'A' : path[0];
    return path[1] == ':' && drive >= 'C' && drive < 'C' + (int)NV_VOLUME_MAX;
}
static inline int media_resolve_file(char *out, u32 bytes, const char *cwd,
                                      const char *file) {
    if (!out || !bytes || !file || !*file) return -NV_EINVAL;
    u32 n = 0;
    if (media_absolute_path(file)) {
        if (*file != '/' && file[2] && file[2] != '/') return -NV_EINVAL;
    } else {
        if (!cwd || !*cwd) return -NV_EINVAL;
        n = (u32)strlcpy(out, cwd, bytes);
        if (n >= bytes) return -NV_E2BIG;
        if (out[n - 1] != '/') {
            if (n + 1 >= bytes) return -NV_E2BIG;
            out[n++] = '/'; out[n] = 0;
        }
    }
    return strlcpy(out + n, file, bytes - n) >= bytes - n ? -NV_E2BIG : 0;
}
static inline int media_parent_path(char *out, u32 bytes, const char *file) {
    if (!out || !bytes || !file || !*file) return -NV_EINVAL;
    if (strlcpy(out, file, bytes) >= bytes) return -NV_E2BIG;
    char *slash = NULL;
    for (char *p = out; *p; ++p) if (*p == '/') slash = p;
    if (!slash) return -NV_EINVAL;
    if (slash == out) slash[1] = 0;
    else if (slash == out + 2 && out[1] == ':' && media_absolute_path(out)) slash[1] = 0;
    else *slash = 0;
    return 0;
}

struct media_view {
    const char *path, *message, *title, *media_info;
    const struct nv_dirent64 *entries;
    u32 count, selected, scroll, current, previous, next;
    u32 seconds, duration, peak_left, peak_right, volume_percent;
    bool playing, paused, video, audio_ready, pointer, duration_known;
    u32 pointer_x, pointer_y;
    const plm_frame_t *frame;
};
enum { MEDIA_HIT_NONE, MEDIA_HIT_BACK, MEDIA_HIT_ITEM, MEDIA_HIT_PAUSE,
       MEDIA_HIT_VOLUME, MEDIA_HIT_PREVIOUS, MEDIA_HIT_NEXT };
struct media_hit { u32 kind, index; };
struct media_geometry {
    u32 s, text_s, body_x, body_w, art_x, art_y, art_size, title_y, title_s, info_y;
    u32 timeline_y, control_y, control_size, previous_x, next_x;
    u32 library_x, library_y, library_w, library_rows, row_h;
    u32 volume_x, volume_y, volume_w;
};
static u32 media_scale(u32 width, u32 height) {
    return width >= 780 && height >= 900 ? 3 :
           width >= 450 && height >= 580 ? 2 : 1;
}
static u32 media_visible(u32 height, u32 s) {
    return height > 104 * s ? MAX(1u, (height - 104 * s) / (22 * s)) : 1;
}
static struct media_geometry media_layout(u32 width, u32 height,
                                         const struct media_view *v) {
    struct media_geometry g = {0};
    g.s = media_scale(width, height);
    /* Controls stay comfortably sized; text follows its own hierarchy. */
    g.text_s = MAX(1u,g.s-1);
    u32 s = g.s, center = width / 2;
    g.body_w = MIN(width, 320 * s); g.body_x = (width - g.body_w) / 2;
    g.row_h = 22 * s;
    g.library_x = g.body_x + MIN(14 * s, g.body_w / 8);
    g.library_w = g.body_w - 2 * (g.library_x - g.body_x);
    g.library_rows = height >= 580 ? 3 : height >= 360 ? 2 : 1;
    if (v->playing) {
        u32 footer = g.library_rows * g.row_h + 16 * s;
        g.library_y = height > footer ? height - footer : height / 2;
    } else {
        g.library_y = 80 * s;
        if (g.library_y >= height) g.library_y = height / 3;
        g.library_rows = height > g.library_y + 24 * s ?
                         (height - g.library_y - 24 * s) / g.row_h : 1;
    }
    g.control_size = 32 * s;
    g.control_y = g.library_y > 46 * s ? g.library_y - 46 * s : 24 * s;
    g.previous_x = center > 57 * s ? center - 57 * s : 0;
    g.next_x = center + 35 * s;
    g.timeline_y = g.library_y > 56 * s ? g.library_y - 56 * s : 20 * s;
    g.title_s = g.text_s;
    u32 title_gap = 23 * s + desktop_face(2, g.title_s)->height;
    g.title_y = g.timeline_y > title_gap ? g.timeline_y - title_gap : 48 * s;
    g.info_y = g.timeline_y > 21 * s ? g.timeline_y - 21 * s : 64 * s;
    g.art_y = 36 * s;
    u32 art_bottom = g.title_y > 9 * s ? g.title_y - 9 * s : g.art_y;
    u32 art_width = g.body_w > 60 * s ? g.body_w - 60 * s : g.body_w / 2;
    g.art_size = MIN(150 * s, MIN(art_width, art_bottom > g.art_y ? art_bottom - g.art_y : 0));
    g.art_x = (width - g.art_size) / 2;
    g.art_y += art_bottom > g.art_y + g.art_size ? (art_bottom - g.art_y - g.art_size) / 2 : 0;
    g.volume_w = MIN(70 * s, g.body_w / 3);
    g.volume_x = center > g.volume_w / 2 ? center - g.volume_w / 2 : 0;
    g.volume_y = g.library_y > 7 * s ? g.library_y - 7 * s : 0;
    return g;
}
static u32 media_adjacent(const struct nv_dirent64 *entries, u32 count,
                          u32 current, bool forward) {
    if (!entries || current >= count) return ~0u;
    if (forward) {
        for (u32 i = current + 1; i < count; ++i)
            if (entries[i].kind == NV_FILE) return i;
    } else {
        for (u32 i = current; i; --i)
            if (entries[i - 1].kind == NV_FILE) return i - 1;
    }
    return ~0u;
}
static bool media_inside(u32 x, u32 y, u32 left, u32 top, u32 w, u32 h) {
    return x >= left && x - left < w && y >= top && y - top < h;
}
static struct media_hit media_hit(u32 width, u32 height,
                                  const struct media_view *v, u32 x, u32 y) {
    if (x >= width || y >= height) return (struct media_hit){MEDIA_HIT_NONE, 0};
    struct media_geometry g = media_layout(width, height, v);
    u32 s = g.s;
    if (media_inside(x, y, g.body_x + 13 * s, 10 * s, 24 * s, 24 * s))
        return (struct media_hit){MEDIA_HIT_BACK, 0};
    if (v->playing) {
        if (v->audio_ready && g.volume_w > 1 &&
            media_inside(x, y, g.volume_x, g.volume_y - 3 * s, g.volume_w, 9 * s))
            return (struct media_hit){MEDIA_HIT_VOLUME, MIN(100u, (x - g.volume_x) * 100 / (g.volume_w - 1))};
        if (media_inside(x, y, width / 2 - g.control_size / 2, g.control_y,
                         g.control_size, g.control_size))
            return (struct media_hit){MEDIA_HIT_PAUSE, 0};
        if (v->previous < v->count && v->previous != v->current &&
            media_inside(x, y, g.previous_x, g.control_y + 5 * s, 22 * s, 22 * s))
            return (struct media_hit){MEDIA_HIT_PREVIOUS, v->previous};
        if (v->next < v->count && v->next != v->current &&
            media_inside(x, y, g.next_x, g.control_y + 5 * s, 22 * s, 22 * s))
            return (struct media_hit){MEDIA_HIT_NEXT, v->next};
    }
    if (media_inside(x, y, g.library_x, g.library_y, g.library_w, g.library_rows * g.row_h)) {
        u32 index = v->scroll + (y - g.library_y) / g.row_h;
        if (index < v->count && (!v->playing || v->entries[index].kind == NV_FILE))
            return (struct media_hit){MEDIA_HIT_ITEM, index};
    }
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
static void media_picture(struct nv_canvas *c, u32 top, u32 bottom, u32 margin,
                          u32 radius, const plm_frame_t *frame) {
    if (c->width <= 2 * margin || bottom <= top) return;
    u32 area_w = c->width - 2 * margin, area_h = bottom - top;
    bool has_frame = frame && frame->width && frame->height;
    u32 w = has_frame ? MIN(area_w, (u32)((u64)area_h * frame->width / frame->height)) : 0;
    u32 h = w ? MIN(area_h, (u32)((u64)w * frame->height / frame->width)) : 0;
    u32 left = (c->width - w) / 2, picture_top = top + (area_h - h) / 2;
    u32 xwhole = w ? frame->width / w : 0, xpart = w ? frame->width % w : 0;
    u32 background = nv_display_rgb(c->format, 0x10213b);
    /* Compose picture and letterboxes through one mask, so fitted frames cannot
     * paint square corners or apply the edge coverage a second time. */
    for (u32 py = MAX(top, c->y0); py < MIN(bottom, c->y0 + c->rows); ++py) {
        bool picture_row = h && py >= picture_top && py - picture_top < h;
        u32 sy = picture_row ? (u32)((u64)(py - picture_top) * frame->height / h) : 0;
        u32 luma_row = picture_row ? sy * frame->y.width : 0;
        u32 chroma_row = picture_row ? (sy / 2) * frame->cb.width : 0;
        u32 *dst = c->pixels + (usize)(py - c->y0) * c->width;
        u32 sx = 0, remainder = 0;
        for (u32 px = margin; px < c->width - margin; ++px) {
            u32 alpha = desktop_round_coverage(px - margin, py - top, area_w, area_h, radius);
            u32 color = background;
            if (picture_row && px >= left && px - left < w) {
                if (alpha) {
                    u32 chroma = chroma_row + sx / 2;
                    color = nv_display_rgb(c->format, media_rgb(frame->y.data[luma_row + sx],
                                            frame->cb.data[chroma], frame->cr.data[chroma]));
                }
                sx += xwhole; remainder += xpart;
                if (remainder >= w) { ++sx; remainder -= w; }
            }
            if (alpha) dst[px] = alpha == 255 ? color : desktop_blend(color, dst[px], alpha);
        }
    }
}
/* An original procedural cover for audio with no embedded artwork support.
 * Position-based pixels keep the cover identical across arbitrary frame tiles. */
static void media_cover(struct nv_canvas *c, struct desktop_clip clip,
                         u32 x, u32 y, u32 side, u32 seed) {
    if (!side) return;
    u32 radius = MAX(1u, side / 16), top = MAX(MAX(y, c->y0), clip.top);
    u32 bottom = MIN(MIN(y + side, c->y0 + c->rows), clip.bottom);
    u32 left = MAX(x, clip.left), right = MIN(MIN(x + side, c->width), clip.right);
    for (u32 py = top; py < bottom; ++py) {
        u32 ly = py - y, ny = ly * 1024 / side;
        u32 *out = c->pixels + (usize)(py - c->y0) * c->width;
        for (u32 px = left; px < right; ++px) {
            u32 lx = px - x, nx = lx * 1024 / side;
            u32 alpha=desktop_round_coverage(lx,ly,side,side,radius);
            if (!alpha) continue;
            u32 rgb = ny < 760 ? desktop_blend(0x759ab9, 0x102e68, ny * 230 / 760) :
                      desktop_blend(0x122344, 0x254f77, (ny - 760) * 255 / 264);
            i32 mx = (i32)nx - 790, my = (i32)ny - 205;
            if (ny < 700) {
                u32 hx = nx / 7, hy = ny / 7;
                u32 star = hx * 374761393u + hy * 668265263u + seed * 1447u;
                star = (star ^ (star >> 13)) * 1274126177u;
                if ((star & 1023u) < 5 && nx % 7 < 2 && ny % 7 < 2)
                    rgb = desktop_blend(0xdceaff, rgb, 100 + (star >> 24) / 2);
                if (mx * mx + my * my < 31 * 31 && (mx - 15) * (mx - 15) + (my - 6) * (my - 6) > 29 * 29)
                    rgb = 0xe4edff;
            } else if (ny > 775 && ((nx + ny / 9 + seed) % 113 < 50) && ny % 23 < 2)
                rgb = desktop_blend(0x6486ab, rgb, 90);
            if (ny >= 746 && ny <= 760) rgb = 0x315780;
            u32 color=nv_display_rgb(c->format,rgb);
            out[px] = alpha==255?color:desktop_blend(color,out[px],alpha);
        }
    }
}
static void media_center_text(struct nv_canvas *c, struct desktop_clip clip,
                              u32 center, u32 y, const char *value, u32 max_w,
                              u32 s, u32 color, bool bold) {
    if (!value || !max_w) return;
    u32 n = (u32)strlen(value), w = desktop_text_width(value, n, s);
    const struct desktop_face *face = desktop_face(bold ? 2 : 0, s);
    if (bold) { w = 0; for (u32 i = 0; i < n; ++i) w += face->advances[desktop_glyph((u8)value[i])]; }
    u32 left = center > MIN(w, max_w) / 2 ? center - MIN(w, max_w) / 2 : 0;
    clip.right = MIN(clip.right, left + max_w);
    if (bold) desktop_bold_text(c, clip, left, y, value, n, s, color);
    else desktop_label(c, clip, left, y, value, max_w / (6 * s), s, color);
}
static void media_clock(char *out, u32 bytes, u32 seconds) {
    char minutes[12]; number(minutes, seconds / 60, 10);
    u32 n = (u32)strlcpy(out, minutes, bytes);
    if (n + 3 >= bytes) return;
    out[n++] = ':'; out[n++] = '0' + seconds % 60 / 10;
    out[n++] = '0' + seconds % 10; out[n] = 0;
}
static void media_button(struct nv_canvas *c, struct desktop_clip clip,
                          u32 x, u32 y, u32 size, u32 icon, u32 fg, u32 bg) {
    desktop_round_alpha(c, clip, x, y + 3, size, size, size / 2, 0x8196b9, 18);
    desktop_round(c, clip, x, y, size, size, size / 2, bg);
    if (icon == UI_ICON_PLAY) {
        u32 h = size / 2;
        for (u32 row = 0; row < h; ++row) {
            u32 extent = MIN(row + 1, h - row) * (size * 3 / 8) / MAX(1u, h / 2);
            desktop_box(c, clip, x + size * 3 / 8, y + size / 4 + row, extent, 1, fg);
        }
    } else if (icon == UI_ICON_PAUSE) {
        desktop_round(c, clip, x + size * 3 / 8, y + size / 3, size / 12, size / 3, size / 24, fg);
        desktop_round(c, clip, x + size * 9 / 16, y + size / 3, size / 12, size / 3, size / 24, fg);
    } else ui_line_icon(c, clip, x + size / 4, y + size / 4, size / 2, icon, fg);
}
static void media_render(struct nv_canvas *c, u32 height, const struct media_view *v) {
    if (!c->width || !height) return;
    struct desktop_clip clip = {0, c->y0, c->width, MIN(height, c->y0 + c->rows)};
    struct media_geometry g = media_layout(c->width, height, v);
    u32 s = g.s, text_s=g.text_s, center = c->width / 2;
    nv_gfx_fill(c, 0, 0, c->width, height, UI_BG);
    desktop_round(c, clip, g.body_x, 0, g.body_w, height, 16 * s, UI_BG);
    media_button(c, clip, g.body_x + 13 * s, 10 * s, 24 * s, UI_ICON_BACK, UI_TEXT, 0xffffff);
    media_center_text(c, clip, center, 15 * s, v->video && v->playing ? "Video" : "Music",
                      g.body_w / 2, text_s, UI_TEXT, true);
    if (v->playing) {
        if (v->video) {
            u32 bottom = g.title_y > 8 * s ? g.title_y - 8 * s : 48 * s;
            media_picture(c, 38 * s, bottom, 12 * s, 8 * s, v->frame);
        } else {
            if (v->previous < v->count && v->previous != v->current && g.art_size) {
                u32 peek = MIN(34 * s, g.body_w / 8);
                desktop_round(c, clip, g.body_x, g.art_y + 8 * s, peek,
                              g.art_size > 16 * s ? g.art_size - 16 * s : 0, 10 * s, 0x5085ff);
            }
            if (v->next < v->count && v->next != v->current && g.art_size) {
                u32 peek = MIN(34 * s, g.body_w / 8);
                desktop_round(c, clip, g.body_x + g.body_w - peek, g.art_y + 8 * s, peek,
                              g.art_size > 16 * s ? g.art_size - 16 * s : 0, 10 * s, 0xab96ed);
            }
            desktop_shadow(c, clip, g.art_x, g.art_y, g.art_size, g.art_size, MAX(1u,g.art_size/16),s);
            media_cover(c, clip, g.art_x, g.art_y, g.art_size, 17);
        }
        media_center_text(c, clip, center, g.title_y, v->title ? v->title : "Local media",
                          g.body_w > 28 * s ? g.body_w - 28 * s : g.body_w, g.title_s, UI_TEXT, true);
        media_center_text(c, clip, center, g.info_y,
                          v->media_info ? v->media_info : v->video ? "Local video" : "Local audio",
                          g.body_w > 28 * s ? g.body_w - 28 * s : g.body_w, text_s, UI_MUTED, false);
        char clock[20], total[20]; media_clock(clock, sizeof(clock), v->seconds);
        if (v->duration_known) media_clock(total, sizeof(total), v->duration);
        else strlcpy(total, "--:--", sizeof(total));
        u32 timeline_x = g.body_x + 46 * s, timeline_w = g.body_w > 92 * s ? g.body_w - 92 * s : 0;
        desktop_text(c, clip, g.body_x + 15 * s, g.timeline_y - 5 * text_s, clock, (u32)strlen(clock), text_s, UI_MUTED);
        u32 total_w = desktop_text_width(total, (u32)strlen(total), text_s);
        desktop_text(c, clip, g.body_x + g.body_w - 15 * s - MIN(total_w, g.body_w / 3),
                     g.timeline_y - 5 * text_s, total, (u32)strlen(total), text_s, UI_MUTED);
        desktop_round(c, clip, timeline_x, g.timeline_y, timeline_w, 2 * s, s, 0xdde3ed);
        if (v->duration_known && v->duration && timeline_w) {
            u32 filled = (u32)((u64)timeline_w * MIN(v->seconds, v->duration) / v->duration);
            desktop_round(c, clip, timeline_x, g.timeline_y, filled, 2 * s, s, UI_BLUE);
            desktop_round(c, clip, timeline_x + filled - MIN(filled, 2 * s), g.timeline_y - s,
                          4 * s, 4 * s, 2 * s, 0x93b6ff);
        }
        media_button(c, clip, center - g.control_size / 2, g.control_y, g.control_size,
                     v->paused ? UI_ICON_PLAY : UI_ICON_PAUSE, 0xffffff, UI_BLUE);
        media_button(c, clip, g.previous_x, g.control_y + 5 * s, 22 * s, UI_ICON_PREVIOUS,
                     v->previous < v->count && v->previous != v->current ? UI_TEXT : 0xc5cbd5, 0xffffff);
        media_button(c, clip, g.next_x, g.control_y + 5 * s, 22 * s, UI_ICON_NEXT,
                     v->next < v->count && v->next != v->current ? UI_TEXT : 0xc5cbd5, 0xffffff);
        if (v->audio_ready) {
            ui_line_icon(c, clip, g.volume_x - 16 * s, g.volume_y - 5 * s, 11 * s, UI_ICON_SPEAKER, UI_MUTED);
            desktop_round(c, clip, g.volume_x, g.volume_y, g.volume_w, 2 * s, s, 0xdde3ed);
            desktop_round(c, clip, g.volume_x, g.volume_y, g.volume_w * MIN(100u, v->volume_percent) / 100,
                          2 * s, s, UI_BLUE);
            char level[8]; number(level, MIN(100u, v->volume_percent), 10);
            u32 n = (u32)strlen(level); strlcpy(level + n, "%", sizeof(level) - n);
            desktop_text(c, clip, g.volume_x + g.volume_w + 7 * s, g.volume_y - 5 * text_s,
                         level, (u32)strlen(level), text_s, UI_MUTED);
        }
    } else {
        desktop_bold_text(c, clip, g.library_x + 3 * s, 44 * s, "Your library", 12, s, UI_TEXT);
        desktop_label(c, clip, g.library_x + 3 * s, 65 * s, v->path ? v->path : "/",
                      g.library_w / (6 * text_s), text_s, UI_MUTED);
    }
    for (u32 row = 0; row < g.library_rows && v->scroll + row < v->count; ++row) {
        u32 i = v->scroll + row, y = g.library_y + row * g.row_h;
        bool selected = i == (v->playing ? v->current : v->selected);
        desktop_round(c, clip, g.library_x, y + s, g.library_w, g.row_h - 2 * s,
                      5 * s, selected ? 0xedf3ff : 0xffffff);
        ui_line_icon(c, clip, g.library_x + 6 * s, y + 6 * s, 11 * s,
                     v->entries[i].kind == NV_DIR ? UI_ICON_FOLDER : UI_ICON_MUSIC,
                     selected ? UI_BLUE : UI_MUTED);
        const char *name=v->entries[i].name;u32 length=strlen(name);
        while (length && desktop_text_width(name,length,text_s)>
            (g.library_w>40*s?g.library_w-40*s:0)) --length;
        desktop_text(c, clip, g.library_x + 24 * s,
                      y+(g.row_h-desktop_face(0,text_s)->height)/2,name,length,
                      text_s, selected ? UI_BLUE : UI_TEXT);
        if (selected) ui_line_icon(c, clip, g.library_x + g.library_w - 14 * s,
                                  y + 8 * s, 8 * s, UI_ICON_CHECK, UI_BLUE);
    }
    if (!v->count && !v->playing)
        desktop_text(c, clip, g.library_x + 6 * s, g.library_y + 7 * s,
                     "No media files here", 19, text_s, UI_MUTED);
    const char *message = v->message && *v->message ? v->message :
                          v->playing ? "Space  Play / pause" : "Enter  Open     Backspace  Parent";
    desktop_label(c, clip, g.library_x + 3 * s, height > 11 * s ? height - 11 * s : 0,
                  message, g.library_w / (6 * text_s), text_s, UI_MUTED);
    if (v->pointer) {
        for (u32 i = 0; i < 9; ++i)
            nv_gfx_fill(c, v->pointer_x, v->pointer_y + i * s,
                        (i < 7 ? i + 1 : 9 - i) * s, s, UI_TEXT);
    }
}
#endif
