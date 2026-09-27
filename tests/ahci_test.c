/* Run the AHCI command-list/FIS path against a tiny HBA register and DMA
 * model.  The model completes commands when PxCI is written, so this checks
 * the same polling and bounce-buffer code used by the guest. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
#define AHCI_MMIO 0x3000u
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; u32 version; u64 data_first, data_end; };
static u8 dma[8][PAGE], mmio[AHCI_MMIO];
static u32 next_page, pci_command, port_command, port_clb, port_ci, writes, lba28_reads;
static bool fail_io, lba28;
static uptr page_alloc_below(u64 limit) {
    assert(limit >= 0x100000000ull && next_page < 4);
    ++next_page; memset(dma[next_page], 0, PAGE); return next_page * PAGE;
}
static void page_free(uptr page) { assert(page / PAGE <= next_page); }
static void *phys_ptr(uptr physical) {
    assert(physical && physical % PAGE == 0 && physical / PAGE <= next_page);
    return dma[physical / PAGE];
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
static u32 sim_read(u32 offset);
static void sim_write(u32 offset, u32 value);
#define AHCI_REG_READ(off) sim_read(off)
#define AHCI_REG_WRITE(off, value) sim_write(off, value)
#include "../kernel/ahci.c"

static u32 sim_read(u32 offset) {
    u32 port = 0x100;
    if (offset == 0x0c) return 1u; /* one implemented port */
    if (offset == port + 0x18) return port_command;
    if (offset == port + 0x24) return 0x101u;
    if (offset == port + 0x28) return 0x103u; /* device present, active */
    if (offset == port + 0x38) return port_ci;
    return *(u32 *)(mmio + offset);
}
static void sim_write(u32 offset, u32 value) {
    u32 port = 0x100;
    if (offset == port + 0x18) { port_command = value; return; }
    if (offset == port + 0x00) { port_clb = value; return; }
    if (offset == port + 0x38 && (value & 1u)) {
        struct ahci_header *header = phys_ptr(port_clb);
        u8 *table = phys_ptr((uptr)header->table);
        u8 opcode = table[2];
        if (opcode == 0x25 || opcode == 0x35) {
            u64 lba = (u64)table[4] | (u64)table[5] << 8 | (u64)table[6] << 16 |
                      (u64)table[8] << 24 | (u64)table[9] << 32 | (u64)table[10] << 40;
            assert(table[12] == 1 && table[13] == 0);
            assert(lba == (opcode == 0x25 ? 0x123456789aull : 8u));
        }
        if (opcode == 0x20 || opcode == 0x30) {
            u32 lba = (u32)table[4] | (u32)table[5] << 8 |
                      (u32)table[6] << 16 | (u32)(table[7] & 15u) << 24;
            assert(lba28 && table[12] == 1 && !table[13] &&
                   !table[8] && !table[9] && !table[10]);
            assert(lba == (opcode == 0x20 ? 0x1234567u : 8u));
        }
        if (opcode == 0xec) {
            struct ahci_prdt *prdt = (struct ahci_prdt *)(table + 0x80);
            u16 *id = phys_ptr((uptr)prdt->address);
            memset(id, 0, 512); id[49] = 1u << 9; id[83] = lba28 ? 0 : 0x4400;
            if (lba28) { id[60] = 0; id[61] = 0x1000; }
            else { id[100] = 0; id[101] = 0; id[102] = 0x2000; } /* 2^45 sectors. */
        } else if (opcode == 0x25 || opcode == 0x20) {
            struct ahci_prdt *prdt = (struct ahci_prdt *)(table + 0x80);
            memset(phys_ptr((uptr)prdt->address), 0xa5, 512);
            if (lba28) ++lba28_reads;
        } else if (opcode == 0x35 || opcode == 0x30) {
            ++writes;
        }
        if (fail_io) *(u32 *)(mmio + port + 0x10) = 1u << 30;
        port_ci = 0;
        return;
    }
    if (offset == port + 0x38) { port_ci = value; return; }
    if (offset == port + 0x10 || offset == port + 0x30) {
        *(u32 *)(mmio + offset) &= ~value; return; /* W1C */
    }
    *(u32 *)(mmio + offset) = value;
}

int main(void) {
    u64 capacity = 0; u32 address = 0, port = 32;
    assert(ahci_init(&capacity, 0, 0, &address, &port) &&
           capacity == (1ull << 45) && address == 0x2000 && port == 0);
    u8 out[512]; assert(!ahci_read(0x123456789aull, out));
    for (u32 i = 0; i < sizeof(out); ++i) assert(out[i] == 0xa5);
    memset(out, 0x3c, sizeof(out)); assert(!ahci_write(8, out) && writes == 1);
    assert(!ahci_flush() && ahci_ready());
    fail_io = true;
    assert(ahci_read(0x123456789aull, out) == -NV_EIO && !ahci_ready());
    assert(ahci_shutdown() && !ahci_ready());
    next_page = 0; fail_io = false; lba28 = true;
    assert(ahci_init(&capacity, 0, 0, &address, &port) && capacity == (1u << 28));
    assert(!ahci_read(0x1234567u, out) && lba28_reads == 1);
    assert(!ahci_write(8, out) && writes == 2);
    assert(!ahci_flush());
    assert(ahci_read(1ull << 28, out) == -NV_ENODEV);
    assert(ahci_shutdown());
    puts("PASS AHCI: LBA48/LBA28 FIS, DMA bounce read/write, flush and shutdown");
}
