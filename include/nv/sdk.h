#ifndef NV_SDK_H
#define NV_SDK_H
#include <nv/abi.h>
/* Public x86-64 freestanding application entry points. Compile without a red
 * zone, link user/start64.S and user/linker64.ld; see docs/SDK.md. Existing
 * call IDs and ABI 1 records remain valid. This header needs no libc. */
#ifndef __x86_64__
#error "Nuvora application SDK currently supports only x86-64"
#endif
static inline int nv_syscall(u32 op, u32 a, u32 b, u32 c) {
    int result;
    __asm__ volatile("int $0x81" : "=a"(result) : "0"(op), "b"(a), "c"(b), "d"(c)
                     : "memory", "cc");
    return result;
}
static inline int nv_display_info(struct nv_display_info *out) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_INFO, (u32)(uptr)out);
}
static inline int nv_display_acquire(void) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_ACQUIRE, 0);
}
static inline int nv_display_present(const struct nv_display_present *rect) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_PRESENT, (u32)(uptr)rect);
}
static inline int nv_display_release(void) {
    return nv_syscall(NV_DEVCTL, NV_SUB_DISPLAY, NV_DISPLAY_RELEASE, 0);
}
/* Pixel words have a fixed meaning irrespective of the firmware's channel
 * order; applications convert once when writing their own buffer. */
static inline u32 nv_display_rgb(u32 format, u32 rgb) {
    rgb &= 0xffffffu;
    return format == NV_DISPLAY_RGBX8 ?
        ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | (rgb >> 16) : rgb;
}
#endif
