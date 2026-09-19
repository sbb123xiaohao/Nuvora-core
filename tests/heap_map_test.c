#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"

enum { HEAP_PAGES = KHEAP_SIZE / PAGE, POOL_PAGES = HEAP_PAGES + 2 };
static bool owned[POOL_PAGES], pinned[POOL_PAGES];
static u32 budget, issued, live, pin_count, switches;
static const uptr pool_base = 0x2000000u;
static uptr fixture_page_alloc(void) {
    if (issued == budget) return 0;
    u32 i = issued++;
    assert(i < POOL_PAGES && !owned[i] && !pinned[i]);
    owned[i] = true; ++live;
    uptr p = pool_base + (uptr)i * 2 * PAGE;
    memset(phys_ptr(p), 0, PAGE);
    return p;
}
static u32 index_of(uptr p) {
    assert(p >= pool_base && (p - pool_base) % (2 * PAGE) == 0);
    u32 i = (p - pool_base) / (2 * PAGE);
    assert(i < POOL_PAGES && owned[i] && !pinned[i]);
    return i;
}
static void fixture_page_free(uptr p) { owned[index_of(p)] = false; --live; }
static void fixture_page_pin(uptr p) {
    u32 i = index_of(p); owned[i] = false; pinned[i] = true; ++pin_count;
}
static void fixture_load_cr3(uptr p) { assert(p == (uptr)kernel_pd); ++switches; }
#define page_alloc fixture_page_alloc
#define page_free fixture_page_free
#define page_pin fixture_page_pin
#define load_cr3 fixture_load_cr3
#include "../kernel/memory64.c"
void panic(const char *message) { fprintf(stderr, "PANIC: %s\n", message); abort(); }
static void reset(u32 n) {
    memset(owned,0,sizeof(owned)); memset(pinned,0,sizeof(pinned));
    memset(pml4,0,sizeof(pml4)); memset(heap_pdpt,0,sizeof(heap_pdpt));
    memset(heap_pd,0,sizeof(heap_pd)); memset(heap_pt,0,sizeof(heap_pt));
    budget=n; issued=live=pin_count=switches=0;
}
int main(void) {
    const usize length = POOL_PAGES * 2 * PAGE;
    void *pool = mmap((void *)(PHYS_WINDOW + pool_base), length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(pool == (void *)(PHYS_WINDOW + pool_base));
    const u32 failures[] = {0, 1, 17, HEAP_PAGES/2, HEAP_PAGES-1};
    for (u32 n=0;n<ARRAY_LEN(failures);++n) {
        reset(failures[n]);
        assert(vm_heap_create() == NULL && !live && !pin_count && !switches);
        assert(!pml4[KHEAP_WINDOW >> 39]);
        for (u32 i=0;i<HEAP_PAGES;++i) assert(!heap_pt[i/512][i%512]);
    }
    reset(POOL_PAGES);
    assert(vm_heap_create() == (void *)KHEAP_WINDOW);
    assert(live == HEAP_PAGES && pin_count == HEAP_PAGES && switches == 1);
    assert((pml4[KHEAP_WINDOW >> 39] & 7) == 3 && (heap_pdpt[0] & 7) == 3);
    for (u32 i=0;i<HEAP_PAGES;++i) {
        pte_t entry=heap_pt[i/512][i%512];
        assert((entry & P_ADDRESS) == pool_base + (uptr)i*2*PAGE);
        assert((entry & (NX | 7)) == (NX | 3));
        assert((heap_pd[i/512] & 7) == 3 && pinned[i] && !owned[i]);
    }
    pte_t *user = vm_create(); assert(user);
    assert(user[KHEAP_WINDOW >> 39] == pml4[KHEAP_WINDOW >> 39]);
    assert(!(user[KHEAP_WINDOW >> 39] & P_USER));
    vm_destroy(user);
    assert(live == HEAP_PAGES && pin_count == HEAP_PAGES);
    assert(!munmap(pool,length));
    puts("PASS heap mapping: fragmented backing, five OOM rollback points, pinned NX supervisor pages");
}
