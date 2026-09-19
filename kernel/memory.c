#include "kernel.h"
#define NPAGE (PHYS_LIMIT / PAGE)
#define HEAP_SIZE KHEAP_SIZE
#define BLOCK_MAGIC 0x4e56424cu
static u32 bitmap[NPAGE / 32], allocated[NPAGE / 32], free_count, total_count, search_word;
#ifndef __x86_64__
bool fb_window_mapped;
static u32 initial_pd[1024] ALIGNED(PAGE),
    initial_pt[PHYS_LIMIT / (4u * 1024u * 1024u)][1024] ALIGNED(PAGE);
u32 *kernel_pd = initial_pd;
pte_t stack_pt[1024] ALIGNED(PAGE);
pte_t mmio_pt[1024] ALIGNED(PAGE);
#endif
extern pte_t stack_pt[];
extern pte_t mmio_pt[];
static u32 mmio_pages;
_Static_assert(NV_TASK_MAX *KSTACK_STRIDE <= 512, "kernel stacks fit one page table");
#ifdef __x86_64__
/* Keep the fixed kernel load image below firmware reservations (OVMF uses
 * ACPI NVS at 8 MiB). Map and pin ordinary pages from actual usable RAM. */
static u8 *heap;
#else
static u8 heap[HEAP_SIZE] ALIGNED(16);
#endif
struct block {
    u32 magic, size, free, pad;
    struct block *prev, *next;
#ifndef __x86_64__
    u32 reserved[2];
#endif
};
_Static_assert(sizeof(struct block) == 32, "aligned heap metadata");
static struct block *head;
static u32 used_bytes;
extern u8 kernel_begin[], kernel_ro_end[], kernel_end[];
static bool marked(u32 i) {
    return (bitmap[i / 32] & (1u << (i % 32))) != 0;
}
static void reserve(u64 start, u64 end) {
    if (start >= PHYS_LIMIT)
        return;
    end = MIN(end, (u64)PHYS_LIMIT);
    for (u64 i = start / PAGE; i < ALIGN_UP(end, PAGE) / PAGE; ++i) {
        u32 w = (u32)(i / 32);
        u32 bit = (u32)(i % 32);
        if (!(bitmap[w] & (1u << bit))) {
            bitmap[w] |= 1u << bit;
            --free_count;
        }
    }
}
void memory_reserve(u64 base, u64 length) {
    if (length && base <= ~0ull - length)
        reserve(base, base + length);
}
static void available(u64 base, u64 length) {
    if (base >= PHYS_LIMIT || !length)
        return;
    u64 end = base + length;
    if (end < base)
        return;
    if (end > PHYS_LIMIT)
        end = PHYS_LIMIT;
    for (u64 i = ALIGN_UP(base, PAGE) / PAGE; i < end / PAGE; ++i) {
        u32 w = (u32)(i / 32);
        u32 bit = (u32)(i % 32);
        if (bitmap[w] & (1u << bit)) {
            bitmap[w] &= ~(1u << bit);
            ++free_count;
        }
    }
}
void memory_init(const struct boot_info *bi) {
    memset(bitmap, 0xff, sizeof(bitmap));
    if (!bi->mem_count || bi->mem_count > NV_BOOT_MEM_MAX || bi->res_count > NV_BOOT_RES_MAX)
        panic("bootloader provided no RAM map");
    /* Usable RAM first; reserved entries then win over overlapping regions. */
    for (u32 i = 0; i < bi->mem_count; ++i)
        if (bi->mem[i].type == 1)
            available(bi->mem[i].base, bi->mem[i].length);
    for (u32 i = 0; i < bi->mem_count; ++i) {
        const struct boot_mem_entry *m = &bi->mem[i];
        if (m->type != 1 && m->base < PHYS_LIMIT) {
            u64 e = m->base + m->length;
            if (e < m->base || e > PHYS_LIMIT)
                e = PHYS_LIMIT;
            reserve(m->base, e);
        }
    }
    for (u32 i = 0; i < bi->res_count; ++i) {
        u64 e = bi->res[i].base + bi->res[i].length;
        if (e < bi->res[i].base)
            continue;
        reserve(bi->res[i].base, e);
    }
    total_count = free_count;
    reserve(0, (uptr)kernel_end);
    /* A framebuffer living inside managed RAM must never be handed out. */
    if (bi->fb.format != NV_FB_NONE && bi->fb.address < PHYS_LIMIT) {
        u64 e = bi->fb.address + (u64)bi->fb.pitch * bi->fb.height;
        if (e > bi->fb.address)
            reserve(bi->fb.address, e);
    }
    if (free_count < 1024)
        panic("at least 32 MiB RAM is recommended");
    vm_kernel_init();
#ifdef __x86_64__
    heap = vm_heap_create();
    if (!heap)
        panic("cannot reserve kernel heap; at least 32 MiB RAM is recommended");
#endif
    head = (struct block *)heap;
    *head = (struct block){.magic = BLOCK_MAGIC, .size = HEAP_SIZE - sizeof(*head), .free = 1};
}
uptr page_alloc_below(u64 limit) {
    if (!free_count)
        return 0;
    /* Low-first scan: keeps DMA-capable allocations near the bottom of RAM. */
    u64 top_page = MIN(limit, (u64)PHYS_LIMIT) / PAGE;
    u64 top_word = MIN(top_page / 32 + 1, (u64)ARRAY_LEN(bitmap));
    for (u32 w = 0; w < top_word; ++w) {
        if (bitmap[w] == 0xffffffffu)
            continue;
        for (u32 bit = 0; bit < 32; ++bit) {
            u64 p = (u64)w * 32 + bit;
            if (p >= top_page)
                return 0;
            if (bitmap[w] & (1u << bit))
                continue;
            bitmap[w] |= 1u << bit;
            allocated[w] |= 1u << bit;
            --free_count;
            uptr page = (uptr)(p * PAGE);
            memset(phys_ptr(page), 0, PAGE);
            return page;
        }
    }
    return 0;
}
uptr page_alloc(void) {
    if (!free_count)
        return 0;
    for (u32 step = 0; step < ARRAY_LEN(bitmap); ++step) {
        u32 w = (search_word + step) % ARRAY_LEN(bitmap);
        if (bitmap[w] != 0xffffffffu) {
            u32 bit = (u32)__builtin_ctz(~bitmap[w]);
            bitmap[w] |= 1u << bit;
            allocated[w] |= 1u << bit;
            search_word = w;
            --free_count;
            uptr p = (uptr)((u64)w * 32 * PAGE + (u64)bit * PAGE);
            memset(phys_ptr(p), 0, PAGE);
            return p;
        }
    }
    return 0;
}
uptr page_alloc_run(u32 count) {
    if (!count || free_count < count)
        return 0;
    u64 total = (u64)ARRAY_LEN(bitmap) * 32;
    u64 run = 0, start = 0;
    for (u64 i = 0; i < total; ++i) {
        if (marked((u32)i)) {
            run = 0;
            continue;
        }
        if (!run)
            start = i;
        if (++run == count) {
            for (u64 j = start; j < start + count; ++j) {
                u32 w = (u32)(j / 32);
                u32 bit = (u32)(j % 32);
                bitmap[w] |= 1u << bit;
                allocated[w] |= 1u << bit;
            }
            free_count -= count;
            uptr p = (uptr)(start * PAGE);
            memset(phys_ptr(p), 0, (usize)count * PAGE);
            return p;
        }
    }
    return 0;
}
void page_pin(uptr p) {
    if (p >= PHYS_LIMIT || p % PAGE ||
        !(allocated[p / PAGE / 32] & (1u << (p / PAGE % 32))))
        panic("invalid page pin");
    allocated[p / PAGE / 32] &= ~(1u << (p / PAGE % 32));
}
void page_free(uptr p) {
    if (p >= PHYS_LIMIT || p % PAGE ||
        !(allocated[p / PAGE / 32] & (1u << (p / PAGE % 32))))
        panic("invalid page free");
    allocated[p / PAGE / 32] &= ~(1u << (p / PAGE % 32));
    bitmap[p / PAGE / 32] &= ~(1u << (p / PAGE % 32));
    ++free_count;
}
u32 pages_free(void) {
    return free_count;
}
u32 pages_total(void) {
    return total_count;
}
u32 heap_used(void) {
    return used_bytes;
}
u32 heap_total(void) {
    return HEAP_SIZE;
}
void *kmalloc(usize n) {
    if (!n || n > HEAP_SIZE - sizeof(struct block) - 15)
        return NULL;
    n = ALIGN_UP(n, 16);
    for (struct block *b = head; b; b = b->next) {
        if (b->magic != BLOCK_MAGIC)
            panic("heap metadata damaged");
        if (!b->free || b->size < n)
            continue;
        if (b->size >= n + sizeof(*b) + 16) {
            struct block *rest = (void *)(uptr)((u8 *)(uptr)(b + 1) + n);
            *rest = (struct block){.magic = BLOCK_MAGIC,
                                   .size = b->size - n - sizeof(*b),
                                   .free = 1,
                                   .prev = b,
                                   .next = b->next};
            if (rest->next)
                rest->next->prev = rest;
            b->next = rest;
            b->size = n;
        }
        b->free = 0;
        used_bytes += b->size;
        memset(b + 1, 0, b->size);
        return b + 1;
    }
    return NULL;
}
static void merge(struct block *b) {
    struct block *next = b->next;
    if (next && next->free) {
        b->size += sizeof(*b) + next->size;
        b->next = next->next;
        if (b->next)
            b->next->prev = b;
        next->magic = 0;
    }
}
void kfree(void *ptr) {
    if (!ptr)
        return;
    if ((uptr)ptr < (uptr)heap + sizeof(struct block) || (uptr)ptr >= (uptr)heap + HEAP_SIZE ||
        (uptr)ptr % 16)
        panic("heap pointer out of range");
    struct block *b = (struct block *)ptr - 1;
    if (b->magic != BLOCK_MAGIC || b->free)
        panic("invalid heap free");
    used_bytes -= b->size;
    b->free = 1;
    merge(b);
    if (b->prev && b->prev->free)
        merge(b->prev);
}
#ifndef __x86_64__
void vm_kernel_init(void) {
    for (u32 i = 0; i < ARRAY_LEN(initial_pt); ++i) {
        initial_pd[i] = (u32)initial_pt[i] | P_PRESENT | P_WRITE;
        for (u32 j = 0; j < 1024; ++j) {
            u32 p = (i * 1024 + j) * PAGE;
            u32 flags = P_PRESENT | P_WRITE;
            if (p >= (uptr)kernel_begin && p < (uptr)kernel_ro_end)
                flags = P_PRESENT;
            initial_pt[i][j] = p ? p | flags : 0;
        }
    }
    initial_pd[KSTACK_BASE >> 22] = (uptr)stack_pt | P_PRESENT | P_WRITE;
    initial_pd[MMIO_BASE >> 22] = (uptr)mmio_pt | P_PRESENT | P_WRITE;
    load_cr3((uptr)kernel_pd);
    u32 cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    /* Keep FP disabled until cpu_init configures eager per-task state. */
    cr0 |= 0x8001000cu;
    __asm__ volatile("mov %0,%%cr0" ::"r"(cr0) : "memory");
}
u32 *vm_create(void) {
    u32 p = page_alloc();
    if (!p)
        return NULL;
    u32 *pd = (u32 *)(uptr)p;
    for (u32 i = 0; i < USER_BASE >> 22; ++i)
        pd[i] = kernel_pd[i];
    return pd;
}
int vm_map(u32 *pd, u32 va, u32 flags) {
    if (va < USER_BASE || va >= 0x80000000u || va % PAGE)
        return -NV_EINVAL;
    u32 di = va >> 22, ti = (va >> 12) & 1023;
    if (!(pd[di] & P_PRESENT)) {
        u32 p = page_alloc();
        if (!p)
            return -NV_ENOMEM;
        pd[di] = p | 7;
    }
    u32 *pt = (void *)(uptr)(pd[di] & ~4095u);
    if (pt[ti] & P_PRESENT)
        return -NV_EEXIST;
    u32 p = page_alloc();
    if (!p) {
        vm_unmap(pd, va);
        return -NV_ENOMEM;
    }
    pt[ti] = p | P_PRESENT | P_USER | (flags & P_WRITE);
    return 0;
}
uptr vm_translate(u32 *pd, u32 va) {
    if (!(pd[va >> 22] & P_PRESENT))
        return 0;
    u32 *pt = (void *)(uptr)(pd[va >> 22] & ~4095u);
    u32 p = pt[(va >> 12) & 1023];
    return p & P_PRESENT ? (p & ~4095u) + (va & 4095) : 0;
}
void vm_unmap(u32 *pd, u32 va) {
    u32 di = va >> 22;
    if (di < USER_BASE >> 22 || di >= 512 || !(pd[di] & 1))
        return;
    u32 *pt = (void *)(uptr)(pd[di] & ~4095u);
    u32 ti = (va >> 12) & 1023;
    if (pt[ti] & 1) {
        page_free(pt[ti] & ~4095u);
        pt[ti] = 0;
        __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
    }
    bool any = false;
    for (u32 i = 0; i < 1024; ++i)
        if (pt[i] & 1) {
            any = true;
            break;
        }
    if (!any) {
        page_free((uptr)pt);
        pd[di] = 0;
    }
}
void vm_destroy(u32 *pd) {
    if (!pd)
        return;
    for (u32 i = USER_BASE >> 22; i < 512; ++i)
        if (pd[i] & 1) {
            u32 *pt = (void *)(uptr)(pd[i] & ~4095u);
            for (u32 j = 0; j < 1024; ++j)
                if (pt[j] & 1)
                    page_free(pt[j] & ~4095u);
            page_free((uptr)pt);
        }
    page_free((uptr)pd);
}
bool user_range(u32 *pd, u32 va, u32 len, bool write) {
    if (!len)
        return true;
    if (va < USER_BASE || va >= 0x80000000u || len > 0x80000000u - va)
        return false;
    u32 end = va + len - 1, mask = P_PRESENT | P_USER | (write ? P_WRITE : 0);
    for (u32 p = va & ~4095u;; p += PAGE) {
        u32 de = pd[p >> 22];
        if ((de & mask) != mask)
            return false;
        u32 *pt = (void *)(uptr)(de & ~4095u);
        if ((pt[(p >> 12) & 1023] & mask) != mask)
            return false;
        if (p == (end & ~4095u))
            break;
    }
    return true;
}
#endif
void *vm_mmio_map(u64 physical, u32 length) {
    /* The final PTE is a shared single-page firmware/ECAM aperture. */
    if (!length || physical % PAGE || physical >> 52 || length > 511 * PAGE ||
        physical > (1ull << 52) - length)
        return NULL;
#ifndef __x86_64__
    if (physical >> 32 || physical + length > 0x100000000ull)
        return NULL;
#endif
    u32 count = ALIGN_UP(length, PAGE) / PAGE;
    if (count > 511 - mmio_pages)
        return NULL;
    uptr base = MMIO_BASE + mmio_pages * PAGE;
    for (u32 i = 0; i < count; ++i) {
        pte_t flags = P_PRESENT | P_WRITE | 0x18; /* PCD + PWT: uncached registers. */
#ifdef __x86_64__
        flags |= 1ull << 63;
#endif
        mmio_pt[mmio_pages++] = (pte_t)(physical + i * PAGE) | flags;
        __asm__ volatile("invlpg (%0)" ::"r"(base + i * PAGE) : "memory");
    }
    return (void *)base;
}
void *vm_mmio_remap(u64 physical) {
    if (physical % PAGE || physical >> 52)
        return NULL;
#ifndef __x86_64__
    if (physical >> 32)
        return NULL;
#endif
    uptr address = MMIO_BASE + 511u * PAGE;
    pte_t flags = P_PRESENT | P_WRITE | 0x18; /* uncached supervisor-only aperture */
#ifdef __x86_64__
    flags |= 1ull << 63;
#endif
    mmio_pt[511] = (pte_t)physical | flags;
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
    return (void *)address;
}
/* Shared supervisor mappings: unmapped guard, four stack pages, unmapped guard.
 * A task's stack remains mapped until execution has left that stack. */
void *vm_stack_alloc(u32 slot) {
    if (slot >= NV_TASK_MAX)
        return NULL;
    u32 first = slot * KSTACK_STRIDE + 1;
    for (u32 i = 0; i < KSTACK_PAGES; ++i)
        if (stack_pt[first + i] & P_PRESENT)
            panic("kernel stack slot already occupied");
    for (u32 i = 0; i < KSTACK_PAGES; ++i) {
        uptr p = page_alloc();
        if (!p) {
            vm_stack_free((void *)(uptr)(KSTACK_BASE + first * PAGE));
            return NULL;
        }
        pte_t flags = P_PRESENT | P_WRITE;
#ifdef __x86_64__
        flags |= 1ull << 63;
#endif
        stack_pt[first + i] = p | flags;
        __asm__ volatile("invlpg (%0)" ::"r"((uptr)(KSTACK_BASE + (first + i) * PAGE)) : "memory");
    }
    return (void *)(uptr)(KSTACK_BASE + first * PAGE);
}
void vm_stack_free(void *base) {
    if (!base)
        return;
    uptr address = (uptr)base;
    if (address < KSTACK_BASE + PAGE ||
        address >= KSTACK_BASE + NV_TASK_MAX * KSTACK_STRIDE * PAGE ||
        (address - KSTACK_BASE - PAGE) % (KSTACK_STRIDE * PAGE))
        panic("invalid kernel stack free");
    u32 first = (address - KSTACK_BASE) / PAGE;
    for (u32 i = 0; i < KSTACK_PAGES; ++i) {
        if (!(stack_pt[first + i] & P_PRESENT))
            continue;
        uptr p = (uptr)(stack_pt[first + i] & P_ADDRESS);
        stack_pt[first + i] = 0;
        __asm__ volatile("invlpg (%0)" ::"r"(address + i * PAGE) : "memory");
        page_free(p);
    }
}
int user_string(u32 ptr, char *out, u32 cap) {
    for (u32 i = 0; i < cap; ++i) {
        if (ptr > 0xffffffffu - i || !user_range(current->pd, ptr + i, 1, false))
            return -NV_EFAULT;
        out[i] = *(const char *)(uptr)(ptr + i);
        if (!out[i])
            return 0;
    }
    return -NV_E2BIG;
}
int copy_to_space(pte_t *pd, u32 va, const void *src, u32 len) {
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
#ifndef __x86_64__
u32 vm_page_count(u32 *pd) {
    u32 n = 0;
    for (u32 i = USER_BASE >> 22; i < 512; ++i)
        if (pd[i] & 1) {
            u32 *pt = (void *)(uptr)(pd[i] & ~4095u);
            for (u32 j = 0; j < 1024; ++j)
                if (pt[j] & 1)
                    ++n;
        }
    return n;
}
#endif
static void exhaustion_selftest(void) {
    u32 before = free_count, old = heap_used();
    /* Drain every page without recording the (potentially huge) list: the
     * allocator's own ownership bitmap lets us hand everything back at the
     * end, so the test cost is independent of managed RAM size. */
    uptr donor[KSTACK_PAGES];
    for (u32 i = 0; i < ARRAY_LEN(donor); ++i)
        if (!(donor[i] = page_alloc()))
            panic("OOM selftest setup");
    pte_t *pd = vm_create();
    if (!pd)
        panic("OOM selftest setup");
    while (page_alloc()) {
    }
    u32 freed = 0;
    for (u32 budget = 0; budget <= 4; ++budget) {
        while (freed < budget)
            page_free(donor[freed++]);
        u32 free_before = free_count;
        int r = vm_map(pd, USER_BASE, P_WRITE);
        if (r == 0)
            vm_unmap(pd, USER_BASE);
        else if (r != -NV_ENOMEM)
            panic("unexpected map exhaustion result");
        if (free_before != free_count || vm_page_count(pd))
            panic("page table allocation rollback leak");
        void *stack = vm_stack_alloc(0);
        if ((budget < KSTACK_PAGES && stack) || (budget == KSTACK_PAGES && !stack))
            panic("kernel stack allocation exhaustion");
        vm_stack_free(stack);
        if (free_before != free_count)
            panic("kernel stack allocation rollback leak");
    }
    vm_destroy(pd);
    /* donor[0..3] are already free; return every remaining owned page. */
    for (u32 w = 0; w < ARRAY_LEN(allocated); ++w)
        while (allocated[w]) {
            u32 bit = (u32)__builtin_ctz(allocated[w]);
            page_free((uptr)((u64)w * 32 * PAGE + (u64)bit * PAGE));
        }
    if (free_count != before || heap_used() != old)
        panic("exhaustion selftest cleanup");
    kprintf("[ok] OOM rollback: zero through four available pages, no leaks\n");
}
void memory_selftest(void) {
    u32 before = free_count;
    uptr p = page_alloc(), q = page_alloc();
    if (!p || !q || p == q)
        panic("physical allocator selftest");
    memset(phys_ptr(p), 0xa5, PAGE);
    page_free(q);
    page_free(p);
    if (free_count != before)
        panic("physical page leak");
    u32 old = heap_used();
    void *a = kmalloc(13), *b = kmalloc(7000), *c = kmalloc(29);
    if (!a || !b || !c)
        panic("heap allocator selftest");
    kfree(b);
    kfree(a);
    kfree(c);
    if (heap_used() != old)
        panic("heap coalescing selftest");
    pte_t *pd = vm_create();
    if (!pd || vm_map(pd, USER_BASE, P_WRITE) < 0)
        panic("virtual allocator selftest");
    if (!user_range(pd, USER_BASE, 4096, true) || user_range(pd, 0x100000, 1, false) ||
        user_range(pd, 0x7fffffff, 2, false))
        panic("user range selftest");
    vm_destroy(pd);
    if (free_count != before)
        panic("address space leak");
    void *stack = vm_stack_alloc(0);
    if (!stack)
        panic("kernel stack allocation selftest");
    memset(stack, 0xa5, KSTACK_SIZE);
    pd = vm_create();
    if (!pd || user_range(pd, (uptr)stack, KSTACK_SIZE, true))
        panic("kernel stack exposed to user mode");
    vm_destroy(pd);
    vm_stack_free(stack);
    if (free_count != before)
        panic("kernel stack reclamation leak");
    if (test_mode)
        exhaustion_selftest();
    kprintf("[ok] physical pages, heap coalescing, page permissions\n");
}
