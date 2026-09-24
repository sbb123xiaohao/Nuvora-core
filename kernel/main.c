#include "kernel.h"
extern const u8 archive_start[], archive_end[];
bool test_mode;
const struct boot_info *boot_info;
static struct boot_info multiboot_boot;
/* The x64 assembly entry initially maps only the first GiB. Boot metadata
 * must be readable before memory_init installs the full kernel mappings. */
#define BOOT_READ_LIMIT (1u << 30)
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
static void reserve_boot_range(struct boot_info *bi, u64 base, u64 length) {
    if (bi->res_count == NV_BOOT_RES_MAX)
        panic("too many boot reservations");
    bi->res[bi->res_count++] = (struct boot_range){base, length};
}
/* Translate Multiboot v1 data into the neutral boot_info. Everything the
 * kernel still needs to read later is recorded as a reserved range. */
static void boot_from_multiboot(u32 magic, const struct multiboot *mb, struct boot_info *bi) {
    memset(bi, 0, sizeof(*bi));
    if (magic != 0x2badb002 || !mb || (uptr)mb > BOOT_READ_LIMIT - sizeof(*mb))
        panic("Multiboot v1 boot required");
    if (mb->flags & (1u << 6)) {
        if (!mb->mmap_addr || mb->mmap_length > 1024u * 1024u ||
            mb->mmap_addr > BOOT_READ_LIMIT - mb->mmap_length)
            panic("invalid memory map");
        u32 pos = mb->mmap_addr, end = pos + mb->mmap_length;
        while (pos < end) {
            if (end - pos < sizeof(struct mmap_entry))
                panic("truncated memory map");
            const struct mmap_entry *m = (void *)(uptr)pos;
            if (m->size < 20 || m->size > end - pos - 4)
                panic("bad memory map entry");
            if (m->len && (m->base > ~0ull - m->len))
                panic("memory map entry overflow");
            if (bi->mem_count == NV_BOOT_MEM_MAX)
                panic("too many memory map entries");
            bi->mem[bi->mem_count++] =
                (struct boot_mem_entry){m->base, m->len, m->type == 1 ? 1u : 0u};
            pos += m->size + 4;
        }
    } else if (mb->flags & 1) {
        bi->mem[bi->mem_count++] = (struct boot_mem_entry){0x100000ull, (u64)mb->mem_upper * 1024u, 1};
    } else
        panic("bootloader provided no RAM map");
    reserve_boot_range(bi, (uptr)mb, sizeof(*mb));
    if ((mb->flags & (1u << 3)) && mb->mods_count) {
        if (mb->mods_count > NV_BOOT_RES_MAX - bi->res_count - 1 || !mb->mods_addr ||
            mb->mods_addr > BOOT_READ_LIMIT - mb->mods_count * 16)
            panic("invalid boot modules");
        reserve_boot_range(bi, mb->mods_addr, mb->mods_count * 16);
        const u32 *m = (void *)(uptr)mb->mods_addr;
        for (u32 i = 0; i < mb->mods_count; ++i) {
            if (m[i * 4 + 1] < m[i * 4])
                panic("invalid boot module range");
            reserve_boot_range(bi, m[i * 4], (u64)m[i * 4 + 1] - m[i * 4]);
        }
    }
    if ((mb->flags & (1u << 2)) && mb->cmdline &&
        mb->cmdline <= BOOT_READ_LIMIT - sizeof(bi->cmdline)) {
        const char *src = (const void *)(uptr)mb->cmdline;
        u32 n = (u32)strnlen(src, sizeof(bi->cmdline) - 1);
        memcpy(bi->cmdline, src, n);
    }
}
/* Test-only firmware-map fixture: usable RAM remains ample at 32 MiB, but
 * page-sized holes prevent an 8 MiB physically contiguous allocation. */
static void boot_memory_fixture(struct boot_info *bi) {
    if (!boot_option(bi->cmdline, "nv.test=1") ||
        !boot_option(bi->cmdline, "nv.memory-test=fragmented"))
        return;
    for (u64 hole = 8u * 1024u * 1024u; hole < 32u * 1024u * 1024u;
         hole += 4u * 1024u * 1024u)
        reserve_boot_range(bi, hole, PAGE);
}
void kernel_main(u32 magic, const struct multiboot *mb) {
    boot_from_multiboot(magic, mb, &multiboot_boot);
    boot_memory_fixture(&multiboot_boot);
    kernel_start(&multiboot_boot);
}
void kernel_uefi_main(const struct boot_info *bi) {
    /* Copy while firmware mappings are still active. The handover and active
     * stack must belong to the kernel before its page tables replace CR3. */
    memcpy(&multiboot_boot, bi, sizeof(multiboot_boot));
    boot_memory_fixture(&multiboot_boot);
    kernel_start(&multiboot_boot);
}
void kernel_start(const struct boot_info *bi) {
    boot_info = bi;
    console_init(bi);
    kprintf("\nNuvora Core " NV_VERSION "\n" NV_ARCH_NAME
            " kernel | private ABI | built from original sources\n\n");
    test_mode = boot_option(bi->cmdline, "nv.test=1");
    arch_init();
    memory_init(bi);
    cpu_init();
    acpi_init(boot_option(bi->cmdline, "nv.no-ecam=1"), bi->rsdp);
    kprintf("[ok] GDT, TSS, IDT, PIT 100 Hz, supervisor paging\n");
    memory_selftest();
    console_fb_enable();
    task_init();
    fs_init();
    fs_unpack(archive_start, (usize)(archive_end - archive_start));
    kprintf("[ok] ramfs, devfs, proc views, embedded ELF programs\n");
    disk_init();
    store_init();
    gpu_init();
    usb_init();
    kprintf("[ok] %u MiB managed RAM, %u free pages\n", pages_total() / 256, pages_free());
    const char *program = test_mode ? "/apps/probe" : "/apps/loom";
    if (test_mode && boot_option(bi->cmdline, "nv.init-fault=1"))
        program = "/apps/fault";
    int pid = task_spawn(program, "", NULL);
    if (pid < 0) {
        kprintf("spawn error %d\n", pid);
        panic("cannot load initial process");
    }
    if (test_mode && boot_option(bi->cmdline, "nv.guard-test=lower")) {
        kprintf("[test] forcing kernel stack overflow\n");
        arch_guard_probe((uptr)tasks[0].kstack);
    }
    if (test_mode && boot_option(bi->cmdline, "nv.guard-test=upper")) {
        kprintf("[test] touching upper kernel stack guard\n");
        *(volatile u8 *)((u8 *)tasks[0].kstack + KSTACK_SIZE) = 1;
        panic("upper stack guard missing");
    }
    kprintf("[ok] entering Ring 3: %s, pid %u\n\n", program, (uptr)pid);
    task_start(pid);
}
