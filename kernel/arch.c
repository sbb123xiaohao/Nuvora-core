#ifndef __x86_64__
#include "kernel.h"
struct table_ptr {
    u16 limit;
    u32 base;
} PACKED;
struct gdt_entry {
    u16 low, base_low;
    u8 base_mid, access, flags, base_high;
} PACKED;
struct idt_entry {
    u16 low, selector;
    u8 zero, flags;
    u16 high;
} PACKED;
struct tss32 {
    u32 prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} PACKED;
static struct gdt_entry gdt[7];
static struct idt_entry idt[256];
static struct tss32 tss, fault_tss;
static u8 emergency_stack[8192] ALIGNED(16);
extern void double_fault_entry(void);
NORETURN void double_fault_panic(void) {
    panic("double fault on emergency stack");
}
extern void gdt_load(const struct table_ptr *);
extern u32 isr_table[];
volatile u32 ticks;
static void segment(u32 i, u32 base, u32 limit, u8 access, u8 flags) {
    gdt[i] = (struct gdt_entry){
        (u16)limit,      (u16)base, (u8)(base >> 16), access, (u8)((limit >> 16) & 15) | flags,
        (u8)(base >> 24)};
}
static void gate(u32 v, u32 address, u8 flags) {
    idt[v] = (struct idt_entry){(u16)address, 8, 0, flags, (u16)(address >> 16)};
}
void arch_set_stack(u32 top) {
    tss.esp0 = top;
}
void arch_init(void) {
    segment(1, 0, 0xfffff, 0x9a, 0xc0);
    segment(2, 0, 0xfffff, 0x92, 0xc0);
    segment(3, 0, 0xfffff, 0xfa, 0xc0);
    segment(4, 0, 0xfffff, 0xf2, 0xc0);
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = 0x10;
    tss.iomap = sizeof(tss);
    segment(5, (u32)&tss, sizeof(tss) - 1, 0x89, 0);
    fault_tss = (struct tss32){.cr3 = (uptr)kernel_pd,
                               .eip = (uptr)double_fault_entry,
                               .eflags = 2,
                               .esp = (uptr)emergency_stack + sizeof(emergency_stack),
                               .cs = 8,
                               .ss = 0x10,
                               .ds = 0x10,
                               .es = 0x10,
                               .fs = 0x10,
                               .gs = 0x10,
                               .iomap = sizeof(fault_tss)};
    segment(6, (uptr)&fault_tss, sizeof(fault_tss) - 1, 0x89, 0);
    struct table_ptr gp = {sizeof(gdt) - 1, (u32)gdt};
    gdt_load(&gp);
    for (u32 i = 0; i < 48; ++i)
        gate(i, isr_table[i], 0x8e);
    /* A task gate supplies a working stack even when #PF cannot push a frame. */
    idt[8] = (struct idt_entry){.selector = 0x30, .flags = 0x85};
    gate(0x81, isr_table[48], 0xee);
    struct table_ptr ip = {sizeof(idt) - 1, (u32)idt};
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
    u16 divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, (u8)divisor);
    outb(0x40, (u8)(divisor >> 8));
}
struct frame *interrupt_dispatch(struct frame *f) {
    if (f->vector == 0x81)
        return syscall_dispatch(f);
    if (f->vector < 32) {
        u32 fault_address = 0;
        if (f->vector == 14)
            __asm__ volatile("mov %%cr2,%0" : "=r"(fault_address));
        if ((f->cs & 3) == 3 && current) {
            kprintf("\n[isolate] pid=%u trap=%u error=%x eip=%x address=%x\n", current->pid,
                    f->vector, f->error, f->eip, fault_address);
            task_exit(128 + (int)f->vector);
            return schedule(f);
        }
        kprintf("trap=%u error=%x eip=%x address=%x\n", f->vector, f->error, f->eip, fault_address);
        panic("exception in supervisor mode");
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
    if (f->vector == 32 && (f->cs & 3) == 3 && ticks % 5 == 0)
        return schedule(f);
    return f;
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
