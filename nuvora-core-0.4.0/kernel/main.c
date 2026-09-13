#include "kernel.h"
extern const u8 archive_start[], archive_end[];
bool test_mode;
static bool boot_option(const char *command, const char *option) {
    usize n = strlen(option);
    for (const char *p = command; *p;) {
        while (*p == ' ' || *p == '\t')
            ++p;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t')
            ++p;
        if ((usize)(p - start) == n && !memcmp(start, option, n))
            return true;
    }
    return false;
}
void kernel_main(u32 magic, const struct multiboot *mb) {
    console_init();
    kprintf("\nNuvora Core " NV_VERSION "\n" NV_ARCH_NAME
            " kernel | private ABI | built from original sources\n\n");
    if (magic != 0x2badb002 || !mb)
        panic("Multiboot v1 boot required");
    char command[512] = {0};
    if ((mb->flags & (1u << 2)) && mb->cmdline && mb->cmdline < PHYS_LIMIT - sizeof(command)) {
        const char *src = (const void *)(uptr)mb->cmdline;
        u32 n = strnlen(src, sizeof(command) - 1);
        memcpy(command, src, n);
    }
    test_mode = boot_option(command, "nv.test=1");
    arch_init();
    memory_init(mb);
    kprintf("[ok] GDT, TSS, IDT, PIT 100 Hz, supervisor paging\n");
    memory_selftest();
    task_init();
    fs_init();
    fs_unpack(archive_start, (usize)(archive_end - archive_start));
    kprintf("[ok] ramfs, devfs, proc views, embedded ELF programs\n");
    disk_init();
    store_init();
    usb_init();
    kprintf("[ok] %u MiB managed RAM, %u free pages\n", pages_total() / 256, pages_free());
    const char *program = test_mode ? "/apps/probe" : "/apps/loom";
    if (test_mode && boot_option(command, "nv.init-fault=1"))
        program = "/apps/fault";
    int pid = task_spawn(program, "", NULL);
    if (pid < 0) {
        kprintf("spawn error %d\n", pid);
        panic("cannot load initial process");
    }
    if (test_mode && boot_option(command, "nv.guard-test=lower")) {
        kprintf("[test] forcing kernel stack overflow\n");
        arch_guard_probe((uptr)tasks[0].kstack);
    }
    if (test_mode && boot_option(command, "nv.guard-test=upper")) {
        kprintf("[test] touching upper kernel stack guard\n");
        *(volatile u8 *)((u8 *)tasks[0].kstack + KSTACK_SIZE) = 1;
        panic("upper stack guard missing");
    }
    kprintf("[ok] entering Ring 3: %s, pid %u\n\n", program, (uptr)pid);
    task_start(pid);
}
