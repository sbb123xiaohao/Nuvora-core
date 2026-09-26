#ifndef NV_TYPES_H
#define NV_TYPES_H
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed int i32;
typedef signed long long i64;
typedef __SIZE_TYPE__ usize;
typedef __UINTPTR_TYPE__ uptr;
typedef __INTPTR_TYPE__ iptr;
typedef _Bool bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define PACKED __attribute__((packed))
#define ALIGNED(n) __attribute__((aligned(n)))
#define NORETURN __attribute__((noreturn))
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
/* Widen the mask before complementing it: PAGE is u32 even for u64 addresses. */
#define ALIGN_UP(a, n) (((a) + (n) - 1) & ~((__typeof__((a) + (n)))((n) - 1)))
_Static_assert(sizeof(u32) == 4, "32-bit ABI");
#endif
