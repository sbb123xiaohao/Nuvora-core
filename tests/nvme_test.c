/* Run the actual NVMe queue/GPT/disk code against a doorbell and DMA model.
 * The disk image is the same generated dual-partition GPT fixture as ATA. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
#define SNAP_CAP_MAX (16u * 1024u * 1024u)
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; };
static FILE *disk_file;
static u64 disk_sectors;
static u8 dma[8][PAGE];
static u32 next_page, pci_command, completed, flushes, writes;
static bool fail_io;
static u64 cap = 15u | 1ull << 37;
static u32 ready;
static uptr page_alloc_below(u64 limit) {
    assert(limit >= 0x100000000ull && next_page < 7);
    ++next_page;
    memset(dma[next_page], 0, PAGE);
    return next_page * PAGE;
}
static void page_free(uptr page) { assert(page / PAGE <= next_page); }
static void *phys_ptr(uptr physical) {
    assert(physical && physical % PAGE == 0 && physical / PAGE <= next_page);
    return dma[physical / PAGE];
}
static void *vm_mmio_map(u64 physical, u32 size) {
    assert(physical == 0x40000000u && size == 2 * PAGE);
    return dma[0];
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x1000);
    return offset == 0x10 ? 0x40000000u : offset == 4 ? pci_command : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert(address == 0x1000 && offset == 4);
    pci_command = value;
}
static void pci_visit(void (*visit)(u32, u32, u32)) {
    visit(0x1000, 0x12348086, 0x01080200);
}
static u8 inb(u16 port) { (void)port; return 0xff; }
static u16 inw(u16 port) { (void)port; return 0; }
static void outb(u16 port, u8 value) { (void)port; (void)value; }
static void outw(u16 port, u16 value) { (void)port; (void)value; }
static u32 sim_read(u32 offset) {
    if (offset == 0) return (u32)cap;
    if (offset == 4) return (u32)(cap >> 32);
    if (offset == 0x1c) return ready;
    return 0;
}
static void sim_write(u32 offset, u32 value);
#define NVME_REG_READ(off) sim_read(off)
#define NVME_REG_WRITE(off, value) sim_write(off, value)
#include "../kernel/nvme.c"
#include "../kernel/disk.c"

static void sim_write(u32 offset, u32 value) {
    if (offset == 0x14) { ready = !!(value & 1); return; }
    if (offset != 0x1000 && offset != 0x1008) return;
    u32 qid = offset == 0x1008;
    struct nvme_queue *q = qid ? &nvme.io : &nvme.admin;
    struct nvme_command *sq = phys_ptr(q->sq_page);
    struct nvme_command *cmd = &sq[(value + q->depth - 1) % q->depth];
    struct nvme_completion *cq = phys_ptr(q->cq_page);
    struct nvme_completion done = {.cid = (u16)(cmd->cdw0 >> 16),
                                   .sq_id = (u16)qid, .status = q->phase};
    if (!qid && (cmd->cdw0 & 255u) == 6) {
        u8 *id = phys_ptr((uptr)cmd->prp1);
        if (cmd->cdw10 == 1) {
            u32 n = 1;
            memcpy(id + 516, &n, 4);
        } else {
            memcpy(id, &disk_sectors, 8);
            memcpy(id + 8, &disk_sectors, 8);
            id[130] = 9;
        }
    } else if (qid) {
        u8 opcode = cmd->cdw0 & 255u;
        if (!opcode) ++flushes;
        if (opcode == 1 || opcode == 2) {
            u64 lba = (u64)cmd->cdw10 | (u64)cmd->cdw11 << 32;
            assert(lba < disk_sectors && cmd->nsid == 1);
            assert(cmd->prp1 == nvme.data_page && !cmd->prp2 && !cmd->cdw12);
            if (fail_io) done.status |= 2u;
            else {
                assert(fseek(disk_file, (long)(lba * 512), SEEK_SET) == 0);
                u8 *buffer = phys_ptr((uptr)cmd->prp1);
                if (opcode == 1) {
                    assert(fwrite(buffer, 1, 512, disk_file) == 512);
                    ++writes;
                } else assert(fread(buffer, 1, 512, disk_file) == 512);
            }
        }
    }
    cq[q->head] = done;
    ++completed;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    disk_file = fopen(argv[1], "r+b");
    assert(disk_file);
    disk_sectors = (u64)strtoul(argv[2], NULL, 10) * 2048;
    u8 id[PAGE] = {0};
    memcpy(id, &disk_sectors, 8);
    memcpy(id + 8, &disk_sectors, 8);
    id[130] = 9;
    u64 blocks = 0;
    assert(nvme_namespace(id, &blocks) && blocks == disk_sectors);
    id[130] = 12; assert(!nvme_namespace(id, &blocks));
    id[130] = 9; id[128] = 8; assert(!nvme_namespace(id, &blocks));
    id[128] = 0; id[29] = 1; assert(!nvme_namespace(id, &blocks));
    assert(disk_init() && nvme_disk && (pci_command & 6u) == 6u);
    assert(disk_volume_count() == 2 && disk_partition_count() == 2);
    assert(completed > 30); /* repeated wraps of both phase and command ID */
    u8 sector[512];
    assert(!disk_volume_read(1, 0, sector) && !memcmp(sector, "NVSTORE2", 8));
    assert(disk_write(8, sector) == -NV_EINVAL);
    memset(sector, 0xa7, sizeof(sector));
    assert(!disk_volume_write(0, 10, sector) && writes == 1);
    memset(sector, 0, sizeof(sector));
    assert(!disk_volume_read(0, 10, sector) && sector[0] == 0xa7);
    assert(!disk_flush() && flushes == 1);
    fail_io = true;
    assert(disk_read(0, sector) == -NV_EIO);
    assert(disk_read(0, sector) == -NV_ENODEV);
    assert(!disk_ready());
    nvme_shutdown();
    assert(!(pci_command & 4u) && !nvme_ready());
    fclose(disk_file);
    puts("PASS NVMe: PCI, admin/I-O queues, namespace format, GPT volumes, bounded writes, flush and I/O errors");
}
