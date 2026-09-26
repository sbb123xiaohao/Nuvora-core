/* Exercise the real ECAM selector against sparse and incorrect MCFG ranges. */
#include <assert.h>
#include <stdio.h>
#include <nv/acpi.h>
#include <nv/string.h>
#define NV_KERNEL_H

static u32 cf8_address, probes;
static u64 last_ecam_address;
static u32 legacy_id, modern_id;
static bool device_present;
static u32 config[4096 / 4];
static uptr irq_save(void) { return 0; }
static void irq_restore(uptr flags) { (void)flags; }
static void outl(u16 port, u32 value) {
    assert(port == 0xcf8);
    cf8_address = value;
}
static u32 inl(u16 port) {
    assert(port == 0xcfc);
    ++probes;
    u32 address = cf8_address & 0x00ffff00u;
    return device_present && address == ((6u << 16) | (2u << 11)) ?
           legacy_id : 0xffffffffu;
}
static void outw(u16 port, u16 value) { (void)port; (void)value; }
static volatile u8 *vm_mmio_remap(u64 physical) {
    last_ecam_address = physical;
    memset(config, 0xff, sizeof(config));
    if (device_present && physical ==
        0xb0000000ull + (6ull << 20) + (2ull << 15)) {
        config[0] = modern_id;
        config[0x100 / 4] = 0x00100001u;
    }
    return (volatile u8 *)config;
}

#include "../kernel/pci.c"

int main(void) {
    struct nv_mcfg_region range = {0xb0000000ull, 0, 5, 6};
    struct nv_mcfg_region first;
    u32 rejected = 0;

    /* An empty first bus cannot certify an MCFG region by comparing -1/-1. */
    assert(pci_ecam_configure(&range, 1, 48, &rejected, &first) == 0);
    assert(rejected == 1 && !ecam_count);

    device_present = true;
    legacy_id = modern_id = 0x12348086u;
    rejected = probes = 0;
    assert(pci_ecam_configure(&range, 1, 48, &rejected, &first) == 1);
    assert(!rejected && first.start_bus == 5 && first.end_bus == 6);
    assert(probes > 32); /* Function 0 was found after an empty first bus. */
    u32 address = (6u << 16) | (2u << 11);
    assert(pci_read(address, 0) == legacy_id);
    assert(last_ecam_address == 0xb0000000ull + (6ull << 20) + (2ull << 15));
    assert(pci_read(address, 0x100) == 0x00100001u);

    modern_id = 0x56788086u;
    rejected = 0;
    assert(pci_ecam_configure(&range, 1, 48, &rejected, &first) == 0);
    assert(rejected == 1 && !ecam_count);
    assert(pci_read(address, 0x100) == 0xffffffffu); /* Legacy fallback. */
    puts("PASS PCI ECAM: empty bus rejected, populated sparse bus matched, mismatched range rejected");
}
