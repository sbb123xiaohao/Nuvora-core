#ifndef NV_MEDIA_STREAM_H
#define NV_MEDIA_STREAM_H
/* Shared by the guest player and the host decoder regression. No stdio or
 * whole-file allocation is needed, even when the file exceeds 4 GiB. */
struct media_source {
    void *context;
    int (*read)(void *, void *, u32);
    int (*seek)(void *, u64);
    u64 position, length;
    int error;
};
static void media_stream_load(plm_buffer_t *buffer, void *user) {
    struct media_source *s = user;
    u8 data[4096];
    for (u32 i = 0; i < 16; ++i) {
        int n = s->error < 0 ? s->error : s->read(s->context, data, sizeof(data));
        if (n <= 0) {
            if (n < 0) s->error = n;
            plm_buffer_signal_end(buffer); return;
        }
        s->position += (u32)n;
        plm_buffer_write(buffer, data, (size_t)n);
    }
}
static void media_stream_seek(plm_buffer_t *buffer, size_t offset, void *user) {
    (void)buffer;
    struct media_source *s = user;
    int r = s->seek(s->context, (u64)offset);
    if (r < 0) s->error = r; else s->position = offset;
}
static size_t media_stream_tell(plm_buffer_t *buffer, void *user) {
    (void)buffer;
    return (size_t)((struct media_source *)user)->position;
}
#endif
