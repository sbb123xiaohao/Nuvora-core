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
static pte_t fb_pt[4][512] ALIGNED(PAGE); /* FB_WINDOW: 8 MiB framebuffer aperture */
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
    pml4[PHYS_WINDOW >> 39] = (uptr)pdpt | 3;
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
    if (fb && fb->format != NV_FB_NONE && !(fb->address & (PAGE - 1u)) &&
        fb->pitch && fb->height && (u64)fb->pitch * fb->height <= FB_WINDOW_PAGES * PAGE &&
        fb->address <= (1ull << 52) - FB_WINDOW_PAGES * PAGE) {
        memory_reserve(FB_WINDOW, FB_WINDOW_PAGES * PAGE);
        for (u32 s = 0; s < ARRAY_LEN(fb_pt); ++s) {
            pd0[(FB_WINDOW >> 21) + s] = (uptr)fb_pt[s] | 3;
            for (u32 j = 0; j < 512; ++j)
                fb_pt[s][j] = (fb->address + ((u64)s * 512u + j) * PAGE) | 3 | 0x18 | NX;
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
    if (pml4[KHEAP_WINDOW >> 39] & P_PRESENT)
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
    pml4[KHEAP_WINDOW >> 39] = (uptr)heap_pdpt | P_PRESENT | P_WRITE;
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
    root[PHYS_WINDOW >> 39] = pml4[PHYS_WINDOW >> 39];
    root[KHEAP_WINDOW >> 39] = pml4[KHEAP_WINDOW >> 39];
    l3[0] = pdpt[0];
    return root;
}
/* User space occupies PDPT slot 1 (1..2 GiB); slot 0 is supervisor-only. */
static pte_t *leaf(pte_t *root, u32 va, bool create) {
    if (va < USER_BASE || va >= 0x80000000u || !(root[0] & 1))
        return NULL;
    pte_t *l3 = table(root[0]);
    if (!(l3[1] & 1)) {
        if (!create)
            return NULL;
        uptr p = page_alloc();
        if (!p)
            return NULL;
        l3[1] = p | 7;
    }
    pte_t *l2 = table(l3[1]);
    u32 i = (va >> 21) & 511;
    if (!(l2[i] & 1)) {
        if (!create)
            return NULL;
        uptr p = page_alloc();
        if (!p)
            return NULL;
        l2[i] = p | 7;
    }
    return &table(l2[i])[(va >> 12) & 511];
}
int vm_map(pte_t *root, u32 va, u32 flags) {
    if (va < USER_BASE || va >= 0x80000000u || va % PAGE)
        return -NV_EINVAL;
    pte_t *p = leaf(root, va, true);
    if (!p) {
        vm_unmap(root, va);
        return -NV_ENOMEM;
    }
    if (*p & 1)
        return -NV_EEXIST;
    uptr physical = page_alloc();
    if (!physical) {
        vm_unmap(root, va);
        return -NV_ENOMEM;
    }
    *p = physical | 5 | (flags & P_WRITE) | ((flags & P_EXEC) ? 0 : NX);
    return 0;
}
uptr vm_translate(pte_t *root, u32 va) {
    pte_t *p = leaf(root, va, false);
    return p && (*p & 1) ? (uptr)(*p & ADDRESS) + (va & 4095) : 0;
}
static bool empty(pte_t *t) {
    for (u32 i = 0; i < 512; ++i)
        if (t[i] & 1)
            return false;
    return true;
}
void vm_unmap(pte_t *root, u32 va) {
    pte_t *p = leaf(root, va, false);
    if (p && (*p & 1)) {
        page_free((uptr)(*p & ADDRESS));
        *p = 0;
        __asm__ volatile("invlpg (%0)" ::"r"((uptr)va) : "memory");
    }
    if (va < USER_BASE || va >= 0x80000000u)
        return;
    pte_t *l3 = table(root[0]);
    if (!(l3[1] & 1))
        return;
    pte_t *l2 = table(l3[1]);
    u32 i = (va >> 21) & 511;
    if ((l2[i] & 1) && empty(table(l2[i]))) {
        page_free((uptr)(l2[i] & ADDRESS));
        l2[i] = 0;
    }
    if (empty(l2)) {
        page_free(ptr_phys(l2));
        l3[1] = 0;
    }
}
void vm_destroy(pte_t *root) {
    if (!root)
        return;
    pte_t *l3 = table(root[0]);
    if (l3[1] & 1) {
        pte_t *l2 = table(l3[1]);
        for (u32 i = 0; i < 512; ++i)
            if (l2[i] & 1) {
                pte_t *l1 = table(l2[i]);
                for (u32 j = 0; j < 512; ++j)
                    if (l1[j] & 1)
                        page_free((uptr)(l1[j] & ADDRESS));
                page_free(ptr_phys(l1));
            }
        page_free(ptr_phys(l2));
    }
    page_free(ptr_phys(l3));
    page_free(ptr_phys(root));
}
bool user_range(pte_t *root, u32 va, u32 len, bool write) {
    if (!len)
        return true;
    if (va < USER_BASE || va >= 0x80000000u || len > 0x80000000u - va)
        return false;
    pte_t mask = 5 | (write ? 2 : 0);
    if ((root[0] & mask) != mask)
        return false;
    pte_t *l3 = table(root[0]);
    if ((l3[1] & mask) != mask)
        return false;
    pte_t *l2 = table(l3[1]);
    u32 end = (va + len - 1) & ~4095u;
    for (u32 p = va & ~4095u;; p += PAGE) {
        pte_t de = l2[(p >> 21) & 511];
        if ((de & mask) != mask)
            return false;
        pte_t e = table(de)[(p >> 12) & 511];
        if ((e & mask) != mask)
            return false;
        if (p == end)
            break;
    }
    return true;
}
u32 vm_page_count(pte_t *root) {
    u32 count = 0;
    pte_t *l3 = table(root[0]);
    if (!(l3[1] & 1))
        return 0;
    pte_t *l2 = table(l3[1]);
    for (u32 i = 0; i < 512; ++i)
        if (l2[i] & 1) {
            pte_t *l1 = table(l2[i]);
            for (u32 j = 0; j < 512; ++j)
                if (l1[j] & 1)
                    ++count;
        }
    return count;
}
#endif
