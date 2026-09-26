#include "runtime.h"
#include <stddef.h>
int abs(int n) { return n < 0 ? -n : n; }

/* The process owns these pages until exit. pl_mpeg frees individual buffers,
 * but its small allocation count makes page-granular reclamation unnecessary. */
static void *media_alloc(size_t bytes) {
    if (bytes > 0x7fffffffu - sizeof(size_t) - NV_PAGE) {
        println("Media: decoder allocation exceeds the address space.");
        finish(1);
    }
    u32 pages = (u32)(bytes + sizeof(size_t) + NV_PAGE - 1) / NV_PAGE;
    u8 *block = grow((i32)MAX(1u, pages));
    if ((iptr)block < 0) {
        println("Media: insufficient memory for decoding.");
        finish(1);
    }
    *(size_t *)block = bytes;
    return block + sizeof(size_t);
}
static void media_free(void *ptr) { (void)ptr; }
static void *media_realloc(void *ptr, size_t bytes) {
    if (!ptr) return media_alloc(bytes);
    void *next = media_alloc(bytes);
    size_t old = *((size_t *)ptr - 1);
    memcpy(next, ptr, MIN(old, bytes));
    return next;
}
#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "../third_party/minimp3.h"
#define PLM_NO_STDIO
#define PLM_MALLOC(sz) media_alloc(sz)
#define PLM_REALLOC(p, sz) media_realloc(p, sz)
#define PLM_FREE(p) media_free(p)
#define PL_MPEG_IMPLEMENTATION
#include "../third_party/pl_mpeg.h"

#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DRFLAC_MALLOC(sz) media_alloc(sz)
#define DRFLAC_REALLOC(p, sz) media_realloc(p, sz)
#define DRFLAC_FREE(p) media_free(p)
#define DRFLAC_ASSERT(x) ((void)0)
#define DR_FLAC_IMPLEMENTATION
#include "../third_party/dr_flac.h"
