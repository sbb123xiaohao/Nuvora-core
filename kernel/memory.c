#include "kernel.h"
#include <nv/page_buddy.h>
#include <nv/slab.h>
#define NPAGE (PHYS_LIMIT / PAGE)
#define HEAP_SIZE KHEAP_SIZE
#define BLOCK_MAGIC 0x4e56424cu
static u32 bitmap[NPAGE / 32], allocated[NPAGE / 32], free_count, total_count, search_word;
static u32 normal_cursor = 1u << 20, dma_cursor = 1;
static bool normal_depleted;
static struct nv_page_buddy buddy;
static u8 *buddy_levels;
static u32 managed_pages;
static struct nv_slab_pool slabs;
static bool slabs_active;
static void *slab_get_page(void *unused);
static void slab_put_page(void *unused, void *ptr);
extern pte_t stack_pt[];
extern pte_t mmio_pt[];
static u32 mmio_pages;
_Static_assert(NV_TASK_MAX *KSTACK_STRIDE <= 512, "kernel stacks fit one page table");
/* Keep the fixed kernel load image below firmware reservations (OVMF uses
 * ACPI NVS at 8 MiB). Map and pin ordinary pages from actual usable RAM. */
static u8 *heap;
struct block {
    u32 magic, size, free, pad;
    struct block *prev, *next;
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
    if (buddy_levels)
        nv_buddy_refresh(&buddy, (u32)(start / PAGE),
                         (u32)(ALIGN_UP(end, PAGE) / PAGE - start / PAGE));
}
static struct nv_spinlock allocator_lock;
static void memory_reserve_locked(u64 base, u64 length);
static uptr page_alloc_locked(void);
static uptr page_alloc_below_locked(u64 limit);
static uptr page_alloc_order_locked(u32 order);
static uptr page_alloc_run_locked(u32 count);
static void page_pin_locked(uptr p);
static void page_free_locked(uptr p);
static u32 pages_free_locked(void);
static u32 heap_used_locked(void);
static void * kmalloc_locked(usize n);
static void kfree_locked(void *p);
static void memory_reserve_locked(u64 base, u64 length) {
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
        if (bi->mem[i].type == 1) {
            available(bi->mem[i].base, bi->mem[i].length);
            if (bi->mem[i].base < PHYS_LIMIT &&
                bi->mem[i].length <= ~0ull - bi->mem[i].base) {
                u64 end = MIN(bi->mem[i].base + bi->mem[i].length, PHYS_LIMIT);
                if (end / PAGE > managed_pages) managed_pages = (u32)(end / PAGE);
            }
        }
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
    heap = vm_heap_create();
    if (!heap)
        panic("cannot reserve kernel heap; at least 32 MiB RAM is recommended");
    head = (struct block *)heap;
    *head = (struct block){.magic = BLOCK_MAGIC, .size = HEAP_SIZE - sizeof(*head), .free = 1};
    /* Allocate the compact free-block index from mapped, pinned kernel pages
     * before publishing it. Page zero in the ordinary RAM bitmap stays busy. */
    buddy_levels = kmalloc_locked(nv_buddy_bytes(managed_pages));
    if (!buddy_levels)
        panic("cannot allocate page allocator metadata");
    nv_buddy_init(&buddy, bitmap, buddy_levels, managed_pages);
    nv_slab_init(&slabs, slab_get_page, slab_put_page, NULL);
    slabs_active = true;
}
static uptr commit_pages(u32 first, u32 count) {
    for (u32 i = first; i < first + count; ++i) {
        bitmap[i / 32] |= 1u << (i % 32);
        allocated[i / 32] |= 1u << (i % 32);
    }
    free_count -= count;
    if (buddy_levels) nv_buddy_refresh(&buddy, first, count);
    uptr p = (uptr)((u64)first * PAGE);
    memset(phys_ptr(p), 0, (usize)count * PAGE);
    return p;
}
static uptr page_alloc_below_locked(u64 limit) {
    if (!free_count)
        return 0;
    /* Low-first scan: keeps DMA-capable allocations near the bottom of RAM. */
    u64 top_page = MIN(limit, (u64)PHYS_LIMIT) / PAGE;
    if (buddy_levels) {
        u32 first = nv_buddy_find(&buddy, 0, 1, (u32)MIN(top_page, managed_pages));
        return first < managed_pages ? commit_pages(first, 1) : 0;
    }
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
static uptr page_alloc_locked(void) {
    if (!free_count)
        return 0;
    if (buddy_levels) {
        u32 first = managed_pages;
        if (!normal_depleted && managed_pages > (1u << 20)) {
            first = nv_buddy_find(&buddy, 0, normal_cursor, managed_pages);
            if (first == managed_pages)
                first = nv_buddy_find(&buddy, 0, 1u << 20, normal_cursor);
            if (first == managed_pages) normal_depleted = true;
        }
        if (first == managed_pages)
            first = nv_buddy_find(&buddy, 0, dma_cursor, MIN(1u << 20, managed_pages));
        if (first == managed_pages)
            first = nv_buddy_find(&buddy, 0, 1, dma_cursor);
        if (first >= (1u << 20) && first < managed_pages)
            normal_cursor = first + 1 < managed_pages ? first + 1 : 1u << 20;
        else if (first < managed_pages)
            dma_cursor = first + 1 < MIN(1u << 20, managed_pages) ? first + 1 : 1;
        return first < managed_pages ? commit_pages(first, 1) : 0;
    }
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
static uptr page_alloc_order_locked(u32 order) {
    if (order > NV_BUDDY_MAX_ORDER || !buddy_levels ||
        free_count < (1u << order)) return 0;
    u32 first = nv_buddy_find(&buddy, order, 1u << 20, managed_pages);
    if (first == managed_pages)
        first = nv_buddy_find(&buddy, order, 1, MIN(1u << 20, managed_pages));
    return first < managed_pages ? commit_pages(first, 1u << order) : 0;
}
static uptr page_alloc_run_locked(u32 count) {
    if (!count || free_count < count)
        return 0;
    if (!(count & (count - 1)) && buddy_levels && count <= (1u << NV_BUDDY_MAX_ORDER))
        return page_alloc_order_locked((u32)__builtin_ctz(count));
    u64 total = buddy_levels ? managed_pages : (u64)ARRAY_LEN(bitmap) * 32;
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
            if (buddy_levels) nv_buddy_refresh(&buddy, (u32)start, count);
            uptr p = (uptr)(start * PAGE);
            memset(phys_ptr(p), 0, (usize)count * PAGE);
            return p;
        }
    }
    return 0;
}
static void page_pin_locked(uptr p) {
    if (p >= PHYS_LIMIT || p % PAGE ||
        !(allocated[p / PAGE / 32] & (1u << (p / PAGE % 32))))
        panic("invalid page pin");
    allocated[p / PAGE / 32] &= ~(1u << (p / PAGE % 32));
}
static void page_free_locked(uptr p) {
    if (p >= PHYS_LIMIT || p % PAGE ||
        !(allocated[p / PAGE / 32] & (1u << (p / PAGE % 32))))
        panic("invalid page free");
    allocated[p / PAGE / 32] &= ~(1u << (p / PAGE % 32));
    bitmap[p / PAGE / 32] &= ~(1u << (p / PAGE % 32));
    ++free_count;
    if (p / PAGE >= (1u << 20)) normal_depleted = false;
    if (buddy_levels) nv_buddy_refresh(&buddy, (u32)(p / PAGE), 1);
}
static u32 pages_free_locked(void) {
    return free_count;
}
u32 pages_total(void) {
    return total_count;
}
static u32 heap_used_locked(void) {
    return used_bytes;
}
u32 heap_total(void) {
    return HEAP_SIZE;
}
static void *slab_get_page(void *unused) {
    (void)unused;
    uptr p = page_alloc_locked();
    return p ? phys_ptr(p) : NULL;
}
static void slab_put_page(void *unused, void *ptr) {
    (void)unused;
    page_free_locked(ptr_phys(ptr));
}
static void *kmalloc_locked(usize n) {
    if (!n || n > HEAP_SIZE - sizeof(struct block) - 15)
        return NULL;
    if (slabs_active && n <= 2048) {
        void *small = nv_slab_alloc(&slabs, n);
        if (small) {
            u32 unit = 16;
            while (unit < n) unit *= 2;
            used_bytes += unit;
            return small;
        }
    }
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
static void kfree_locked(void *ptr) {
    if (!ptr)
        return;
    if (slabs_active && (uptr)ptr >= PHYS_WINDOW &&
        (uptr)ptr - PHYS_WINDOW < PHYS_LIMIT) {
        uptr p = ptr_phys(ptr) & ~(uptr)(PAGE - 1);
        if (!(allocated[p / PAGE / 32] & (1u << (p / PAGE % 32))))
            panic("invalid slab page");
        u32 size = nv_slab_free(&slabs, ptr);
        if (!size || used_bytes < size) panic("invalid slab free");
        used_bytes -= size;
        return;
    }
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

/* One IRQ-safe lock covers ownership, slab callbacks and heap metadata. */
void memory_reserve(u64 base, u64 length) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    memory_reserve_locked(base, length);
    spin_unlock_irqrestore(&allocator_lock, flags);
}
uptr page_alloc(void) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    uptr result = page_alloc_locked();
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
uptr page_alloc_below(u64 limit) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    uptr result = page_alloc_below_locked(limit);
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
uptr page_alloc_order(u32 order) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    uptr result = page_alloc_order_locked(order);
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
uptr page_alloc_run(u32 count) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    uptr result = page_alloc_run_locked(count);
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
void page_pin(uptr p) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    page_pin_locked(p);
    spin_unlock_irqrestore(&allocator_lock, flags);
}
void page_free(uptr p) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    page_free_locked(p);
    spin_unlock_irqrestore(&allocator_lock, flags);
}
u32 pages_free(void) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    u32 result = pages_free_locked();
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
u32 heap_used(void) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    u32 result = heap_used_locked();
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
void * kmalloc(usize n) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    void * result = kmalloc_locked(n);
    spin_unlock_irqrestore(&allocator_lock, flags);
    return result;
}
void kfree(void *p) {
    uptr flags = spin_lock_irqsave(&allocator_lock);
    kfree_locked(p);
    spin_unlock_irqrestore(&allocator_lock, flags);
}
void *vm_mmio_map(u64 physical, u32 length) {
    /* The final PTE is a shared single-page firmware/ECAM aperture. */
    if (!length || physical % PAGE || physical >> 52 || length > 511 * PAGE ||
        physical > (1ull << 52) - length)
        return NULL;
    u32 count = ALIGN_UP(length, PAGE) / PAGE;
    if (count > 511 - mmio_pages)
        return NULL;
    uptr base = MMIO_BASE + mmio_pages * PAGE;
    for (u32 i = 0; i < count; ++i) {
        pte_t flags = P_PRESENT | P_WRITE | 0x18; /* PCD + PWT: uncached registers. */
        flags |= 1ull << 63;
        mmio_pt[mmio_pages++] = (pte_t)(physical + i * PAGE) | flags;
        __asm__ volatile("invlpg (%0)" ::"r"(base + i * PAGE) : "memory");
    }
    return (void *)base;
}
void *vm_mmio_remap(u64 physical) {
    if (physical % PAGE || physical >> 52)
        return NULL;
    uptr address = MMIO_BASE + 511u * PAGE;
    pte_t flags = P_PRESENT | P_WRITE | 0x18; /* uncached supervisor-only aperture */
    flags |= 1ull << 63;
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
        flags |= 1ull << 63;
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
