#include <nv/string.h>
/* Unaligned, alias-safe scalar words work in kernel code without SIMD state.
 * Every access fits inside the requested span, including page-edge tails. */
typedef uptr memory_word __attribute__((__may_alias__, __aligned__(1)));
static void copy_forward(u8 *d, const u8 *s, usize n) {
    while (n>=4*sizeof(memory_word)) {
        ((memory_word *)d)[0]=((const memory_word *)s)[0];
        ((memory_word *)d)[1]=((const memory_word *)s)[1];
        ((memory_word *)d)[2]=((const memory_word *)s)[2];
        ((memory_word *)d)[3]=((const memory_word *)s)[3];
        d+=4*sizeof(memory_word);s+=4*sizeof(memory_word);n-=4*sizeof(memory_word);
    }
    while (n>=sizeof(memory_word)) {
        *(memory_word *)d=*(const memory_word *)s;
        d+=sizeof(memory_word);s+=sizeof(memory_word);n-=sizeof(memory_word);
    }
    while (n--) *d++=*s++;
}
void *memset(void *dst, int c, usize n) {
    u8 *d = dst;
    memory_word value=(~(uptr)0/255u)*(u8)c;
    while (n>=4*sizeof(memory_word)) {
        ((memory_word *)d)[0]=value;((memory_word *)d)[1]=value;
        ((memory_word *)d)[2]=value;((memory_word *)d)[3]=value;
        d+=4*sizeof(memory_word);n-=4*sizeof(memory_word);
    }
    while (n>=sizeof(memory_word)) {
        *(memory_word *)d=value;d+=sizeof(memory_word);n-=sizeof(memory_word);
    }
    while (n--)
        *d++ = (u8)c;
    return dst;
}
void *memcpy(void *dst, const void *src, usize n) {
    copy_forward(dst,src,n);
    return dst;
}
void *memmove(void *dst, const void *src, usize n) {
    u8 *d = dst;
    const u8 *s = src;
    if (d==s || !n) return dst;
    if ((uptr)d<(uptr)s || (uptr)d-(uptr)s>=n) {
        copy_forward(d,s,n);return dst;
    }
    while (n>=sizeof(memory_word)) {
        n-=sizeof(memory_word);
        *(memory_word *)(d+n)=*(const memory_word *)(s+n);
    }
    while (n) {
        --n;
        d[n] = s[n];
    }
    return dst;
}
int memcmp(const void *a, const void *b, usize n) {
    const u8 *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        ++x;
        ++y;
    }
    return 0;
}
usize strlen(const char *s) {
    usize n = 0;
    while (s[n])
        ++n;
    return n;
}
usize strnlen(const char *s, usize cap) {
    usize n = 0;
    while (n < cap && s[n])
        ++n;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (u8)*a - (u8)*b;
}
int strncmp(const char *a, const char *b, usize n) {
    while (n--) {
        if (*a != *b || !*a)
            return (u8)*a - (u8)*b;
        ++a;
        ++b;
    }
    return 0;
}
usize strlcpy(char *d, const char *s, usize cap) {
    usize n = strlen(s);
    if (cap) {
        usize k = MIN(n, cap - 1);
        memcpy(d, s, k);
        d[k] = 0;
    }
    return n;
}
int parse_u32(const char *s, u32 *out) {
    if (!*s)
        return -1;
    u32 n = 0;
    while (*s) {
        if (*s < '0' || *s > '9')
            return -1;
        u32 d = (u32)(*s++ - '0');
        if (n > (0xffffffffu - d) / 10u)
            return -1;
        n = n * 10 + d;
    }
    *out = n;
    return 0;
}
usize number(char *dst, u32 n, u32 base) {
    char tmp[32];
    usize k = 0;
    do {
        tmp[k++] = "0123456789abcdef"[n % base];
        n /= base;
    } while (n);
    for (usize i = 0; i < k; ++i)
        dst[i] = tmp[k - i - 1];
    dst[k] = 0;
    return k;
}
u32 crc32(const void *data, usize n) {
    const u8 *p = data;
    u32 c = 0xffffffffu;
    while (n--) {
        c ^= *p++;
        for (u32 i = 0; i < 8; ++i)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

usize number64(char *dst, u64 n, u32 base) {
    const char digits[] = "0123456789abcdef";
    char reverse[65]; usize count = 0;
    if (base < 2 || base > 16) { dst[0] = 0; return 0; }
    do { reverse[count++] = digits[n % base]; n /= base; } while (n);
    for (usize i = 0; i < count; ++i) dst[i] = reverse[count-i-1];
    dst[count] = 0; return count;
}
