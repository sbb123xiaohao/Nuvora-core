/* Exercise the actual desktop rasterizer across display modes and tile sizes.
 * Catch clipping mistakes and out-of-bounds writes before running with GOP. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../user/desktop_ui.h"

static u32 *guarded(u32 count) {
    u32 *p = malloc(((usize)count + 2) * sizeof(u32));
    assert(p);
    p[0] = 0x9173ace4;
    p[count + 1] = 0x27607845;
    return p;
}

static void case_render(u32 width, u32 height, u32 format, u32 tile_rows, bool help) {
    struct nv_dirent files[24] = {0};
    for (u32 i = 0; i < ARRAY_LEN(files); ++i) {
        files[i].kind = i & 1 ? NV_FILE : NV_DIR;
        strlcpy(files[i].name, i & 1 ? "a long document name.nvd" :
                "very long folder name in a mounted drive", sizeof(files[i].name));
        files[i].size = i * 101;
    }
    strlcpy(files[17].name, "sample.wav", sizeof(files[17].name));
    assert(!strcmp(desktop_kind(&files[17]), "WAV audio"));
    strlcpy(files[18].name, "track.mp3", sizeof(files[18].name));
    files[18].kind = NV_FILE;
    strlcpy(files[19].name, "movie.mpg", sizeof(files[19].name));
    assert(!strcmp(desktop_kind(&files[18]), "MP3 audio"));
    assert(!strcmp(desktop_kind(&files[19]), "MPEG video"));
    struct desktop_view view = {
        "/drives/D/notes/reports/2026/a-very-long-path", "Select a file or press F1 for help",
        "4 drives", files, ARRAY_LEN(files), 16, 12, help, 4,
        true, width / 2, height / 2, true, false, 0};
    u32 n = width * height;
    u32 *full = guarded(n), *tiled = guarded(n);
    struct nv_canvas all = {full + 1, width, 0, height, format};
    desktop_render(&all, height, &view);
    for (u32 y = 0; y < height; y += tile_rows) {
        struct nv_canvas tile = {tiled + 1 + y * width, width, y,
                                 MIN(tile_rows, height - y), format};
        desktop_render(&tile, height, &view);
    }
    assert(!memcmp(full + 1, tiled + 1, (usize)n * sizeof(u32)));
    assert(full[0] == 0x9173ace4 && full[n + 1] == 0x27607845);
    assert(tiled[0] == 0x9173ace4 && tiled[n + 1] == 0x27607845);
    u32 scale = desktop_scale(width, height);
    u32 selected_y = 38 * scale + (52 + (16 - 12) * 14) * scale;
    u32 selected_x = (12 * 2 + 74) * scale + 7 * scale;
    if (!help) assert(full[1 + selected_y * width + selected_x] ==
                      nv_display_rgb(format, 0xd4e7ed));
    u32 drive_y = 38 * scale + (28 + 18) * scale - 4 * scale;
    assert(full[1 + drive_y * width + 17 * scale] ==
           nv_display_rgb(format, 0xcbdfe5));
    struct desktop_hit nav = desktop_hit(width, height, &view, 17 * scale, drive_y);
    struct desktop_hit row = desktop_hit(width, height, &view, selected_x, selected_y);
    assert(nav.kind == DESKTOP_HIT_PLACE && nav.index == 1);
    assert(row.kind == DESKTOP_HIT_FILE && row.index == 16);
    assert(desktop_hit(width, height, &view, 60 * scale, 9 * scale).kind == DESKTOP_HIT_MEDIA);
    assert(desktop_hit(width, height, &view, 25 * scale, height - 14 * scale).kind == DESKTOP_HIT_START);
    assert(full[1 + view.pointer_y * width + view.pointer_x] ==
           nv_display_rgb(format, 0x071724));
    free(full);
    free(tiled);
}

static void menu_render(void) {
    const u32 width = 640, height = 480, count = width * height;
    u32 *full = guarded(count), *tiled = guarded(count);
    struct desktop_view view = {"/home", "", "1 drive", NULL, 0, 0, 0, false,
                                1, false, 0, 0, true, true, 0};
    struct nv_canvas all = {full + 1, width, 0, height, NV_DISPLAY_BGRX8};
    desktop_render(&all, height, &view);
    for (u32 y = 0; y < height; y += 17) {
        struct nv_canvas tile = {tiled + 1 + y * width, width, y,
                                 MIN(17u, height - y), NV_DISPLAY_BGRX8};
        desktop_render(&tile, height, &view);
    }
    assert(!memcmp(full + 1, tiled + 1, count * sizeof(u32)));
    for (u32 i = 0; i < 4; ++i) {
        u32 y = height - 177 * 2 + (56 + 18 * i) * 2;
        struct desktop_hit hit = desktop_hit(width, height, &view, 90, y);
        assert(hit.kind == DESKTOP_HIT_MENU && hit.index == i);
    }
    assert(full[0] == 0x9173ace4 && full[count + 1] == 0x27607845);
    free(full); free(tiled);
}

int main(void) {
    assert(nv_display_rgb(NV_DISPLAY_BGRX8, 0x123456) == 0x123456);
    assert(nv_display_rgb(NV_DISPLAY_RGBX8, 0x123456) == 0x563412);
    for (u32 format = NV_DISPLAY_BGRX8; format <= NV_DISPLAY_RGBX8; ++format) {
        case_render(640, 480, format, 7, false);
        case_render(640, 480, format, 137, true);
        case_render(1280, 800, format, 137, false);
        case_render(2560, 720, format, 31, false);
        case_render(2560, 1440, format, 193, true);
    }
    menu_render();
    puts("PASS display: desktop tile/full-frame equality, clipping and drive selection at four resolutions");
    return 0;
}
