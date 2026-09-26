#ifndef NV_MEDIA_FLAC_H
#define NV_MEDIA_FLAC_H
static size_t flac_read(void *user, void *out, size_t bytes) {
    struct media_source *s = user; size_t total = 0;
    while (total < bytes) {
        int n = s->read(s->context, (u8 *)out+total, (u32)MIN(bytes-total, 16384));
        if (n <= 0) { if (n < 0) s->error = n; break; }
        total += (u32)n; s->position += (u32)n;
    }
    return total;
}
static drflac_bool32 flac_seek(void *user, int offset, drflac_seek_origin origin) {
    struct media_source *s = user;
    if (origin != DRFLAC_SEEK_SET && origin != DRFLAC_SEEK_CUR) return 0;
    if (offset < 0 && (u64)(-(i64)offset) > s->position && origin == DRFLAC_SEEK_CUR) return 0;
    u64 at = origin == DRFLAC_SEEK_SET ? (u64)(i64)offset : s->position+(i64)offset;
    if (at > s->length) return 0;
    int r = s->seek(s->context, at);
    if (r < 0) { s->error = r; return 0; }
    s->position = at; return 1;
}
static drflac_bool32 flac_tell(void *user, drflac_int64 *pos) {
    *pos = (drflac_int64)((struct media_source *)user)->position; return 1;
}
#endif
