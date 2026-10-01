#ifndef NV_SDK_H
#define NV_SDK_H
#include <nv/abi.h>
/* Public x86-64 freestanding application entry points. Compile without a red
 * zone, link user/start64.S and user/linker64.ld; see docs/SDK.md. Existing
 * call IDs and ABI 1 records remain valid. This header needs no libc. */
#ifndef __x86_64__
#error "Nuvora application SDK currently supports only x86-64"
#endif
static inline iptr nv_syscall(u32 op, uptr a, uptr b, uptr c) {
    iptr result;
    __asm__ volatile("int $0x81" : "=a"(result) : "0"((uptr)op | NV_CALL_NATIVE), "b"(a), "c"(b), "d"(c)
                     : "memory", "cc");
    return result;
}
static inline int nv_seek_file64(int fd, i64 offset, u32 origin, u64 *position) {
    struct nv_seek64 io = {.offset=offset, .origin=origin};
    int r = nv_syscall(NV_SEEK64, (u32)fd, (uptr)&io, 0);
    if (!r && position) *position = io.position;
    return r;
}
static inline int nv_stat_file64(int fd, struct nv_stat64 *out) {
    return nv_syscall(NV_STAT64, (u32)fd, (uptr)out, 0);
}
static inline int nv_list_dir64(const char *path, u32 index, struct nv_dirent64 *out) {
    return nv_syscall(NV_LIST64, (uptr)path, index, (uptr)out);
}
static inline int nv_display_info(struct nv_display_info *out) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_INFO, (uptr)out);
}
static inline int nv_display_acquire(void) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_ACQUIRE, 0);
}
static inline int nv_display_present(const struct nv_display_present *rect) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_PRESENT64, (uptr)rect);
}
static inline int nv_display_release(void) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_RELEASE, 0);
}
static inline int nv_window_call(u32 op, void *io) {
    return nv_syscall(NV_DEVCTL, NV_SUB_WINDOW, op, (uptr)io);
}
static inline int nv_window_info(struct nv_window_info *out) {
    return nv_window_call(NV_WINDOW_INFO, out);
}
static inline int nv_window_create(struct nv_window_create *io) {
    return nv_window_call(NV_WINDOW_CREATE, io);
}
static inline int nv_window_destroy(u32 id) {
    struct nv_window_id io = {id};
    return nv_window_call(NV_WINDOW_DESTROY, &io);
}
static inline int nv_window_begin(const struct nv_window_frame *io) {
    return nv_window_call(NV_WINDOW_BEGIN, (void *)io);
}
static inline int nv_window_upload(const struct nv_window_pixels *io) {
    return nv_window_call(NV_WINDOW_UPLOAD64, (void *)io);
}
static inline int nv_window_commit(u32 id) {
    struct nv_window_id io = {id};
    return nv_window_call(NV_WINDOW_COMMIT, &io);
}
static inline int nv_window_poll(u32 id, struct nv_window_event *out) {
    out->id = id;
    return nv_window_call(NV_WINDOW_POLL, out);
}
static inline int nv_input_info(struct nv_input_info *out) {
    return nv_syscall(NV_DEVCTL, NV_SUB_INPUT, NV_INPUT_INFO, (uptr)out);
}
static inline int nv_pointer_poll(struct nv_pointer_event *out) {
    return nv_syscall(NV_DEVCTL, NV_SUB_INPUT, NV_INPUT_POINTER_POLL, (uptr)out);
}
static inline int nv_audio_info(struct nv_audio_info *out) {
    return nv_syscall(NV_DEVCTL, NV_SUB_AUDIO, NV_AUDIO_INFO, (uptr)out);
}
static inline int nv_audio_write(const void *samples, u32 bytes) {
    struct nv_audio_write request = {.pixels=(uptr)samples, .bytes=bytes};
    return nv_syscall(NV_DEVCTL, NV_SUB_AUDIO, NV_AUDIO_WRITE64, (uptr)&request);
}
static inline int nv_audio_get_volume(struct nv_audio_volume *out) {
    return nv_syscall(NV_DEVCTL, NV_SUB_AUDIO, NV_AUDIO_GET_VOLUME, (uptr)out);
}
static inline int nv_audio_set_volume(u32 percent) {
    struct nv_audio_volume request = {percent};
    return nv_syscall(NV_DEVCTL, NV_SUB_AUDIO, NV_AUDIO_SET_VOLUME, (uptr)&request);
}
/* Pixel words have a fixed meaning irrespective of the firmware's channel
 * order; applications convert once when writing their own buffer. */
static inline u32 nv_display_rgb(u32 format, u32 rgb) {
    rgb &= 0xffffffu;
    return format == NV_DISPLAY_RGBX8 ?
        ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | (rgb >> 16) : rgb;
}
#endif
