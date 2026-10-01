#ifdef __x86_64__
#include "kernel.h"
#define ADDRESS P_ADDRESS
#define NX (1ull << 63)
/* 4 KiB pages with per-range permissions cover the kernel image region; the
 * rest of managed RAM uses 2 MiB pages. LOW_COVER must always stay above
 * kernel_end (checked at boot). */
#define LOW_TABLES 16u
#define LOW_COVER (LOW_TABLES * 512u * PAGE)
#define HIGH_PD_COUNT ((PHYS_LIMIT - (1u << 30)) / (1u << 30))
static pte_t pml4[512] ALIGNED(PAGE), pdpt[512] ALIGNED(PAGE), pd0[512] ALIGNED(PAGE);
static pte_t kernel_pt[LOW_TABLES][512] ALIGNED(PAGE);
static pte_t pd_high[HIGH_PD_COUNT][512] ALIGNED(PAGE);
static pte_t heap_pdpt[512] ALIGNED(PAGE), heap_pd[512] ALIGNED(PAGE);
static pte_t heap_pt[KHEAP_SIZE / (512u * PAGE)][512] ALIGNED(PAGE);
_Static_assert(KHEAP_SIZE % (512u * PAGE) == 0, "heap occupies complete page tables");
static pte_t fb_pdpt[512] ALIGNED(PAGE), fb_pd[512] ALIGNED(PAGE);
static pte_t fb_pt[FB_WINDOW_PAGES / 512][512] ALIGNED(PAGE);
pte_t stack_pt[512] ALIGNED(PAGE);
pte_t mmio_pt[512] ALIGNED(PAGE);
pte_t *kernel_pd = pml4;
bool fb_window_mapped;
extern u8 kernel_begin[], kernel_text_end[], kernel_ro_end[], kernel_end[];
static pte_t *table(pte_t entry) {
    return phys_ptr((uptr)(entry & ADDRESS));
}
void memory_reserve(u64 base, u64 length);
void vm_kernel_init(void) {
    if ((uptr)kernel_end >= LOW_COVER)
        panic("kernel image exceeds the low page-table cover");
    pml4[0] = (uptr)pdpt | 3;
    pml4[(PHYS_WINDOW >> 39) & 511] = (uptr)pdpt | 3;
    pdpt[0] = (uptr)pd0 | 3;
    for (u32 i = 0; i < LOW_TABLES; ++i) {
        pd0[i] = (uptr)kernel_pt[i] | 3;
        for (u32 j = 0; j < 512; ++j) {
            uptr p = (i * 512u + j) * PAGE;
            pte_t flags = 3 | NX;
            if (p >= (uptr)kernel_begin && p < (uptr)kernel_text_end)
                flags = 1;
            else if (p >= (uptr)kernel_text_end && p < (uptr)kernel_ro_end)
                flags = 1 | NX;
            kernel_pt[i][j] = p ? p | flags : 0;
        }
    }
    for (u32 i = LOW_TABLES; i < 512; ++i)
        pd0[i] = ((uptr)i << 21) | 0x83 | NX; /* 2 MiB pages, 32 MiB .. 1 GiB */
    for (u32 g = 0; g < HIGH_PD_COUNT; ++g) {
        pdpt[1 + g] = (uptr)pd_high[g] | 3;
        for (u32 j = 0; j < 512; ++j)
            pd_high[g][j] = (0x40000000ull + ((u64)g << 30) + ((u64)j << 21)) | 0x83 | NX;
    }
    pd0[KSTACK_BASE >> 21] = (uptr)stack_pt | 3;
    pd0[MMIO_BASE >> 21] = (uptr)mmio_pt | 3;
    /* These virtual windows replace entire 2 MiB identity mappings. */
    memory_reserve(KSTACK_BASE, 1u << 21);
    memory_reserve(MMIO_BASE, 1u << 21);
    /* Map the firmware framebuffer into the fixed supervisor window and take
     * the covered physical RAM out of the allocator. */
    const struct boot_framebuffer *fb = boot_info ? &boot_info->fb : NULL;
    u32 fb_offset = fb ? (u32)fb->address & (PAGE - 1u) : 0;
    if (fb && fb->format != NV_FB_NONE && fb->pitch && fb->height &&
        (u64)fb->pitch * fb->height + fb_offset <= FB_WINDOW_PAGES * PAGE &&
        fb->address <= (1ull << 52) - FB_WINDOW_PAGES * PAGE) {
        u32 pages = (u32)(((u64)fb->pitch * fb->height + fb_offset + PAGE - 1) / PAGE);
        pml4[(FB_WINDOW >> 39) & 511] = (uptr)fb_pdpt | 3;
        fb_pdpt[0] = (uptr)fb_pd | 3;
        for (u32 s = 0; s < (pages + 511) / 512; ++s) {
            fb_pd[s] = (uptr)fb_pt[s] | 3;
            for (u32 j = 0; j < 512 && s * 512 + j < pages; ++j)
                fb_pt[s][j] = (fb->address - fb_offset + ((u64)s * 512u + j) * PAGE) | 3 | 0x18 | NX;
        }
        fb_window_mapped = true;
    }
    load_cr3((uptr)pml4);
    uptr cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    cr0 |= 0x1000c;
    __asm__ volatile("mov %0,%%cr0" ::"r"(cr0) : "memory");
}
/* Kernel heap has contiguous virtual addresses, not a contiguous physical
 * allocation requirement. Do not publish or pin partial allocations. */
void *vm_heap_create(void) {
    if (pml4[(KHEAP_WINDOW >> 39) & 511] & P_PRESENT)
        panic("kernel heap already mapped");
    u32 count = 0;
    while (count < KHEAP_SIZE / PAGE) {
        uptr p = page_alloc();
        if (!p) {
            while (count) {
                --count;
                pte_t *entry = &heap_pt[count / 512][count % 512];
                page_free((uptr)(*entry & ADDRESS));
                *entry = 0;
            }
            return NULL;
        }
        heap_pt[count / 512][count % 512] = p | P_PRESENT | P_WRITE | NX;
        ++count;
    }
    for (u32 i = 0; i < ARRAY_LEN(heap_pt); ++i)
        heap_pd[i] = (uptr)heap_pt[i] | P_PRESENT | P_WRITE;
    heap_pdpt[0] = (uptr)heap_pd | P_PRESENT | P_WRITE;
    pml4[(KHEAP_WINDOW >> 39) & 511] = (uptr)heap_pdpt | P_PRESENT | P_WRITE;
    for (u32 i = 0; i < count; ++i)
        page_pin((uptr)(heap_pt[i / 512][i % 512] & ADDRESS));
    load_cr3((uptr)kernel_pd);
    return (void *)KHEAP_WINDOW;
}
pte_t *vm_create(void) {
    uptr a = page_alloc();
    if (!a)
        return NULL;
    uptr b = page_alloc();
    if (!b) {
        page_free(a);
        return NULL;
    }
    pte_t *root = phys_ptr(a), *l3 = phys_ptr(b);
    root[0] = b | 7;
    root[(PHYS_WINDOW >> 39) & 511] = pml4[(PHYS_WINDOW >> 39) & 511];
    root[(KHEAP_WINDOW >> 39) & 511] = pml4[(KHEAP_WINDOW >> 39) & 511];
    root[(FB_WINDOW >> 39) & 511] = pml4[(FB_WINDOW >> 39) & 511];
    l3[0] = pdpt[0];
    return root;
}
/* Private page tables cover the entire lower canonical user half. Never
 * descend through a shared supervisor entry, including the boot mapping. */
#ifndef NV_VM_INVALIDATE
#ifdef NV_HOST_TEST
#define NV_VM_INVALIDATE(va) ((void)(va))
#else
#define NV_VM_INVALIDATE(va) __asm__ volatile("invlpg (%0)" :: "r"((uptr)(va)) : "memory")
#endif
#endif
static pte_t *leaf(pte_t *root, uptr va, bool create) {
    if (!root || !nv_user_address(va)) return NULL;
    pte_t *level = root;
    for (u32 shift = 39; shift > 12; shift -= 9) {
        pte_t *entry = &level[(va >> shift) & 511];
        if (!(*entry & P_PRESENT)) {
            if (!create) return NULL;
            uptr p = page_alloc();
            if (!p) return NULL;
            *entry = p | P_PRESENT | P_WRITE | P_USER;
        }
        if ((*entry & (P_PRESENT | P_USER | 0x80)) != (P_PRESENT | P_USER))
            return NULL;
        level = table(*entry);
    }
    return &level[(va >> 12) & 511];
}
int vm_map(pte_t *root, uptr va, u32 flags) {
    if (!root || !nv_user_address(va) || va % PAGE ||
        (flags & (P_WRITE | P_EXEC)) == (P_WRITE | P_EXEC)) return -NV_EINVAL;
    pte_t *p = leaf(root, va, true);
    if (!p) { vm_unmap(root, va); return -NV_ENOMEM; }
    if (*p & P_PRESENT) return -NV_EEXIST;
    uptr physical = page_alloc();
    if (!physical) { vm_unmap(root, va); return -NV_ENOMEM; }
    *p = physical | P_PRESENT | P_USER | (flags & P_WRITE) |
         ((flags & P_EXEC) ? 0 : NX);
    return 0;
}
uptr vm_translate(pte_t *root, uptr va) {
    pte_t *p = leaf(root, va, false);
    return p && (*p & (P_PRESENT | P_USER)) == (P_PRESENT | P_USER) ?
        (uptr)(*p & ADDRESS) + (va & (PAGE - 1)) : 0;
}
static bool empty(pte_t *t) {
    for (u32 i = 0; i < 512; ++i) if (t[i] & P_PRESENT) return false;
    return true;
}
void vm_unmap(pte_t *root, uptr va) {
    if (!root || !nv_user_address(va)) return;
    pte_t *levels[4] = {root}, *entries[3];
    u32 depth = 0;
    for (u32 shift = 39; shift > 12; shift -= 9) {
        pte_t *entry = &levels[depth][(va >> shift) & 511];
        if ((*entry & (P_PRESENT | P_USER | 0x80)) != (P_PRESENT | P_USER)) break;
        entries[depth] = entry;
        levels[++depth] = table(*entry);
    }
    if (depth == 3) {
        pte_t *entry = &levels[3][(va >> 12) & 511];
        if ((*entry & (P_PRESENT | P_USER)) == (P_PRESENT | P_USER)) {
            page_free((uptr)(*entry & ADDRESS)); *entry = 0;
            NV_VM_INVALIDATE(va);
        }
    }
    while (depth && empty(levels[depth])) {
        page_free(ptr_phys(levels[depth]));
        *entries[--depth] = 0;
    }
}
static u64 walk_user(pte_t *level, u32 depth, bool destroy) {
    u64 count = 0;
    for (u32 i = 0; i < 512; ++i) {
        pte_t entry = level[i];
        if ((entry & (P_PRESENT | P_USER)) != (P_PRESENT | P_USER)) continue;
        if (depth == 1) ++count;
        else count += walk_user(table(entry), depth - 1, destroy);
        if (destroy) { page_free((uptr)(entry & ADDRESS)); level[i] = 0; }
    }
    return count;
}
void vm_destroy(pte_t *root) {
    if (!root) return;
    walk_user(root, 4, true);
    page_free(ptr_phys(root));
}
bool user_range(pte_t *root, uptr va, usize len, bool write) {
    if (!len) return true;
    if (!root || !nv_user_bounds(va, len)) return false;
    pte_t mask = P_PRESENT | P_USER | (write ? P_WRITE : 0);
    uptr last = (va + len - 1) & ~(uptr)(PAGE - 1);
    for (uptr address = va & ~(uptr)(PAGE - 1);; address += PAGE) {
        pte_t *level = root;
        for (u32 shift = 39; shift > 12; shift -= 9) {
            pte_t entry = level[(address >> shift) & 511];
            if ((entry & (mask | 0x80)) != mask) return false;
            level = table(entry);
        }
        if ((level[(address >> 12) & 511] & mask) != mask) return false;
        if (address == last) return true;
    }
}
u64 vm_page_count(pte_t *root) { return root ? walk_user(root, 4, false) : 0; }
#endif
