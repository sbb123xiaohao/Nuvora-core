#include "runtime.h"

/* This entire program is compiled with the kernel's integer-only C flags.
 * Only the explicit assembly below touches FP/SIMD registers, so ordinary C
 * calls cannot silently overwrite the test patterns. */
static struct nv_cpu_info cpu;
#define LOAD_X(n, o) "movups " #o "(%0),%%xmm" #n ";"
#define SAVE_X(n, o) "movups %%xmm" #n "," #o "(%0);"
#define X8(op) op(0, 0) op(1, 16) op(2, 32) op(3, 48) op(4, 64) op(5, 80) op(6, 96) op(7, 112)
#define X16(op)                                                                                    \
    X8(op)                                                                                         \
    op(8, 128) op(9, 144) op(10, 160) op(11, 176) op(12, 192) op(13, 208) op(14, 224) op(15, 240)
#define XCOUNT 16
static void xmm_load(const u32 *p) {
    __asm__ volatile(X16(LOAD_X)::"r"(p) : "memory");
}
static void xmm_save(u32 *p) {
    __asm__ volatile(X16(SAVE_X)::"r"(p) : "memory");
}
#define LOAD_M(n, o) "movq " #o "(%0),%%mm" #n ";"
#define SAVE_M(n, o) "movq %%mm" #n "," #o "(%0);"
#define M8(op) op(0, 0) op(1, 8) op(2, 16) op(3, 24) op(4, 32) op(5, 40) op(6, 48) op(7, 56)
static void mmx_load(const u64 *p) {
    __asm__ volatile(M8(LOAD_M)::"r"(p) : "memory");
}
static void mmx_save(u64 *p) {
    __asm__ volatile(M8(SAVE_M)::"r"(p) : "memory");
}
static bool initial(void) {
    u16 cw, sw;
    __asm__ volatile("fnstcw %0; fnstsw %1" : "=m"(cw), "=m"(sw));
    if (cw != 0x037f || sw)
        return false;
    u8 state[512] ALIGNED(16);
    if (cpu.fp_mode == NV_FP_FXSAVE) {
        __asm__ volatile("fxsave64 %0" : "=m"(state));
        if (state[4])
            return false;
        for (u32 i = 0; i < 8; ++i)
            for (u32 j = 0; j < 10; ++j)
                if (state[32 + 16 * i + j])
                    return false;
    } else {
        __asm__ volatile("fnsave %0; frstor %0" : "=m"(state)::"memory");
        if (state[8] != 255 || state[9] != 255)
            return false;
        for (u32 i = 28; i < 108; ++i)
            if (state[i])
                return false;
    }
    if (cpu.usable & NV_CPU_SSE) {
        u32 mxcsr, words[4 * XCOUNT];
        __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
        xmm_save(words);
        if (mxcsr != 0x1f80)
            return false;
        for (u32 i = 0; i < ARRAY_LEN(words); ++i)
            if (words[i])
                return false;
    }
    return true;
}
static i32 numbers[8];
static u32 vectors[4 * XCOUNT], mxcsr;
static u16 control_word;
static void load_numbers(void) {
    for (u32 i = 0; i < 8; ++i)
        __asm__ volatile("fildl %0" ::"m"(numbers[i]));
}
static void pattern(u32 seed) {
    __asm__ volatile("fninit");
    control_word = 0x037f | ((seed & 3) << 10);
    __asm__ volatile("fldcw %0" ::"m"(control_word));
    for (u32 i = 0; i < 8; ++i)
        numbers[i] = (i32)(seed * 12345 + i * 89);
    load_numbers();
    if (cpu.usable & NV_CPU_SSE) {
        mxcsr = 0x1f80 | ((seed & 3) << 13);
        __asm__ volatile("ldmxcsr %0" ::"m"(mxcsr));
        for (u32 i = 0; i < ARRAY_LEN(vectors); ++i)
            vectors[i] = 0x12340000u * seed + i * 0x10101u;
        xmm_load(vectors);
    }
}
static bool intact(void) {
    u16 cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    if (cw != control_word)
        return false;
    for (u32 i = 0; i < 8; ++i) {
        i32 value;
        __asm__ volatile("fistpl %0" : "=m"(value));
        if (value != numbers[7 - i])
            return false;
    }
    load_numbers();
    if (cpu.usable & NV_CPU_SSE) {
        u32 value, words[4 * XCOUNT];
        __asm__ volatile("stmxcsr %0" : "=m"(value));
        xmm_save(words);
        if (value != mxcsr || memcmp(words, vectors, sizeof(words)))
            return false;
    }
    return true;
}
static int worker(u32 seed) {
    pattern(seed);
    /* clock_ticks does not reschedule: during this phase only timer preemption
     * can switch between the two workers. No yield/sleep in this loop. */
    u32 start = clock_ticks();
    do {
        u32 cycles = 2000000u;
        __asm__ volatile("1: sub $1,%%ecx; jnz 1b" : "+c"(cycles)::"cc");
        if (!intact())
            return 11;
    } while (clock_ticks() - start < 25);
    for (u32 i = 0; i < 48; ++i) {
        if (i & 1)
            yield();
        else
            nap(1);
        if (!intact())
            return 12;
    }
    if (cpu.usable & NV_CPU_MMX) {
        u64 want[8], got[8];
        for (u32 i = 0; i < 8; ++i)
            want[i] = ((u64)(seed * 0x1234567u) << 32) | (seed + i);
        mmx_load(want);
        for (u32 i = 0; i < 48; ++i) {
            yield();
            mmx_save(got);
            if (memcmp(want, got, sizeof(want)))
                return 13;
        }
        __asm__ volatile("emms");
    }
    return 0;
}
int user_main(const char *args) {
    if (app_help("vector", args))
        return 0;
    if (cpu_info(&cpu) != 1 || !initial())
        return 10;
    if (!strcmp(args, "a"))
        return worker(1);
    if (!strcmp(args, "b"))
        return worker(2);
    if (!strcmp(args, "clean"))
        return 0;
    if (!strcmp(args, "exec")) {
        pattern(3);
        if (exec_program("/apps/missing", "") != -NV_ENOENT || !intact())
            return 14;
        exec_program("/apps/vector", "clean");
        return 15;
    }
    if (*args) {
        println("Usage: forge vector");
        return 1;
    }
    pattern(3); /* Children must start clean even when the parent owns live FP state. */
    int a = spawn("/apps/vector", "a"), b = spawn("/apps/vector", "b");
    int first = a > 0 ? wait_task(a) : a, second = b > 0 ? wait_task(b) : b;
    int e = spawn("/apps/vector", "exec"), third = e > 0 ? wait_task(e) : e;
    bool ok = a > 0 && b > 0 && e > 0 && !first && !second && !third && intact();
    print("VECTOR workers: ");
    print_u32(first);
    print(" ");
    print_u32(second);
    print(" exec: ");
    print_u32(third);
    print("\n");
    println(ok ? "VECTOR RESULT: PASS" : "VECTOR RESULT: FAIL");
    return ok ? 0 : 1;
}
