#ifndef NV_DESKTOP_UI_H
#define NV_DESKTOP_UI_H
#include <nv/gfx.h>
#include <nv/string.h>
struct desktop_view {
    const char *path, *message, *drive;
    const struct nv_dirent64 *entries;
    u32 count, selected, scroll;
    bool help;
    u32 volumes;
    bool pointer;
    u32 pointer_x, pointer_y;
    bool audio_ready;
    bool menu;
    u32 menu_selected;
};
static void desktop_size(char out[32], u64 size) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    u32 unit = 0; u64 divisor = 1;
    while (unit < 6 && size/divisor >= 1024) { divisor *= 1024; ++unit; }
    u64 whole = size/divisor;
    usize n = number64(out, whole, 10);
    if (unit && whole < 10 && size%divisor) {
        out[n++] = '.';
        out[n++] = (char)('0' + (size%divisor)*10/divisor);
        out[n] = 0;
    }
    strlcpy(out+n, units[unit], 32-n);
}
static const char *desktop_kind(const struct nv_dirent64 *entry) {
    if (entry->kind == NV_DIR) return "Folder";
    usize n = strlen(entry->name);
    if (n >= 5 && !strcmp(entry->name + n - 5, ".flac")) return "FLAC audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".mp2")) return "MP2 audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".wav")) return "WAV audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".mp3")) return "MP3 audio";
    if (n >= 4 && !strcmp(entry->name + n - 4, ".mpg")) return "MPEG video";
    if (n >= 5 && !strcmp(entry->name + n - 5, ".mpeg")) return "MPEG video";
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
enum { DESKTOP_HIT_NONE, DESKTOP_HIT_PLACE, DESKTOP_HIT_FILE,
       DESKTOP_HIT_START, DESKTOP_HIT_MENU, DESKTOP_HIT_MEDIA };
struct desktop_hit { u32 kind, index; };
static struct desktop_hit desktop_hit(u32 width, u32 height,
                                      const struct desktop_view *v, u32 x, u32 y) {
    u32 s = desktop_scale(width, height), margin = 12 * s, nav = 74 * s;
    u32 main_x = 2 * margin + nav, main_y = 38 * s;
    if (v->menu) {
        u32 menu_y = height - 177 * s;
        if (x >= margin && x < margin + 145 * s &&
            y >= menu_y && y < height - 23 * s) {
            if (x >= margin + 24 * s) {
                for (u32 i = 0; i < 4; ++i) {
                    u32 row = menu_y + (56 + 18 * i) * s;
                    if (y >= row - 4 * s && y < row + 13 * s)
                        return (struct desktop_hit){DESKTOP_HIT_MENU, i};
                }
            }
            return (struct desktop_hit){DESKTOP_HIT_NONE, 0};
        }
    }
    if (y >= height - 23 * s && x >= margin && x < margin + 54 * s)
        return (struct desktop_hit){DESKTOP_HIT_START, 0};
    if (y < 26 * s && x >= 56 * s && x < 115 * s)
        return (struct desktop_hit){DESKTOP_HIT_MEDIA, 0};
    if (v->menu) return (struct desktop_hit){DESKTOP_HIT_NONE, 0};
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
    nv_gfx_fill(c, 54 * s, 6 * s, s, 14 * s, 0xb6c0c6);
    nv_gfx_fill(c, 59 * s, 5 * s, 56 * s, 16 * s, 0x23343d);
    nv_gfx_text(c, 65 * s, 9 * s, "Media", 5, s, 0xf6f7f5);
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
    nv_gfx_text(c, main_x + main_w - 110 * s, main_y + 35 * s, "Size", 4, s, 0x53646d);
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
                     (main_w / s - 128) / 6, s, ink);
        if (v->entries[i].kind == NV_FILE) {
            char bytes[32];
            desktop_size(bytes, v->entries[i].size);
            nv_gfx_label(c, main_x + main_w - 110 * s, y, bytes, 7, s, 0x53646d);
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
    nv_gfx_fill(c, margin, height - 20 * s, 54 * s, 17 * s, 0x23343d);
    nv_gfx_fill(c, margin, height - 20 * s, 3 * s, 17 * s, 0xb9814c);
    nv_gfx_text(c, margin + 9 * s, height - 15 * s, "N  Start", 8, s, 0xf7f5f0);
    nv_gfx_text(c, margin + 62 * s, height - 16 * s,
                amount, (u32)strlen(amount), s, 0x53646d);
    if (*v->message) nv_gfx_label(c, margin + 112 * s, height - 16 * s, v->message,
                                   (c->width / s - 124) / 6, s, 0x87402f);
    if (v->help) {
        u32 x = main_x + 10 * s, y = main_y + 59 * s;
        nv_gfx_fill(c, x, y, main_w - 20 * s, 91 * s, 0xf4f7f8);
        nv_gfx_fill(c, x, y, main_w - 20 * s, s, 0x9fb3bc);
        const char *help[] = {"Keyboard", "Arrows / Enter  Select / open",
                              "Backspace       Parent folder", "F2 Folio   F3 Media",
                              "F5 Refresh   F6 Save", "F10 Start   Esc Exit",
                              "1-4 Drives   5-7 Locations", "F1 Close help"};
        for (u32 i = 0; i < ARRAY_LEN(help); ++i)
            nv_gfx_label(c, x + 8 * s, y + (8 + i * 11) * s, help[i],
                         (main_w / s - 36) / 6, s, 0x24333a);
    } else {
        const char *keys = main_w < 300 * s ? "Enter Open   F3 Media   F10 Start" :
                           "Enter Open   F1 Keys   F2 New   F3 Media   F10 Start   F5 Refresh";
        nv_gfx_label(c, main_x + 10 * s, main_y + main_h - 13 * s,
                     keys, (main_w / s - 20) / 6, s, 0x53646d);
    }
    if (v->menu) {
        u32 x = margin, y = height - 177 * s, w = 145 * s, h = 154 * s;
        nv_gfx_fill(c, x + 2 * s, y + 3 * s, w, h, 0x8b969a);
        nv_gfx_fill(c, x, y, w, h, 0xf5f4ef);
        nv_gfx_fill(c, x, y, 24 * s, h, 0x23343d);
        nv_gfx_fill(c, x, y, 24 * s, 3 * s, 0xb9814c);
        nv_gfx_text(c, x + 8 * s, y + 12 * s, "N", 1, s, 0xf5f4ef);
        nv_gfx_text(c, x + 33 * s, y + 11 * s, "Nuvora", 6, s, 0x23343d);
        nv_gfx_text(c, x + 33 * s, y + 34 * s, "APPLICATIONS", 12, s, 0x64757d);
        const char *apps[] = {"Media", "Files", "Folio", "Return to Loom"};
        for (u32 i = 0; i < ARRAY_LEN(apps); ++i) {
            u32 row = y + (56 + 18 * i) * s;
            if (i == v->menu_selected) {
                nv_gfx_fill(c, x + 27 * s, row - 4 * s, 115 * s, 16 * s, 0xe1e9e9);
                nv_gfx_fill(c, x + 27 * s, row - 4 * s, 2 * s, 16 * s, 0xb9814c);
            }
            nv_gfx_text(c, x + 34 * s, row, apps[i], (u32)strlen(apps[i]), s, 0x263a43);
        }
        nv_gfx_fill(c, x + 32 * s, y + 137 * s, 104 * s, s, 0xcbd2d2);
        nv_gfx_text(c, x + 33 * s, y + 142 * s, "C:  HOME", 8, s, 0x64757d);
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
