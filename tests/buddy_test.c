#include <assert.h>
#include <stdio.h>
#include <nv/page_buddy.h>

enum { PAGES = 512, STEPS = 4000, SLOTS = 80 };
static u32 busy[PAGES / 32];
static u8 levels[PAGES / 8];
static struct nv_page_buddy buddy;
static u32 state = 0x31ade93fu;
static u32 random32(void) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
static u32 brute(u32 order, u32 first, u32 limit) {
    u32 count = 1u << order;
    for (u32 p = 0; p + count <= limit; p += count) {
        if (p < first) continue;
        bool free = true;
        for (u32 j = p; j < p + count; ++j)
            if (busy[j / 32] & (1u << (j % 32))) { free = false; break; }
        if (free) return p;
    }
    return PAGES;
}
static void mark(u32 first, u32 count, bool taken) {
    for (u32 p = first; p < first + count; ++p) {
        u32 mask = 1u << (p % 32);
        if (taken) { assert(!(busy[p / 32] & mask)); busy[p / 32] |= mask; }
        else { assert(busy[p / 32] & mask); busy[p / 32] &= ~mask; }
    }
    nv_buddy_refresh(&buddy, first, count);
}
int main(void) {
    struct { u32 first, count; } slots[SLOTS] = {0};
    assert(nv_buddy_bytes(PAGES) <= sizeof(levels));
    /* A sparse physical map with protected and permanently reserved pages. */
    busy[0] |= 1; busy[31 / 32] |= 1u << (31 % 32);
    for (u32 p = 233; p < 246; ++p) busy[p / 32] |= 1u << (p % 32);
    nv_buddy_init(&buddy, busy, levels, PAGES);
    for (u32 step = 0; step < STEPS; ++step) {
        u32 slot = random32() % SLOTS;
        if (slots[slot].count) {
            mark(slots[slot].first, slots[slot].count, false);
            slots[slot].count = 0;
        } else {
            u32 order = random32() % 7;
            u32 first = random32() % 128;
            u32 limit = 256 + random32() % 257;
            u32 got = nv_buddy_find(&buddy, order, first, limit);
            assert(got == brute(order, first, limit));
            if (got < PAGES) {
                slots[slot].first = got;
                slots[slot].count = 1u << order;
                mark(got, slots[slot].count, true);
            }
        }
        if (!(step % 17))
            for (u32 order = 0; order <= 9; ++order) {
                u32 first = random32() % PAGES;
                assert(nv_buddy_find(&buddy, order, first, PAGES) ==
                       brute(order, first, PAGES));
            }
    }
    for (u32 slot = 0; slot < SLOTS; ++slot)
        if (slots[slot].count) mark(slots[slot].first, slots[slot].count, false);
    assert(nv_buddy_find(&buddy, 4, 32, 48) == 32);
    assert(nv_buddy_find(&buddy, 9, 0, PAGES) == PAGES);
    puts("PASS buddy: 4000 fragmented allocations, coalescence, bounds and sparse reservations");
}
