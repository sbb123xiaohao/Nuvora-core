/* A foreign SATA disk on port 0 must not hide a Nuvora disk on port 1. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
#define SNAP_CAP_MAX (128u * 1024u * 1024u)
#define AHCI_MMIO 0x3000u
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; u32 version; u64 data_first, data_end; };
static u8 dma[12][PAGE], mmio[AHCI_MMIO], nuvora_header[512];
static u32 next_page, pci_command, port_command[2], clb[2], writes[2];
static bool first_supports_lba48 = true;
static bool first_is_4kn;
static uptr page_alloc_below(u64 limit) {
    assert(limit == 0x100000000ull && next_page + 1 < 12);
    ++next_page; memset(dma[next_page], 0, PAGE); return next_page * PAGE;
}
static void page_free(uptr page) { assert(page / PAGE <= next_page); }
static void *phys_ptr(uptr physical) {
    assert(physical && physical / PAGE <= next_page);
    return dma[physical / PAGE] + physical % PAGE;
}
static void *vm_mmio_map(u64 physical, u32 size) {
    assert(physical == 0x40000000u && size == AHCI_MMIO); return mmio;
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x2000);
    return offset == 0x24 ? 0x40000000u : offset == 4 ? pci_command : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert(address == 0x2000 && offset == 4); pci_command = value;
}
static void pci_visit(void (*visit)(u32, u32, u32)) {
    visit(0x2000, 0x12348086, 0x01060100);
}
static u8 inb(u16 port) { (void)port; return 0xff; }
static u16 inw(u16 port) { (void)port; return 0; }
static void outb(u16 port, u8 value) { (void)port; (void)value; }
static void outw(u16 port, u16 value) { (void)port; (void)value; }
static bool nvme_init(u64 *capacity) { (void)capacity; return false; }
static int nvme_read(u64 lba, void *buffer) { (void)lba; (void)buffer; return -NV_ENODEV; }
static int nvme_write(u64 lba, const void *buffer) { (void)lba; (void)buffer; return -NV_ENODEV; }
static int nvme_flush(void) { return -NV_ENODEV; }
static bool nvme_ready(void) { return false; }
static void nvme_shutdown(void) {}
static u32 sim_read(u32 offset);
static void sim_write(u32 offset, u32 value);
#define AHCI_REG_READ(offset) sim_read(offset)
#define AHCI_REG_WRITE(offset, value) sim_write(offset, value)
#define transfer ahci_transfer
#include "../kernel/ahci.c"
#undef transfer
#include "../kernel/disk.c"

static u32 sim_read(u32 offset) {
    if (offset == 0x0c) return 3u;
    if (offset >= 0x100 && offset < 0x200) {
        u32 port = (offset - 0x100) / 0x80, reg = (offset - 0x100) % 0x80;
        if (reg == 0x18) return port_command[port];
        if (reg == 0x24) return 0x101u;
        if (reg == 0x28) return 0x103u;
        if (reg == 0x38) return 0;
    }
    return *(u32 *)(mmio + offset);
}
static void sim_write(u32 offset, u32 value) {
    if (offset >= 0x100 && offset < 0x200) {
        u32 port = (offset - 0x100) / 0x80, reg = (offset - 0x100) % 0x80;
        if (reg == 0x18) { port_command[port] = value; return; }
        if (reg == 0x00) { clb[port] = value; return; }
        if (reg == 0x10 || reg == 0x30) {
            *(u32 *)(mmio + offset) &= ~value; return;
        }
        if (reg == 0x38 && (value & 1u)) {
            struct ahci_header *h = phys_ptr(clb[port]);
            u8 *table = phys_ptr((uptr)h->table);
            struct ahci_prdt *prdt = (struct ahci_prdt *)(table + 0x80);
            u8 *buffer = phys_ptr((uptr)prdt->address);
            if (table[2] == 0xec) {
                u16 *id = (u16 *)buffer;
                memset(buffer, 0, 512);
                id[49] = 1u << 9;
                id[83] = (port == 0 && !first_supports_lba48) ? 0 : 0x4400u;
                id[106] = (port == 0 && first_is_4kn) ? 0x5000u : 0x6003u;
                if (port == 0 && first_is_4kn) id[117] = 2048u;
                id[100] = 8192;
            } else if (table[2] == 0x25) {
                u64 lba = (u64)table[4] | (u64)table[5] << 8 | (u64)table[6] << 16 |
                          (u64)table[8] << 24 | (u64)table[9] << 32 | (u64)table[10] << 40;
                memset(buffer, 0, 512);
                if (port == 1 && lba == 0) memcpy(buffer, nuvora_header, 512);
            } else if (table[2] == 0x35) {
                ++writes[port];
            }
            return;
        }
    }
    *(u32 *)(mmio + offset) = value;
}

int main(void) {
    u32 fields[] = {3, 512, 8, 2057, 2049};
    u64 capacity = 8192;
    memcpy(nuvora_header, "NVSTORE3", 8);
    memcpy(nuvora_header + 8, fields, sizeof(fields));
    memcpy(nuvora_header + 32, &capacity, sizeof(capacity));
    u32 crc = crc32(nuvora_header, 40);
    memcpy(nuvora_header + 40, &crc, sizeof(crc));
    assert(disk_init() && ahci_disk && ahci.port == 1);
    assert(disk_volume_count() == 1 && layout.version == 3);
    u8 buffer[512];
    assert(!disk_read(0, buffer) && !memcmp(buffer, "NVSTORE3", 8));
    assert(disk_volume_write(0, 0, buffer) == -NV_EACCESS);
    assert(!disk_volume_write(0, 8, buffer));
    assert(writes[0] == 0 && writes[1] == 1);
    ahci_shutdown();
    next_page = 0; /* The simulated allocator may reuse the freed DMA pages. */
    first_supports_lba48 = false;
    assert(disk_init() && ahci_disk && ahci.port == 1);
    assert(!disk_read(0, buffer) && !memcmp(buffer, "NVSTORE3", 8));
    assert(writes[0] == 0);
    ahci_shutdown();
    next_page = 0;
    first_supports_lba48 = true; first_is_4kn = true;
    assert(disk_init() && ahci_disk && ahci.port == 1);
    assert(writes[0] == 0);
    puts("PASS AHCI multiport: foreign/LBA28/4Kn disk skipped, later 512e Nuvora port mounted, writes isolated");
}
