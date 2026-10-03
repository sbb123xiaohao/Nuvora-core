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
/* The device progresses with elapsed PIT time, independently of how often
 * status is read. Fast status polling alone cannot complete delayed I/O. */
volatile u64 ticks;
static bool irq_enabled, pending;
static u8 ata_status = 0x50, next_status;
static u32 words, data_delay, completion_delay;
static u64 pending_at;
static u32 pending_delay;
uptr irq_save(void) { uptr old = irq_enabled ? 0x200u : 0; irq_enabled = false; return old; }
void irq_restore(uptr old) { irq_enabled = (old & 0x200u) != 0; }
void idle_once(void) { assert(!irq_enabled); ++ticks; }
static void transition(u8 status, u32 delay) {
    pending = delay != 0; pending_at = ticks; pending_delay = delay;
    next_status = status; ata_status = pending ? 0xd0 : status;
}
static u8 inb(u16 p) {
    if (p != 0x1f7) return 0;
    if (pending && ticks - pending_at >= pending_delay) {
        pending = false; ata_status = next_status;
    }
    return ata_status;
}
static u16 inw(u16 p) {
    assert(p == 0x1f0 && ata_status == 0x48 && words < 256);
    if (++words == 256) transition(0x50, completion_delay);
    return 0;
}
static void outb(u16 p, u8 v) {
    assert(nwrites < 64); writes[nwrites].port=p; writes[nwrites++].byte=v;
    if (p == 0x1f7) {
        assert(ata_status == 0x50);
        words = 0;
        transition(v == 0xea || v == 0xe7 ? 0x50 : 0x48,
                   v == 0xea || v == 0xe7 ? completion_delay : data_delay);
    }
}
static void outw(u16 p, u16 v) {
    (void)v; assert(p == 0x1f0 && ata_status == 0x48 && words < 256);
    if (++words == 256) transition(0x50, completion_delay);
}
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
    identify[83] = 0x5400u; assert(!nv_ata_flush_ext(identify));
    identify[83] = 0x7400u; assert(nv_ata_flush_ext(identify));
    identify[83] = 0xb400u; assert(!nv_ata_flush_ext(identify));
    u8 h[512]; sectors = 1ull << 48;
    header(h, "NVSTORE2", 2, 32777, 32769, 1ull << 33);
    assert(parse_header(h) && layout.snap_cap == SNAP_CAP_MAX);
    header(h, "NVSTORE1", 2, 32777, 32769, 1ull << 33); assert(!parse_header(h));
    header(h, "NVSTORE1", 1, 4096, 2049, 0); assert(parse_header(h));
    header(h, "NVSTORE2", 2, 16, 8, 24); assert(parse_header(h) && layout.snap_cap == 3584);
    header(h, "NVSTORE2", 2, 16, 8, 23); assert(!parse_header(h));
    header(h, "NVSTORE2", 2, 0, 0xfffffff8u, 1ull << 33); assert(!parse_header(h));
    identified = owned = lba48 = flush_ext = true;
    assert(!command(0x123456789abcull, 0x30));
    const u8 expected[] = {0, 0x56, 0x34, 0x12, 1, 0xbc, 0x9a, 0x78, 0x40, 0x34};
    assert(nwrites == sizeof(expected));
    for (u32 i = 0; i < nwrites; ++i) assert(writes[i].byte == expected[i]);
    assert(command(1ull << 48, 0x20) == -NV_ENODEV);
    ata_status = 0x50; nwrites = 0; assert(!disk_flush() && writes[0].port == 0x1f7 && writes[0].byte == 0xea);
    flush_ext = false; nwrites = 0;
    assert(!disk_flush() && writes[0].byte == 0xe7); /* LBA48 without FLUSH EXT. */
    lba48 = false; nwrites = 0;
    assert(command(1ull << 28, 0x20) == -NV_ENODEV && !nwrites);
    assert(!disk_flush() && writes[0].byte == 0xe7);
    /* Both the data phase and final device completion can be asynchronous.
     * Verify complete sectors, not a particular number of polling reads. */
    u16 data[256] = {0}; lba48 = true;
    data_delay = 3; completion_delay = 4;
    for (u32 enabled = 0; enabled < 2; ++enabled) {
        irq_enabled = enabled; nwrites = 0; u64 start = ticks;
        assert(!transfer(7, data, true) && words == 256 && ata_status == 0x50);
        assert(ticks - start == 7 && irq_enabled == (bool)enabled);
        nwrites = 0; start = ticks;
        assert(!transfer(8, data, false) && words == 256 && ata_status == 0x50);
        assert(ticks - start == 7 && irq_enabled == (bool)enabled);
        nwrites = 0; start = ticks;
        assert(!disk_flush() && ticks - start == 4 && irq_enabled == (bool)enabled);
    }
    /* A stuck device, stale DRQ, or ATA errors must never publish another
     * command or write any data. Timeouts preserve the caller's IF state. */
    const u8 failed_status[] = {0xd0, 0x48, 0x51, 0x70, 0, 0xff};
    for (u32 i = 0; i < sizeof(failed_status); ++i) {
        pending = false; ata_status = failed_status[i]; irq_enabled = i & 1;
        nwrites = 0; u64 start = ticks;
        int result = wait_ready(false);
        assert(result == (i >= 4 ? -NV_ENODEV : -NV_EIO));
        assert(irq_enabled == (bool)(i & 1));
        if (i < 2) assert(ticks - start == 500);
        else assert(ticks == start);
        assert(command(9, 0x30) < 0 && !nwrites);
    }
    /* Unsigned elapsed time also works across a PIT counter wrap. */
    ticks = ~(u64)0 - 1; irq_enabled = true;
    transition(0x48, 3);
    assert(!wait_ready(true) && ticks == 1 && irq_enabled);
    ata_status = 0xd0; pending = false; ticks = ~(u64)0 - 200;
    u64 start = ticks;
    assert(wait_ready(false) == -NV_EIO && ticks - start == 500 && irq_enabled);
    puts("PASS ATA: elapsed-time delayed I/O, bounded busy/DRQ/error waits, IF and tick-wrap safety;");
    puts("PASS ATA: LBA48/LBA28, advertised FLUSH EXT, NVSTORE headers and 512e/4Kn validation");
}
