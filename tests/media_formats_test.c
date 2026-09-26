#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define MINIMP3_NO_SIMD
#define PL_MPEG_IMPLEMENTATION
#include <nv/abi.h>
#include <nv/string.h>
#include "../third_party/pl_mpeg.h"
#define DR_FLAC_NO_SIMD
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_flac.h"
#include "../user/media_stream.h"
#include "../user/media_flac.h"
#include "../user/wav_reader.h"
static int file_read(void *ctx, void *out, u32 bytes) { return (int)fread(out, 1, bytes, ctx); }
static int file_seek(void *ctx, u64 at) { return fseek(ctx, (long)at, SEEK_SET) ? -NV_EIO : 0; }
static u64 filesize(FILE *f) { assert(!fseek(f, 0, SEEK_END)); u64 n = (u64)ftell(f); rewind(f); return n; }
static void flac_fixture(const char *path) {
    FILE *f = fopen(path, "rb"); assert(f);
    struct media_source source = {.context=f, .read=file_read, .seek=file_seek, .length=filesize(f)};
    drflac *dec = drflac_open(flac_read, flac_seek, flac_tell, &source, NULL); assert(dec);
    assert(dec->channels == 2 && dec->sampleRate == 44100 && dec->bitsPerSample == 24);
    short pcm[2048]; u64 frames = 0, energy = 0;
    for (;;) {
        u64 n = drflac_read_pcm_frames_s16(dec, 1024, pcm); if (!n) break;
        frames += n;
        for (u32 i = 0; i < n*2; ++i) energy += (u64)abs(pcm[i]);
    }
    assert(frames == dec->totalPCMFrameCount && frames > 15000 && energy > 100000 && !source.error);
    assert(!flac_seek(&source, 0, DRFLAC_SEEK_END));
    drflac_close(dec); fclose(f);
}
struct video_frames { u32 count; };
static void got_video(plm_t *p, plm_frame_t *f, void *ctx) {
    (void)p; assert(f->width == 1280 && f->height == 720);
    ++((struct video_frames *)ctx)->count;
}
static void streaming_video(const char *path) {
    FILE *f = fopen(path, "rb"); assert(f); u64 size = filesize(f);
    struct media_source s = {.context=f, .read=file_read, .seek=file_seek, .length=size};
    plm_buffer_t *b = plm_buffer_create_with_callbacks(media_stream_load, media_stream_seek, media_stream_tell, (size_t)size, &s);
    plm_t *p = plm_create_with_buffer(b, 1); assert(p && plm_get_width(p) == 1280);
    struct video_frames v = {0}; plm_set_video_decode_callback(p, got_video, &v); plm_set_audio_enabled(p, 0);
    for (u32 i = 0; i < 100 && !plm_has_ended(p); ++i) plm_decode(p, 0.04);
    assert(v.count == 11 && plm_has_ended(p) && !s.error && b->capacity <= 256*1024);
    /* The callbacks preserve 64-bit offsets instead of truncating to int. */
    media_stream_seek(b, 5ull*1024*1024*1024+7, &s);
    assert(!s.error && media_stream_tell(b, &s) == 5ull*1024*1024*1024+7);
    plm_destroy(p); fclose(f);
}
struct fake_wav { u8 header[128]; u64 pos, size; };
static int wav_read_fake(void *ctx, void *out, u32 n) {
    struct fake_wav *f = ctx;
    if (f->pos >= f->size) return 0;
    n = (u32)MIN(n, f->size-f->pos); memset(out, 0, n);
    if (f->pos < sizeof(f->header)) memcpy(out, f->header+f->pos, (u32)MIN(n, sizeof(f->header)-f->pos));
    f->pos += n; return (int)n;
}
static int wav_seek_fake(void *ctx, u64 p) { ((struct fake_wav *)ctx)->pos=p; return 0; }
static void put16(u8 *p, u16 n) { p[0]=(u8)n; p[1]=(u8)(n>>8); }
static void put32(u8 *p, u32 n) { for (u32 i=0;i<4;++i) p[i]=(u8)(n>>(i*8)); }
static void put64(u8 *p, u64 n) { put32(p,(u32)n);put32(p+4,(u32)(n>>32)); }
static void wav_fixtures(void) {
    struct fake_wav fake={0}; struct wav_io io={&fake,wav_read_fake,wav_seek_fake}; struct wav_format format;
    for (u32 channels=1; channels<=8; ++channels)
        for (u32 bits=8; bits<=32; bits+=8) {
            memset(&fake,0,sizeof(fake)); u32 frame=channels*bits/8;
            fake.size=44+frame*2;
            memcpy(fake.header,"RIFF",4); put32(fake.header+4,(u32)fake.size-8); memcpy(fake.header+8,"WAVEfmt ",8);
            put32(fake.header+16,16); put16(fake.header+20,1);put16(fake.header+22,(u16)channels);
            put32(fake.header+24,44100);put32(fake.header+28,44100*frame);put16(fake.header+32,(u16)frame);put16(fake.header+34,(u16)bits);
            memcpy(fake.header+36,"data",4);put32(fake.header+40,frame*2);
            assert(!wav_open(&io,fake.size,&format) && format.channels==channels && format.bits==bits && format.data==44);
        }
    u8 raw[]={0,0,0,0}; assert(wav_sample(raw,8,1)==-32768);
    raw[1]=0x80;assert(wav_sample(raw,16,1)==-32768);
    raw[1]=0;raw[2]=0x80;assert(wav_sample(raw,24,1)==-32768);
    put32(raw,0x7fc00000);assert(wav_sample(raw,32,3)==0); /* NaN */
    put32(raw,0x7f800000);assert(wav_sample(raw,32,3)==32767);
    put32(raw,0xbf800000);assert(wav_sample(raw,32,3)==-32768);
    memset(&fake,0,sizeof(fake));u64 data=8ull*1024*1024*1024;
    fake.size=data+80;memcpy(fake.header,"RF64",4);put32(fake.header+4,0xffffffff);
    memcpy(fake.header+8,"WAVEds64",8);put32(fake.header+16,28);put64(fake.header+20,data+72);put64(fake.header+28,data);
    memcpy(fake.header+48,"fmt ",4);put32(fake.header+52,16);put16(fake.header+56,1);put16(fake.header+58,2);
    put32(fake.header+60,48000);put32(fake.header+64,192000);put16(fake.header+68,4);put16(fake.header+70,16);
    memcpy(fake.header+72,"data",4);put32(fake.header+76,0xffffffff);
    assert(!wav_open(&io,fake.size,&format) && format.bytes==data && format.data==80);
    put64(fake.header+28,data+512);assert(wav_open(&io,fake.size,&format)==-NV_EINVAL);
}
int main(int argc,char **argv) {
    assert(argc==3);wav_fixtures();flac_fixture(argv[1]);streaming_video(argv[2]);
    puts("PASS media formats: streamed 24-bit FLAC, 720p MPEG with bounded input buffer, 64-bit callbacks, WAV 8/16/24/32-bit and 8 GiB RF64 bounds");
}
