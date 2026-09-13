#include "runtime.h"
extern void _start(void);
int user_main(const char *args) {
    if (app_help("fault", args))
        return 0;
#ifdef __x86_64__
    if (!strcmp(args, "heap-exec")) {
        u8 *p = grow(1);
        if ((iptr)p < 0)
            return 98;
        p[0] = 0xc3;
        ((void (*)(void))(uptr)p)();
        return 99;
    }
    if (!strcmp(args, "stack-exec")) {
        u8 code[1] = {0xc3};
        ((void (*)(void))(uptr)code)();
        return 99;
    }
#endif
    if (!strcmp(args, "kernel")) {
        volatile u32 v = *(volatile u32 *)0x100000;
        (void)v;
    } else if (!strcmp(args, "text"))
        *(volatile u8 *)(uptr)_start = 0;
    else if (!strcmp(args, "io"))
        __asm__ volatile("cli");
    else if (!strcmp(args, "divide")) {
        u32 divisor = 0;
        __asm__ volatile("xorl %%edx,%%edx; divl %0" ::"c"(divisor), "a"(1) : "edx", "cc");
    } else if (!strcmp(args, "opcode"))
        __asm__ volatile("ud2");
    else if (!strcmp(args, "fpu"))
        __asm__ volatile("fninit");
    else if (!strcmp(args, "guard"))
        *(volatile u32 *)(0x7fff0000u - 8 * NV_PAGE - 4) = 1;
    else if (!strcmp(args, "peer")) {
        volatile u32 v = *(volatile u32 *)0x50000000;
        (void)v;
    } else {
        u32 address = 0;
        __asm__ volatile("movl $1,(%0)" ::"r"(address) : "memory");
    }
    return 99;
}
