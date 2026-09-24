#include <nv/slab.h>
#include <nv/string.h>

#define SLAB_PAGE 4096u
#define SLAB_MAGIC 0x4e56534cu
struct nv_slab_page {
    u32 magic, kind, live, slots;
    struct nv_slab_page *next, *prev;
    u32 occupied[8];
};
_Static_assert(sizeof(struct nv_slab_page) == 64, "slab header alignment");

void nv_slab_init(struct nv_slab_pool *pool, void *(*get)(void *),
                  void (*put)(void *, void *), void *context) {
    memset(pool, 0, sizeof(*pool));
    pool->get_page = get;
    pool->put_page = put;
    pool->context = context;
}

void *nv_slab_alloc(struct nv_slab_pool *pool, usize size) {
    if (!size || size > 2048 || !pool->get_page || !pool->put_page) return NULL;
    u32 kind = 0, unit = 16;
    while (unit < size) { unit *= 2; ++kind; }
    struct nv_slab_page *page = pool->head[kind];
    while (page && page->live == page->slots) page = page->next;
    if (!page) {
        page = pool->get_page(pool->context);
        if (!page) return NULL;
        memset(page, 0, SLAB_PAGE);
        page->magic = SLAB_MAGIC;
        page->kind = kind;
        page->slots = (SLAB_PAGE - sizeof(*page)) / unit;
        page->next = pool->head[kind];
        if (page->next) page->next->prev = page;
        pool->head[kind] = page;
    }
    for (u32 slot = 0; slot < page->slots; ++slot) {
        u32 mask = 1u << (slot % 32);
        if (page->occupied[slot / 32] & mask) continue;
        page->occupied[slot / 32] |= mask;
        ++page->live;
        u8 *ptr = (u8 *)page + sizeof(*page) + (usize)slot * unit;
        memset(ptr, 0, unit);
        return ptr;
    }
    return NULL;
}

u32 nv_slab_free(struct nv_slab_pool *pool, void *ptr) {
    if (!ptr) return 0;
    struct nv_slab_page *page = (void *)((uptr)ptr & ~(uptr)(SLAB_PAGE - 1));
    if (page->magic != SLAB_MAGIC || page->kind >= NV_SLAB_CLASSES || !page->live)
        return 0;
    u32 unit = 16u << page->kind;
    u32 slots = (SLAB_PAGE - sizeof(*page)) / unit;
    uptr offset = (uptr)ptr - ((uptr)page + sizeof(*page));
    if (page->slots != slots || offset % unit || offset / unit >= slots)
        return 0;
    u32 slot = (u32)(offset / unit), mask = 1u << (slot % 32);
    if (!(page->occupied[slot / 32] & mask)) return 0;
    page->occupied[slot / 32] &= ~mask;
    if (!--page->live) {
        if (page->prev) page->prev->next = page->next;
        else {
            if (pool->head[page->kind] != page) return 0;
            pool->head[page->kind] = page->next;
        }
        if (page->next) page->next->prev = page->prev;
        page->magic = 0;
        pool->put_page(pool->context, page);
    }
    return unit;
}
