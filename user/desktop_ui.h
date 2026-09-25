#ifndef NV_DESKTOP_UI_H
#define NV_DESKTOP_UI_H
#include <nv/gfx.h>
#include <nv/string.h>
struct desktop_view {
    const char *path, *message, *drive;
    const struct nv_dirent *entries;
    u32 count, selected, scroll;
    bool help;
    u32 volumes;
    bool pointer;
    u32 pointer_x, pointer_y;
};
static u32 desktop_scale(u32 width, u32 height) {
    return width >= 2560 && height >= 1000 ? 4 :
           width >= 1440 && height >= 720 ? 3 : 2;
}
static u32 desktop_visible(u32 height, u32 scale) {
    return height > 128 * scale ? MAX(1u, (height - 128 * scale) / (14 * scale)) : 1;
}
enum { DESKTOP_HIT_NONE, DESKTOP_HIT_PLACE, DESKTOP_HIT_FILE };
struct desktop_hit { u32 kind, index; };
static struct desktop_hit desktop_hit(u32 width, u32 height,
                                      const struct desktop_view *v, u32 x, u32 y) {
    u32 s = desktop_scale(width, height), margin = 12 * s, nav = 74 * s;
    u32 main_x = 2 * margin + nav, main_y = 38 * s;
    if (x >= margin && x < margin + nav) {
        for (u32 i = 0; i < v->volumes + 3; ++i) {
            u32 top = main_y + (28 + i * 18) * s - 4 * s;
            if (y >= top && y < top + 16 * s)
                return (struct desktop_hit){DESKTOP_HIT_PLACE,
                    i < v->volumes ? i : NV_VOLUME_MAX + i - v->volumes};
        }
    }
    u32 first = main_y + 49 * s;
    if (x >= main_x + 4 * s && x < width - margin - 4 * s && y >= first) {
        u32 row = (y - first) / (14 * s), inside = (y - first) % (14 * s);
        u32 index = v->scroll + row;
        u32 drawn_y = main_y + (52 + row * 14) * s;
        u32 main_h = height - main_y - 26 * s;
        if (inside < 13 * s && row < desktop_visible(height, s) &&
            drawn_y + 10 * s <= main_y + main_h - 12 * s && index < v->count)
            return (struct desktop_hit){DESKTOP_HIT_FILE, index};
    }
    return (struct desktop_hit){DESKTOP_HIT_NONE, 0};
}
static void desktop_render(struct nv_canvas *c, u32 height, const struct desktop_view *v) {
    u32 s = desktop_scale(c->width, height), margin = 12 * s, nav = 74 * s;
    u32 main_x = margin * 2 + nav, main_w = c->width - main_x - margin;
    u32 main_y = 38 * s, main_h = height - main_y - 26 * s;
    nv_gfx_fill(c, 0, 0, c->width, height, 0x152434);
    nv_gfx_fill(c, 0, 0, c->width, 26 * s, 0x0b1b2a);
    nv_gfx_text(c, margin, 8 * s, "Nuvora", 6, s, 0xe7f2f7);
    nv_gfx_text(c, c->width - 83 * s, 8 * s, "Desktop 0.1", 11, s, 0xaac0d0);

    nv_gfx_fill(c, margin, main_y, nav, main_h, 0x1d3447);
    nv_gfx_text(c, margin + 8 * s, main_y + 10 * s, "PLACES", 6, s, 0xaec8d7);
    for (u32 i = 0; i < v->volumes && i < NV_VOLUME_MAX; ++i) {
        u32 y = main_y + (28 + i * 18) * s;
        char label[] = "1 C: Drive";
        label[0] += (char)i;
        label[2] += (char)i;
        if (!i) strlcpy(label + 5, "Home", sizeof(label) - 5);
        bool active = !i ? (!strncmp(v->path, "/home", 5) &&
                            (v->path[5] == '/' || !v->path[5])) :
                      (!strncmp(v->path, "/drives/", 8) &&
                       v->path[8] == (char)('C' + i) &&
                       (v->path[9] == '/' || !v->path[9]));
        if (active) nv_gfx_fill(c, margin + 4 * s, y - 4 * s,
                                nav - 8 * s, 16 * s, 0x276078);
        nv_gfx_text(c, margin + 8 * s, y, label, (u32)strlen(label), s, 0xe9f2f4);
    }
    const char *places[] = {"5 Root /", "6 Apps", "7 Temp"};
    for (u32 i = 0; i < ARRAY_LEN(places); ++i) {
        u32 y = main_y + (28 + v->volumes * 18 + i * 18) * s;
        bool active = i == 0 ? !strcmp(v->path, "/") :
                      i == 1 ? (!strncmp(v->path, "/apps", 5) &&
                                (v->path[5] == '/' || !v->path[5])) :
                               (!strncmp(v->path, "/tmp", 4) &&
                                (v->path[4] == '/' || !v->path[4]));
        if (active) nv_gfx_fill(c, margin + 4 * s, y - 4 * s,
                                nav - 8 * s, 16 * s, 0x276078);
        nv_gfx_text(c, margin + 8 * s, y, places[i], (u32)strlen(places[i]), s, 0xe9f2f4);
    }
    nv_gfx_text(c, margin + 8 * s, main_y + main_h - 23 * s,
                v->drive, (u32)strlen(v->drive), s, 0xaec8d7);

    nv_gfx_fill(c, main_x, main_y, main_w, main_h, 0xe9eff3);
    nv_gfx_fill(c, main_x, main_y, main_w, 28 * s, 0xd8e3ea);
    nv_gfx_text(c, main_x + 10 * s, main_y + 8 * s, "Files", 5, s, 0x193549);
    nv_gfx_label(c, main_x + 53 * s, main_y + 8 * s, v->path,
                 (main_w / s - 64) / 6, s, 0x375769);
    nv_gfx_text(c, main_x + 10 * s, main_y + 35 * s, "NAME", 4, s, 0x375769);
    nv_gfx_text(c, main_x + main_w - 76 * s, main_y + 35 * s, "TYPE", 4, s, 0x375769);
    u32 visible = desktop_visible(height, s);
    for (u32 i = v->scroll; i < v->count && i - v->scroll < visible; ++i) {
        u32 y = main_y + (52 + (i - v->scroll) * 14) * s;
        if (y + 10 * s > main_y + main_h - 12 * s) break;
        if (i == v->selected) nv_gfx_fill(c, main_x + 4 * s, y - 3 * s,
                                           main_w - 8 * s, 13 * s, 0x276078);
        u32 ink = i == v->selected ? 0xffffff : 0x17364a;
        if (i == v->selected) nv_gfx_text(c, main_x + 8 * s, y, ">", 1, s, ink);
        nv_gfx_label(c, main_x + 16 * s, y, v->entries[i].name,
                     (main_w / s - 111) / 6, s, ink);
        nv_gfx_text(c, main_x + main_w - 76 * s, y,
                    v->entries[i].kind == NV_DIR ? "Folder" : "File",
                    v->entries[i].kind == NV_DIR ? 6 : 4, s, ink);
    }
    if (!v->count) nv_gfx_text(c, main_x + 10 * s, main_y + 58 * s,
                               "This folder is empty", 20, s, 0x375769);
    nv_gfx_fill(c, 0, height - 23 * s, c->width, 23 * s, 0x0b1b2a);
    nv_gfx_label(c, margin, height - 16 * s, v->message,
                 (c->width / s - 20) / 6, s, 0xe7f2f7);
    if (v->help) {
        u32 x = main_x + 10 * s, y = main_y + 59 * s;
        nv_gfx_fill(c, x, y, main_w - 20 * s, 105 * s, 0x17364a);
        const char *help[] = {"Arrow / mouse Select", "Enter / double click Open",
                              "Backspace   Parent folder", "F1          Close help",
                              "F2          New document", "F5          Refresh",
                              "F6          Save disk", "1-4 Drives   5 Root",
                              "6 Apps  7 Temp  Esc Exit"};
        for (u32 i = 0; i < ARRAY_LEN(help); ++i)
            nv_gfx_label(c, x + 8 * s, y + (8 + i * 11) * s, help[i],
                         (main_w / s - 36) / 6, s, 0xe7f2f7);
    } else {
        const char *keys = main_w < 300 * s ? "Enter Open   F1 Help   Esc Exit" :
                           "Enter Open   F1 Help   F2 New   F5 Refresh   F6 Save   Esc Exit";
        nv_gfx_label(c, main_x + 10 * s, main_y + main_h - 13 * s,
                     keys, (main_w / s - 20) / 6, s, 0x375769);
    }
    if (v->pointer) {
        for (u32 i = 0; i < 9; ++i) {
            u32 width = (i < 7 ? i + 1 : i == 7 ? 4 : 3) * s;
            nv_gfx_fill(c, v->pointer_x, v->pointer_y + i * s,
                        width, s, 0x071724);
            if (i > 1 && i < 7)
                nv_gfx_fill(c, v->pointer_x + s, v->pointer_y + i * s,
                            (i - 1) * s, s, 0xffffff);
        }
    }
}
#endif
