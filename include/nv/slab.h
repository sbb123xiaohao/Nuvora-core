#ifndef NV_SLAB_H
#define NV_SLAB_H
#include <nv/types.h>

#define NV_SLAB_CLASSES 8u
struct nv_slab_page;
struct nv_slab_pool {
    struct nv_slab_page *head[NV_SLAB_CLASSES];
    void *(*get_page)(void *);
    void (*put_page)(void *, void *);
    void *context;
};
void nv_slab_init(struct nv_slab_pool *, void *(*)(void *), void (*)(void *, void *), void *);
void *nv_slab_alloc(struct nv_slab_pool *, usize);
/* Return the released object capacity, or zero for an invalid/double free. */
u32 nv_slab_free(struct nv_slab_pool *, void *);
#endif
