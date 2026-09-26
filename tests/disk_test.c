/* Real ATA/header code with a port-I/O recorder instead of privileged I/O. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define SNAP_CAP_MAX (16u * 1024u * 1024u)
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; u32 version; u64 data_first, data_end; };
static struct { u16 port; u8 byte; } writes[64];
static u32 nwrites;
static u8 inb(u16 p) { (void)p; return 0x48; }
static u16 inw(u16 p) { (void)p; return 0; }
static void outb(u16 p, u8 v) { assert(nwrites < 64); writes[nwrites].port=p; writes[nwrites++].byte=v; }
static void outw(u16 p, u16 v) { (void)p; (void)v; }
static bool nvme_init(u64 *capacity, u32 first_pci, u32 first_nsid,
                      u32 *selected_pci, u32 *selected_nsid) {
    (void)capacity; (void)first_pci; (void)first_nsid;
    (void)selected_pci; (void)selected_nsid; return false;
}
static int nvme_read(u64 lba, void *out) { (void)lba; (void)out; return -NV_ENODEV; }
static int nvme_write(u64 lba, const void *in) { (void)lba; (void)in; return -NV_ENODEV; }
static int nvme_flush(void) { return -NV_ENODEV; }
static bool nvme_ready(void) { return false; }
static bool nvme_shutdown(void) { return true; }
static bool ahci_init(u64 *capacity, u32 first_pci, u32 first_port,
                      u32 *selected_pci, u32 *selected_port) {
    (void)capacity; (void)first_pci; (void)first_port;
    (void)selected_pci; (void)selected_port; return false;
}
static int ahci_read(u64 lba, void *out) { (void)lba; (void)out; return -NV_ENODEV; }
static int ahci_write(u64 lba, const void *in) { (void)lba; (void)in; return -NV_ENODEV; }
static int ahci_flush(void) { return -NV_ENODEV; }
static bool ahci_ready(void) { return false; }
static bool ahci_shutdown(void) { return true; }
#include "../kernel/disk.c"
static void header(u8 *h, const char *magic, u32 version, u32 slot1, u32 count, u64 total) {
    memset(h, 0, 512); memcpy(h, magic, 8);
    u32 fields[] = {version, 512, 8, slot1, count};
    memcpy(h + 8, fields, sizeof(fields)); memcpy(h + 32, &total, 8);
    u32 crc = crc32(h, version == 1 ? 28 : 40);
    memcpy(h + (version == 1 ? 28 : 40), &crc, 4);
}
int main(void) {
    u16 identify[256] = {0};
    assert(nv_ata_sector_512(identify));
    identify[106] = 0x6003u; /* 512e: eight 512-byte logical blocks per physical block. */
    assert(nv_ata_sector_512(identify));
    identify[106] = 0x5000u; identify[117] = 2048u; /* 4Kn: reject all 512-byte I/O. */
    assert(!nv_ata_sector_512(identify));
    u8 h[512]; sectors = 1ull << 48;
    header(h, "NVSTORE2", 2, 32777, 32769, 1ull << 33);
    assert(parse_header(h) && layout.snap_cap == SNAP_CAP_MAX);
    header(h, "NVSTORE1", 2, 32777, 32769, 1ull << 33); assert(!parse_header(h));
    header(h, "NVSTORE1", 1, 4096, 2049, 0); assert(parse_header(h));
    header(h, "NVSTORE2", 2, 16, 8, 24); assert(parse_header(h) && layout.snap_cap == 3584);
    header(h, "NVSTORE2", 2, 16, 8, 23); assert(!parse_header(h));
    header(h, "NVSTORE2", 2, 0, 0xfffffff8u, 1ull << 33); assert(!parse_header(h));
    identified = owned = lba48 = true;
    assert(!command(0x123456789abcull, 0x30));
    const u8 expected[] = {0, 0x56, 0x34, 0x12, 1, 0xbc, 0x9a, 0x78, 0x40, 0x34};
    assert(nwrites == sizeof(expected));
    for (u32 i = 0; i < nwrites; ++i) assert(writes[i].byte == expected[i]);
    assert(command(1ull << 48, 0x20) == -NV_ENODEV);
    nwrites = 0; assert(!disk_flush() && writes[0].port == 0x1f7 && writes[0].byte == 0xea);
    lba48 = false; nwrites = 0;
    assert(command(1ull << 28, 0x20) == -NV_ENODEV && !nwrites);
    assert(!disk_flush() && writes[0].byte == 0xe7);
    puts("PASS ATA: LBA48/LBA28, flush, NVSTORE headers and 512e/4Kn sector validation");
}
