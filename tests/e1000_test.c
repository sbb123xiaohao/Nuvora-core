/* Exercise the real legacy RX/TX descriptor path with polled MMIO/DMA. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
static u8 dma[40][PAGE];
static u32 registers[0x20000u / 4u], pages, pci_command, received, received_size;
static u64 ticks;
static const uptr dma_base = 0x100000000ull;
static uptr page_alloc_below(u64 limit) {
    assert(limit == ~0ull && pages + 1 < 40);
    ++pages; memset(dma[pages], 0, PAGE); return dma_base + pages * PAGE;
}
static void page_free(uptr page) { assert((page - dma_base) / PAGE <= pages); }
static void *phys_ptr(uptr address) {
    assert(address > dma_base); address -= dma_base;
    assert(address && address / PAGE <= pages);
    return dma[address / PAGE] + address % PAGE;
}
static void *vm_mmio_map(u64 address, u32 length) {
    assert(address == 0x40000000u && length == sizeof(registers));
    return registers;
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x3000);
    return offset == 0x10 ? 0x40000000u : offset == 4 ? pci_command : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert(address == 0x3000 && offset == 4); pci_command = value;
}
static void idle_once(void) { ++ticks; }
static u32 sim_read(u32 offset) { return registers[offset / 4]; }
static void sim_write(u32 offset, u32 value);
#define E1000_REG_READ(off) sim_read(off)
#define E1000_REG_WRITE(off, value) sim_write(off, value)
#include "../kernel/net_e1000.c"
static void sim_write(u32 offset, u32 value) {
    if (offset == CTRL && (value & CTRL_RST)) value &= ~CTRL_RST;
    registers[offset / 4] = value;
    if (offset == TDT && running) tx[(value + E1000_RING - 1) % E1000_RING].status = 1;
}
static void receive_frame(const void *data, u32 size) {
    assert(size == 60 && ((const u8 *)data)[0] == 0x42);
    ++received; received_size = size;
}
int main(void) {
    /* Reject an unprogrammed, zero, multicast or broadcast station address. */
    u8 mac[6];
    registers[RAL / 4] = 0x03020102u;
    registers[RAH / 4] = 0x00000504u;
    assert(!net_e1000_start(0x3000, mac) && !pages);
    registers[RAL / 4] = 0;
    registers[RAH / 4] = 0x80000000u;
    assert(!net_e1000_start(0x3000, mac) && !pages);
    registers[RAL / 4] = 0x03020103u;
    registers[RAH / 4] = 0x80000504u;
    assert(!net_e1000_start(0x3000, mac) && !pages);
    registers[RAL / 4] = 0xffffffffu;
    registers[RAH / 4] = 0x8000ffffu;
    assert(!net_e1000_start(0x3000, mac) && !pages);
    /* 0xff inside a unicast address is legal; only the first octet's
     * multicast bit determines whether it can be a station address. */
    registers[RAL / 4] = 0x03ff0102u;
    registers[RAH / 4] = 0x80000504u;
    registers[STATUS / 4] = 2u;
    assert(net_e1000_start(0x3000, mac));
    assert(!memcmp(mac, "\x02\x01\xff\x03\x04\x05", sizeof(mac)));
    assert((pci_command & 6u) == 6u && rx[0].address == rx_buf[0]);
    assert(registers[RDLEN / 4] == 16 * E1000_RING);
    assert(registers[RDBAH / 4] == 1 && registers[TDBAH / 4] == 1);
    u8 frame[60] = {0x42};
    assert(!net_e1000_send(frame, sizeof(frame)) && tx[0].length == sizeof(frame));
    assert(tx[0].command == 0x0b && tx[0].status == 1);
    assert(tx[0].address > 0xffffffffull && rx[0].address > 0xffffffffull);
    memcpy(phys_ptr(rx_buf[0]), frame, sizeof(frame));
    rx[0].length = sizeof(frame); rx[0].status = 3;
    net_e1000_poll(receive_frame);
    assert(received == 1 && received_size == sizeof(frame));
    assert(rx[0].status == 0 && registers[RDT / 4] == 0);
    registers[STATUS / 4] = 0;
    assert(net_e1000_send(frame, sizeof(frame)) == -NV_ENODEV);
    puts("PASS e1000: BAR/MAC, DMA above 4 GiB, TX completion, RX recycle and link loss");
}
