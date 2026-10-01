#ifndef NV_ADDRESS_H
#define NV_ADDRESS_H
#include <nv/types.h>
/* Four-level x86-64 paging: the lower canonical half belongs to applications.
 * The first GiB remains supervisor-only for the boot image and kernel stacks.
 * Legacy ELF64 applications retain their original image, heap and stack. */
#define NV_USER_MIN 0x40000000ull
#define NV_USER_END 0x0000800000000000ull
#define NV_USER_IMAGE 0x0000000100000000ull
#define NV_USER_IMAGE_END 0x0000001000000000ull
#define NV_USER_HEAP 0x0000001000000000ull
#define NV_USER_HEAP_END 0x0000400000000000ull
#define NV_USER_STACK 0x00007ffffff00000ull
#ifdef NV_HOST_TEST
/* A host process cannot map the supervisor canonical half. Fixtures use
 * equivalent aliases in its lower half, while exercising the same walkers. */
#define NV_PHYS_WINDOW (1ull << 39)
#define NV_KHEAP_WINDOW (2ull << 39)
#define NV_FB_WINDOW (3ull << 39)
#else
#define NV_PHYS_WINDOW 0xffff800000000000ull
#define NV_KHEAP_WINDOW 0xffffc00000000000ull
#define NV_FB_WINDOW 0xffffc08000000000ull
#endif
static inline bool nv_user_address(uptr address) {
    return address >= NV_USER_MIN && address < NV_USER_END;
}
static inline bool nv_user_bounds(uptr address, usize length) {
    return !length || (nv_user_address(address) && length <= NV_USER_END - address);
}
#endif
