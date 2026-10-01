#ifndef NV_PIXEL_H
#define NV_PIXEL_H
#include <nv/types.h>
static inline bool nv_pixel_mask(u32 mask) {
    if (!mask) return false;
    while (!(mask & 1u)) mask >>= 1;
    return !(mask & (mask + 1u));
}
static inline bool nv_pixel_masks_valid(const u32 masks[4]) {
    return nv_pixel_mask(masks[0]) && nv_pixel_mask(masks[1]) &&
        nv_pixel_mask(masks[2]) && !(masks[0] & masks[1]) &&
        !(masks[0] & masks[2]) && !(masks[1] & masks[2]) &&
        !(masks[3] & (masks[0] | masks[1] | masks[2]));
}
static inline u32 nv_pixel_channel(u32 value, u32 mask) {
    u32 shift = (u32)__builtin_ctz(mask), maximum = mask >> shift;
    return (u32)(((u64)value * maximum + 127u) / 255u) << shift;
}
static inline u32 nv_pixel_pack(u32 rgb, const u32 masks[4]) {
    return nv_pixel_channel((rgb >> 16) & 255, masks[0]) |
        nv_pixel_channel((rgb >> 8) & 255, masks[1]) |
        nv_pixel_channel(rgb & 255, masks[2]);
}
#endif
