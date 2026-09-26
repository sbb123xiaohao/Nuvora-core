#include "runtime.h"
#include "media_ui.h"
#include "../third_party/minimp3.h"
#include "../third_party/dr_flac.h"
#include "media_stream.h"
#include "wav_reader.h"
#include "media_flac.h"
typedef short i16;

#define MEDIA_ITEMS 512u
static struct nv_dirent entries[MEDIA_ITEMS];
static struct nv_display_info mode;
static struct media_view view;
static u32 *tile, tile_rows;
static char path[NV_PATH_MAX], status[160], title[NV_NAME_MAX + 1];
static bool audio_ready, stop_playback;
static u32 paused_ticks;
static u32 pointer_buttons;
static i16 decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
static u8 compressed[16384];

static bool suffix(const char *name, const char *ext) {
    u32 n = (u32)strlen(name), m = (u32)strlen(ext);
    if (n < m) return false;
    name += n - m;
    for (u32 i = 0; i < m; ++i) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != ext[i]) return false;
    }
    return true;
}
static bool supported(const char *name) {
    return suffix(name, ".mp3") || suffix(name, ".wav") ||
           suffix(name, ".mpg") || suffix(name, ".mpeg") || suffix(name, ".flac") ||
           suffix(name, ".mp2") || suffix(name, ".wave");
}
static void note(const char *text) { strlcpy(status, text, sizeof(status)); }
static void failure(const char *action, int error) {
    strlcpy(status, action, sizeof(status));
    u32 len = (u32)strlen(status);
    strlcpy(status + len, ": ", sizeof(status) - len);
    len = (u32)strlen(status);
    strlcpy(status + len, error_name(error), sizeof(status) - len);
}
static int refresh(void) {
    int r = getcwd_path(path, sizeof(path));
    if (r < 0) return r;
    u32 found = 0;
    for (u32 i = 0; found < MEDIA_ITEMS; ++i) {
        struct nv_dirent entry;
        r = list_dir(".", i, &entry);
        if (r <= 0) break;
        if (entry.kind == NV_DIR || (entry.kind == NV_FILE && supported(entry.name)))
            entries[found++] = entry;
    }
    if (r < 0) return r;
    view.entries = entries; view.count = found;
    view.path = path; view.message = status;
    if (view.selected >= found) view.selected = found ? found - 1 : 0;
    if (view.scroll > view.selected) view.scroll = view.selected;
    return 0;
}
static int draw(void) {
    for (u32 y = 0; y < mode.height; y += tile_rows) {
        struct nv_canvas canvas = {tile, mode.width, y,
                                   MIN(tile_rows, mode.height - y), mode.format};
        media_render(&canvas, mode.height, &view);
        struct nv_display_present rect = {
            .x = 0, .y = y, .width = mode.width, .height = canvas.rows,
            .stride = mode.width * 4, .pixels = (u32)(uptr)tile};
        int r = nv_display_present(&rect);
        if (r < 0) return r;
    }
    return 0;
}
static void scroll_to_selection(void) {
    u32 visible = media_visible(mode.height, media_scale(mode.width, mode.height));
    if (view.selected < view.scroll) view.scroll = view.selected;
    if (view.selected >= view.scroll + visible)
        view.scroll = view.selected - visible + 1;
}
static int pointer_input(bool *clicked) {
    struct nv_pointer_event event;
    int r = nv_pointer_poll(&event);
    if (r <= 0) return r;
    view.pointer = true;
    view.pointer_x = (u32)MAX(0, MIN((i32)view.pointer_x + event.dx, (i32)mode.width - 1));
    view.pointer_y = (u32)MAX(0, MIN((i32)view.pointer_y + event.dy, (i32)mode.height - 1));
    *clicked = !!((event.buttons & NV_POINTER_LEFT) && !(pointer_buttons & NV_POINTER_LEFT));
    pointer_buttons = event.buttons;
    return 1;
}
struct audio_sink {
    i16 samples[NV_AUDIO_MAX_WRITE / sizeof(i16)];
    u32 count, rate, phase, peak_left, peak_right;
    u64 played_frames;
    i16 previous_left, previous_right;
    bool has_previous;
    int error;
};
static struct audio_sink sound;
static int flush_audio(void) {
    if (!sound.count || sound.error < 0) return sound.error;
    int bytes = sound.count * 4;
    int r = nv_audio_write(sound.samples, (u32)bytes);
    sound.count = 0;
    sound.error = r == bytes ? 0 : r < 0 ? r : -NV_EIO;
    return sound.error;
}
static void write_stereo(i16 left, i16 right) {
    if (sound.error < 0) return;
    sound.samples[sound.count * 2] = left;
    sound.samples[sound.count * 2 + 1] = right;
    ++sound.count;
    ++sound.played_frames;
    u32 a = left == -32768 ? 32768 : (u32)(left < 0 ? -left : left);
    u32 b = right == -32768 ? 32768 : (u32)(right < 0 ? -right : right);
    sound.peak_left = MAX(sound.peak_left, a);
    sound.peak_right = MAX(sound.peak_right, b);
    if (sound.count == ARRAY_LEN(sound.samples) / 2) flush_audio();
}
static void sample_at_rate(i16 left, i16 right, u32 rate) {
    if (rate < 8000 || rate > 655350) { sound.error = -NV_EINVAL; return; }
    if (sound.rate != rate) { sound.rate = rate; sound.has_previous = false; sound.phase = 0; }
    if (!sound.has_previous) {
        sound.previous_left = left; sound.previous_right = right;
        sound.has_previous = true; return;
    }
    while (sound.phase < 48000) {
        i32 l = (i32)sound.previous_left +
                ((i64)left - sound.previous_left) * sound.phase / 48000;
        i32 r = (i32)sound.previous_right +
                ((i64)right - sound.previous_right) * sound.phase / 48000;
        write_stereo((i16)l, (i16)r);
        sound.phase += rate;
    }
    sound.phase -= 48000;
    sound.previous_left = left; sound.previous_right = right;
}
static int meter(void) {
    view.peak_left = sound.peak_left; view.peak_right = sound.peak_right;
    sound.peak_left = sound.peak_right = 0;
    view.seconds = (u32)(sound.played_frames / 48000);
    return draw();
}
static int transport(void) {
    bool clicked = false;
    int mouse = pointer_input(&clicked);
    if (mouse < 0) return mouse;
    if (clicked) {
        struct media_hit hit = media_hit(mode.width, mode.height, &view,
                                         view.pointer_x, view.pointer_y);
        if (hit.kind == MEDIA_HIT_BACK) { stop_playback = true; return 0; }
        if (hit.kind == MEDIA_HIT_PAUSE) view.paused = !view.paused;
    }
    int key = key_event();
    if (key >= 0) {
        u32 k = (u32)key & 4095u;
        if (k == 27) stop_playback = true;
        if (k == ' ' || k == '\n') view.paused = !view.paused;
    } else if (key != -NV_EAGAIN) return key;
    u32 pause_begin = clock_ticks();
    bool had_pause = view.paused;
    if (had_pause) {
        int r = draw();
        if (r < 0) return r;
    }
    while (view.paused && !stop_playback) {
        if (mouse || clicked) {
            int r = draw();
            if (r < 0) return r;
        }
        nap(25);
        mouse = pointer_input(&clicked);
        if (mouse < 0) return mouse;
        if (clicked) {
            struct media_hit hit = media_hit(mode.width, mode.height, &view,
                                             view.pointer_x, view.pointer_y);
            if (hit.kind == MEDIA_HIT_BACK) stop_playback = true;
            if (hit.kind == MEDIA_HIT_PAUSE) view.paused = false;
        }
        key = key_event();
        if (key >= 0) {
            u32 k = (u32)key & 4095u;
            if (k == 27) stop_playback = true;
            if (k == ' ' || k == '\n') view.paused = false;
        } else if (key != -NV_EAGAIN) return key;
    }
    if (had_pause) paused_ticks += clock_ticks() - pause_begin;
    return 0;
}
static int play_mp3(int fd) {
    if (!audio_ready) return -NV_ENODEV;
    mp3dec_t decoder;
    mp3dec_init(&decoder);
    u32 length = 0, frames = 0, last_draw = clock_ticks();
    bool eof = false;
    for (;;) {
        if (transport() < 0) return -NV_EIO;
        if (stop_playback || sound.error < 0) break;
        if (!eof && length < 12000) {
            int n = take(fd, compressed + length, sizeof(compressed) - length);
            if (n < 0) return n;
            if (!n) eof = true;
            else length += (u32)n;
        }
        if (!length || (length < 2048 && !eof)) {
            if (eof) break;
            continue;
        }
        mp3dec_frame_info_t info = {0};
        int count = mp3dec_decode_frame(&decoder, compressed, (int)length, decoded, &info);
        if (info.frame_bytes <= 0 || (u32)info.frame_bytes > length) {
            if (!eof && length < sizeof(compressed)) {
                int n = take(fd, compressed + length, sizeof(compressed) - length);
                if (n < 0) return n;
                if (n) { length += (u32)n; continue; }
                eof = true;
            }
            if (!eof && length > 2048) {
                memmove(compressed, compressed + length - 2048, 2048);
                length = 2048;
                continue;
            }
            break;
        }
        if (count > 0 && (info.channels == 1 || info.channels == 2)) {
            for (int i = 0; i < count; ++i)
                sample_at_rate(decoded[i * info.channels],
                               decoded[i * info.channels + info.channels - 1],
                               (u32)info.hz);
            ++frames;
        }
        length -= (u32)info.frame_bytes;
        memmove(compressed, compressed + info.frame_bytes, length);
        if (clock_ticks() - last_draw >= 10) {
            int r = meter();
            if (r < 0) return r;
            last_draw = clock_ticks();
        }
        if (eof && !length) break;
    }
    if (!frames && !stop_playback) return -NV_EINVAL;
    if (!stop_playback) flush_audio();
    return sound.error;
}
static int source_read(void *context, void *out, u32 bytes) {
    return take(*(int *)context, out, bytes);
}
static int source_seek(void *context, u64 offset) {
    if (offset > NV_FILE_MAX64) return -NV_EINVAL;
    return seek_file64(*(int *)context, (i64)offset, 0, NULL);
}
static void mix_channels(const i32 *pcm, u32 channels, u32 rate) {
    i32 left = pcm[0], right = pcm[channels > 1 ? 1 : 0];
    /* Preserve stereo; multichannel files use a normalized stereo downmix. */
    if (channels > 2) {
        i32 weight = 2;
        left *= 2; right *= 2;
        for (u32 c = 2; c < channels; ++c) { left += pcm[c]; right += pcm[c]; ++weight; }
        left /= weight; right /= weight;
    }
    sample_at_rate((i16)left, (i16)right, rate);
}
static int play_wav(int fd) {
    if (!audio_ready) return -NV_ENODEV;
    struct nv_stat64 st;
    int r = stat_file64(fd, &st); if (r < 0) return r;
    struct wav_io io = {&fd, source_read, source_seek};
    struct wav_format f;
    r = wav_open(&io, st.size, &f); if (r < 0) return r;
    u64 bytes = f.bytes; u32 last_draw = clock_ticks();
    while (bytes && !stop_playback && sound.error >= 0) {
        r = transport(); if (r < 0) return r;
        u32 chunk = (u32)MIN(bytes, sizeof(compressed)/f.frame*f.frame);
        r = wav_exact(&io, compressed, chunk); if (r < 0) return r;
        for (u32 at = 0; at < chunk; at += f.frame) {
            i32 pcm[8];
            for (u32 c = 0; c < f.channels; ++c)
                pcm[c] = wav_sample(compressed+at+c*(f.bits/8), f.bits, f.tag);
            mix_channels(pcm, f.channels, f.rate);
        }
        bytes -= chunk;
        if (clock_ticks()-last_draw >= 10) { r = meter(); if (r < 0) return r; last_draw = clock_ticks(); }
    }
    if (!stop_playback) flush_audio();
    return sound.error;
}
static int play_flac(int fd) {
    if (!audio_ready) return -NV_ENODEV;
    struct nv_stat64 st; int sr = stat_file64(fd, &st); if (sr < 0) return sr;
    struct media_source source = {.context=&fd, .read=source_read, .seek=source_seek, .length=st.size};
    drflac *flac = drflac_open(flac_read, flac_seek, flac_tell, &source, NULL);
    if (!flac) return source.error < 0 ? source.error : -NV_EINVAL;
    u64 frames = 0; u32 last_draw = clock_ticks(); int r = 0;
    if (!flac->channels || flac->channels > 8 || flac->sampleRate < 8000 || flac->sampleRate > 655350) r = -NV_EINVAL;
    while (!r && !stop_playback && sound.error >= 0) {
        r = transport(); if (r < 0) break;
        u64 got = drflac_read_pcm_frames_s16(flac, ARRAY_LEN(decoded)/flac->channels, decoded);
        if (!got) break;
        frames += got;
        for (u32 i = 0; i < got; ++i) {
            i32 pcm[8];
            for (u32 c = 0; c < flac->channels; ++c) pcm[c] = decoded[i*flac->channels+c];
            mix_channels(pcm, flac->channels, flac->sampleRate);
        }
        if (clock_ticks()-last_draw >= 10) { r = meter(); last_draw = clock_ticks(); }
    }
    if (!r && !stop_playback && flac->totalPCMFrameCount && frames != flac->totalPCMFrameCount) r = -NV_EIO;
    drflac_close(flac);
    if (!stop_playback) flush_audio();
    return r < 0 ? r : source.error < 0 ? source.error : sound.error;
}
struct video_state { int error; u32 frames; };
static void video_frame(plm_t *plm, plm_frame_t *frame, void *user) {
    (void)plm;
    struct video_state *state = user;
    if (state->error < 0 || stop_playback) return;
    view.frame = frame;
    view.seconds = (u32)frame->time;
    state->error = draw();
    ++state->frames;
}
static void video_audio(plm_t *plm, plm_samples_t *samples, void *user) {
    struct video_state *state = user;
    if (state->error < 0 || stop_playback) return;
    u32 rate = (u32)plm_get_samplerate(plm);
    for (u32 i = 0; i < samples->count; ++i) {
        float l = samples->interleaved[2 * i], r = samples->interleaved[2 * i + 1];
        i32 a = (i32)(l * 32767.0f), b = (i32)(r * 32767.0f);
        sample_at_rate((i16)MAX(-32768, MIN(a, 32767)),
                       (i16)MAX(-32768, MIN(b, 32767)), rate);
    }
    state->error = sound.error;
}
static int play_video(int fd) {
    struct nv_stat64 st;
    int r = stat_file64(fd, &st);
    if (r < 0 || !st.size || source_seek(&fd, 0) < 0) return r < 0 ? r : -NV_EINVAL;
    struct media_source source = {.context=&fd, .read=source_read, .seek=source_seek, .length=st.size};
    plm_buffer_t *buffer = plm_buffer_create_with_callbacks(media_stream_load,
        media_stream_seek, media_stream_tell, (size_t)st.size, &source);
    plm_t *plm = plm_create_with_buffer(buffer, 1);
    if (!plm || plm_get_num_video_streams(plm) != 1 ||
        plm_get_width(plm) < 1 || plm_get_height(plm) < 1 || plm_get_framerate(plm) <= 0) {
        if (plm) plm_destroy(plm);
        return source.error < 0 ? source.error : -NV_EINVAL;
    }
    struct video_state state = {0};
    plm_set_video_decode_callback(plm, video_frame, &state);
    if (audio_ready && plm_get_num_audio_streams(plm) && plm_get_samplerate(plm) > 0)
        plm_set_audio_decode_callback(plm, video_audio, &state);
    else plm_set_audio_enabled(plm, 0);
    u32 previous = clock_ticks();
    while (!plm_has_ended(plm) && !stop_playback && state.error >= 0 && source.error >= 0) {
        u32 paused_before = paused_ticks;
        r = transport();
        if (r < 0) { state.error = r; break; }
        u32 now = clock_ticks(), delta = MIN(now - previous, 25u);
        u32 paused_here = paused_ticks - paused_before;
        delta = delta > paused_here ? delta - paused_here : 0;
        previous = now;
        plm_decode(plm, (double)delta / 100.0);
        if (!delta) nap(5);
    }
    if (!stop_playback && state.error >= 0) flush_audio();
    view.frame = NULL;
    plm_destroy(plm);
    return source.error < 0 ? source.error : state.error < 0 ? state.error :
           sound.error < 0 ? sound.error : state.frames ? 0 : -NV_EINVAL;
}
static int play_mp2(int fd) {
    if (!audio_ready) return -NV_ENODEV;
    struct nv_stat64 st; int r = stat_file64(fd, &st); if (r < 0) return r;
    struct media_source source = {.context=&fd, .read=source_read, .seek=source_seek, .length=st.size};
    plm_buffer_t *buffer = plm_buffer_create_with_callbacks(media_stream_load,
        media_stream_seek, media_stream_tell, (size_t)st.size, &source);
    plm_audio_t *decoder = plm_audio_create_with_buffer(buffer, 1);
    u32 frames = 0, last_draw = clock_ticks();
    while (!stop_playback && source.error >= 0 && sound.error >= 0) {
        r = transport(); if (r < 0) break;
        plm_samples_t *samples = plm_audio_decode(decoder); if (!samples) break;
        u32 rate = (u32)plm_audio_get_samplerate(decoder);
        for (u32 i = 0; i < samples->count; ++i) {
            i32 l = (i32)(samples->interleaved[2*i]*32767.0f);
            i32 right = (i32)(samples->interleaved[2*i+1]*32767.0f);
            sample_at_rate((i16)MAX(-32768, MIN(l, 32767)), (i16)MAX(-32768, MIN(right, 32767)), rate);
        }
        ++frames;
        if (clock_ticks()-last_draw >= 10) { r = meter(); if (r < 0) break; last_draw = clock_ticks(); }
    }
    plm_audio_destroy(decoder);
    if (!stop_playback) flush_audio();
    return r < 0 ? r : source.error < 0 ? source.error : sound.error < 0 ? sound.error : frames ? 0 : -NV_EINVAL;
}
static int play(const char *file) {
    int fd = open_file(file, NV_READ);
    if (fd < 0) return fd;
    u32 heap_mark = (u32)(uptr)grow(0);
    memset(&sound, 0, sizeof(sound));
    view.playing = true; view.paused = false;
    view.video = suffix(file, ".mpg") || suffix(file, ".mpeg");
    view.frame = NULL; view.peak_left = view.peak_right = view.seconds = 0;
    view.title = title;
    stop_playback = false;
    paused_ticks = 0;
    const char *base = file;
    for (const char *p = file; *p; ++p) if (*p == '/') base = p + 1;
    strlcpy(title, base, sizeof(title));
    int r = draw();
    if (r >= 0) r = view.video ? play_video(fd) :
                    suffix(file, ".mp3") ? play_mp3(fd) : suffix(file, ".flac") ? play_flac(fd) :
                    suffix(file, ".mp2") ? play_mp2(fd) : play_wav(fd);
    close_file(fd);
    view.playing = false; view.frame = NULL;
    u32 heap_end = (u32)(uptr)grow(0);
    if (heap_end > heap_mark) grow(-((i32)(heap_end - heap_mark) / (i32)NV_PAGE));
    if (r == -NV_EINVAL) note("Unsupported or damaged media format.");
    else if (r == -NV_ENODEV) note("Audio output unavailable; connect an HDA device.");
    else if (r < 0) failure("Playback", r);
    else note(stop_playback ? "Stopped." : "Finished.");
    return draw();
}
static int open_selected(void) {
    if (!view.count) return 0;
    const struct nv_dirent *e = &entries[view.selected];
    if (e->kind == NV_DIR) {
        int r = chdir_path(e->name);
        if (r < 0) return r;
        view.selected = view.scroll = 0;
        note("");
        return refresh();
    }
    char full[NV_PATH_MAX];
    u32 n = (u32)strlcpy(full, path, sizeof(full));
    if (n >= sizeof(full)) return -NV_E2BIG;
    if (n != 1 || full[0] != '/') {
        if (n + 1 >= sizeof(full)) return -NV_E2BIG;
        full[n++] = '/'; full[n] = 0;
    }
    if (strlcpy(full + n, e->name, sizeof(full) - n) >= sizeof(full) - n)
        return -NV_E2BIG;
    return play(full);
}
int user_main(const char *args) {
    if (app_help("media", args)) return 0;
    if (*args && !supported(args)) {
        println("Usage: media [FILE.mp3 | FILE.flac | FILE.wav | FILE.mp2 | FILE.mpg]");
        return 1;
    }
    int r = nv_display_info(&mode);
    if (r < 0 || mode.api_version != NV_DISPLAY_API_VERSION ||
        (mode.format != NV_DISPLAY_BGRX8 && mode.format != NV_DISPLAY_RGBX8) ||
        mode.width < 640 || mode.height < 480 || mode.width > 8192 ||
        (u64)mode.width * 4 > mode.max_copy_bytes ||
        mode.max_copy_bytes > NV_DISPLAY_MAX_COPY) {
        println("Media requires a UEFI pixel framebuffer (640x480 or larger).");
        return 1;
    }
    struct nv_audio_info audio;
    audio_ready = nv_audio_info(&audio) == 0 && audio.api_version == NV_AUDIO_API_VERSION &&
                  audio.outputs && audio.sample_rate == 48000 && audio.channels == 2 &&
                  audio.format == NV_AUDIO_S16LE && audio.max_write_bytes >= NV_AUDIO_MAX_WRITE;
    view.audio_ready = audio_ready;
    tile_rows = mode.max_copy_bytes / (mode.width * 4);
    tile = grow((mode.max_copy_bytes + NV_PAGE - 1) / NV_PAGE);
    if ((iptr)tile < 0) { report_error("Media buffer", (int)(iptr)tile); return 1; }
    r = refresh();
    if (r < 0) { report_error("Media files", r); return 1; }
    r = nv_display_acquire();
    if (r < 0) { report_error("Media display", r); return 1; }
    view.pointer_x = mode.width / 2; view.pointer_y = mode.height / 2;
    note(audio_ready ? "" : "No HDA output; silent MPEG video remains available.");
    if (*args) {
        r = play(args);
        if (r < 0) goto done;
    }
    u32 last_click = ~0u, last_tick = 0;
    for (;;) {
        r = draw();
        if (r < 0) break;
        bool clicked = false;
        int mouse = pointer_input(&clicked);
        if (mouse < 0) { r = mouse; break; }
        if (clicked) {
            struct media_hit hit = media_hit(mode.width, mode.height, &view,
                                             view.pointer_x, view.pointer_y);
            if (hit.kind == MEDIA_HIT_BACK) {
                r = chdir_path("..");
                if (r >= 0) { view.selected = view.scroll = 0; r = refresh(); }
                if (r < 0) failure("Parent folder", r);
            } else if (hit.kind == MEDIA_HIT_ITEM) {
                u32 tick = clock_ticks();
                bool open = last_click == hit.index && tick - last_tick <= 40;
                view.selected = hit.index; last_click = hit.index; last_tick = tick;
                if (open) {
                    r = open_selected(); last_click = ~0u;
                    if (r < 0) failure("Open item", r);
                }
            }
        }
        int key = key_event();
        if (key == -NV_EAGAIN) { nap(25); continue; }
        if (key < 0) { r = key; break; }
        u32 k = (u32)key & 4095u;
        if (k == 27) break;
        if (k == NV_KEY_UP && view.selected) --view.selected;
        else if (k == NV_KEY_DOWN && view.selected + 1 < view.count) ++view.selected;
        else if (k == NV_KEY_PGUP) view.selected = view.selected > 8 ? view.selected - 8 : 0;
        else if (k == NV_KEY_PGDN && view.count) view.selected = MIN(view.count - 1, view.selected + 8);
        else if (k == '\b') {
            r = chdir_path("..");
            if (r >= 0) { view.selected = view.scroll = 0; r = refresh(); }
            if (r < 0) failure("Parent folder", r);
        } else if (k == '\n') {
            r = open_selected();
            if (r < 0) failure("Open item", r);
        } else if (k == NV_KEY_F5) {
            r = refresh();
            if (r < 0) failure("Refresh", r);
        }
        if (r < 0) r = 0;
        scroll_to_selection();
    }
done:
    nv_display_release();
    if (r < 0) { report_error("Media", r); return 1; }
    return 0;
}
