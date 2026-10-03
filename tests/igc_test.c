/* Exercise the real I225/I226 path, including station-address validation. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
static u8 dma[128][PAGE];
static u32 device_registers[2][0x20000u / 4u], *registers = device_registers[0];
static u32 pci_command[2], received, mapped_device, allocation_calls, allocation_fail;
static u32 page_owner[128], attempt, allocated_pages, freed_pages;
static bool published[128], reset_stuck;
static u32 queue_stuck;
static u32 transmitted;
static u64 ticks;
static const uptr dma_base = 0x100000000ull;
static uptr page_alloc_below(u64 limit) {
    assert(limit == ~0ull);
    if (++allocation_calls == allocation_fail) return 0;
    for (u32 i = 1; i < 128; ++i) {
        if (page_owner[i]) continue;
        page_owner[i] = attempt; ++allocated_pages;
        memset(dma[i], 0, PAGE); return dma_base + i * PAGE;
    }
    assert(!"fixture DMA pool exhausted"); return 0;
}
static void page_free(uptr page) {
    u32 i = (u32)((page - dma_base) / PAGE);
    assert(page > dma_base && i < 128 && page_owner[i] && !published[i]);
    page_owner[i] = 0; --allocated_pages; ++freed_pages;
}
static void *phys_ptr(uptr address) {
    assert(address > dma_base); address -= dma_base;
    assert(address / PAGE < 128 && page_owner[address / PAGE]);
    return dma[address / PAGE] + address % PAGE;
}
static void *vm_mmio_map(u64 address, u32 length) {
    assert((address == 0x40000000u || address == 0x50000000u) &&
           length == sizeof(device_registers[0]));
    mapped_device = address == 0x50000000u;
    registers = device_registers[mapped_device];
    return registers;
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x2000 || address == 0x2100);
    u32 device = address == 0x2100;
    return offset == 0x10 ? (device ? 0x50000000u : 0x40000000u) :
           offset == 4 ? pci_command[device] : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert((address == 0x2000 || address == 0x2100) && offset == 4);
    pci_command[address == 0x2100] = value;
}
static void idle_once(void) {
    ++ticks;
    if (!reset_stuck || mapped_device) registers[0] &= ~(1u << 26);
}
static u32 sim_read(u32 offset);
static void sim_write(u32 offset, u32 value);
#define IGC_REG_READ(offset) sim_read(offset)
#define IGC_REG_WRITE(offset, value) sim_write(offset, value)
#include "../kernel/net_igc.c"
static u32 sim_read(u32 offset) {
    if (!mapped_device && queue_stuck == offset) return registers[offset / 4] & ~(1u << 25);
    return registers[offset / 4];
}
static void sim_write(u32 offset, u32 value) {
    registers[offset / 4] = value;
    if (offset == RDBAL || offset == TDBAL) {
        /* Once a ring base is visible, none of this attempt's DMA pages may
         * enter the allocator again, even if queue enable times out. */
        for (u32 i = 1; i < 128; ++i)
            if (page_owner[i] == attempt) published[i] = true;
    }
}
static void prepare_controller(void) {
    ++attempt; allocation_calls = allocation_fail = queue_stuck = 0;
    reset_stuck = false;
    memset(device_registers[0], 0, sizeof(device_registers[0]));
    for (u32 device = 0; device < 2; ++device) {
        device_registers[device][RAL / 4] = 0x03ff0102u;
        device_registers[device][RAH / 4] = 0x8000ff04u;
        device_registers[device][STATUS / 4] = 2u;
    }
}
static void check_failed_controller(const char *scenario) {
    prepare_controller();
    u32 live = allocated_pages, freed = freed_pages;
    if (!strcmp(scenario, "zero")) {
        device_registers[0][RAL / 4] = 0; device_registers[0][RAH / 4] = 0x80000000u;
    } else if (!strcmp(scenario, "multicast")) {
        device_registers[0][RAL / 4] |= 1u;
    } else if (!strcmp(scenario, "broadcast")) {
        device_registers[0][RAL / 4] = 0xffffffffu; device_registers[0][RAH / 4] = 0x8000ffffu;
    } else if (!strcmp(scenario, "unprogrammed")) {
        device_registers[0][RAH / 4] &= ~(1u << 31);
    } else if (!strcmp(scenario, "reset")) {
        reset_stuck = true;
    } else if (!strcmp(scenario, "rx-timeout")) {
        queue_stuck = RXDCTL;
    } else if (!strcmp(scenario, "tx-timeout")) {
        queue_stuck = TXDCTL;
    } else {
        assert(!strcmp(scenario, "oom")); allocation_fail = 17;
    }
    u8 mac[6];
    assert(!net_igc_start(0x2000, mac) && !net_igc_link() && !(pci_command[0] & 4u));
    if (queue_stuck) {
        assert(allocated_pages == live + 34 && freed_pages == freed);
    } else {
        assert(allocated_pages == live);
        if (reset_stuck) assert(allocation_calls == 34 && freed_pages == freed + 34);
        else if (allocation_fail) assert(allocation_calls == 18 && freed_pages == freed + 17);
        else assert(!allocation_calls && freed_pages == freed);
    }
}
static void check_all_allocation_failures(void) {
    for (u32 fail = 1; fail <= 34; ++fail) {
        prepare_controller(); allocation_fail = fail;
        u32 live = allocated_pages;
        u8 mac[6];
        assert(!net_igc_start(0x2000, mac));
        assert(allocation_calls >= fail && allocated_pages == live && !(pci_command[0] & 4u));
    }
}
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
    if (argc == 2) {
        check_failed_controller(argv[1]);
    } else {
        check_failed_controller("zero");
        check_all_allocation_failures();
        check_failed_controller("reset");
        check_failed_controller("rx-timeout");
        check_failed_controller("tx-timeout");
    }
    u32 quarantined = allocated_pages, freed = freed_pages;
    prepare_controller();
    u8 mac[6];
    assert(net_igc_start(0x2100, mac));
    assert(mapped_device == 1 && allocated_pages == quarantined + 34 && freed_pages == freed);
    assert(!(pci_command[0] & 4u));
    /* A late DMA write from the failed controller can touch its retained
     * pages, but must never overwrite the new controller's allocations. */
    for (u32 i = 1; i < 128; ++i)
        if (published[i] && page_owner[i] != attempt) memset(dma[i], 0xa5, PAGE);
    assert(!memcmp(mac, "\x02\x01\xff\x03\x04\xff", sizeof(mac)));
    assert((pci_command[1] & 6u) == 6u && rx[0].address == rx_buf[0]);
    assert(registers[RDBAH / 4] == 1 && registers[TDBAH / 4] == 1);
    u32 live = allocated_pages;
    assert(!net_igc_start(0x2000, mac) && mapped_device == 1 && allocated_pages == live);
    u8 frame[60] = {0x42};
    check_tx_pressure(frame);
    memcpy(phys_ptr(rx_buf[0]), frame, sizeof(frame));
    rx[0].flags = (u64)sizeof(frame) << 32 | 3u;
    net_igc_poll(receive_frame);
    assert(received == 1 && rx[0].flags == 0 && rx[0].address == rx_buf[0]);
    registers[STATUS / 4] = 0;
    assert(net_igc_send(frame, sizeof(frame)) == -NV_ENODEV);
    puts("PASS igc: failed first controller fallback, DMA quarantine, high DMA, TX pressure and RX recycling");
}
