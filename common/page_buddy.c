#include <nv/page_buddy.h>
#include <nv/string.h>

static bool free_block(const struct nv_page_buddy *b, u32 order, u32 index) {
    if (!order)
        return !(b->busy[index / 32] & (1u << (index % 32)));
    u32 bit = b->offset[order] + index;
    return !!(b->levels[bit / 8] & (1u << (bit % 8)));
}

static void set_block(struct nv_page_buddy *b, u32 order, u32 index, bool free) {
    u32 bit = b->offset[order] + index;
    u8 mask = (u8)(1u << (bit % 8));
    if (free) b->levels[bit / 8] |= mask;
    else b->levels[bit / 8] &= (u8)~mask;
}

usize nv_buddy_bytes(u32 pages) {
    u32 bits = 0;
    for (u32 order = 1; order <= NV_BUDDY_MAX_ORDER; ++order)
        bits += pages >> order;
    return ((usize)bits + 7) / 8;
}

void nv_buddy_init(struct nv_page_buddy *b, const u32 *busy, u8 *levels, u32 pages) {
    b->busy = busy;
    b->levels = levels;
    b->pages = pages;
    u32 bits = 0;
    for (u32 order = 1; order <= NV_BUDDY_MAX_ORDER; ++order) {
        b->offset[order] = bits;
        bits += pages >> order;
    }
    memset(levels, 0, nv_buddy_bytes(pages));
    for (u32 order = 1; order <= NV_BUDDY_MAX_ORDER; ++order)
        for (u32 i = 0; i < pages >> order; ++i)
            if (free_block(b, order - 1, i * 2) &&
                free_block(b, order - 1, i * 2 + 1))
                set_block(b, order, i, true);
}

void nv_buddy_refresh(struct nv_page_buddy *b, u32 first, u32 count) {
    if (!count || first >= b->pages || count > b->pages - first)
        return;
    u32 last = first + count - 1;
    for (u32 order = 1; order <= NV_BUDDY_MAX_ORDER; ++order) {
        u32 end = MIN(last >> order, (b->pages >> order) ?
                      (b->pages >> order) - 1 : 0);
        if (first >> order >= b->pages >> order) continue;
        for (u32 i = first >> order; i <= end; ++i)
            set_block(b, order, i,
                      free_block(b, order - 1, i * 2) &&
                      free_block(b, order - 1, i * 2 + 1));
    }
}

u32 nv_buddy_find(const struct nv_page_buddy *b, u32 order, u32 first, u32 limit) {
    if (order > NV_BUDDY_MAX_ORDER || limit > b->pages || first >= limit)
        return b->pages;
    u32 block = 1u << order;
    u32 begin = first / block + !!(first % block);
    u32 end = MIN(limit / block, b->pages >> order);
    if (begin >= end) return b->pages;
    if (!order) {
        for (u32 w = begin / 32; w <= (end - 1) / 32; ++w) {
            u32 bits = ~b->busy[w];
            if (w == begin / 32) bits &= 0xffffffffu << (begin % 32);
            if (w == (end - 1) / 32)
                bits &= 0xffffffffu >> (31 - (end - 1) % 32);
            if (bits) return w * 32 + (u32)__builtin_ctz(bits);
        }
    } else {
        u32 low = b->offset[order] + begin;
        u32 high = b->offset[order] + end;
        for (u32 byte = low / 8; byte <= (high - 1) / 8; ++byte) {
            u32 bits = b->levels[byte];
            if (byte == low / 8) bits &= 0xffu << (low % 8);
            if (byte == (high - 1) / 8)
                bits &= 0xffu >> (7 - (high - 1) % 8);
            if (bits)
                return ((byte * 8 + (u32)__builtin_ctz(bits)) - b->offset[order]) * block;
        }
    }
    return b->pages;
}
