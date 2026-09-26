#include "runtime.h"

/* PCM WAVE reader. RIFF sizes, chunk padding and file bounds are checked
 * before sending bytes to the HDA driver. Streaming uses a fixed user buffer. */
static u8 buffer[NV_AUDIO_MAX_WRITE];
static u32 le32(const u8 *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static u16 le16(const u8 *p) { return (u16)(p[0] | (u16)p[1] << 8); }
static int exact(int fd, u8 *out, u32 bytes) {
    for (u32 done = 0; done < bytes;) {
        int n = take(fd, out + done, bytes - done);
        if (n <= 0) return n < 0 ? n : -NV_EIO;
        done += (u32)n;
    }
    return 0;
}
static int play_file(const char *path) {
    int fd = open_file(path, NV_READ);
    if (fd < 0) return fd;
    int size = seek_file(fd, 0, 2);
    if (size < 12 || seek_file(fd, 0, 0) != 0) {
        close_file(fd); return -NV_EINVAL;
    }
    u8 header[16];
    int r = exact(fd, header, 12);
    if (r < 0 || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4) ||
        le32(header + 4) > (u32)size - 8 || le32(header + 4) < 4) {
        close_file(fd); return -NV_EINVAL;
    }
    u32 end = le32(header + 4) + 8, offset = 12, data_start = 0, data_bytes = 0;
    bool format_ok = false;
    while (offset + 8 <= end) {
        if (seek_file(fd, (i32)offset, 0) != (int)offset || exact(fd, header, 8) < 0) {
            r = -NV_EIO; break;
        }
        u32 bytes = le32(header + 4);
        if (bytes > end - offset - 8 || (bytes & 1u && bytes == end - offset - 8)) {
            r = -NV_EINVAL; break;
        }
        if (!memcmp(header, "fmt ", 4)) {
            if (bytes < 16 || exact(fd, header, 16) < 0) { r = -NV_EINVAL; break; }
            format_ok = le16(header) == 1 && le16(header + 2) == 2 &&
                        le32(header + 4) == 48000 && le32(header + 8) == 192000 &&
                        le16(header + 12) == 4 && le16(header + 14) == 16;
        } else if (!memcmp(header, "data", 4) && !data_bytes) {
            data_start = offset + 8; data_bytes = bytes;
        }
        offset += 8 + bytes + (bytes & 1u);
        if (offset > end) { r = -NV_EINVAL; break; }
    }
    if (r >= 0 && (!format_ok || !data_bytes || data_bytes % 4 ||
                   seek_file(fd, (i32)data_start, 0) != (int)data_start)) r = -NV_EINVAL;
    if (r >= 0) {
        for (u32 remaining = data_bytes; remaining;) {
            u32 chunk = MIN(remaining, NV_AUDIO_MAX_WRITE);
            r = exact(fd, buffer, chunk);
            if (r < 0) break;
            r = nv_audio_write(buffer, chunk);
            if (r != (int)chunk) { if (r >= 0) r = -NV_EIO; break; }
            remaining -= chunk;
        }
        if (r >= 0) r = 0;
    }
    close_file(fd);
    return r;
}
static int test_tone(void) {
    /* One second of a modest 440 Hz triangle, with a 50 ms fade. */
    u32 phase = 0;
    for (u32 at = 0; at < 48000; at += NV_AUDIO_MAX_WRITE / 4) {
        u32 frames = MIN(48000 - at, NV_AUDIO_MAX_WRITE / 4);
        for (u32 i = 0; i < frames; ++i) {
            u32 t = at + i;
            phase += 440;
            if (phase >= 48000) phase -= 48000;
            i32 triangle = phase < 24000 ? (i32)phase - 12000 : 36000 - (i32)phase;
            i32 fade = MIN(MIN(t, 47999 - t), 2400u);
            i32 sample = triangle * (i32)fade / 9600;
            for (u32 channel = 0; channel < 2; ++channel) {
                u32 p = 4 * i + 2 * channel;
                buffer[p] = (u8)sample;
                buffer[p + 1] = (u8)(sample >> 8);
            }
        }
        int r = nv_audio_write(buffer, frames * 4);
        if (r != (int)(frames * 4)) return r < 0 ? r : -NV_EIO;
    }
    return 0;
}
int user_main(const char *args) {
    if (app_help("wave", args)) return 0;
    if (!*args) { println("Usage: wave FILE.wav | wave --test"); return 1; }
    struct nv_audio_info audio;
    int r = nv_audio_info(&audio);
    if (r < 0) { report_error("Audio", r); return 1; }
    if (audio.api_version != NV_AUDIO_API_VERSION || !audio.outputs) {
        println("No supported HDA analog output. Connect speakers/headphones to a supported controller.");
        return 1;
    }
    if (audio.sample_rate != 48000 || audio.channels != 2 ||
        audio.format != NV_AUDIO_S16LE || audio.max_write_bytes < NV_AUDIO_MAX_WRITE) {
        println("Audio: unsupported output format."); return 1;
    }
    r = !strcmp(args, "--test") ? test_tone() : play_file(args);
    if (r < 0) {
        if (r == -NV_EINVAL) println("Expected PCM WAV: 48 kHz, stereo, 16-bit.");
        else report_error("Playback", r);
        return 1;
    }
    return 0;
}
