/* Exercise the real I225/I226 path, including station-address validation. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
static u8 dma[40][PAGE];
static u32 registers[0x20000u / 4u], pages, pci_command, received;
static u64 ticks;
static const uptr dma_base = 0x100000000ull;
static uptr page_alloc_below(u64 limit) {
    assert(limit == ~0ull && pages + 1 < 40);
    ++pages; memset(dma[pages], 0, PAGE); return dma_base + pages * PAGE;
}
static void page_free(uptr page) { assert((page - dma_base) / PAGE <= pages); }
static void *phys_ptr(uptr address) {
    assert(address > dma_base); address -= dma_base;
    assert(address / PAGE <= pages);
    return dma[address / PAGE] + address % PAGE;
}
static void *vm_mmio_map(u64 address, u32 length) {
    assert(address == 0x40000000u && length == sizeof(registers));
    return registers;
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x2000);
    return offset == 0x10 ? 0x40000000u : offset == 4 ? pci_command : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert(address == 0x2000 && offset == 4); pci_command = value;
}
static void idle_once(void) {
    ++ticks;
    registers[0] &= ~(1u << 26); /* device completes CTRL.RST */
}
#include "../kernel/net_igc.c"
static void receive_frame(const void *data, u32 size) {
    assert(size == 60 && ((const u8 *)data)[0] == 0x42);
    ++received;
}
int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    registers[RAL / 4] = 0x03ff0102u;
    registers[RAH / 4] = 0x8000ff04u;
    registers[STATUS / 4] = 2u;
    if (argc == 2) {
        if (!strcmp(argv[1], "zero")) {
            registers[RAL / 4] = 0; registers[RAH / 4] = 0x80000000u;
        } else if (!strcmp(argv[1], "multicast")) {
            registers[RAL / 4] |= 1u;
        } else if (!strcmp(argv[1], "broadcast")) {
            registers[RAL / 4] = 0xffffffffu; registers[RAH / 4] = 0x8000ffffu;
        } else {
            assert(!strcmp(argv[1], "unprogrammed"));
            registers[RAH / 4] &= ~(1u << 31);
        }
    }
    u8 mac[6];
    if (argc == 2) {
        assert(!net_igc_start(0x2000, mac) && !pages && !(pci_command & 4u));
        puts("PASS igc: invalid station address rejected before DMA allocation");
        return 0;
    }
    assert(net_igc_start(0x2000, mac));
    assert(!memcmp(mac, "\x02\x01\xff\x03\x04\xff", sizeof(mac)));
    assert((pci_command & 6u) == 6u && rx[0].address == rx_buf[0]);
    assert(registers[RDBAH / 4] == 1 && registers[TDBAH / 4] == 1);
    u8 frame[60] = {0x42};
    for (u32 i = 0; i < IGC_RING; ++i) assert(!net_igc_send(frame, sizeof(frame)));
    assert(net_igc_send(frame, sizeof(frame)) == -NV_EAGAIN);
    tx[0].flags |= 1ull << 32;
    assert(!net_igc_send(frame, sizeof(frame)));
    memcpy(phys_ptr(rx_buf[0]), frame, sizeof(frame));
    rx[0].flags = (u64)sizeof(frame) << 32 | 3u;
    net_igc_poll(receive_frame);
    assert(received == 1 && rx[0].flags == 0 && rx[0].address == rx_buf[0]);
    registers[STATUS / 4] = 0;
    assert(net_igc_send(frame, sizeof(frame)) == -NV_ENODEV);
    puts("PASS igc: unicast MAC with ff octets, high DMA, TX pressure and RX recycling");
}
