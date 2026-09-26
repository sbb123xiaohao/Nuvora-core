/* Exercise the actual xHCI DMA layout, including a two-page pointer table
 * and failure rollback. The physical alias matches the kernel's RAM window. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../kernel/kernel.h"

enum { DMA_PAGES = 1100 };
static const uptr dma_base = 0x08000000u;
static bool used[DMA_PAGES];
static u32 live, allocated, fail_after;
volatile u32 ticks;
uptr page_alloc_below(u64 limit) {
    assert(limit == 0x100000000ull);
    if (allocated >= fail_after) return 0;
    for (u32 i = 1; i < DMA_PAGES; ++i)
        if (!used[i]) {
            used[i] = true;
            ++allocated;
            ++live;
            uptr p = dma_base + i * PAGE;
            memset(phys_ptr(p), 0, PAGE);
            return p;
        }
    return 0;
}
uptr page_alloc_run_below(u32 count, u64 limit) {
    assert(limit == 0x100000000ull && count <= 2);
    if (allocated + count > fail_after) return 0;
    for (u32 i = 1; i + count <= DMA_PAGES; ++i) {
        bool free = true;
        for (u32 j = 0; j < count; ++j) free &= !used[i + j];
        if (!free) continue;
        for (u32 j = 0; j < count; ++j) used[i + j] = true;
        allocated += count;
        live += count;
        uptr p = dma_base + i * PAGE;
        memset(phys_ptr(p), 0, count * PAGE);
        return p;
    }
    return 0;
}
void page_free(uptr p) {
    assert(p >= dma_base && (p - dma_base) % PAGE == 0);
    u32 i = (u32)((p - dma_base) / PAGE);
    assert(i < DMA_PAGES && used[i] && live);
    used[i] = false;
    --live;
}
#include "../kernel/usb.c"

static void check_ports(void) {
    static u32 regs[MMIO_SIZE / sizeof(u32)];
    struct host h = {.mmio = (volatile u8 *)regs};
    regs[0x14 / 4] = 0x2000;
    regs[0x18 / 4] = 0x1000;
    regs[0x100 / 4] = 2; /* Supported Protocol at extended capability offset. */
    regs[0x108 / 4] = 33 | 32u << 8;
    regs[0x10c / 4] = 3;
    assert(host_capabilities(&h, 0x01000020, 64u << 24 | 16, 0x40u << 16));
    assert(h.info.ports == 64 && h.slot_type[32] == 3 && h.slot_type[63] == 3);
    assert(h.slot_type[31] == 0);
    regs[0x108 / 4] = 255 | 1u << 8;
    assert(host_capabilities(&h, 0x01000020, 255u << 24 | 16, 0x40u << 16));
    assert(h.slot_type[254] == 3);
    regs[0x108 / 4] = 255 | 2u << 8;
    assert(!host_capabilities(&h, 0x01000020, 255u << 24 | 16, 0x40u << 16));
}

static void check(u32 count, u32 budget) {
    struct host h = {0};
    allocated = 0;
    fail_after = budget;
    h.dcbaa = (u32)page_alloc_below(0x100000000ull);
    h.scratch_count = count;
    bool success = budget >= count + 1 + (count > PAGE / sizeof(u64) ? 2u : 1u);
    assert(scratch_alloc(&h) == success);
    if (success) {
        assert(h.scratch_array_pages == (count > PAGE / sizeof(u64) ? 2u : 1u));
        u64 *table = phys_ptr(h.scratch_array);
        assert(((u64 *)phys_ptr(h.dcbaa))[0] == h.scratch_array);
        for (u32 i = 0; i < count; ++i) {
            assert(table[i] == h.scratch[i] && table[i] < 0x100000000ull);
            assert(used[(table[i] - dma_base) / PAGE]);
        }
    }
    scratch_free(&h);
    page_free(h.dcbaa);
    assert(!live && !h.scratch_array && !h.scratch_array_pages);
}
int main(void) {
    check_ports();
    void *mapping = mmap((void *)(PHYS_WINDOW + dma_base), DMA_PAGES * PAGE,
                         PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS |
                         MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapping == (void *)(PHYS_WINDOW + dma_base));
    check(33, DMA_PAGES);
    check(512, DMA_PAGES);
    check(513, DMA_PAGES);
    check(1023, DMA_PAGES);
    check(513, 1);
    check(513, 2);
    check(513, 3);
    check(1023, 70);
    assert(munmap(mapping, DMA_PAGES * PAGE) == 0);
    puts("PASS USB xHCI: 64/255 ports, 33/512/513/1023 scratchpads, DMA rollback");
}
