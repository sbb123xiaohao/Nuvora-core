/* Small freestanding C support library for Nuvora ABI 1; not POSIX/glibc. */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include "../user/runtime.h"
struct allocation { usize size; struct allocation *next; u32 free, pad; u64 reserved; };
_Static_assert(sizeof(struct allocation) % 16 == 0, "malloc alignment");
static struct allocation *blocks;
void *malloc(size_t n) {
    if (!n) n = 1;
    if (n > 0x1ffff000u - sizeof(struct allocation) - 15) return NULL;
    n = ALIGN_UP(n, 16);
    for (struct allocation *p = blocks; p; p = p->next) if (p->free && p->size >= n) {
        if (p->size >= n + sizeof(*p) + 16) {
            struct allocation *q = (void *)((u8 *)(p + 1) + n);
            *q = (struct allocation){p->size - n - sizeof(*q), p->next, 1, 0, 0};
            p->next = q; p->size = n;
        }
        p->free = 0; return p + 1;
    }
    usize pages = ALIGN_UP(n + sizeof(struct allocation), NV_PAGE) / NV_PAGE;
    struct allocation *p = grow((i32)pages);
    if ((iptr)p < 0) return NULL;
    *p = (struct allocation){pages * NV_PAGE - sizeof(*p), NULL, 1, 0, 0};
    struct allocation **tail = &blocks;
    while (*tail) tail = &(*tail)->next;
    *tail = p;
    return malloc(n);
}
void free(void *ptr) {
    if (!ptr) return;
    struct allocation *p = (struct allocation *)ptr - 1;
    p->free = 1;
    for (p = blocks; p && p->next;) {
        struct allocation *q = p->next;
        if (p->free && q->free && (u8 *)(p + 1) + p->size == (u8 *)q) {
            p->size += sizeof(*q) + q->size; p->next = q->next;
        } else p = q;
    }
}
void *calloc(size_t n, size_t size) {
    if (size && n > (size_t)-1 / size) return NULL;
    void *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    usize old = ((struct allocation *)p - 1)->size;
    if (n <= old) return p;
    void *q = malloc(n);
    if (q) { memcpy(q, p, old); free(p); }
    return q;
}
_Noreturn void exit(int status) { finish(status); }
_Noreturn void abort(void) { finish(134); }
static int write_all(const char *p, usize n) {
    int total = 0;
    while (n) {
        int r = emit(1, p, (u32)MIN(n, 16384));
        if (r <= 0) return -1;
        p += r; n -= (u32)r; total += r;
    }
    return total;
}
int putchar(int c) { char b = (char)c; return write_all(&b, 1) < 0 ? -1 : (u8)b; }
int puts(const char *s) {
    if (write_all(s, strlen(s)) < 0 || putchar('\n') < 0) return -1;
    return 0;
}
int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int count = 0;
    while (*fmt) {
        if (*fmt != '%') { if (putchar(*fmt++) < 0) goto fail; ++count; continue; }
        ++fmt;
        u32 wide = 0;
        while (*fmt == 'l' && wide < 2) { ++wide; ++fmt; }
        if (!*fmt) goto fail;
        char spec = *fmt++;
        if (spec == 's') {
            const char *s = va_arg(ap, const char *); if (!s) s = "(null)";
            int n = write_all(s, strlen(s)); if (n < 0) goto fail; count += n;
        } else if (spec == '%' || spec == 'c') {
            if (putchar(spec == '%' ? '%' : va_arg(ap, int)) < 0) goto fail;
            ++count;
        } else if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x') {
            u64 value;
            if (spec == 'd' || spec == 'i') {
                long long v = wide == 2 ? va_arg(ap, long long) :
                              wide == 1 ? va_arg(ap, long) : va_arg(ap, int);
                value = (u64)v;
                if (v < 0) { if (putchar('-') < 0) goto fail; ++count; value = 0ull - value; }
            } else value = wide == 2 ? va_arg(ap, unsigned long long) :
                           wide == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
            u32 base = spec == 'x' ? 16 : 10, n = 0; char b[32];
            do { b[n++] = "0123456789abcdef"[value % base]; value /= base; } while (value);
            while (n) { if (putchar(b[--n]) < 0) goto fail; ++count; }
        } else goto fail;
    }
    va_end(ap); return count;
fail:
    va_end(ap); return -1;
}
