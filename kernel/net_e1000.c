#include "kernel.h"

/* Polled legacy-descriptor path for Intel PRO/1000-compatible devices.
 * Supported controllers have 64-bit descriptor and ring base addresses. */
#define E1000_RING 16u
#define E1000_FRAME 2048u
#define E1000_MMIO 0x20000u
#define E1000_DMA_LIMIT (~0ull)
#define CTRL 0x0000u
#define STATUS 0x0008u
#define TIPG 0x0410u
#define RCTL 0x0100u
#define TCTL 0x0400u
#define IMC 0x00d8u
#define RAL 0x5400u
#define RAH 0x5404u
#define RDBAL 0x2800u
#define RDBAH 0x2804u
#define RDLEN 0x2808u
#define RDH 0x2810u
#define RDT 0x2818u
#define TDBAL 0x3800u
#define TDBAH 0x3804u
#define TDLEN 0x3808u
#define TDH 0x3810u
#define TDT 0x3818u
#define CTRL_RST (1u << 26)

struct e1000_rx_desc {
    u64 address;
    u16 length, checksum;
    u8 status, errors;
    u16 special;
} PACKED;
struct e1000_tx_desc {
    u64 address;
    u16 length;
    u8 checksum, command;
    u8 status, css;
    u16 special;
} PACKED;
_Static_assert(sizeof(struct e1000_rx_desc) == 16, "e1000 receive descriptor");
_Static_assert(sizeof(struct e1000_tx_desc) == 16, "e1000 transmit descriptor");

static volatile u8 *regs;
static struct e1000_rx_desc *rx;
static struct e1000_tx_desc *tx;
static uptr rx_page, tx_page, rx_buf[E1000_RING], tx_buf[E1000_RING];
static u32 tx_head, rx_head, pci_address;
static u16 saved_command;
static bool running;

#ifndef E1000_REG_READ
#define E1000_REG_READ(offset) (*(volatile u32 *)(regs + (offset)))
#define E1000_REG_WRITE(offset, value) (*(volatile u32 *)(regs + (offset)) = (value))
#endif
static u32 rd(u32 offset) { return E1000_REG_READ(offset); }
static void wr(u32 offset, u32 value) { E1000_REG_WRITE(offset, value); }
static void fence(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

static void release_pages(void) {
    for (u32 i = 0; i < E1000_RING; ++i) {
        if (rx_buf[i]) { page_free(rx_buf[i]); rx_buf[i] = 0; }
        if (tx_buf[i]) { page_free(tx_buf[i]); tx_buf[i] = 0; }
    }
    if (rx_page) { page_free(rx_page); rx_page = 0; }
    if (tx_page) { page_free(tx_page); tx_page = 0; }
}

static bool reset_done(void) {
    u64 start = ticks;
    while (ticks - start < 100) {
        if (!(rd(CTRL) & CTRL_RST)) return true;
        idle_once();
    }
    return false;
}

bool net_e1000_start(u32 address, u8 mac[6]) {
    if (regs || running) return false;
    u32 bar = pci_read(address, 0x10);
    if ((bar & 1u) || ((bar & 6u) != 0 && (bar & 6u) != 4u)) return false;
    u64 physical = bar & ~15u;
    if ((bar & 6u) == 4u) physical |= (u64)pci_read(address, 0x14) << 32;
    if (!physical || physical >> 52) return false;
    regs = vm_mmio_map(physical, E1000_MMIO);
    if (!regs) return false;
    pci_address = address;
    saved_command = (u16)pci_read(address, 4);
    pci_write16(address, 4, (u16)((saved_command | 2u) & ~4u));

    u32 low = rd(RAL), high = rd(RAH);
    for (u32 i = 0; i < 4; ++i) mac[i] = (u8)(low >> (8 * i));
    mac[4] = (u8)high; mac[5] = (u8)(high >> 8);
    /* Interior 0xff octets are valid in a unicast station address. The
     * multicast bit also rejects the all-ones broadcast address. */
    bool valid = (high & (1u << 31)) && (low | (high & 0xffffu)) && !(mac[0] & 1u);
    if (!valid) {
        pci_write16(address, 4, (u16)((saved_command | 2u) & ~4u));
        release_pages(); regs = NULL; return false;
    }

    rx_page = page_alloc_below(E1000_DMA_LIMIT);
    tx_page = page_alloc_below(E1000_DMA_LIMIT);
    if (!rx_page || !tx_page) goto fail;
    for (u32 i = 0; i < E1000_RING; ++i) {
        rx_buf[i] = page_alloc_below(E1000_DMA_LIMIT);
        tx_buf[i] = page_alloc_below(E1000_DMA_LIMIT);
        if (!rx_buf[i] || !tx_buf[i]) goto fail;
    }
    rx = phys_ptr(rx_page); tx = phys_ptr(tx_page);
    memset(rx, 0, PAGE); memset(tx, 0, PAGE);
    for (u32 i = 0; i < E1000_RING; ++i) {
        rx[i].address = rx_buf[i];
        tx[i].status = 1; /* DD: a fresh descriptor is available to software. */
    }

    wr(IMC, 0xffffffffu);
    wr(RCTL, 0);
    wr(TCTL, 0);
    wr(CTRL, rd(CTRL) | CTRL_RST);
    if (!reset_done()) goto fail_bus;
    wr(IMC, 0xffffffffu);
    wr(RAL, low); wr(RAH, high);
    wr(CTRL, rd(CTRL) | (1u << 6)); /* SLU: do not wait for a PHY reset. */
    pci_write16(address, 4, (u16)(saved_command | 6u | (1u << 10)));

    wr(RDBAL, (u32)rx_page); wr(RDBAH, (u32)(rx_page >> 32)); wr(RDLEN, E1000_RING * 16u);
    wr(RDH, 0); wr(RDT, E1000_RING - 1u);
    wr(TDBAL, (u32)tx_page); wr(TDBAH, (u32)(tx_page >> 32)); wr(TDLEN, E1000_RING * 16u);
    wr(TDH, 0); wr(TDT, 0);
    wr(TIPG, (10u << 0) | (4u << 10) | (6u << 20));
    fence();
    wr(TCTL, (1u << 1) | (1u << 3) | (15u << 4) | (0x40u << 12));
    wr(RCTL, (1u << 1) | (1u << 15) | (1u << 26));
    running = true;
    return true;

fail_bus:
    pci_write16(address, 4, (u16)((saved_command | 2u) & ~4u));
fail:
    release_pages();
    regs = NULL; running = false; pci_address = 0;
    return false;
}

bool net_e1000_link(void) { return running && (rd(STATUS) & 2u); }

int net_e1000_send(const void *frame, u32 size) {
    if (!net_e1000_link()) return -NV_ENODEV;
    if (size < 14 || size > 1514) return -NV_EINVAL;
    struct e1000_tx_desc *d = &tx[tx_head];
    fence();
    if (!(d->status & 1u)) return -NV_EAGAIN;
    memcpy(phys_ptr(tx_buf[tx_head]), frame, size);
    d->address = tx_buf[tx_head];
    d->length = (u16)size;
    d->checksum = 0;
    d->command = (u8)(0x01u | 0x02u | 0x08u); /* EOP | IFCS | RS. */
    fence();
    d->status = 0;
    fence();
    tx_head = (tx_head + 1u) % E1000_RING;
    wr(TDT, tx_head);
    return 0;
}

void net_e1000_poll(void (*receive)(const void *, u32)) {
    if (!running) return;
    for (u32 budget = 0; budget < E1000_RING; ++budget) {
        struct e1000_rx_desc *d = &rx[rx_head];
        fence();
        u8 status = d->status;
        if (!(status & 1u)) break; /* DD */
        u16 length = d->length;
        if ((status & 2u) && !d->errors && length >= 14 && length <= E1000_FRAME)
            receive(phys_ptr(rx_buf[rx_head]), length);
        d->status = 0;
        fence();
        wr(RDT, rx_head);
        rx_head = (rx_head + 1u) % E1000_RING;
    }
}
