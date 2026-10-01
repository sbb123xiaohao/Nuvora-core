#include "kernel.h"
int user_string(uptr ptr, char *out, u32 cap) {
    for (u32 i = 0; i < cap; ++i) {
        if (ptr > (uptr)-1 - i || !user_range(current->pd, ptr + i, 1, false))
            return -NV_EFAULT;
        out[i] = *(const char *)(uptr)(ptr + i);
        if (!out[i])
            return 0;
    }
    return -NV_E2BIG;
}
int copy_to_space(pte_t *pd, uptr va, const void *src, usize len) {
    if (!nv_user_bounds(va, len)) return -NV_EFAULT;
    const u8 *s = src;
    while (len) {
        uptr p = vm_translate(pd, va);
        if (!p)
            return -NV_EFAULT;
        u32 n = MIN(len, PAGE - (va & 4095));
        memcpy(phys_ptr(p), s, n);
        s += n;
        va += n;
        len -= n;
    }
    return 0;
}
