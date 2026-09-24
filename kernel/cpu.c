#include "kernel.h"

static struct nv_cpu_info identity;
static const i32 fp_zero;
static void cpuid(u32 leaf, u32 sub, u32 *a, u32 *b, u32 *c, u32 *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
void cpu_fp_reset(struct fp_state *state) {
    memset(state, 0, sizeof(*state));
    state->words[0] = 0x037f; /* x87 masked exceptions, round to nearest */
    if (identity.fp_mode == NV_FP_FXSAVE)
        state->words[6] = 0x1f80; /* MXCSR, all exceptions masked */
    else
        state->words[2] = 0xffff; /* full x87 tag word: all eight slots empty */
}
void cpu_fp_save(struct fp_state *state) {
    if (identity.fp_mode == NV_FP_FXSAVE) {
        __asm__ volatile("fxsave64 %0" : "=m"(*state)::"memory");
    } else
        __asm__ volatile("fnsave %0" : "=m"(*state)::"memory");
}
void cpu_fp_restore(const struct fp_state *state) {
    if (identity.fp_mode == NV_FP_FXSAVE) {
        /* Some older AMD CPUs do not restore x87 FIP/FDP when ES=0.
         * Overwrite their previous owner before FXRSTOR (also clears pending
         * exceptions). The dummy operand has a fixed kernel address. */
        __asm__ volatile("fninit; fildl %0" ::"m"(fp_zero) : "memory");
        __asm__ volatile("fxrstor64 %0" ::"m"(*state) : "memory");
    } else
        __asm__ volatile("frstor %0" ::"m"(*state) : "memory");
}
void cpu_init(void) {
    u32 a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d);
    identity.max_basic = a;
    memcpy(identity.vendor, &b, 4);
    memcpy(identity.vendor + 4, &d, 4);
    memcpy(identity.vendor + 8, &c, 4);
    cpuid(1, 0, &a, &b, &c, &d);
    u32 family = (a >> 8) & 15;
    identity.family = family + (family == 15 ? (a >> 20) & 255 : 0);
    identity.model = ((a >> 4) & 15) | ((family == 6 || family == 15) ? (a >> 12) & 0xf0 : 0);
    identity.stepping = a & 15;
    identity.features_edx = d;
    identity.features_ecx = c;
    identity.bits = sizeof(uptr) * 8;
    identity.version = 1;
    identity.online_cpus = 1; /* APs are not started; do not report CPUID threads as online. */
    identity.fp_mode = d & (1u << 24) ? NV_FP_FXSAVE : NV_FP_X87;
    identity.usable = NV_CPU_X87 | ((d & (1u << 23)) ? NV_CPU_MMX : 0);
    if (identity.fp_mode == NV_FP_FXSAVE && (d & (1u << 25))) {
        identity.usable |= NV_CPU_SSE;
        if (d & (1u << 26))
            identity.usable |= NV_CPU_SSE2;
    }
    identity.physical_bits = d & (1u << 6) ? 36 : 32;
    if (identity.max_basic >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        identity.leaf7_ebx = b;
    }
    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    identity.max_extended = a;
    if (a >= 0x80000001u) {
        cpuid(0x80000001u, 0, &a, &b, &c, &d);
        identity.extended_edx = d;
    }
    if (identity.max_extended >= 0x80000004u) {
        for (u32 i = 0; i < 3; ++i) {
            u32 words[4];
            cpuid(0x80000002u + i, 0, &words[0], &words[1], &words[2], &words[3]);
            memcpy(identity.brand + i * 16, words, 16);
        }
    } else
        strlcpy(identity.brand, "Brand string unavailable", sizeof(identity.brand));
    if (identity.max_extended >= 0x80000008u) {
        cpuid(0x80000008u, 0, &a, &b, &c, &d);
        if ((a & 255) >= 32 && (a & 255) <= 52)
            identity.physical_bits = a & 255;
    }
    uptr cr0, cr4;
    __asm__ volatile("mov %%cr0,%0; mov %%cr4,%1" : "=r"(cr0), "=r"(cr4));
    cr0 = (cr0 | 0x22u) & ~(uptr)0xcu;                   /* NE, MP; clear EM and TS */
    cr4 &= ~(uptr)((1u << 9) | (1u << 10) | (1u << 18)); /* OSXSAVE stays off */
    if (identity.fp_mode == NV_FP_FXSAVE)
        cr4 |= 1u << 9;
    if (identity.usable & NV_CPU_SSE)
        cr4 |= 1u << 10;
    __asm__ volatile("mov %0,%%cr0; mov %1,%%cr4" ::"r"(cr0), "r"(cr4) : "memory");
    identity.features_ecx &= ~(1u << 27); /* CPUID.OSXSAVE reflects the new CR4. */
    struct fp_state initial;
    cpu_fp_reset(&initial);
    cpu_fp_restore(&initial);
    kprintf("[ok] CPU %s family %u model %u; %s context; 1 CPU online\n", identity.vendor,
            identity.family, identity.model, identity.fp_mode == NV_FP_FXSAVE ? "FXSAVE" : "x87");
}
void cpu_get_info(struct nv_cpu_info *out) {
    *out = identity;
    out->online_cpus = smp_online();
}
int cpu_ioctl(u32 op, u32 user_ptr) {
    (void)op;
    (void)user_ptr;
    /* No CPU control operations yet (NV_HARDWARE/NV_HW_CPU already covers
     * read-only query). Reserved for future work, e.g. ROADMAP.md #8
     * (per-CPU-capability XSAVE/AVX enablement). */
    return -NV_ENOSYS;
}
