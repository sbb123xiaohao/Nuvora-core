/* Run the AHCI command-list/FIS path against a tiny HBA register and DMA
 * model. Commands can complete when PxCI is written or after the first PxIS
 * read, exercising the guest's polling and bounce-buffer code. */
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
static u8 last_flush;
static bool fail_io, lba28, short_dma, taskfile_error, no_flush_ext, overflow, overflow_late;
static volatile u64 ticks;
static bool irq_enabled, handoff_supported, bios_stuck;
static u32 hba_version = 0x00010301u, bohc, handoff_delay, handoff_requests;
static u32 unsafe_config, cap2_reads, bohc_reads, bohc_writes, freed;
static u32 device_address = 0x2000;
static u64 handoff_at;
static uptr irq_save(void) { uptr flags = irq_enabled ? 0x200u : 0; irq_enabled = false; return flags; }
static void irq_restore(uptr flags) { irq_enabled = !!(flags & 0x200u); }
static void idle_once(void) { assert(!irq_enabled); ++ticks; }
static uptr page_alloc_below(u64 limit) {
    assert(limit >= 0x100000000ull && next_page < 4);
    if (handoff_supported && (bohc & 0x11u)) ++unsafe_config;
    ++next_page; memset(dma[next_page], 0, PAGE); return next_page * PAGE;
}
static void page_free(uptr page) { assert(page / PAGE <= next_page); ++freed; }
static void *phys_ptr(uptr physical) {
    assert(physical && physical % PAGE == 0 && physical / PAGE <= next_page);
    return dma[physical / PAGE];
}
static void *vm_mmio_map(u64 physical, u32 size) {
    assert(physical == 0x40000000u && size == AHCI_MMIO); return mmio;
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == device_address);
    return offset == 0x24 ? 0x40000000u : offset == 4 ? pci_command : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert(address == device_address && offset == 4); pci_command = value;
}
static void pci_visit(void (*visit)(u32, u32, u32)) {
    visit(device_address, 0x12348086, 0x01060100);
}
static u32 sim_read(u32 offset);
static void sim_write(u32 offset, u32 value);
#define AHCI_REG_READ(off) sim_read(off)
#define AHCI_REG_WRITE(off, value) sim_write(off, value)
#include "../kernel/ahci.c"

static u32 sim_read(u32 offset) {
    u32 port = 0x100;
    if (offset == 0x0c) return 1u; /* one implemented port */
    if (offset == 0x10) return hba_version;
    if (offset == 0x24) { ++cap2_reads; return handoff_supported ? 1u : 0; }
    if (offset == 0x28) {
        ++bohc_reads;
        if ((bohc & 2u) && !bios_stuck && ticks - handoff_at >= handoff_delay)
            bohc &= ~0x1du; /* BIOS finishes I/O, then clears BB/BOS/SOOE/OOC. */
        return bohc;
    }
    if (offset == port + 0x18) return port_command;
    if (offset == port + 0x24) return 0x101u;
    if (offset == port + 0x28) return 0x103u; /* device present, active */
    if (offset == port + 0x38) {
        /* Overflow may arrive after software reads PxIS but before it sees
         * CI clear. The PRDBC count still only reports the PRDT's 512 bytes. */
        if (overflow && overflow_late && port_ci) {
            *(u32 *)(mmio + port + 0x10) = 1u << 24;
            port_ci = 0;
        }
        return port_ci;
    }
    return *(u32 *)(mmio + offset);
}
static void sim_write(u32 offset, u32 value) {
    u32 port = 0x100;
    if (offset == 0x28) {
        ++bohc_writes;
        /* Software requests OOS without changing BIOS-owned bits or writing
         * one to the W1C OOC event before firmware observes its SMI. */
        assert((value & 0x15u) == (bohc & 0x15u) && !(value & 8u));
        if ((value & 2u) && !(bohc & 2u)) { ++handoff_requests; handoff_at = ticks; }
        bohc = (bohc & ~2u) | (value & 2u);
        bohc |= 8u; /* OOS transition generates OOC/firmware SMI. */
        return;
    }
    if (handoff_supported && (bohc & 0x11u) && (offset == 4 || offset >= port))
        ++unsafe_config;
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
            memset(id, 0, 512); id[49] = 1u << 9;
            id[83] = lba28 ? 0 : (no_flush_ext ? 0x5400 : 0x7400);
            if (lba28) { id[60] = 0; id[61] = 0x1000; }
            else { id[100] = 0; id[101] = 0; id[102] = 0x2000; } /* 2^45 sectors. */
        } else if (opcode == 0x25 || opcode == 0x20) {
            struct ahci_prdt *prdt = (struct ahci_prdt *)(table + 0x80);
            memset(phys_ptr((uptr)prdt->address), 0xa5, 512);
            if (lba28) ++lba28_reads;
        } else if (opcode == 0x35 || opcode == 0x30) {
            ++writes;
        }
        if (opcode == 0xe7 || opcode == 0xea) last_flush = opcode;
        header->bytes = header->prdt_count ? (short_dma ? 256 : 512) : 0;
        if (fail_io) *(u32 *)(mmio + port + 0x10) = 1u << 30;
        /* AHCI 1.3.1 sections 5.4.1, 6.1.5 and 6.2.2: overflow is reported
         * separately, PRDBC need not include excess bytes, and the controller
         * may continue operating until the command completes normally. */
        if (overflow && !overflow_late) *(u32 *)(mmio + port + 0x10) = 1u << 24;
        if (taskfile_error) *(u32 *)(mmio + port + 0x20) = 1u;
        port_ci = overflow && overflow_late ? 1 : 0;
        return;
    }
    if (offset == port + 0x38) { port_ci = value; return; }
    if (offset == port + 0x10 || offset == port + 0x30) {
        *(u32 *)(mmio + offset) &= ~value; return; /* W1C */
    }
    *(u32 *)(mmio + offset) = value;
}

static void handoff_reset(u32 status, u32 delay, bool stuck) {
    memset(mmio, 0, sizeof(mmio));
    next_page = freed = port_command = port_clb = port_ci = 0;
    cap2_reads = bohc_reads = bohc_writes = handoff_requests = unsafe_config = 0;
    handoff_supported = true; bios_stuck = stuck;
    bohc = status; handoff_delay = delay;
    pci_command = 0x405u; /* Firmware bus mastering remains enabled during cleanup. */
    hba_version = 0x00010301u;
    device_address = 0x2000;
}
static void handoff_tests(void) {
    u64 capacity; u32 address, port;
    for (u32 enabled = 0; enabled < 2; ++enabled) {
        for (u32 delay = 0; delay < 2; ++delay) {
            handoff_reset(0x1du, delay ? 175 : 0, false);
            irq_enabled = enabled; ticks = enabled ? ~(u64)0 - 100 : 13;
            u64 before = ticks;
            assert(ahci_init(&capacity, 0, 0, &address, &port));
            assert(handoff_requests == 1 && bohc_writes == 1 && (bohc & 2u));
            assert(!unsafe_config && !(bohc & 0x11u) && ticks - before == handoff_delay);
            assert(irq_enabled == (bool)enabled && next_page == 4 && !freed);
            assert(ahci_shutdown() && freed == 4);
        }
    }
    const u32 blocked[] = {1u, 16u, 17u};
    for (u32 i = 0; i < sizeof(blocked) / sizeof(*blocked); ++i) {
        handoff_reset(blocked[i] | 4u | 8u, 0, true);
        if (i == 2) device_address = 0x00ffff00u;
        irq_enabled = i & 1u; ticks = ~(u64)0 - 99;
        u64 before = ticks; capacity = 0xfeed; address = 0; port = 32;
        assert(!ahci_init(&capacity, 0, 0, &address, &port));
        assert(handoff_requests == 1 && ticks - before >= 200 && ticks - before <= 500);
        assert(!next_page && !freed && !unsafe_config && !port_clb && !port_command);
        assert(pci_command == 0x405u && capacity == 0xfeed && !address && port == 32);
        assert(irq_enabled == (bool)(i & 1u) && ahci_shutdown());
        assert(!unsafe_config && pci_command == 0x405u);
    }
    handoff_reset(0x1du, 0, true); handoff_supported = false;
    assert(ahci_init(&capacity, 0, 0, &address, &port) && cap2_reads && !bohc_reads && !bohc_writes);
    assert(ahci_shutdown());
    handoff_reset(0x1du, 0, true); hba_version = 0x00010100u;
    handoff_supported = false;
    assert(ahci_init(&capacity, 0, 0, &address, &port) && !cap2_reads && !bohc_reads && !bohc_writes);
    assert(ahci_shutdown());
    handoff_reset(0, 0, false); handoff_supported = false;
    puts("PASS AHCI firmware handoff: delayed ownership, BOS/BB timeout, IF/tick wrap, untouched DMA and old/non-BOH HBAs");
}

int main(void) {
    handoff_tests();
    u64 capacity = 0; u32 address = 0, port = 32;
    assert(ahci_init(&capacity, 0, 0, &address, &port) &&
           capacity == (1ull << 45) && address == 0x2000 && port == 0);
    u8 out[512];
    for (u32 late = 0; late < 2; ++late) {
        overflow = true;
        overflow_late = late != 0;
        memset(out, 0x3c, sizeof(out));
        int overflow_result = ahci_read(0x123456789aull, out);
        fprintf(stderr, "read %s overflow: PxIS=%08x, CI=%u, PRDBC=%u, return=%d expected=%d\n",
                late ? "late" : "immediate", *(u32 *)(mmio + 0x110), port_ci,
                ((struct ahci_header *)phys_ptr(port_clb))->bytes, overflow_result, -NV_EIO);
        assert(overflow_result == -NV_EIO && !ahci_ready());
        for (u32 i = 0; i < sizeof(out); ++i) assert(out[i] == 0x3c);
        assert(ahci_shutdown());
        next_page = 0; overflow = overflow_late = false;
        assert(ahci_init(&capacity, 0, 0, &address, &port));
    }
    assert(!ahci_read(0x123456789aull, out));
    for (u32 i = 0; i < sizeof(out); ++i) assert(out[i] == 0xa5);
    memset(out, 0x3c, sizeof(out)); assert(!ahci_write(8, out) && writes == 1);
    assert(!ahci_flush() && ahci_ready() && last_flush == 0xea);
    short_dma = true;
    assert(ahci_read(0x123456789aull, out) == -NV_EIO && !ahci_ready());
    assert(ahci_shutdown());
    next_page = 0; short_dma = false;
    assert(ahci_init(&capacity, 0, 0, &address, &port) && ahci_ready());
    taskfile_error = true;
    assert(ahci_read(0x123456789aull, out) == -NV_EIO && !ahci_ready());
    assert(ahci_shutdown());
    next_page = 0; taskfile_error = false;
    *(u32 *)(mmio + 0x120) = 0; /* Replaced controller after an ATA error. */
    assert(ahci_init(&capacity, 0, 0, &address, &port));
    fail_io = true;
    assert(ahci_read(0x123456789aull, out) == -NV_EIO && !ahci_ready());
    assert(ahci_shutdown() && !ahci_ready());
    next_page = 0; fail_io = false; no_flush_ext = true;
    assert(ahci_init(&capacity, 0, 0, &address, &port));
    assert(!ahci_flush() && last_flush == 0xe7);
    assert(ahci_shutdown());
    next_page = 0; no_flush_ext = false; lba28 = true;
    assert(ahci_init(&capacity, 0, 0, &address, &port) && capacity == (1u << 28));
    assert(!ahci_read(0x1234567u, out) && lba28_reads == 1);
    assert(!ahci_write(8, out) && writes == 2);
    assert(!ahci_flush() && last_flush == 0xe7);
    assert(ahci_read(1ull << 28, out) == -NV_ENODEV);
    assert(ahci_shutdown());
    puts("PASS AHCI: LBA48/LBA28 FIS, immediate/deferred overflow, DMA errors, flush and shutdown");
}
