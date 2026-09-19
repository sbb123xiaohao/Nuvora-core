#ifndef NV_BOOTINFO_H
#define NV_BOOTINFO_H
#include <nv/types.h>
/* Neutral boot handover shared by the Multiboot path (arch boot.S entries)
 * and the x64 UEFI stub (arch/x86_64/uefi.c). The kernel consumes this via
 * kernel_start() and never talks to a bootloader protocol directly. */
#define NV_BOOT_MEM_MAX 128
#define NV_BOOT_RES_MAX 16
#define NV_BOOT_CMDLINE_MAX 512
/* Framebuffer pixel formats. 0 means "no framebuffer". */
enum { NV_FB_NONE = 0, NV_FB_BGRX8 = 1, NV_FB_RGBX8 = 2 };
struct boot_mem_entry {
    u64 base, length;
    u32 type; /* 1 = usable RAM, 0 = reserved */
};
struct boot_range {
    u64 base, length;
};
struct boot_framebuffer {
    u64 address;
    u32 width, height, pitch, format;
};
struct boot_info {
    u32 mem_count;
    struct boot_mem_entry mem[NV_BOOT_MEM_MAX];
    u32 res_count;
    struct boot_range res[NV_BOOT_RES_MAX];
    struct boot_framebuffer fb;
    u64 rsdp; /* ACPI RSDP from the EFI configuration table, 0 when unknown */
    char cmdline[NV_BOOT_CMDLINE_MAX];
};
#endif
