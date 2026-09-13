#ifdef __x86_64__
#include "kernel.h"
#define ADDRESS 0x000ffffffffff000ull
#define NX (1ull << 63)
static pte_t pml4[512] ALIGNED(PAGE), pdpt[512] ALIGNED(PAGE), pd0[512] ALIGNED(PAGE);
static pte_t kernel_pt[PHYS_LIMIT / (512 * PAGE)][512] ALIGNED(PAGE);
pte_t stack_pt[512] ALIGNED(PAGE);
pte_t mmio_pt[512] ALIGNED(PAGE);
pte_t *kernel_pd = pml4;
extern u8 kernel_begin[], kernel_text_end[], kernel_ro_end[];
static pte_t *table(pte_t entry) {
    return (void *)(uptr)(entry & ADDRESS);
}
void vm_kernel_init(void) {
    pml4[0] = (uptr)pdpt | 3;
    pdpt[0] = (uptr)pd0 | 3;
    for (u32 i = 0; i < ARRAY_LEN(kernel_pt); ++i) {
        pd0[i] = (uptr)kernel_pt[i] | 3;
        for (u32 j = 0; j < 512; ++j) {
            uptr p = (i * 512 + j) * PAGE;
            pte_t flags = 3 | NX;
            if (p >= (uptr)kernel_begin && p < (uptr)kernel_text_end)
                flags = 1;
            else if (p >= (uptr)kernel_text_end && p < (uptr)kernel_ro_end)
                flags = 1 | NX;
            kernel_pt[i][j] = p ? p | flags : 0;
        }
    }
    pd0[KSTACK_BASE >> 21] = (uptr)stack_pt | 3;
    pd0[MMIO_BASE >> 21] = (uptr)mmio_pt | 3;
    load_cr3((uptr)pml4);
    uptr cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    cr0 |= 0x1000c;
    __asm__ volatile("mov %0,%%cr0" ::"r"(cr0) : "memory");
}
pte_t *vm_create(void) {
    u32 a = page_alloc();
    if (!a)
        return NULL;
    u32 b = page_alloc();
    if (!b) {
        page_free(a);
        return NULL;
    }
    pte_t *root = (void *)(uptr)a, *l3 = (void *)(uptr)b;
    root[0] = b | 7;
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
        u32 p = page_alloc();
        if (!p)
            return NULL;
        l3[1] = p | 7;
    }
    pte_t *l2 = table(l3[1]);
    u32 i = (va >> 21) & 511;
    if (!(l2[i] & 1)) {
        if (!create)
            return NULL;
        u32 p = page_alloc();
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
    u32 physical = page_alloc();
    if (!physical) {
        vm_unmap(root, va);
        return -NV_ENOMEM;
    }
    *p = physical | 5 | (flags & P_WRITE) | ((flags & P_EXEC) ? 0 : NX);
    return 0;
}
u32 vm_translate(pte_t *root, u32 va) {
    pte_t *p = leaf(root, va, false);
    return p && (*p & 1) ? (u32)(*p & ADDRESS) + (va & 4095) : 0;
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
        page_free((u32)(*p & ADDRESS));
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
        page_free((u32)(l2[i] & ADDRESS));
        l2[i] = 0;
    }
    if (empty(l2)) {
        page_free((u32)(uptr)l2);
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
                        page_free((u32)(l1[j] & ADDRESS));
                page_free((u32)(uptr)l1);
            }
        page_free((u32)(uptr)l2);
    }
    page_free((u32)(uptr)l3);
    page_free((u32)(uptr)root);
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
