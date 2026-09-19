#include "runtime.h"
extern void _start(void);
int user_main(const char *args) {
    if (app_help("fault", args))
        return 0;
#ifdef __x86_64__
    if (!strcmp(args, "physical-alias")) {
        volatile u8 value = *(volatile u8 *)((1ull << 39) + 0x100000u);
        (void)value;
        return 99;
    }
    if (!strcmp(args, "kernel-heap")) {
        volatile u8 value = *(volatile u8 *)(2ull << 39);
        (void)value;
        return 99;
    }
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
    if (!strcmp(args, "mmio")) {
        volatile u32 v = *(volatile u32 *)0x20000000;
        (void)v;
    } else if (!strcmp(args, "kernel")) {
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
    else if (!strcmp(args, "avx"))
        __asm__ volatile(".byte 0xc5,0xf8,0x77"); /* VZEROUPPER: OSXSAVE is off. */
    else if (!strcmp(args, "fpu")) {
        u16 control = 0x037b; /* unmask x87 divide-by-zero */
        __asm__ volatile("fninit; fldcw %0; fldz; fld1; fdiv %%st(1),%%st; fwait" ::"m"(control));
    } else if (!strcmp(args, "sse")) {
        struct nv_cpu_info cpu;
        if (cpu_info(&cpu) < 0 || !(cpu.usable & NV_CPU_SSE))
            return 77;
        u32 mxcsr = 0x1d80, one = 0x3f800000;
        __asm__ volatile(
            "ldmxcsr %0; xorps %%xmm1,%%xmm1; movss %1,%%xmm0; divss %%xmm1,%%xmm0" ::"m"(mxcsr),
            "m"(one)
            : "memory");
        u32 observed, a, b, c, d;
        __asm__ volatile("stmxcsr %0" : "=m"(observed));
        /* QEMU 8.2 TCG records MXCSR flags but does not deliver #XM. Report
         * this as a skip only for that hypervisor, never for physical CPUs. */
        if ((cpu.features_ecx & (1u << 31)) && (observed & 0x204) == 4) {
            char hypervisor[13] = {0};
            __asm__ volatile("cpuid"
                             : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                             : "a"(0x40000000u), "c"(0));
            memcpy(hypervisor, &b, 4);
            memcpy(hypervisor + 4, &c, 4);
            memcpy(hypervisor + 8, &d, 4);
            if (!strcmp(hypervisor, "TCGTCGTCGTCG"))
                return 78;
        }
    } else if (!strcmp(args, "guard"))
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
