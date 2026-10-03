/* Exercise the real I225/I226 path, including station-address validation. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
static u8 dma[40][PAGE];
static u32 registers[0x20000u / 4u], pages, pci_command, received;
static u32 transmitted;
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
static u32 consume_tx(u32 budget) {
    u32 done = 0, head = registers[TDH / 4];
    /* Match the NIC's head/tail contract; equal positions are an empty queue. */
    while (head != registers[TDT / 4] && done < budget) {
        const u8 *packet = phys_ptr(tx[head].address);
        assert(!(tx[head].flags & (1ull << 32)) && (tx[head].flags & 0xffffu) == 60);
        assert(packet[0] == 0x42 && (packet[1] | (u32)packet[2] << 8) == transmitted);
        ++transmitted; ++done;
        tx[head].flags |= 1ull << 32;
        head = (head + 1u) % IGC_RING;
        registers[TDH / 4] = head;
    }
    return done;
}
static void check_full_tx(const u8 frame[60]) {
    u32 tail = tx_head, doorbell = registers[TDT / 4];
    struct igc_desc descriptor = tx[tail];
    u8 buffer[60]; memcpy(buffer, phys_ptr(tx_buf[tail]), sizeof(buffer));
    assert(net_igc_send(frame, 60) == -NV_EAGAIN);
    assert(tx_head == tail && registers[TDT / 4] == doorbell);
    assert(tx[tail].address == descriptor.address && tx[tail].flags == descriptor.flags &&
           !memcmp(buffer, phys_ptr(tx_buf[tail]), sizeof(buffer)));
}
static void check_tx_pressure(u8 frame[60]) {
    for (u32 i = 0; i < IGC_RING - 1u; ++i) {
        frame[1] = (u8)(transmitted + i); frame[2] = (u8)((transmitted + i) >> 8);
        assert(!net_igc_send(frame, 60));
    }
    frame[1] = (u8)(transmitted + IGC_RING - 1u);
    frame[2] = (u8)((transmitted + IGC_RING - 1u) >> 8);
    check_full_tx(frame);
    assert(consume_tx(1) == 1);
    assert(!net_igc_send(frame, 60));
    check_full_tx(frame);
    assert(consume_tx(IGC_RING) == IGC_RING - 1u);
    for (u32 round = 0; round < 32; ++round) {
        for (u32 i = 0; i < IGC_RING - 1u; ++i) {
            frame[1] = (u8)(transmitted + i); frame[2] = (u8)((transmitted + i) >> 8);
            assert(!net_igc_send(frame, 60));
        }
        check_full_tx(frame);
        assert(consume_tx(IGC_RING) == IGC_RING - 1u);
    }
    puts("PASS igc TX pressure: one empty slot, unchanged full queue, completion recovery and 32 pressure cycles");
}
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
    check_tx_pressure(frame);
    memcpy(phys_ptr(rx_buf[0]), frame, sizeof(frame));
    rx[0].flags = (u64)sizeof(frame) << 32 | 3u;
    net_igc_poll(receive_frame);
    assert(received == 1 && rx[0].flags == 0 && rx[0].address == rx_buf[0]);
    registers[STATUS / 4] = 0;
    assert(net_igc_send(frame, sizeof(frame)) == -NV_ENODEV);
    puts("PASS igc: unicast MAC with ff octets, high DMA, TX pressure and RX recycling");
}
