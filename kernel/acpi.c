#include "kernel.h"

static struct nv_platform_info platform;
static u64 cached_page = ~0ull;
static const u8 *cached_mapping;
static u32 address_bits;

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
#ifndef __x86_64__
    address_bits = MIN(address_bits, 32u);
#endif
    struct nv_acpi_result tables;
    bool found = rsdp ? nv_acpi_parse_rsdp(physical_read, NULL, rsdp, &tables) : false;
    if (!found)
        found = nv_acpi_discover(physical_read, NULL, &tables);
    if (!found) {
        kprintf("[info] ACPI root not found; PCI uses CF8/CFC fallback\n");
        return;
    }
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
