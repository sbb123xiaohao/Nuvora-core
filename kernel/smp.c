#include "kernel.h"
/* BSP owns process scheduling, mutable page tables and device I/O. APs may
 * use the locked allocators and kernel-only work buffers; no user pointers. */
struct worker {
    u32 apic, state, pending, done; /* state: 0 absent, 1 starting, 2 online, 3 failed */
    void (*fn)(void *, u32, u32);
    void *context;
    u32 participants;
    uptr stack;
} ALIGNED(64);
static struct worker workers[NV_CPU_MAX];
static u32 online = 1;
static volatile u32 *lapic;
extern const u8 ap_trampoline[], ap_trampoline_end[];
static u64 rdmsr(u32 reg) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(reg));
    return (u64)hi << 32 | lo;
}
static void wrmsr(u32 reg, u64 value) {
    __asm__ volatile("wrmsr" :: "c"(reg), "a"((u32)value), "d"((u32)(value >> 32)) : "memory");
}
static void apic_write(u32 offset, u32 value) {
    lapic[offset / 4] = value;
    (void)lapic[0x20 / 4];
}
/* Boot-time PIT2 delay, independent of the BSP scheduler timer. */
static bool delay_us(u32 us) {
    u32 count = (us * 1193u + 999) / 1000;
    if (!count || count > 65535) return false;
    u8 old = inb(0x61);
    outb(0x61, old & ~3u); outb(0x43, 0xb0);
    outb(0x42, (u8)count); outb(0x42, (u8)(count >> 8));
    outb(0x61, (old & ~2u) | 1u);
    bool ok = false;
    for (u32 n = 0; n < 10000000; ++n) {
        if (inb(0x61) & 0x20) { ok = true; break; }
        nv_cpu_relax();
    }
    outb(0x61, old); return ok;
}
static bool ipi(u32 id, u32 command) {
    for (u32 n = 0; n < 1000000; ++n) {
        if (!(lapic[0x300 / 4] & (1u << 12))) {
            apic_write(0x310, id << 24); apic_write(0x300, command);
            for (u32 j = 0; j < 1000000; ++j) {
                if (!(lapic[0x300 / 4] & (1u << 12))) return true;
                nv_cpu_relax();
            }
            return false;
        }
        nv_cpu_relax();
    }
    return false;
}
u32 smp_online(void) { return online; }
void smp_eoi(void) { if (lapic) apic_write(0xb0, 0); }
static NORETURN void smp_ap_entry(u32 slot) {
    arch_ap_init(slot, workers[slot].stack);
    wrmsr(0x1b, rdmsr(0x1b) | (1u << 11));
    apic_write(0xf0, 0x1ff); apic_write(0x80, 0);
    apic_write(0x320, 1u << 16); apic_write(0x350, 1u << 16);
    apic_write(0x360, 1u << 16); apic_write(0x370, 1u << 16);
    struct fp_state initial;
    cpu_fp_reset(&initial); cpu_fp_restore(&initial);
    u32 starting = 1;
    if (!__atomic_compare_exchange_n(&workers[slot].state, &starting, 2, false,
                                    __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        for (;;) __asm__ volatile("cli; hlt");
    for (;;) {
        irq_disable();
        if (__atomic_load_n(&workers[slot].pending, __ATOMIC_ACQUIRE)) {
            /* The BSP may have recycled a kernel stack since the last job.
             * Flush shared translations while all VM mutations are quiescent. */
            load_cr3((uptr)kernel_pd);
            workers[slot].fn(workers[slot].context, slot, workers[slot].participants);
            __atomic_store_n(&workers[slot].pending, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&workers[slot].done, 1, __ATOMIC_RELEASE);
        } else {
            /* STI shadow closes the check-to-sleep race with a wake IPI. */
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }
}
static bool trampoline_ram(void) {
    bool available = false;
    for (u32 i = 0; i < boot_info->mem_count; ++i) {
        const struct boot_mem_entry *m = &boot_info->mem[i];
        if (m->length > ~0ull - m->base) return false;
        if (m->base < 0x9000 && m->base + m->length > 0x7000) {
            if (m->type != 1) return false;
            if (m->base <= 0x7000 && m->base + m->length >= 0x9000) available = true;
        }
    }
    for (u32 i = 0; i < boot_info->res_count; ++i) {
        const struct boot_range *r = &boot_info->res[i];
        if (r->length > ~0ull - r->base ||
            (r->base < 0x9000 && r->base + r->length > 0x7000)) return false;
    }
    return available;
}
void smp_init(bool disabled) {
    const struct nv_madt *m = acpi_madt();
    struct nv_cpu_info cpu; cpu_get_info(&cpu);
    if (disabled || !m->valid || m->cpu_count < 2 || !(cpu.features_edx & (1u << 9)) ||
        (rdmsr(0x1b) & (1u << 10)) || m->lapic >> cpu.physical_bits || !trampoline_ram()) {
        kprintf("[info] SMP: BSP only; disabled=%u MADT=%u CPUs=%u APIC=%u low-RAM=%u\n",
                disabled, m->valid, m->cpu_count, !!(cpu.features_edx & (1u << 9)), trampoline_ram());
        return;
    }
    if ((rdmsr(0x1b) & 0xfffff000ull) != m->lapic) return;
    lapic = vm_mmio_map(m->lapic, PAGE);
    if (!lapic) return;
    wrmsr(0x1b, rdmsr(0x1b) | (1u << 11)); apic_write(0xf0, 0x1ff);
    workers[0].apic = lapic[0x20 / 4] >> 24; workers[0].state = 2;
    usize size = (usize)(ap_trampoline_end - ap_trampoline);
    if (size > PAGE) panic("AP trampoline overflow");
    memcpy((void *)0x7000, ap_trampoline, size);
    vm_ap_bootstrap(); /* RX code, NX parameters, already physically reserved */
    for (u32 i = 0; i < m->cpu_count && online < NV_CPU_MAX; ++i) {
        u32 id = m->cpus[i].apic_id, slot = online;
        if (id == workers[0].apic || id >= 255) continue;
        uptr stack = page_alloc_order(2);
        if (!stack) break;
        for (u32 j = 0; j < 4; ++j) page_pin(stack + j * PAGE);
        workers[slot].stack = (uptr)phys_ptr(stack) + 4 * PAGE;
        workers[slot].apic = id;
        __atomic_store_n(&workers[slot].state, 1, __ATOMIC_RELAXED);
        u64 params[3] = {(u32)(uptr)kernel_pd | ((u64)slot << 32),
                         workers[slot].stack, (uptr)smp_ap_entry};
        memcpy((void *)0x8000, params, sizeof(params));
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        bool ok = ipi(id, 0xc500) && delay_us(10000) && ipi(id, 0x8500) &&
                  delay_us(200) && ipi(id, 0x607) && delay_us(200) && ipi(id, 0x607);
        for (u32 wait = 0; ok && wait < 100; ++wait) {
            if (__atomic_load_n(&workers[slot].state, __ATOMIC_ACQUIRE) == 2) break;
            ok = delay_us(1000);
        }
        u32 starting = 1;
        if (__atomic_compare_exchange_n(&workers[slot].state, &starting, 3, false,
                                         __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            kprintf("[info] AP %u timeout; bootstrap and stack quarantined\n", id);
            break; /* Never reuse parameters while a late AP could read them. */
        }
        if (starting != 2) break;
        ++online;
    }
    kprintf("[ok] SMP: %u CPU(s) online; MADT %u enabled, %u omitted; AP kernel workers\n",
            online, m->cpu_count, m->omitted);
}
/* Synchronous BSP-only dispatch: join before the caller can release buffers. */
void smp_parallel(void (*fn)(void *, u32, u32), void *context) {
    uptr flags = irq_save();
    for (u32 i = 1; i < online; ++i) {
        workers[i].fn = fn; workers[i].context = context; workers[i].participants = online;
        __atomic_store_n(&workers[i].done, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&workers[i].pending, 1, __ATOMIC_RELEASE);
        if (!ipi(workers[i].apic, 0xf0)) panic("SMP work IPI failed");
    }
    fn(context, 0, online);
    for (u32 i = 1; i < online; ++i)
        while (!__atomic_load_n(&workers[i].done, __ATOMIC_ACQUIRE)) nv_cpu_relax();
    irq_restore(flags);
}
struct stress { struct nv_spinlock lock; u32 count, failed, mask; };
static void stress_worker(void *context, u32 slot, u32 participants) {
    (void)participants;
    struct stress *s = context;
    for (u32 i = 0; i < 512; ++i) {
        uptr p = page_alloc(); u8 *a = kmalloc(17 + (i % 2000));
        bool bad = !p || !a;
        if (!bad) {
            volatile u32 *v = phys_ptr(p); v[0] = slot + 1; v[1023] = i;
            memset(a, (int)slot, 17 + (i % 2000));
            bad = v[0] != slot + 1 || v[1023] != i;
        }
        nv_spin_lock(&s->lock);
        ++s->count; s->failed += bad; s->mask |= 1u << slot;
        nv_spin_unlock(&s->lock);
        kfree(a); if (p) page_free(p);
    }
}
void smp_selftest(void) {
    u32 before = pages_free(), used = heap_used(); struct stress s = {0};
    smp_parallel(stress_worker, &s);
    if (s.failed || s.count != online * 512 || pages_free() != before || heap_used() != used ||
        s.mask != (u32)((1ull << online) - 1)) panic("SMP allocator/lock selftest");
    kprintf("[ok] SMP allocator: %u workers, %u operations, no leaks\n", online, s.count);
}
