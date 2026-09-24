#ifndef NV_PAGE_BUDDY_H
#define NV_PAGE_BUDDY_H
#include <nv/types.h>

/* Free blocks of 2^order 4 KiB pages. The caller's busy bitmap is order 0;
 * one bit for each higher-order aligned block records whether both children
 * are free. Metadata is one bit per managed page, even with sparse RAM. */
#define NV_BUDDY_MAX_ORDER 18u
struct nv_page_buddy {
    const u32 *busy;
    u8 *levels;
    u32 pages;
    u32 offset[NV_BUDDY_MAX_ORDER + 1];
};
usize nv_buddy_bytes(u32 pages);
void nv_buddy_init(struct nv_page_buddy *, const u32 *busy, u8 *levels, u32 pages);
void nv_buddy_refresh(struct nv_page_buddy *, u32 first, u32 count);
u32 nv_buddy_find(const struct nv_page_buddy *, u32 order, u32 first, u32 limit);
#endif
