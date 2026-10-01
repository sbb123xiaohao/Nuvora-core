#include "kernel.h"
#include <nv/power.h>

static struct nv_platform_info platform;
static u64 cached_page = ~0ull;
static const u8 *cached_mapping;
static u32 address_bits;
static struct nv_acpi_power power;
static volatile void *power_maps[3];

static bool physical_read(void *context, u64 address, void *out, u32 length) {
    (void)context;
    if (!length)
        return true;
    u64 end = address + length;
    if (end < address || (address_bits < 64 && end > (1ull << address_bits)))
        return false;
    u8 *destination = out;
    while (length) {
        u64 page = address & ~(u64)(PAGE - 1);
        if (page != cached_page) {
            cached_mapping = vm_mmio_remap(page);
            if (!cached_mapping)
                return false;
            cached_page = page;
        }
        u32 offset = (u32)address & (PAGE - 1);
        u32 part = MIN(length, PAGE - offset);
        memcpy(destination, cached_mapping + offset, part);
        destination += part;
        address += part;
        length -= part;
    }
    return true;
}

void acpi_init(bool no_ecam, u64 rsdp) {
    memset(&platform, 0, sizeof(platform));
    platform.flags = NV_PLATFORM_CF8;
    platform.config_bytes = 256;
    struct nv_cpu_info cpu;
    cpu_get_info(&cpu);
    address_bits = MIN(cpu.physical_bits, 52u);
    struct nv_acpi_result tables;
    bool found = rsdp ? nv_acpi_parse_rsdp(physical_read, NULL, rsdp, &tables) : false;
    if (!found)
        found = nv_acpi_discover(physical_read, NULL, &tables);
    if (!found) {
        kprintf("[info] ACPI root not found (firmware RSDP %x); PCI uses CF8/CFC fallback\n", (uptr)rsdp);
        return;
    }
    nv_acpi_power_parse(physical_read, NULL, tables.fadt_address, &power);
    platform.flags |= NV_PLATFORM_ACPI;
    if (tables.root_kind == NV_ACPI_ROOT_XSDT)
        platform.flags |= NV_PLATFORM_XSDT;
    if (tables.mcfg_entries)
        platform.flags |= NV_PLATFORM_MCFG;
    platform.acpi_revision = tables.revision;
    platform.mcfg_entries = tables.mcfg_entries;
    platform.rejected_entries = tables.rejected_entries;
    memcpy(platform.oem_id, tables.oem_id, sizeof(platform.oem_id));
    memcpy(platform.oem_table_id, tables.oem_table_id, sizeof(platform.oem_table_id));
    struct nv_mcfg_region first = {0};
    if (no_ecam)
        platform.flags |= NV_PLATFORM_ECAM_DISABLED;
    else
        platform.ecam_regions =
            pci_ecam_configure(tables.regions, tables.region_count, address_bits,
                               &platform.rejected_entries, &first);
    if (platform.ecam_regions) {
        platform.flags |= NV_PLATFORM_ECAM;
        platform.segment = first.segment;
        platform.start_bus = first.start_bus;
        platform.end_bus = first.end_bus;
        platform.base_low = (u32)first.address;
        platform.base_high = (u32)(first.address >> 32);
        platform.config_bytes = 4096;
    }
    kprintf("[ok] ACPI rev %u via %s, OEM %s; MCFG %u, ECAM %u%s, rejected %u\n",
            platform.acpi_revision,
            platform.flags & NV_PLATFORM_XSDT ? "XSDT" : "RSDT",
            platform.oem_id[0] ? platform.oem_id : "unknown", platform.mcfg_entries,
            platform.ecam_regions, no_ecam ? " (disabled)" : "", platform.rejected_entries);
}

void acpi_get_info(struct nv_platform_info *out) {
    *out = platform;
}
static bool power_register(const struct nv_acpi_gas *r, u32 *value, bool write) {
    if (!r->address) return false;
    if (r->space == 1) {
        u16 port=(u16)r->address;
        if (r->width==8) { if (write) outb(port,(u8)*value); else *value=inb(port); }
        else if (r->width==16) { if (write) outw(port,(u16)*value); else *value=inw(port); }
        else if (r->width==32) { if (write) outl(port,*value); else *value=inl(port); }
        else return false;
        return true;
    }
    if (r->space || r->address >= (1ull << address_bits)) return false;
    u32 index=r==&power.reset?0:r==&power.pm1a?1:2;
    if (!power_maps[index]) power_maps[index]=vm_mmio_map(r->address,r->width/8);
    volatile void *mapped=power_maps[index];
    if (!mapped) return false;
    if (r->width==8) { if (write) *(volatile u8 *)mapped=(u8)*value; else *value=*(volatile u8 *)mapped; }
    else if (r->width==16) { if (write) *(volatile u16 *)mapped=(u16)*value; else *value=*(volatile u16 *)mapped; }
    else if (r->width==32) { if (write) *(volatile u32 *)mapped=*value; else *value=*(volatile u32 *)mapped; }
    else return false;
    return true;
}
bool acpi_power_reset(void) {
    if (!power.can_reset) return false;
    u32 value=power.reset_value;
    return power_register(&power.reset,&value,true);
}
bool acpi_power_off(void) {
    if (!power.can_sleep) return false;
    u32 a,b=0;
    if (!power_register(&power.pm1a,&a,false) ||
        (power.pm1b.address && !power_register(&power.pm1b,&b,false))) return false;
    if (!(a&1u)) {
        if (!power.smi_command || power.smi_command>65535 || !power.acpi_enable) return false;
        outb((u16)power.smi_command,power.acpi_enable);
        u32 attempts=1000000;
        while (attempts-- && !(a&1u)) {
            __asm__ volatile("pause");
            if (!power_register(&power.pm1a,&a,false)) return false;
        }
        if (!(a&1u)) return false;
    }
    a=(a & ~0x3c00u) | (u32)power.sleep_a<<10 | 1u<<13;
    b=(b & ~0x3c00u) | (u32)power.sleep_b<<10 | 1u<<13;
    if (!power_register(&power.pm1a,&a,true)) return false;
    return !power.pm1b.address || power_register(&power.pm1b,&b,true);
}
