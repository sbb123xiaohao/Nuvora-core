#ifdef __x86_64__
#include "kernel.h"
struct table_ptr64 {
    u16 limit;
    u64 base;
} PACKED;
struct idt_entry64 {
    u16 low, selector;
    u8 ist, flags;
    u16 middle;
    u32 high, zero;
} PACKED;
struct tss64 {
    u32 reserved0;
    u64 rsp[3], reserved1, ist[7], reserved2;
    u16 reserved3, iomap;
} PACKED;
_Static_assert(sizeof(struct tss64) == 104, "long-mode TSS");
static u64 gdt[7];
static struct idt_entry64 idt[256];
static struct tss64 tss;
static u8 emergency_stack[8192] ALIGNED(16);
extern uptr isr_table[];
extern void gdt_load(const struct table_ptr64 *);
volatile u32 ticks;
void arch_set_stack(uptr top) {
    tss.rsp[0] = top;
}
static void gate(u32 v, uptr a, u8 flags, u8 ist) {
    idt[v] = (struct idt_entry64){(u16)a, 8, ist, flags, (u16)(a >> 16), (u32)(a >> 32), 0};
}
void arch_init(void) {
    gdt[1] = 0x00af9a000000ffffull;
    gdt[2] = 0x00cf92000000ffffull;
    gdt[3] = 0x00affa000000ffffull;
    gdt[4] = 0x00cff2000000ffffull;
    memset(&tss, 0, sizeof(tss));
    tss.iomap = sizeof(tss);
    tss.ist[0] = (uptr)emergency_stack + sizeof(emergency_stack);
    u64 b = (uptr)&tss, limit = sizeof(tss) - 1;
    gdt[5] = (limit & 0xffff) | ((b & 0xffffff) << 16) | (0x89ull << 40) |
             ((limit & 0xf0000) << 32) | ((b & 0xff000000) << 32);
    gdt[6] = b >> 32;
    struct table_ptr64 gp = {sizeof(gdt) - 1, (uptr)gdt};
    gdt_load(&gp);
    for (u32 i = 0; i < 48; ++i)
        gate(i, isr_table[i], 0x8e, i == 8 ? 1 : 0);
    gate(0x81, isr_table[48], 0xee, 0);
    struct table_ptr64 ip = {sizeof(idt) - 1, (uptr)idt};
    __asm__ volatile("lidt %0" ::"m"(ip));
    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, 0x20);
    outb(0xa1, 0x28);
    outb(0x21, 4);
    outb(0xa1, 2);
    outb(0x21, 1);
    outb(0xa1, 1);
    outb(0x21, 0xfc);
    outb(0xa1, 0xff);
    u16 d = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, (u8)d);
    outb(0x40, (u8)(d >> 8));
}
struct frame *interrupt_dispatch(struct frame *f) {
    if (f->vector == 0x81)
        return syscall_dispatch(f);
    if (f->vector == 8)
        panic("double fault on emergency stack");
    if (f->vector < 32) {
        uptr address = 0;
        if (f->vector == 14)
            __asm__ volatile("mov %%cr2,%0" : "=r"(address));
        if ((f->cs & 3) == 3 && current) {
            kprintf("\n[isolate x64] pid=%u trap=%u error=%x rip=%x address=%x\n", current->pid,
                    (u32)f->vector, (u32)f->error, (u32)f->eip, (u32)address);
            task_exit(128 + (int)f->vector);
            return schedule(f);
        }
        kprintf("trap=%u error=%x rip=%x address=%x\n", (u32)f->vector, (u32)f->error, (u32)f->eip,
                (u32)address);
        panic("exception in 64-bit supervisor mode");
    }
    if (f->vector == 39) {
        outb(0x20, 0x0b);
        if (!(inb(0x20) & 0x80))
            return f;
    }
    if (f->vector == 47) {
        outb(0xa0, 0x0b);
        if (!(inb(0xa0) & 0x80)) {
            outb(0x20, 0x20);
            return f;
        }
    }
    if (f->vector == 32) {
        ++ticks;
        task_tick();
    }
    if (f->vector == 33)
        keyboard_irq();
    if (f->vector >= 40)
        outb(0xa0, 0x20);
    outb(0x20, 0x20);
    return f->vector == 32 && (f->cs & 3) == 3 && ticks % 5 == 0 ? schedule(f) : f;
}
NORETURN void machine_poweroff(void) {
    irq_disable();
    kprintf("\nNuvora Core halted.\n");
    outw(0x604, 0x2000);
    outw(0xb004, 0x2000);
    for (;;)
        __asm__ volatile("hlt");
}
NORETURN void machine_reboot(void) {
    irq_disable();
    for (u32 i = 0; i < 100000; ++i)
        if (!(inb(0x64) & 2))
            break;
    outb(0x64, 0xfe);
    for (;;)
        __asm__ volatile("hlt");
}
#endif
