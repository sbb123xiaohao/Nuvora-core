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
struct video_result { u32 frames, audio; bool ui_checked; };
static void check_ui(plm_frame_t *frame) {
    const u32 width = 640, height = 480, length = width * height;
    u32 *full = calloc(length + 2, sizeof(u32));
    u32 *tiled = calloc(length + 2, sizeof(u32));
    assert(full && tiled);
    full[0] = tiled[0] = 0x12341234;
    full[length + 1] = tiled[length + 1] = 0x48793421;
    struct media_view v = {
        .title = "clip.mpg", .playing = true, .video = true, .frame = frame,
        .pointer = true, .pointer_x = 125, .pointer_y = 100};
    struct nv_canvas all = {full + 1, width, 0, height, NV_DISPLAY_BGRX8};
    media_render(&all, height, &v);
    for (u32 y = 0; y < height; y += 31) {
        struct nv_canvas tile = {tiled + 1 + y * width, width, y,
                                 MIN(31u, height - y), NV_DISPLAY_BGRX8};
        media_render(&tile, height, &v);
    }
    assert(!memcmp(full + 1, tiled + 1, length * sizeof(u32)));
    assert(full[0] == 0x12341234 && full[length + 1] == 0x48793421);
    assert(tiled[0] == 0x12341234 && tiled[length + 1] == 0x48793421);
    assert(media_hit(width, height, &v, 15, 12).kind == MEDIA_HIT_BACK);
    assert(media_hit(width, height, &v, 40, 470).kind == MEDIA_HIT_PAUSE);
    free(full); free(tiled);
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
    mp3_fixture(argv[1]);
    video_fixture(argv[2]);
    puts("PASS media: decoded MP3 PCM, MPEG-1 video/MP2 audio and tiled graphical playback");
    return 0;
}
