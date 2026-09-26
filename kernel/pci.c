#include "kernel.h"

/* Segment 0 PCI configuration. ACPI MCFG regions use a single supervisor-only
 * remapping aperture; buses not covered by a trusted region retain mechanism 1. */
static struct nv_mcfg_region ecam[NV_ACPI_MCFG_MAX];
static u32 ecam_count;

static u32 legacy_read(u32 address, u32 offset) {
    if (offset > 252)
        return 0xffffffffu;
    outl(0xcf8, 0x80000000u | address | offset);
    return inl(0xcfc);
}

static const struct nv_mcfg_region *region_for(u32 address) {
    u32 bus = (address >> 16) & 255;
    for (u32 i = 0; i < ecam_count; ++i)
        if (bus >= ecam[i].start_bus && bus <= ecam[i].end_bus)
            return &ecam[i];
    return NULL;
}

static volatile u8 *ecam_device(const struct nv_mcfg_region *region, u32 address) {
    u32 bus = (address >> 16) & 255;
    u32 device = (address >> 11) & 31;
    u32 function = (address >> 8) & 7;
    u64 physical = region->address + ((u64)bus << 20) + ((u64)device << 15) +
                   ((u64)function << 12);
    return vm_mmio_remap(physical);
}

static u32 ecam_read(const struct nv_mcfg_region *region, u32 address, u32 offset) {
    volatile u8 *device = ecam_device(region, address);
    return device ? *(volatile u32 *)(device + offset) : 0xffffffffu;
}

static bool ecam_matches_legacy(const struct nv_mcfg_region *region) {
    /* A range may begin with an empty bus. An all-ones comparison on that bus
     * proves nothing; find a populated function 0 anywhere in the range. */
    for (u32 bus = region->start_bus; bus <= region->end_bus; ++bus) {
        for (u32 dev = 0; dev < 32; ++dev) {
            u32 address = (bus << 16) | (dev << 11);
            uptr flags = irq_save();
            u32 legacy = legacy_read(address, 0);
            if ((legacy & 0xffffu) == 0xffffu || !(legacy & 0xffffu)) {
                irq_restore(flags);
                continue;
            }
            u32 modern = ecam_read(region, address, 0);
            irq_restore(flags);
            return legacy == modern;
        }
    }
    return false;
}

u32 pci_ecam_configure(const struct nv_mcfg_region *regions, u32 count, u32 physical_bits,
                       u32 *rejected, struct nv_mcfg_region *first) {
    ecam_count = 0;
    if (first)
        memset(first, 0, sizeof(*first));
    u64 limit = 1ull << MIN(physical_bits, 52u);
    for (u32 i = 0; i < count; ++i) {
        const struct nv_mcfg_region *candidate = &regions[i];
        u64 end = candidate->address + (((u64)candidate->end_bus + 1) << 20);
        bool valid = candidate->segment == 0 && candidate->start_bus <= candidate->end_bus &&
                     candidate->address < limit && end > candidate->address && end <= limit;
        for (u32 j = 0; valid && j < ecam_count; ++j)
            if (candidate->start_bus <= ecam[j].end_bus &&
                ecam[j].start_bus <= candidate->end_bus)
                valid = false;
        /* Both paths must describe the same real segment-0 device. */
        if (valid) valid = ecam_matches_legacy(candidate);
        if (!valid || ecam_count == NV_ACPI_MCFG_MAX) {
            if (rejected)
                ++*rejected;
            continue;
        }
        ecam[ecam_count++] = *candidate;
        if (ecam_count == 1 && first)
            *first = *candidate;
    }
    return ecam_count;
}

u32 pci_read(u32 address, u32 offset) {
    if (address & ~0x00ffff00u || offset > 4092 || (offset & 3))
        return 0xffffffffu;
    uptr flags = irq_save();
    const struct nv_mcfg_region *region = region_for(address);
    u32 value = region ? ecam_read(region, address, offset) : legacy_read(address, offset);
    irq_restore(flags);
    return value;
}

void pci_write16(u32 address, u32 offset, u16 value) {
    if (address & ~0x00ffff00u || offset > 4094 || (offset & 1))
        return;
    uptr flags = irq_save();
    const struct nv_mcfg_region *region = region_for(address);
    if (region) {
        volatile u8 *device = ecam_device(region, address);
        if (device)
            *(volatile u16 *)(device + offset) = value;
    } else if (offset <= 254) {
        outl(0xcf8, 0x80000000u | address | (offset & ~3u));
        outw(0xcfc + (offset & 2), value);
    }
    irq_restore(flags);
}

void pci_write32(u32 address, u32 offset, u32 value) {
    if (address & ~0x00ffff00u || offset > 4092 || (offset & 3))
        return;
    uptr flags = irq_save();
    const struct nv_mcfg_region *region = region_for(address);
    if (region) {
        volatile u8 *device = ecam_device(region, address);
        if (device)
            *(volatile u32 *)(device + offset) = value;
    } else if (offset <= 252) {
        outl(0xcf8, 0x80000000u | address | offset);
        outl(0xcfc, value);
    }
    irq_restore(flags);
}

void pci_visit(void (*visit)(u32, u32, u32)) {
    for (u32 bus = 0; bus < 256; ++bus)
        for (u32 dev = 0; dev < 32; ++dev) {
            u32 base = (bus << 16) | (dev << 11);
            if ((pci_read(base, 0) & 0xffff) == 0xffff)
                continue;
            u32 functions = pci_read(base, 0x0c) & (1u << 23) ? 8 : 1;
            for (u32 fn = 0; fn < functions; ++fn) {
                u32 address = base | (fn << 8), id = pci_read(address, 0);
                if ((id & 0xffff) != 0xffff)
                    visit(address, id, pci_read(address, 8));
            }
        }
}
