#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3.h"
#define PL_MPEG_IMPLEMENTATION
#include "../user/media_ui.h"

static u8 *load(const char *path, u32 *size) {
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long n = ftell(file);
    assert(n > 0 && n <= NV_FILE_MAX && !fseek(file, 0, SEEK_SET));
    u8 *data = malloc((usize)n);
    assert(data && fread(data, 1, (usize)n, file) == (usize)n);
    assert(!fclose(file));
    *size = (u32)n;
    return data;
}
static void mp3_fixture(const char *path) {
    u32 size;
    u8 *data = load(path, &size);
    mp3dec_t dec;
    mp3dec_init(&dec);
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    u32 offset = 0, samples = 0;
    u64 energy = 0;
    while (offset < size) {
        mp3dec_frame_info_t frame = {0};
        int n = mp3dec_decode_frame(&dec, data + offset, (int)(size - offset), pcm, &frame);
        if (frame.frame_bytes <= 0) break;
        assert((u32)frame.frame_bytes <= size - offset);
        if (n) {
            assert(frame.hz == 44100 && frame.channels == 2);
            for (int i = 0; i < n * 2; ++i)
                energy += pcm[i] < 0 ? (u32)-pcm[i] : (u32)pcm[i];
            samples += (u32)n;
        }
        offset += (u32)frame.frame_bytes;
    }
    assert(samples > 15000 && energy > 100000);
    free(data);
}
static void launch_paths(void) {
    static const struct { const char *cwd, *input, *file, *parent; } cases[] = {
        {"C:/users/tester", "C:/users/tester/Nightfall.wav", "C:/users/tester/Nightfall.wav", "C:/users/tester"},
        {"C:/users/tester", "D:/Music/Nightfall.wav", "D:/Music/Nightfall.wav", "D:/Music"},
        {"C:/users/tester", "e:/Nightfall.wav", "e:/Nightfall.wav", "e:/"},
        {"C:/users/tester", "F:/Nightfall.wav", "F:/Nightfall.wav", "F:/"},
        {"C:/users/tester", "/home/Nightfall.wav", "/home/Nightfall.wav", "/home"},
        {"C:/users/tester", "/Nightfall.wav", "/Nightfall.wav", "/"},
        {"C:/users/tester", "Nightfall.wav", "C:/users/tester/Nightfall.wav", "C:/users/tester"},
        {"C:/users/tester", "Music/Nightfall.wav", "C:/users/tester/Music/Nightfall.wav", "C:/users/tester/Music"},
        {"C:/users/tester", "../Music/Nightfall.wav", "C:/users/tester/../Music/Nightfall.wav", "C:/users/tester/../Music"},
        {"C:/", "Nightfall.wav", "C:/Nightfall.wav", "C:/"},
        {"/", "Nightfall.wav", "/Nightfall.wav", "/"},
        {"C:/users/tester/", "Nightfall.wav", "C:/users/tester/Nightfall.wav", "C:/users/tester"},
    };
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char full[NV_PATH_MAX], parent[NV_PATH_MAX];
        assert(!media_resolve_file(full, sizeof(full), cases[i].cwd, cases[i].input));
        assert(!strcmp(full, cases[i].file));
        assert(!media_parent_path(parent, sizeof(parent), full));
        assert(!strcmp(parent, cases[i].parent));
    }
    assert(media_absolute_path("C:/x.wav") && media_absolute_path("f:/x.wav"));
    assert(!media_absolute_path("B:/x.wav") && !media_absolute_path("G:/x.wav"));
    assert(!media_absolute_path("x") && !media_absolute_path("") && !media_absolute_path(NULL));
    char full[NV_PATH_MAX], small[10];
    assert(media_resolve_file(full, sizeof(full), "C:/users/tester", "C:Nightfall.wav") == -NV_EINVAL);
    assert(media_resolve_file(full, sizeof(full), "C:/users/tester", "d:Nightfall.wav") == -NV_EINVAL);
    assert(media_resolve_file(full, sizeof(full), "C:/users/tester", "") == -NV_EINVAL);
    assert(media_resolve_file(small, sizeof(small), "C:/users/tester", "Nightfall.wav") == -NV_E2BIG);
    assert(media_resolve_file(small, sizeof(small), "C:/", "Nightfall.wav") == -NV_E2BIG);
    assert(media_resolve_file(small, sizeof(small), "C:/users/tester", "D:/Nightfall.wav") == -NV_E2BIG);
    assert(media_parent_path(small, sizeof(small), "C:/users/tester/Nightfall.wav") == -NV_E2BIG);
}
enum { GUARD_WORDS = 16, TILE_ROWS = 31, LIBRARY_ITEMS = 40 };
static const u32 ui_sizes[][2] = {{620, 668}, {620, 760}, {640, 480}, {320, 240}};
static const u32 tiny_sizes[][2] = {{160, 120}, {80, 60}, {17, 19}, {1, 1}};
static struct nv_dirent64 library[LIBRARY_ITEMS];
static void prepare_library(void) {
    for (u32 i = 0; i < LIBRARY_ITEMS; ++i) {
        strlcpy(library[i].name, "A long local recording name.mp3", sizeof(library[i].name));
        library[i].kind = NV_FILE;
        library[i].size = 500000 + i * 4000;
    }
    library[0].kind = library[4].kind = library[7].kind = NV_DIR;
    strlcpy(library[0].name, "Albums", sizeof(library[0].name));
    strlcpy(library[4].name, "More albums", sizeof(library[4].name));
    assert(media_adjacent(library, LIBRARY_ITEMS, 5, false) == 3);
    assert(media_adjacent(library, LIBRARY_ITEMS, 6, true) == 8);
    assert(media_adjacent(library, LIBRARY_ITEMS, 1, false) == ~0u);
    assert(media_adjacent(library, LIBRARY_ITEMS, LIBRARY_ITEMS - 1, true) == ~0u);
    assert(media_adjacent(library, LIBRARY_ITEMS, ~0u, true) == ~0u);
    assert(media_adjacent(NULL, LIBRARY_ITEMS, 2, false) == ~0u);
}
static void guard_set(u32 *buffer, u32 count) {
    for (u32 i = 0; i < GUARD_WORDS; ++i) {
        buffer[i] = 0x12341234u ^ i;
        buffer[GUARD_WORDS + count + i] = 0x48793421u ^ i;
    }
    memset(buffer + GUARD_WORDS, 0xcd, (usize)count * sizeof(u32));
}
static void guard_check(const u32 *buffer, u32 count) {
    for (u32 i = 0; i < GUARD_WORDS; ++i) {
        assert(buffer[i] == (0x12341234u ^ i));
        assert(buffer[GUARD_WORDS + count + i] == (0x48793421u ^ i));
    }
}
static u32 swapped(u32 rgb) {
    return ((rgb & 255u) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 255u);
}
static u32 yuv_channel(double value) {
    return value < 0 ? 0 : value > 255 ? 255 : (u32)(value + 0.5);
}
static void check_picture(const u32 *pixels, u32 width, u32 height, u32 format,
                           const struct media_view *v) {
    struct media_geometry g = media_layout(width, height, v);
    const plm_frame_t *f = v->frame;
    u32 area_top = 38 * g.s, area_bottom = g.title_y - 8 * g.s;
    u32 available_h = area_bottom - area_top, available_w = width - 24 * g.s;
    u32 w = MIN(available_w, (u32)((u64)available_h * f->width / f->height));
    u32 h = MIN(available_h, (u32)((u64)w * f->height / f->width));
    assert(w && h);
    u32 left = (width - w) / 2, top = area_top + (available_h - h) / 2;
    u32 x = w / 3, y = h / 2;
    u32 sx = (u32)((u64)x * f->width / w), sy = (u32)((u64)y * f->height / h);
    double luma = MAX(0.0, (double)f->y.data[sy * f->y.width + sx] - 16);
    double cb = (double)f->cb.data[(sy / 2) * f->cb.width + sx / 2] - 128;
    double cr = (double)f->cr.data[(sy / 2) * f->cr.width + sx / 2] - 128;
    u32 expected = yuv_channel(1.16438356 * luma + 1.59602678 * cr) << 16 |
                   yuv_channel(1.16438356 * luma - 0.39176229 * cb - 0.81296765 * cr) << 8 |
                   yuv_channel(1.16438356 * luma + 2.01723214 * cb);
    u32 actual = pixels[(top + y) * width + left + x];
    if (format == NV_DISPLAY_RGBX8) actual = swapped(actual);
    for (u32 shift = 0; shift <= 16; shift += 8) {
        i32 difference = (i32)((actual >> shift) & 255u) - (i32)((expected >> shift) & 255u);
        assert(difference >= -2 && difference <= 2);
    }
    /* The fitted 4:3 fixture leaves side letterboxes in every size. */
    assert(left > 12 * g.s + 2);
    assert(pixels[(top + y) * width + left - 2] == nv_display_rgb(format, 0x10213b));
}
static void check_render(u32 width, u32 height, const struct media_view *source) {
    const u32 length = width * height;
    u32 *full = malloc((usize)(length + 2 * GUARD_WORDS) * sizeof(u32));
    u32 *tiled = malloc((usize)length * sizeof(u32));
    u32 *scratch = malloc((usize)(width * TILE_ROWS + 2 * GUARD_WORDS) * sizeof(u32));
    u32 *bgr = malloc((usize)length * sizeof(u32));
    assert(full && tiled && scratch && bgr);
    struct media_view v = *source;
    v.pointer = true; v.pointer_x = width > 1 ? width - 2 : 0;
    v.pointer_y = height > 1 ? height - 2 : 0;
    for (u32 format = NV_DISPLAY_BGRX8; format <= NV_DISPLAY_RGBX8; ++format) {
        guard_set(full, length);
        struct nv_canvas all = {full + GUARD_WORDS, width, 0, height, format};
        media_render(&all, height, &v);
        guard_check(full, length);
        for (u32 y = 0; y < height; y += TILE_ROWS) {
            u32 rows = MIN((u32)TILE_ROWS, height - y), count = width * rows;
            guard_set(scratch, count);
            struct nv_canvas tile = {scratch + GUARD_WORDS, width, y, rows, format};
            media_render(&tile, height, &v);
            guard_check(scratch, count);
            memcpy(tiled + (usize)y * width, scratch + GUARD_WORDS, (usize)count * sizeof(u32));
        }
        assert(!memcmp(full + GUARD_WORDS, tiled, (usize)length * sizeof(u32)));
        for (u32 i = 0; i < length; ++i) {
            assert(!(full[GUARD_WORDS + i] & 0xff000000u));
            if (format == NV_DISPLAY_BGRX8) bgr[i] = full[GUARD_WORDS + i];
            else assert(full[GUARD_WORDS + i] == swapped(bgr[i]));
        }
        if (v.video && v.frame && width >= 320 && height >= 240)
            check_picture(full + GUARD_WORDS, width, height, format, &v);
    }
    free(full); free(tiled); free(scratch); free(bgr);
}
static void hit_is(u32 width, u32 height, const struct media_view *v,
                    u32 x, u32 y, u32 kind, u32 index) {
    struct media_hit hit = media_hit(width, height, v, x, y);
    assert(hit.kind == kind);
    if (kind == MEDIA_HIT_ITEM || kind == MEDIA_HIT_VOLUME ||
        kind == MEDIA_HIT_PREVIOUS || kind == MEDIA_HIT_NEXT) assert(hit.index == index);
}
static void check_hits(u32 width, u32 height, const struct media_view *v) {
    struct media_geometry g = media_layout(width, height, v);
    assert(g.body_x + g.body_w <= width && g.art_x + g.art_size <= width);
    assert(g.art_y + g.art_size <= height && g.library_x + g.library_w <= width);
    assert(g.library_y + g.library_rows * g.row_h <= height);
    assert(g.library_rows && g.row_h && media_visible(height, g.s));
    if (!v->playing) assert(media_visible(height, g.s) == g.library_rows);
    hit_is(width, height, v, g.body_x + 25 * g.s, 22 * g.s, MEDIA_HIT_BACK, 0);
    hit_is(width, height, v, g.body_x + 13 * g.s, 10 * g.s, MEDIA_HIT_BACK, 0);
    hit_is(width, height, v, g.body_x + 37 * g.s - 1, 34 * g.s - 1, MEDIA_HIT_BACK, 0);
    hit_is(width, height, v, g.body_x + 37 * g.s, 10 * g.s, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, g.body_x + 13 * g.s, 34 * g.s, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, width, height / 2, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, width / 2, height, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, ~0u, ~0u, MEDIA_HIT_NONE, 0);
    for (u32 row = 0; row < g.library_rows; ++row) {
        u32 index = v->scroll + row;
        u32 kind = index < v->count && (!v->playing || v->entries[index].kind == NV_FILE) ?
                   MEDIA_HIT_ITEM : MEDIA_HIT_NONE;
        hit_is(width, height, v, g.library_x, g.library_y + row * g.row_h, kind, index);
        hit_is(width, height, v, g.library_x + g.library_w - 1,
               g.library_y + (row + 1) * g.row_h - 1, kind, index);
    }
    hit_is(width, height, v, g.library_x - 1, g.library_y, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, g.library_x + g.library_w, g.library_y, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, g.library_x, g.library_y + g.library_rows * g.row_h, MEDIA_HIT_NONE, 0);
    if (!v->playing) return;
    u32 pause_x = width / 2 - g.control_size / 2;
    hit_is(width, height, v, width / 2, g.control_y + g.control_size / 2, MEDIA_HIT_PAUSE, 0);
    hit_is(width, height, v, pause_x, g.control_y, MEDIA_HIT_PAUSE, 0);
    hit_is(width, height, v, pause_x + g.control_size - 1,
           g.control_y + g.control_size - 1, MEDIA_HIT_PAUSE, 0);
    hit_is(width, height, v, pause_x + g.control_size, g.control_y, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, pause_x, g.control_y + g.control_size, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, g.previous_x + 11 * g.s, g.control_y + 16 * g.s,
           MEDIA_HIT_PREVIOUS, v->previous);
    hit_is(width, height, v, g.next_x + 11 * g.s, g.control_y + 16 * g.s,
           MEDIA_HIT_NEXT, v->next);
    const u32 neighbor_x[] = {g.previous_x, g.next_x};
    const u32 neighbor_kind[] = {MEDIA_HIT_PREVIOUS, MEDIA_HIT_NEXT};
    const u32 neighbor_index[] = {v->previous, v->next};
    for (u32 i = 0; i < 2; ++i) {
        hit_is(width, height, v, neighbor_x[i], g.control_y + 5 * g.s,
               neighbor_kind[i], neighbor_index[i]);
        hit_is(width, height, v, neighbor_x[i] + 22 * g.s - 1, g.control_y + 27 * g.s - 1,
               neighbor_kind[i], neighbor_index[i]);
        hit_is(width, height, v, neighbor_x[i] + 22 * g.s, g.control_y + 5 * g.s, MEDIA_HIT_NONE, 0);
        hit_is(width, height, v, neighbor_x[i], g.control_y + 27 * g.s, MEDIA_HIT_NONE, 0);
    }
    assert(g.volume_w > 1);
    for (u32 x = 0; x < g.volume_w; ++x)
        hit_is(width, height, v, g.volume_x + x, g.volume_y, MEDIA_HIT_VOLUME,
               x * 100 / (g.volume_w - 1));
    hit_is(width, height, v, g.volume_x, g.volume_y - 3 * g.s, MEDIA_HIT_VOLUME, 0);
    hit_is(width, height, v, g.volume_x, g.volume_y + 6 * g.s, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, g.volume_x - 1, g.volume_y, MEDIA_HIT_NONE, 0);
    hit_is(width, height, v, g.volume_x + g.volume_w, g.volume_y, MEDIA_HIT_NONE, 0);
    struct media_view disabled = *v;
    disabled.audio_ready = false; disabled.previous = disabled.next = ~0u;
    hit_is(width, height, &disabled, g.volume_x, g.volume_y, MEDIA_HIT_NONE, 0);
    hit_is(width, height, &disabled, g.previous_x + 11 * g.s, g.control_y + 16 * g.s, MEDIA_HIT_NONE, 0);
    hit_is(width, height, &disabled, g.next_x + 11 * g.s, g.control_y + 16 * g.s, MEDIA_HIT_NONE, 0);
    disabled.previous = disabled.next = disabled.current;
    hit_is(width, height, &disabled, g.previous_x + 11 * g.s, g.control_y + 16 * g.s, MEDIA_HIT_NONE, 0);
    hit_is(width, height, &disabled, g.next_x + 11 * g.s, g.control_y + 16 * g.s, MEDIA_HIT_NONE, 0);
}
static struct media_view audio_view(void) {
    return (struct media_view){
        .path = "/Media/Albums", .message = "", .title = "A long local recording title",
        .media_info = "MP3 / 44.1 kHz / stereo", .entries = library, .count = LIBRARY_ITEMS,
        .selected = 3, .scroll = 2, .current = 3, .previous = 2, .next = 5,
        .seconds = 79, .duration = 217, .duration_known = true, .playing = true,
        .paused = true, .audio_ready = true, .volume_percent = 72,
        .peak_left = 12345, .peak_right = 28000};
}
static void video_preview_corners(void) {
    static const u32 sizes[][2] = {{780, 900}, {620, 668}, {640, 480}, {320, 240}, {80, 240}};
    for (u32 size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
        u32 width = sizes[size][0], height = sizes[size][1], count = width * height;
        struct media_view v = audio_view(); v.video = true; v.pointer = false;
        struct media_geometry g = media_layout(width, height, &v);
        u32 top = 38 * g.s, bottom = g.title_y - 8 * g.s, margin = 12 * g.s;
        u32 area_w = width - 2 * margin, area_h = bottom - top;
        assert(area_w && area_h);
        for (u32 aspect = 0; aspect < 5; ++aspect) {
            /* Match, portrait, wide, a changing full frame, and no decoded frame. */
            u32 fw = aspect == 1 ? 16 : aspect == 2 ? 96 : area_w;
            u32 fh = aspect == 1 ? 96 : aspect == 2 ? 16 : area_h;
            u32 cw = (fw + 1) / 2, ch = (fh + 1) / 2;
            u8 *yuv = malloc((usize)fw * fh + 2 * cw * ch);
            assert(yuv);
            memset(yuv, 150, (usize)fw * fh);
            if (aspect == 3) for (u32 y = 0; y < fh; ++y) for (u32 x = 0; x < fw; ++x)
                yuv[(usize)y * fw + x] = 64 + (x * 3 + y * 7) % 128;
            memset(yuv + (usize)fw * fh, 70, (usize)cw * ch);
            memset(yuv + (usize)fw * fh + cw * ch, 210, (usize)cw * ch);
            plm_frame_t frame = {.width = fw, .height = fh,
                .y = {fw, fh, yuv}, .cb = {cw, ch, yuv + (usize)fw * fh},
                .cr = {cw, ch, yuv + (usize)fw * fh + cw * ch}};
            v.frame = aspect == 4 ? NULL : &frame; v.paused = (aspect & 1u) != 0;
            u32 picture_w = v.frame ? MIN(area_w, (u32)((u64)area_h * fw / fh)) : 0;
            u32 picture_h = v.frame ? MIN(area_h, (u32)((u64)picture_w * fh / fw)) : 0;
            u32 picture_x = (width - picture_w) / 2;
            u32 picture_y = top + (area_h - picture_h) / 2;
            u32 *actual = malloc((usize)(count + 2 * GUARD_WORDS) * sizeof(u32));
            u32 *dark = malloc((usize)count * sizeof(u32));
            u32 *light = malloc((usize)count * sizeof(u32));
            u32 *scratch = malloc((usize)(width * TILE_ROWS + 2 * GUARD_WORDS) * sizeof(u32));
            assert(actual && dark && light && scratch);
            for (u32 format = NV_DISPLAY_BGRX8; format <= NV_DISPLAY_RGBX8; ++format) {
                guard_set(actual, count);
                struct nv_canvas canvas = {actual + GUARD_WORDS, width, 0, height, format};
                media_render(&canvas, height, &v);
                guard_check(actual, count);
                assert(v.playing && v.paused == ((aspect & 1u) != 0));
                struct desktop_clip clip = {0, 0, width, height};
                struct nv_canvas background = {dark, width, 0, height, format};
                struct nv_canvas foreground = {light, width, 0, height, format};
                nv_gfx_fill(&background, 0, 0, width, height, UI_BG);
                nv_gfx_fill(&foreground, 0, 0, width, height, UI_BG);
                desktop_round(&background, clip, margin, top, area_w, area_h, 8 * g.s, 0x10213b);
                desktop_round(&foreground, clip, margin, top, area_w, area_h, 8 * g.s,
                              media_rgb(150, 70, 210));
                for (u32 py = top; py < bottom; ++py) for (u32 px = margin; px < width - margin; ++px) {
                    bool in_picture = media_inside(px, py, picture_x, picture_y, picture_w, picture_h);
                    u32 expected = (in_picture ? light : dark)[(usize)py * width + px];
                    if (aspect == 3) {
                        /* Source coordinates stay absolute even after masked corner pixels. */
                        u32 color = nv_display_rgb(format,
                            media_rgb(yuv[(usize)(py - top) * fw + px - margin], 70, 210));
                        u32 alpha = desktop_round_coverage(px - margin, py - top, area_w, area_h, 8 * g.s);
                        expected = desktop_blend(color, nv_display_rgb(format, UI_BG), alpha);
                    }
                    assert(actual[GUARD_WORDS + (usize)py * width + px] == expected);
                }
                for (u32 y = 0; y < height; y += TILE_ROWS) {
                    u32 rows = MIN((u32)TILE_ROWS, height - y), length = width * rows;
                    guard_set(scratch, length);
                    struct nv_canvas tile = {scratch + GUARD_WORDS, width, y, rows, format};
                    media_render(&tile, height, &v);
                    guard_check(scratch, length);
                    assert(!memcmp(actual + GUARD_WORDS + (usize)y * width,
                                   scratch + GUARD_WORDS, (usize)length * sizeof(u32)));
                }
            }
            free(actual); free(dark); free(light); free(scratch); free(yuv);
        }
    }
}
static void audio_and_browse_ui(void) {
    struct media_view audio = audio_view(), browse = audio;
    browse.playing = false; browse.current = ~0u;
    struct media_view empty = {.path = "/Empty", .message = "No supported local files",
                              .current = ~0u, .previous = ~0u, .next = ~0u};
    for (u32 i = 0; i < sizeof(ui_sizes) / sizeof(ui_sizes[0]); ++i) {
        u32 width = ui_sizes[i][0], height = ui_sizes[i][1];
        check_render(width, height, &audio); check_hits(width, height, &audio);
        check_render(width, height, &browse); check_hits(width, height, &browse);
        check_render(width, height, &empty);
        struct media_view silent = audio;
        silent.audio_ready = silent.duration_known = false;
        silent.previous = silent.next = ~0u;
        silent.title = silent.media_info = NULL;
        check_render(width, height, &silent);
        struct media_view tail = browse; tail.scroll = tail.count - 1;
        check_hits(width, height, &tail);
    }
    /* Rendering remains bounded when a compositor gives a very small surface. */
    for (u32 i = 0; i < sizeof(tiny_sizes) / sizeof(tiny_sizes[0]); ++i) {
        check_render(tiny_sizes[i][0], tiny_sizes[i][1], &audio);
        check_render(tiny_sizes[i][0], tiny_sizes[i][1], &browse);
    }
}
struct video_result { u32 frames, audio; bool ui_checked; };
static void check_ui(plm_frame_t *frame) {
    struct media_view v = audio_view();
    v.title = "clip.mpg"; v.media_info = "MPEG-1 / 160 x 120 / MP2";
    v.video = true; v.frame = frame; v.paused = false;
    for (u32 i = 0; i < sizeof(ui_sizes) / sizeof(ui_sizes[0]); ++i) {
        check_render(ui_sizes[i][0], ui_sizes[i][1], &v);
        check_hits(ui_sizes[i][0], ui_sizes[i][1], &v);
    }
    for (u32 i = 0; i < sizeof(tiny_sizes) / sizeof(tiny_sizes[0]); ++i)
        check_render(tiny_sizes[i][0], tiny_sizes[i][1], &v);
}
static void got_frame(plm_t *plm, plm_frame_t *frame, void *user) {
    (void)plm;
    struct video_result *result = user;
    assert(frame->width == 160 && frame->height == 120);
    if (!result->ui_checked) { check_ui(frame); result->ui_checked = true; }
    ++result->frames;
}
static void got_audio(plm_t *plm, plm_samples_t *samples, void *user) {
    (void)plm;
    struct video_result *result = user;
    for (u32 i = 0; i < samples->count * 2; ++i)
        if (samples->interleaved[i] != 0) { ++result->audio; break; }
}
static void video_fixture(const char *path) {
    u32 size;
    u8 *data = load(path, &size);
    plm_t *plm = plm_create_with_memory(data, size, 0);
    assert(plm && plm_get_num_video_streams(plm) == 1 && plm_get_num_audio_streams(plm) == 1);
    assert(plm_get_samplerate(plm) == 48000);
    struct video_result result = {0};
    plm_set_video_decode_callback(plm, got_frame, &result);
    plm_set_audio_decode_callback(plm, got_audio, &result);
    for (u32 i = 0; i < 100 && !plm_has_ended(plm); ++i) plm_decode(plm, 0.04);
    assert(result.frames >= 10 && result.audio > 0 && result.ui_checked);
    plm_destroy(plm);
    free(data);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    launch_paths();
    prepare_library();
    video_preview_corners();
    audio_and_browse_ui();
    mp3_fixture(argv[1]);
    video_fixture(argv[2]);
    puts("PASS media: MP3 PCM, MPEG-1/MP2, guarded tiled audio/library/video rendering in both pixel formats and playback controls");
    return 0;
}
