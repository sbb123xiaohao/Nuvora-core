#ifndef NV_WAV_READER_H
#define NV_WAV_READER_H
#include <nv/abi.h>
#include <nv/string.h>
/* RIFF/RF64 reader, independent of syscall and audio hardware. */
struct wav_io {
    void *context;
    int (*read)(void *, void *, u32);
    int (*seek)(void *, u64);
};
struct wav_format { u64 data, bytes; u32 rate, channels, bits, tag, frame; };
static u16 wav_u16(const u8 *p) { return (u16)(p[0] | (u16)p[1] << 8); }
static u32 wav_u32(const u8 *p) { return (u32)p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24; }
static u64 wav_u64(const u8 *p) { return (u64)wav_u32(p) | (u64)wav_u32(p+4)<<32; }
static int wav_exact(struct wav_io *io, void *out, u32 len) {
    u8 *p = out;
    while (len) {
        int r = io->read(io->context, p, MIN(len, 16384u));
        if (r <= 0 || (u32)r > len) return r < 0 ? r : -NV_EIO;
        p += r; len -= (u32)r;
    }
    return 0;
}
static int wav_open(struct wav_io *io, u64 size, struct wav_format *f) {
    u8 h[40]; memset(f, 0, sizeof(*f));
    if (size < 12 || io->seek(io->context, 0) < 0 || wav_exact(io, h, 12) < 0 || memcmp(h+8, "WAVE", 4)) return -NV_EINVAL;
    bool rf64 = !memcmp(h, "RF64", 4);
    if ((!rf64 && memcmp(h, "RIFF", 4)) || (!rf64 && (u64)wav_u32(h+4)+8 > size)) return -NV_EINVAL;
    u64 end = rf64 ? size : (u64)wav_u32(h+4)+8, at = 12, data64 = 0;
    bool ds64 = false, data = false, format = false;
    while (at <= end && end-at >= 8) {
        if (io->seek(io->context, at) < 0 || wav_exact(io, h, 8) < 0) return -NV_EIO;
        u64 chunk = wav_u32(h+4); bool isdata = !memcmp(h, "data", 4);
        if (chunk == 0xffffffffu && rf64) {
            if (!ds64 || !isdata) return -NV_EINVAL;
            chunk = data64;
        }
        if (chunk > end-at-8 || (chunk & 1) > end-at-8-chunk) return -NV_EINVAL;
        if (rf64 && !memcmp(h, "ds64", 4)) {
            if (ds64 || chunk < 28 || wav_exact(io, h, 28) < 0) return -NV_EINVAL;
            u64 riff = wav_u64(h);
            if (riff > size-8 || riff+8 < at+8+chunk) return -NV_EINVAL;
            end = riff+8; data64 = wav_u64(h+8); ds64 = true;
        } else if (!memcmp(h, "fmt ", 4)) {
            if (format || chunk < 16 || wav_exact(io, h, (u32)MIN(chunk, 40)) < 0) return -NV_EINVAL;
            f->tag = wav_u16(h); f->channels = wav_u16(h+2); f->rate = wav_u32(h+4);
            f->frame = wav_u16(h+12); f->bits = wav_u16(h+14);
            if (f->tag == 0xfffe) {
                static const u8 guid_tail[14] = {0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if (chunk < 40 || wav_u16(h+16) < 22 || memcmp(h+26, guid_tail, sizeof(guid_tail))) return -NV_EINVAL;
                f->tag = wav_u16(h+24);
                if (!wav_u16(h+18) || wav_u16(h+18) > f->bits) return -NV_EINVAL;
            }
            if (f->channels < 1 || f->channels > 8 || f->rate < 8000 || f->rate > 384000 ||
                (f->tag != 1 && f->tag != 3) ||
                (f->tag == 1 && f->bits != 8 && f->bits != 16 && f->bits != 24 && f->bits != 32) ||
                (f->tag == 3 && f->bits != 32) || f->frame != f->channels*(f->bits/8) ||
                wav_u32(h+8) != f->rate*f->frame) return -NV_EINVAL;
            format = true;
        } else if (isdata) {
            if (data) return -NV_EINVAL;
            f->data = at+8; f->bytes = chunk; data = true;
        }
        at += 8+chunk+(chunk&1);
    }
    if (!format || !data || (rf64 && !ds64) || f->bytes%f->frame) return -NV_EINVAL;
    return io->seek(io->context, f->data);
}
static i32 wav_sample(const u8 *p, u32 bits, u32 tag) {
    if (tag == 3) {
        float value; u32 raw = wav_u32(p); memcpy(&value, &raw, sizeof(value));
        if (value != value) return 0;
        if (value >= 1.0f) return 32767;
        if (value <= -1.0f) return -32768;
        return (i32)(value*32767.0f);
    }
    if (bits == 8) return ((i32)p[0]-128)*256;
    if (bits == 16) return (short)wav_u16(p);
    if (bits == 24) return (short)((u16)p[1] | (u16)p[2]<<8);
    return (i32)wav_u32(p) >> 16;
}
#endif
