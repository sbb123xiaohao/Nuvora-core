/* Nuvora Core x64 UEFI stub. A self-contained EFI application that loads the
 * ELF64 kernel image, converts the EFI memory map into the neutral boot_info
 * handover, exits boot services and enters the kernel in long mode.
 * Built with -fshort-wchar; the binary is produced by scripts/mkuefi.py. */
#include <nv/bootinfo.h>
#include <nv/string.h>
#include <nv/pixel.h>
#include <stddef.h>
#define MSABI __attribute__((ms_abi))
typedef u64 efi_status;
typedef void *efi_handle;
typedef u16 char16;
#define EFI_ERROR_BIT 0x8000000000000000ull
#define EFI_ERROR(s) ((s)&EFI_ERROR_BIT)
#define EFI_SUCCESS 0ull
#define EFI_LOAD_ERROR (1ull | EFI_ERROR_BIT)
#define EFI_INVALID_PARAMETER (2ull | EFI_ERROR_BIT)
#define EFI_UNSUPPORTED (3ull | EFI_ERROR_BIT)
#define EFI_BUFFER_TOO_SMALL (5ull | EFI_ERROR_BIT)
#define EFI_DEVICE_ERROR (7ull | EFI_ERROR_BIT)
#define EFI_NOT_FOUND (14ull | EFI_ERROR_BIT)
enum efi_memory_type {
    EFI_RESERVED = 0,
    EFI_LOADER_CODE = 1,
    EFI_LOADER_DATA = 2,
    EFI_BOOT_SERVICES_CODE = 3,
    EFI_BOOT_SERVICES_DATA = 4,
    EFI_RUNTIME_SERVICES_CODE = 5,
    EFI_RUNTIME_SERVICES_DATA = 6,
    EFI_CONVENTIONAL = 7
};
enum efi_allocate_type { EFI_ANY_PAGES = 0, EFI_ALLOCATE_MAX_ADDRESS = 1, EFI_ALLOCATE_ADDRESS = 2 };
enum efi_pool_type { EFI_LOADER_POOL = 2 };
enum efi_open_mode { EFI_FILE_MODE_READ = 1 };
struct efi_table_header {
    u64 signature;
    u32 revision, header_size, crc32, reserved;
};
struct efi_guid {
    u32 a;
    u16 b, c;
    u8 d[8];
};
struct efi_memory_descriptor {
    u32 type, pad;
    u64 physical_start, virtual_start, number_of_pages, attribute;
};
struct efi_config_table {
    struct efi_guid vendor_guid;
    void *vendor_table;
};
struct efi_simple_text_output {
    void *reset;
    efi_status(MSABI *output_string)(struct efi_simple_text_output *, const char16 *);
};
struct efi_boot_services {
    struct efi_table_header hdr;
    void *raise_tpl, *restore_tpl;
    efi_status(MSABI *allocate_pages)(u32, u32, uptr, u64 *);
    efi_status(MSABI *free_pages)(u64, uptr);
    efi_status(MSABI *get_memory_map)(uptr *, struct efi_memory_descriptor *, uptr *, uptr *, u32 *);
    efi_status(MSABI *allocate_pool)(u32, uptr, void **);
    efi_status(MSABI *free_pool)(void *);
    void *create_event, *set_timer, *wait_for_event, *signal_event, *close_event, *check_event;
    void *install_protocol_interface, *reinstall_protocol_interface,
        *uninstall_protocol_interface;
    efi_status(MSABI *handle_protocol)(efi_handle, const struct efi_guid *, void **);
    void *reserved;
    void *register_protocol_notify, *locate_handle, *locate_device_path,
        *install_configuration_table, *load_image, *start_image, *exit, *unload_image;
    efi_status(MSABI *exit_boot_services)(efi_handle, uptr);
    void *get_next_monotonic_count, *stall;
    efi_status(MSABI *set_watchdog_timer)(uptr, u64, uptr, const char16 *);
    void *connect_controller, *disconnect_controller, *open_protocol, *close_protocol,
        *open_protocol_information, *protocols_per_handle;
    efi_status(MSABI *locate_handle_buffer)(u32, const struct efi_guid *, void *, uptr *,
                                            efi_handle **);
    efi_status(MSABI *locate_protocol)(const struct efi_guid *, void *, void **);
    void *install_multiple_protocol_interfaces, *uninstall_multiple_protocol_interfaces,
        *calculate_crc32, *copy_mem, *set_mem, *create_event_ex;
};
struct efi_system_table {
    struct efi_table_header hdr;
    const char16 *firmware_vendor;
    u32 firmware_revision;
    efi_handle console_in_handle;
    void *con_in;
    efi_handle console_out_handle;
    struct efi_simple_text_output *con_out;
    efi_handle standard_error_handle;
    void *std_err;
    void *runtime_services;
    struct efi_boot_services *boot_services;
    uptr number_of_table_entries;
    struct efi_config_table *configuration_table;
};
struct efi_loaded_image {
    u32 revision;
    efi_handle parent_handle;
    struct efi_system_table *system_table;
    efi_handle device_handle;
    void *file_path, *reserved;
    u32 load_options_size;
    void *load_options;
    void *image_base;
    u64 image_size;
    u32 image_code_type, image_data_type;
    void *unload;
};
struct efi_gop_mode_info {
    u32 version, horizontal, vertical, pixel_format;
    u32 pixel_information[4];
    u32 pixels_per_scanline;
};
struct efi_gop_mode {
    u32 max_mode, mode;
    struct efi_gop_mode_info *info;
    uptr size_of_info;
    u64 framebuffer_base;
    uptr framebuffer_size;
};
struct efi_gop {
    efi_status(MSABI *query_mode)(struct efi_gop *, u32, uptr *, struct efi_gop_mode_info **);
    efi_status(MSABI *set_mode)(struct efi_gop *, u32);
    void *blt;
    struct efi_gop_mode *mode;
};
struct efi_sfs {
    u64 revision;
    efi_status(MSABI *open_volume)(struct efi_sfs *, void **);
};
struct efi_file {
    u64 revision;
    efi_status(MSABI *open)(struct efi_file *, void **, const char16 *, u64, u64);
    efi_status(MSABI *close)(struct efi_file *);
    void *delete_;
    efi_status(MSABI *read)(struct efi_file *, uptr *, void *);
    void *write;
    efi_status(MSABI *get_position)(struct efi_file *, u64 *);
    efi_status(MSABI *set_position)(struct efi_file *, u64);
    void *get_info, *set_info, *flush;
};
static const struct efi_guid loaded_image_guid =
    {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const struct efi_guid gop_guid =
    {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
static const struct efi_guid sfs_guid =
    {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const struct efi_guid acpi20_guid =
    {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static const struct efi_guid acpi10_guid =
    {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
_Static_assert(offsetof(struct efi_system_table, boot_services) == 96, "system table layout");
_Static_assert(offsetof(struct efi_boot_services, get_memory_map) == 56, "boot services layout");
_Static_assert(offsetof(struct efi_boot_services, allocate_pool) == 64, "boot services layout");
_Static_assert(offsetof(struct efi_boot_services, handle_protocol) == 152, "boot services layout");
_Static_assert(offsetof(struct efi_boot_services, exit_boot_services) == 232,
               "boot services layout");
_Static_assert(offsetof(struct efi_boot_services, set_watchdog_timer) == 256,
               "boot services layout");
_Static_assert(offsetof(struct efi_boot_services, locate_handle_buffer) == 312,
               "boot services layout");
_Static_assert(offsetof(struct efi_boot_services, locate_protocol) == 320,
               "boot services layout");
_Static_assert(offsetof(struct efi_gop, mode) == 24, "GOP layout");
_Static_assert(offsetof(struct efi_sfs, open_volume) == 8, "simple file system layout");
_Static_assert(offsetof(struct efi_file, read) == 32, "file protocol layout");
_Static_assert(offsetof(struct efi_file, set_position) == 56, "file protocol layout");
/* The UEFI ELF is linked at 16 MiB and must stay below the stub image. */
#define KERNEL_LOAD_BASE 0x100000ull
#define KERNEL_LOAD_LIMIT 0x02000000ull
struct elf64_ehdr {
    u8 ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct elf64_phdr {
    u32 type, flags;
    u64 offset, vaddr, paddr, filesz, memsz, align;
};
static struct boot_info bi;
static efi_status kernel_load_status = EFI_LOAD_ERROR;
static u64 kernel_first, kernel_limit, pending_entry;
static bool kernel_pages_owned;
static uptr kernel_file_pages;
static const u8 *pending_kernel;
static u8 handover_stack[32768] ALIGNED(16);
_Static_assert(sizeof(struct elf64_ehdr) == 64, "ELF64 header");
_Static_assert(sizeof(struct elf64_phdr) == 56, "ELF64 program header");
extern void kernel_start(const struct boot_info *) __attribute__((sysv_abi));
static u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static void outb(u16 port, u8 v) {
    __asm__ volatile("outb %0,%1" ::"a"(v), "Nd"(port));
}
static void serial_putc(char c) {
    for (u32 t = 100000; t && !(inb(0x3fd) & 0x20); --t) {
    }
    outb(0x3f8, (u8)c);
}
static void serial_text(const char *s) {
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s++);
    }
}
/* Keep early failures visible on machines without a wired serial port. The
 * firmware console is used only before ExitBootServices. */
static struct efi_simple_text_output *early_console;
static void console_text(const char *s) {
    if (!early_console || !early_console->output_string) return;
    char16 line[96];
    u32 count = 0;
    for (; *s; ++s) {
        if (count >= ARRAY_LEN(line) - 3) {
            line[count] = 0;
            early_console->output_string(early_console, line);
            count = 0;
        }
        if (*s == '\n') line[count++] = '\r';
        line[count++] = (u8)*s;
    }
    if (count) {
        line[count] = 0;
        early_console->output_string(early_console, line);
    }
}
static void loader_text(const char *s) {
    serial_text(s);
    console_text(s);
}
static void loader_status(const char *prefix, efi_status s) {
    static const char hex[] = "0123456789abcdef";
    char value[18];
    for (u32 i = 0; i < 16; ++i)
        value[i] = hex[(s >> (60 - 4 * i)) & 15];
    value[16] = '\n'; value[17] = 0;
    loader_text(prefix);
    loader_text(value);
}
static void enable_nx(void) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    lo |= 0x800u; /* EFER.NXE */
    __asm__ volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(0xc0000080u));
}
static bool add_mem(u64 base, u64 length, u32 type) {
    if (!length)
        return true;
    if (bi.mem_count) {
        struct boot_mem_entry *last = &bi.mem[bi.mem_count - 1];
        if (last->type == type && last->base <= ~0ull - last->length &&
            last->base + last->length == base && length <= ~0ull - last->length) {
            last->length += length;
            return true;
        }
    }
    if (bi.mem_count == NV_BOOT_MEM_MAX)
        return false;
    bi.mem[bi.mem_count++] = (struct boot_mem_entry){base, length, type};
    return true;
}
static bool add_res(u64 base, u64 length) {
    if (!length)
        return true;
    if (bi.res_count == NV_BOOT_RES_MAX)
        return false;
    bi.res[bi.res_count++] = (struct boot_range){base, length};
    return true;
}
static bool range_overlaps(u64 a0, u64 a1, u64 b0, u64 b1) {
    return a0 < b1 && b0 < a1;
}
/* The kernel ELF entry is the 32-bit Multiboot gate, so the stub locates the
 * 64-bit uefi_entry symbol in .symtab instead of trusting e_entry. */
static bool find_kernel_entry(const u8 *file, uptr size, const struct elf64_ehdr *eh, u64 *entry) {
    if (eh->shentsize != 64 || !eh->shnum || eh->shoff > size ||
        eh->shnum > (size - eh->shoff) / 64)
        return false;
    u64 symtab_off = 0, symtab_size = 0, symtab_entsize = 0, strtab_off = 0, strtab_size = 0;
    for (u16 i = 0; i < eh->shnum; ++i) {
        const u8 *sh = file + eh->shoff + (u64)i * eh->shentsize;
        u32 sh_type, sh_link;
        u64 sh_offset, sh_size, sh_entsize;
        memcpy(&sh_type, sh + 4, 4);
        memcpy(&sh_link, sh + 40, 4);
        memcpy(&sh_offset, sh + 24, 8);
        memcpy(&sh_size, sh + 32, 8);
        memcpy(&sh_entsize, sh + 56, 8);
        if (sh_type != 2 /* SHT_SYMTAB */)
            continue;
        symtab_off = sh_offset;
        symtab_size = sh_size;
        symtab_entsize = sh_entsize;
        if (sh_link < eh->shnum) {
            const u8 *strsh = file + eh->shoff + (u64)sh_link * eh->shentsize;
            memcpy(&strtab_off, strsh + 24, 8);
            memcpy(&strtab_size, strsh + 32, 8);
        }
    }
    if (!symtab_off || symtab_entsize != 24 || symtab_size % 24 || symtab_off > size ||
        symtab_size > size - symtab_off || strtab_off > size || strtab_size > size - strtab_off)
        return false;
    for (u64 off = 0; off + 24 <= symtab_size; off += 24) {
        const u8 *sym = file + symtab_off + off;
        u32 st_name;
        u16 st_shndx;
        u64 st_value;
        memcpy(&st_name, sym, 4);
        memcpy(&st_shndx, sym + 6, 2);
        memcpy(&st_value, sym + 8, 8);
        if (!st_name || st_name >= strtab_size || strtab_size - st_name < 11 ||
            !st_shndx || st_shndx >= eh->shnum)
            continue;
        const char *name = (const char *)(file + strtab_off + st_name);
        if (!memcmp(name, "uefi_entry", 11) && st_value >= KERNEL_LOAD_BASE &&
            st_value < KERNEL_LOAD_LIMIT) {
            *entry = st_value;
            return true;
        }
    }
    return false;
}
static void free_kernel_file(struct efi_boot_services *bs, u8 *file) {
    if (kernel_file_pages)
        bs->free_pages((u64)(uptr)file, kernel_file_pages);
    else
        bs->free_pool(file);
    kernel_file_pages = 0;
}
static bool load_kernel(struct efi_boot_services *bs, u8 **image, uptr size,
                        u64 stub_base, u64 stub_size, u64 *entry) {
    const u8 *file = image ? *image : NULL;
    kernel_file_pages = 0;
    kernel_load_status = EFI_LOAD_ERROR;
    if (!file || size > ~(uptr)0 - 4095 || size > ~0ull - (u64)(uptr)file ||
        size < sizeof(struct elf64_ehdr) || memcmp(file, "\x7f" "ELF", 4) ||
        stub_size > ~0ull - stub_base)
        return false;
    const struct elf64_ehdr *eh = (const void *)file;
    if (eh->ident[4] != 2 || eh->ident[5] != 1 || eh->ident[6] != 1 || eh->type != 2 ||
        eh->machine != 62 || eh->version != 1 || eh->ehsize != sizeof(*eh) ||
        !eh->phnum || eh->phnum > 128 || eh->phentsize != sizeof(struct elf64_phdr) ||
        eh->phoff > size || eh->phnum > (size - eh->phoff) / sizeof(struct elf64_phdr) ||
        !find_kernel_entry(file, size, eh, entry))
        return false;
    const struct elf64_phdr *ph = (const void *)(file + eh->phoff);
    u64 first = KERNEL_LOAD_LIMIT, end = KERNEL_LOAD_BASE;
    bool executable_entry = false;
    /* Validate the complete image before reserving or writing any target RAM. */
    for (u16 i = 0; i < eh->phnum; ++i) {
        const struct elf64_phdr *p = &ph[i];
        if (p->type == 2 || p->type == 3)
            return false;
        if (p->type != 1)
            continue;
        if (p->memsz < p->filesz || p->memsz > KERNEL_LOAD_LIMIT ||
            p->paddr < KERNEL_LOAD_BASE || p->paddr > KERNEL_LOAD_LIMIT - p->memsz ||
            p->vaddr != p->paddr || p->offset > size || p->filesz > size - p->offset ||
            (p->flags & ~7u) || (p->flags & 3) == 3 ||
            (p->align > 1 && ((p->align & (p->align - 1)) ||
                              ((p->offset ^ p->vaddr) & (p->align - 1)))))
            return false;
        if (!p->memsz)
            continue;
        u64 start_page = p->paddr & ~4095ull;
        u64 end_page = ALIGN_UP(p->paddr + p->memsz, 4096ull);
        if (range_overlaps(start_page, end_page, stub_base, stub_base + stub_size))
            return false;
        for (u16 j = 0; j < i; ++j)
            if (ph[j].type == 1 && ph[j].memsz &&
                range_overlaps(p->paddr, p->paddr + p->memsz,
                               ph[j].paddr, ph[j].paddr + ph[j].memsz))
                return false;
        first = MIN(first, start_page);
        end = MAX(end, end_page);
        if ((p->flags & 1) && *entry >= p->paddr && *entry - p->paddr < p->memsz)
            executable_entry = true;
    }
    if (!executable_entry || end <= first)
        return false;
    /* Pool allocation can place the ELF inside its own fixed destination on
     * low-memory firmware. Move it only after validating every segment, and
     * keep the replacement below the first destination page. Firmware owns
     * both allocations until ExitBootServices; never overwrite live pages. */
    if (range_overlaps(first, end, (u64)(uptr)file, (u64)(uptr)file + size)) {
        u64 staging = first - 1;
        uptr pages = ALIGN_UP(size, 4096ull) / 4096;
        kernel_load_status = bs->allocate_pages(EFI_ALLOCATE_MAX_ADDRESS,
                                                EFI_LOADER_DATA, pages, &staging);
        if (EFI_ERROR(kernel_load_status))
            return false;
        if (staging > first || pages > (first - staging) / 4096 ||
            range_overlaps(staging, staging + pages * 4096, stub_base, stub_base + stub_size)) {
            bs->free_pages(staging, pages);
            kernel_load_status = EFI_DEVICE_ERROR;
            return false;
        }
        memcpy((void *)(uptr)staging, file, size);
        bs->free_pool(*image);
        *image = (void *)(uptr)staging;
        kernel_file_pages = pages;
    }
    /* Firmware still owns RAM here. AllocateAddress must succeed before copy. */
    u64 address = first;
    kernel_load_status = bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_CODE,
                                            (end - first) / 4096, &address);
    kernel_pages_owned = !EFI_ERROR(kernel_load_status);
    if (!kernel_pages_owned && kernel_load_status != EFI_NOT_FOUND)
        return false;
    kernel_first = first;
    kernel_limit = end;
    return true;
}
/* A fixed low load address may still contain boot-service allocations. Those
 * become reclaimable only after ExitBootServices; loader/runtime/reserved
 * ranges must never be overwritten. Inspect the map matching the exit key. */
static bool kernel_destination_ready(void) {
    if (kernel_pages_owned)
        return true;
    for (u32 i = 0; i < bi.mem_count; ++i)
        if (!bi.mem[i].type && range_overlaps(kernel_first, kernel_limit,
                bi.mem[i].base, bi.mem[i].base + bi.mem[i].length))
            return false;
    u64 cursor = kernel_first;
    while (cursor < kernel_limit) {
        u64 next = cursor;
        for (u32 i = 0; i < bi.mem_count; ++i)
            if (bi.mem[i].type == 1 && bi.mem[i].base <= cursor &&
                bi.mem[i].base + bi.mem[i].length > next)
                next = bi.mem[i].base + bi.mem[i].length;
        if (next == cursor)
            return false;
        cursor = next;
    }
    return true;
}
static void copy_kernel_segments(const u8 *file) {
    const struct elf64_ehdr *eh = (const void *)file;
    const struct elf64_phdr *ph = (const void *)(file + eh->phoff);
    for (u16 i = 0; i < eh->phnum; ++i) {
        const struct elf64_phdr *p = &ph[i];
        if (p->type != 1 || !p->memsz)
            continue;
        memcpy((void *)(uptr)p->paddr, file + p->offset, p->filesz);
        memset((void *)(uptr)(p->paddr + p->filesz), 0, p->memsz - p->filesz);
    }
}
static NORETURN void finish_handover(void) {
    copy_kernel_segments(pending_kernel);
    ((void(__attribute__((sysv_abi)) *)(const struct boot_info *))pending_entry)(&bi);
    for (;;)
        __asm__ volatile("cli; hlt");
}
/* This gate abandons the firmware stack before reclaiming boot-service RAM. */
__attribute__((naked, noreturn, sysv_abi))
static void switch_handover_stack(void *stack __attribute__((unused)),
                                  void (*finish)(void) __attribute__((unused))) {
    __asm__ volatile("mov %rdi,%rsp; and $-16,%rsp; xor %ebp,%ebp; call *%rsi; ud2");
}
static efi_status read_whole_file(struct efi_boot_services *bs, void *volume,
                                  const char16 *path, u8 **out, uptr *out_size) {
    void *root = NULL;
    efi_status s = ((struct efi_sfs *)volume)->open_volume(volume, &root);
    if (EFI_ERROR(s) || !root)
        return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
    void *handle = NULL;
    s = ((struct efi_file *)root)->open(root, &handle, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(s) || !handle) {
        ((struct efi_file *)root)->close(root);
        return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
    }
    struct efi_file *f = handle;
    u64 size = 0;
    s = f->set_position(f, ~0ull);
    if (!EFI_ERROR(s))
        s = f->get_position(f, &size);
    if (EFI_ERROR(s) || !size || size > 256ull * 1024 * 1024) {
        f->close(f);
        ((struct efi_file *)root)->close(root);
        return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
    }
    s = f->set_position(f, 0);
    if (EFI_ERROR(s)) {
        f->close(f);
        ((struct efi_file *)root)->close(root);
        return s;
    }
    u8 *buffer = NULL;
    s = bs->allocate_pool(EFI_LOADER_POOL, size, (void **)&buffer);
    if (EFI_ERROR(s)) {
        f->close(f);
        ((struct efi_file *)root)->close(root);
        return s;
    }
    uptr remaining = (uptr)size;
    u8 *cursor = buffer;
    while (remaining) {
        uptr chunk = remaining;
        s = f->read(f, &chunk, cursor);
        if (EFI_ERROR(s) || !chunk || chunk > remaining)
            break;
        cursor += chunk;
        remaining -= chunk;
    }
    f->close(f);
    ((struct efi_file *)root)->close(root);
    if (EFI_ERROR(s) || remaining) {
        bs->free_pool(buffer);
        return EFI_ERROR(s) ? s : EFI_DEVICE_ERROR;
    }
    *out = buffer;
    *out_size = (uptr)size;
    return EFI_SUCCESS;
}
static u64 find_rsdp(struct efi_system_table *st) {
    u64 legacy = 0;
    for (uptr i = 0; i < st->number_of_table_entries; ++i) {
        const struct efi_config_table *t = &st->configuration_table[i];
        if (!t->vendor_table) continue;
        if (!memcmp(&t->vendor_guid, &acpi20_guid, sizeof(struct efi_guid)))
            return (u64)(uptr)t->vendor_table;
        if (!memcmp(&t->vendor_guid, &acpi10_guid, sizeof(struct efi_guid)))
            legacy = (u64)(uptr)t->vendor_table;
    }
    return legacy;
}
static void fill_framebuffer(void *gop_void) {
    memset(&bi.fb, 0, sizeof(bi.fb));
    const struct efi_gop *gop = gop_void;
    if (!gop) return;
    const struct efi_gop_mode *mode = gop->mode;
    if (!mode)
        return;
    const struct efi_gop_mode_info *info = mode->info;
    if (!info)
        return;
    u32 format = NV_FB_NONE;
    if (info->pixel_format == 0)
        format = NV_FB_RGBX8;
    else if (info->pixel_format == 1)
        format = NV_FB_BGRX8;
    else if (info->pixel_format == 2) {
        if (!nv_pixel_masks_valid(info->pixel_information)) return;
        u32 red = info->pixel_information[0], blue = info->pixel_information[2];
        if (info->pixel_information[1] == 0x0000ff00u &&
            red == 0x00ff0000u && blue == 0x000000ffu)
            format = NV_FB_BGRX8;
        else if (info->pixel_information[1] == 0x0000ff00u &&
                 red == 0x000000ffu && blue == 0x00ff0000u)
            format = NV_FB_RGBX8;
        else format = NV_FB_BITMASK;
    }
    if (format == NV_FB_NONE || !mode->framebuffer_base ||
        mode->framebuffer_base >= (1ull << 52) ||
        info->horizontal < 80 || info->vertical < 25 ||
        info->pixels_per_scanline < info->horizontal ||
        info->pixels_per_scanline > 0xffffffffu / 4 ||
        (u64)info->pixels_per_scanline * 4 * info->vertical > 64ull * 1024 * 1024 ||
        (u64)info->pixels_per_scanline * 4 * info->vertical >
            (1ull << 52) - mode->framebuffer_base ||
        (u64)info->pixels_per_scanline * 4 * info->vertical > mode->framebuffer_size)
        return;
    bi.fb.address = mode->framebuffer_base;
    bi.fb.width = info->horizontal;
    bi.fb.height = info->vertical;
    bi.fb.pitch = info->pixels_per_scanline * 4;
    bi.fb.format = format;
    memcpy(bi.fb.masks, info->pixel_information, sizeof(bi.fb.masks));
}
static void select_framebuffer(struct efi_boot_services *bs, struct efi_gop *gop) {
    fill_framebuffer(gop);
    if (bi.fb.format != NV_FB_NONE && bi.fb.width >= 640 && bi.fb.height >= 480) return;
    if (!gop || !gop->mode || !gop->query_mode || !gop->set_mode) return;
    u32 count = MIN(gop->mode->max_mode, 256u);
    u64 sizes[256];
    for (u32 index = 0; index < count; ++index) {
        sizes[index] = ~0ull;
        struct efi_gop_mode_info *info = NULL;
        uptr size = 0;
        efi_status status = gop->query_mode(gop, index, &size, &info);
        if (!EFI_ERROR(status) && info && size >= sizeof(*info) &&
            info->horizontal >= 640 && info->vertical >= 480 &&
            info->pixels_per_scanline >= info->horizontal &&
            info->pixels_per_scanline <= 0xffffffffu / 4 &&
            (info->pixel_format < 2 || (info->pixel_format == 2 &&
             nv_pixel_masks_valid(info->pixel_information)))) {
            u64 bytes = (u64)info->pixels_per_scanline * 4 * info->vertical;
            if (bytes <= 64ull * 1024 * 1024) sizes[index] = bytes;
        }
        if (info) bs->free_pool(info);
    }
    for (u32 attempt = 0; attempt < count; ++attempt) {
        u32 selected = count; u64 best = ~0ull;
        for (u32 i = 0; i < count; ++i)
            if (sizes[i] < best) { best = sizes[i]; selected = i; }
        if (selected == count) break;
        sizes[selected] = ~0ull;
        if (EFI_ERROR(gop->set_mode(gop, selected))) continue;
        fill_framebuffer(gop);
        if (bi.fb.format != NV_FB_NONE && bi.fb.width >= 640 && bi.fb.height >= 480) return;
    }
}
static bool convert_memory_map(const struct efi_memory_descriptor *map, uptr size, uptr stride) {
    if (!map || !size || stride < sizeof(*map) || size % stride)
        return false;
    bi.mem_count = 0;
    for (uptr off = 0; off < size; off += stride) {
        const struct efi_memory_descriptor *d = (const void *)((const u8 *)map + off);
        if (d->number_of_pages > (~0ull - d->physical_start) / 4096)
            return false;
        u32 usable = d->type == EFI_CONVENTIONAL || d->type == EFI_BOOT_SERVICES_CODE ||
                     d->type == EFI_BOOT_SERVICES_DATA;
        if (d->attribute & (1ull << 63)) /* EFI_MEMORY_RUNTIME */
            usable = 0;
        if (!add_mem(d->physical_start, d->number_of_pages * 4096, usable))
            return false;
    }
    return bi.mem_count != 0;
}
static bool cpu_supported(void) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1u), "c"(0));
    if ((d & 0x07008061u) != 0x07008061u)
        return false;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (a < 0x80000001u)
        return false;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001u), "c"(0));
    return (d & 0x20100000u) == 0x20100000u;
}
__attribute__((ms_abi)) efi_status efi_main(efi_handle image, struct efi_system_table *st) {
    memset(&bi, 0, sizeof(bi));
    early_console = st->con_out;
    loader_text("\nNuvora Core UEFI stub\n");
    if (!cpu_supported()) {
        loader_text("CPU UNSUPPORTED: need x64 FPU CMOV MSR PAE FXSR SSE2 NX.\n");
        return EFI_UNSUPPORTED;
    }
    struct efi_boot_services *bs = st->boot_services;
    bs->set_watchdog_timer(0, 0, 0, NULL);
    void *loaded = NULL;
    efi_status s = bs->handle_protocol(image, &loaded_image_guid, &loaded);
    if (EFI_ERROR(s) || !loaded) {
        loader_status("no loaded image protocol: ", s);
        return EFI_UNSUPPORTED;
    }
    struct efi_loaded_image *li = loaded;
    u64 stub_base = (u64)(uptr)li->image_base, stub_size = li->image_size;
    void *gop = NULL;
    /* Prefer the active console when a machine exposes several GOP devices. */
    if (st->console_out_handle &&
        EFI_ERROR(bs->handle_protocol(st->console_out_handle, &gop_guid, &gop)))
        gop = NULL;
    if (gop)
        select_framebuffer(bs, gop);
    if (bi.fb.format == NV_FB_NONE &&
        !EFI_ERROR(bs->locate_protocol(&gop_guid, NULL, &gop)) && gop)
        select_framebuffer(bs, gop);
    u8 *kernel = NULL;
    uptr kernel_size = 0;
    static const char16 kernel_paths[2][24] = {L"\\EFI\\NUVORA\\NUVORA.ELF",
                                               L"\\EFI\\BOOT\\NUVORA.ELF"};
    void *boot_sfs = NULL;
    if (!EFI_ERROR(bs->handle_protocol(li->device_handle, &sfs_guid, &boot_sfs)) && boot_sfs)
        for (u32 i = 0; i < 2 && !kernel; ++i)
            s = read_whole_file(bs, boot_sfs, kernel_paths[i], &kernel, &kernel_size);
    if (!kernel) {
        /* Some firmwares expose the boot medium on a different handle (for
         * example El Torito CD boot): try every SimpleFileSystem volume. */
        uptr count = 0;
        efi_handle *handles = NULL;
        if (!EFI_ERROR(bs->locate_handle_buffer(2 /* BY_PROTOCOL */, &sfs_guid, NULL, &count,
                                                &handles)) &&
            handles) {
            for (uptr i = 0; i < count && !kernel; ++i) {
                void *sfs = NULL;
                if (EFI_ERROR(bs->handle_protocol(handles[i], &sfs_guid, &sfs)) || !sfs)
                    continue;
                for (u32 k = 0; k < 2 && !kernel; ++k)
                    s = read_whole_file(bs, sfs, kernel_paths[k], &kernel, &kernel_size);
                if (kernel)
                    boot_sfs = sfs;
            }
            bs->free_pool(handles);
        }
    }
    if (!kernel) {
        loader_status("kernel ELF not found on any ESP volume: ", s);
        return EFI_NOT_FOUND;
    }
    u8 *cmdline = NULL;
    uptr cmdline_size = 0;
    if (boot_sfs &&
        !EFI_ERROR(read_whole_file(bs, boot_sfs, L"\\EFI\\NUVORA\\CMDLINE", &cmdline,
                                   &cmdline_size)) &&
        cmdline) {
        uptr n = MIN(cmdline_size, sizeof(bi.cmdline) - 1);
        memcpy(bi.cmdline, cmdline, n);
        while (n && (bi.cmdline[n - 1] == '\n' || bi.cmdline[n - 1] == '\r'))
            bi.cmdline[--n] = 0;
        bs->free_pool(cmdline);
    }
    u64 entry = 0;
    if (!load_kernel(bs, &kernel, kernel_size, stub_base, stub_size, &entry)) {
        loader_status("kernel ELF validation/allocation failed: ", kernel_load_status);
        free_kernel_file(bs, kernel);
        return EFI_LOAD_ERROR;
    }
    pending_kernel = kernel;
    pending_entry = entry;
    add_res(stub_base, stub_size);
    if (bi.fb.format != NV_FB_NONE && bi.fb.address < 0x100000000ull)
        add_res(bi.fb.address, (u64)bi.fb.pitch * bi.fb.height);
    uptr map_key = 0, descriptor_size = 0, map_capacity = 0;
    u32 descriptor_version = 0;
    struct efi_memory_descriptor *map = NULL;
    bool exit_attempted = false, exited = false;
    bi.rsdp = find_rsdp(st);
    for (u32 attempt = 0; attempt < 8; ++attempt) {
        uptr map_size = map_capacity;
        s = bs->get_memory_map(&map_size, map, &map_key, &descriptor_size, &descriptor_version);
        if (s == EFI_BUFFER_TOO_SMALL && !exit_attempted) {
            if (descriptor_size < sizeof(*map) || descriptor_size > (~(uptr)0 - map_size) / 8)
                return EFI_DEVICE_ERROR;
            if (map)
                bs->free_pool(map);
            map_capacity = map_size + 8 * descriptor_size;
            s = bs->allocate_pool(EFI_LOADER_POOL, map_capacity, (void **)&map);
            if (EFI_ERROR(s))
                return s;
            continue;
        }
        if (EFI_ERROR(s))
            return s;
        if (!convert_memory_map(map, map_size, descriptor_size)) {
            loader_text("invalid or overly complex EFI memory map\n");
            return EFI_DEVICE_ERROR;
        }
        if (!kernel_destination_ready()) {
            loader_text("kernel load address intersects reserved firmware memory\n");
            loader_status("kernel start: 0x", kernel_first);
            loader_status("kernel end:   0x", kernel_limit);
            for (u32 i = 0; i < bi.mem_count; ++i)
                if (!bi.mem[i].type && range_overlaps(kernel_first, kernel_limit,
                        bi.mem[i].base, bi.mem[i].base + bi.mem[i].length)) {
                    loader_status("reserved at:  0x", bi.mem[i].base);
                    break;
                }
            return EFI_LOAD_ERROR;
        }
        /* Reconvert every retry: only the map matching the successful exit
         * key may be handed to the allocator. Do not replace firmware CR3. */
        exit_attempted = true;
        s = bs->exit_boot_services(image, map_key);
        if (!EFI_ERROR(s)) {
            exited = true;
            break;
        }
        if (s != EFI_INVALID_PARAMETER)
            return s;
    }
    if (!exited)
        return EFI_DEVICE_ERROR;
    __asm__ volatile("cli" ::: "memory");
    enable_nx();
    serial_text("entering kernel\n");
    switch_handover_stack(handover_stack + sizeof(handover_stack), finish_handover);
}
