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
    bool audio_ready;
};
static const char *desktop_kind(const struct nv_dirent *entry) {
    if (entry->kind == NV_DIR) return "Folder";
    usize n = strlen(entry->name);
    if (n >= 4 && !strcmp(entry->name + n - 4, ".wav")) return "WAV audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".txt")) return "Text";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".nvd")) return "Document";
    return "File";
}
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
    nv_gfx_fill(c, 0, 0, c->width, height, 0xdce2e6);
    nv_gfx_fill(c, 0, 0, c->width, 26 * s, 0xf8f9f9);
    nv_gfx_fill(c, 0, 26 * s - s, c->width, s, 0xb6c0c6);
    nv_gfx_text(c, margin, 8 * s, "Files", 5, s, 0x24333a);
    nv_gfx_text(c, c->width - 54 * s, 8 * s, "Nuvora", 6, s, 0x53646d);

    nv_gfx_fill(c, margin, main_y, nav, main_h, 0xe9edef);
    nv_gfx_text(c, margin + 8 * s, main_y + 10 * s, "LOCATIONS", 9, s, 0x56656d);
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
        if (active) {
            nv_gfx_fill(c, margin + 2 * s, y - 4 * s, nav - 4 * s, 16 * s, 0xcbdfe5);
            nv_gfx_fill(c, margin + 2 * s, y - 4 * s, 2 * s, 16 * s, 0x286984);
        }
        nv_gfx_text(c, margin + 8 * s, y, label, (u32)strlen(label), s, 0x263b45);
    }
    const char *places[] = {"5 Root /", "6 Apps", "7 Temp"};
    for (u32 i = 0; i < ARRAY_LEN(places); ++i) {
        u32 y = main_y + (28 + v->volumes * 18 + i * 18) * s;
        bool active = i == 0 ? !strcmp(v->path, "/") :
                      i == 1 ? (!strncmp(v->path, "/apps", 5) &&
                                (v->path[5] == '/' || !v->path[5])) :
                               (!strncmp(v->path, "/tmp", 4) &&
                                (v->path[4] == '/' || !v->path[4]));
        if (active) {
            nv_gfx_fill(c, margin + 2 * s, y - 4 * s, nav - 4 * s, 16 * s, 0xcbdfe5);
            nv_gfx_fill(c, margin + 2 * s, y - 4 * s, 2 * s, 16 * s, 0x286984);
        }
        nv_gfx_text(c, margin + 8 * s, y, places[i], (u32)strlen(places[i]), s, 0x263b45);
    }
    nv_gfx_text(c, margin + 8 * s, main_y + main_h - 34 * s,
                v->drive, (u32)strlen(v->drive), s, 0x53646d);
    const char *audio = v->audio_ready ? "HDA output" : "No audio";
    nv_gfx_label(c, margin + 8 * s, main_y + main_h - 21 * s,
                 audio, (nav / s - 12) / 6, s, 0x53646d);

    nv_gfx_fill(c, main_x, main_y, main_w, main_h, 0xffffff);
    nv_gfx_fill(c, main_x, main_y, main_w, 28 * s, 0xf3f5f6);
    nv_gfx_text(c, main_x + 10 * s, main_y + 8 * s, "Path", 4, s, 0x54636b);
    nv_gfx_label(c, main_x + 40 * s, main_y + 8 * s, v->path,
                 (main_w / s - 48) / 6, s, 0x24333a);
    nv_gfx_fill(c, main_x, main_y + 27 * s, main_w, s, 0xd6dde1);
    nv_gfx_text(c, main_x + 10 * s, main_y + 35 * s, "Name", 4, s, 0x53646d);
    nv_gfx_text(c, main_x + main_w - 98 * s, main_y + 35 * s, "Size", 4, s, 0x53646d);
    nv_gfx_text(c, main_x + main_w - 58 * s, main_y + 35 * s, "Type", 4, s, 0x53646d);
    nv_gfx_fill(c, main_x, main_y + 46 * s, main_w, s, 0xd6dde1);
    u32 visible = desktop_visible(height, s);
    for (u32 i = v->scroll; i < v->count && i - v->scroll < visible; ++i) {
        u32 y = main_y + (52 + (i - v->scroll) * 14) * s;
        if (y + 10 * s > main_y + main_h - 12 * s) break;
        if (i == v->selected) {
            nv_gfx_fill(c, main_x + 4 * s, y - 3 * s,
                        main_w - 8 * s, 13 * s, 0xd4e7ed);
            nv_gfx_fill(c, main_x + 4 * s, y - 3 * s, 2 * s, 13 * s, 0x286984);
        }
        u32 ink = 0x24333a;
        nv_gfx_label(c, main_x + 10 * s, y, v->entries[i].name,
                     (main_w / s - 116) / 6, s, ink);
        if (v->entries[i].kind == NV_FILE) {
            char bytes[16];
            number(bytes, v->entries[i].size, 10);
            nv_gfx_label(c, main_x + main_w - 98 * s, y, bytes, 7, s, 0x53646d);
        }
        const char *kind = desktop_kind(&v->entries[i]);
        nv_gfx_label(c, main_x + main_w - 58 * s, y, kind, 9, s, 0x53646d);
    }
    if (!v->count) nv_gfx_text(c, main_x + 10 * s, main_y + 58 * s,
                               "Empty folder", 12, s, 0x53646d);
    nv_gfx_fill(c, 0, height - 23 * s, c->width, 23 * s, 0xf8f9f9);
    nv_gfx_fill(c, 0, height - 23 * s, c->width, s, 0xb6c0c6);
    char amount[20];
    number(amount, v->count, 10);
    strlcpy(amount + strlen(amount), v->count == 1 ? " item" : " items",
            sizeof(amount) - strlen(amount));
    nv_gfx_text(c, margin, height - 16 * s, amount, (u32)strlen(amount), s, 0x53646d);
    if (*v->message) nv_gfx_label(c, margin + 72 * s, height - 16 * s, v->message,
                                   (c->width / s - 84) / 6, s, 0x87402f);
    if (v->help) {
        u32 x = main_x + 10 * s, y = main_y + 59 * s;
        nv_gfx_fill(c, x, y, main_w - 20 * s, 112 * s, 0xf4f7f8);
        nv_gfx_fill(c, x, y, main_w - 20 * s, s, 0x9fb3bc);
        const char *help[] = {"Keyboard", "Arrows       Select item",
                              "Enter        Open item", "Backspace    Parent folder",
                              "F2           New document", "F5           Refresh",
                              "F6           Save drives", "1-4          Drives",
                              "5-7          Root, apps, temp", "F1           Close help"};
        for (u32 i = 0; i < ARRAY_LEN(help); ++i)
            nv_gfx_label(c, x + 8 * s, y + (8 + i * 11) * s, help[i],
                         (main_w / s - 36) / 6, s, 0x24333a);
    } else {
        const char *keys = main_w < 300 * s ? "Enter Open   F1 Keys   Esc Exit" :
                           "Enter Open   F1 Keys   F2 New   F5 Refresh   F6 Save   Esc Exit";
        nv_gfx_label(c, main_x + 10 * s, main_y + main_h - 13 * s,
                     keys, (main_w / s - 20) / 6, s, 0x53646d);
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
