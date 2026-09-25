#include "kernel.h"

/* Small polled I225-V/I226-V path. The NIC is a physical PCIe bus master:
 * descriptors and packet buffers live in page-aligned RAM below 4 GiB. IRQ
 * routing is not yet available in this kernel, so completion is polled.
 * This driver intentionally does not match unrelated e1000/igb hardware. */
#define IGC_RING 16u
#define IGC_FRAME 2048u
#define IGC_MMIO 0x20000u
#define DMA_LIMIT 0x100000000ull
#define CTRL 0x0000u
#define STATUS 0x0008u
#define RCTL 0x0100u
#define TCTL 0x0400u
#define IMC 0x150cu
#define RDBAL 0xc000u
#define RDBAH 0xc004u
#define RDLEN 0xc008u
#define SRRCTL 0xc00cu
#define RDH 0xc010u
#define RDT 0xc018u
#define RXDCTL 0xc028u
#define TDBAL 0xe000u
#define TDBAH 0xe004u
#define TDLEN 0xe008u
#define TDH 0xe010u
#define TDT 0xe018u
#define TXDCTL 0xe028u
#define RAL 0x5400u
#define RAH 0x5404u
struct igc_desc { volatile u64 address, flags; };
static volatile u8 *regs;
static struct igc_desc *rx, *tx;
static uptr rx_page, tx_page, rx_buf[IGC_RING], tx_buf[IGC_RING];
static u8 pending[IGC_RING];
static u32 rx_head, tx_head;
static bool running;

static u32 rd(u32 offset) { return *(volatile u32 *)(regs + offset); }
static void wr(u32 offset, u32 value) { *(volatile u32 *)(regs + offset) = value; }
static void fence(void) { __asm__ volatile("mfence" ::: "memory"); }
static bool enabled(u32 offset) {
    u32 start = ticks;
    while (ticks - start < 10) {
        if (rd(offset) & (1u << 25)) return true;
        idle_once();
    }
    return false;
}
static void free_unpublished(void) {
    for (u32 i = 0; i < IGC_RING; ++i) {
        if (rx_buf[i]) { page_free(rx_buf[i]); rx_buf[i] = 0; }
        if (tx_buf[i]) { page_free(tx_buf[i]); tx_buf[i] = 0; }
    }
    if (rx_page) { page_free(rx_page); rx_page = 0; }
    if (tx_page) { page_free(tx_page); tx_page = 0; }
}
bool net_igc_start(u32 address, u8 mac[6]) {
    if (running || regs) return false;
    u32 bar = pci_read(address, 0x10);
    if ((bar & 1) || ((bar & 6) != 0 && (bar & 6) != 4)) return false;
    u64 phys = bar & ~15u;
    if ((bar & 6) == 4) phys |= (u64)pci_read(address, 0x14) << 32;
    if (!phys || phys % PAGE) return false;
    regs = vm_mmio_map(phys, IGC_MMIO);
    if (!regs) return false;
    u16 command = (u16)pci_read(address, 4);
    pci_write16(address, 4, (u16)((command | 2) & ~4u));
    u32 low = rd(RAL), high = rd(RAH);
    for (u32 i = 0; i < 4; ++i) mac[i] = (u8)(low >> (8 * i));
    mac[4] = (u8)high; mac[5] = (u8)(high >> 8);
    bool valid = (high & (1u << 31)) && !(mac[0] & 1);
    for (u32 i = 0; i < 6; ++i) valid &= mac[i] != 0xff;
    if (!(low | (high & 0xffff)) || !valid) return false;
    rx_page = page_alloc_below(DMA_LIMIT);
    tx_page = page_alloc_below(DMA_LIMIT);
    if (!rx_page || !tx_page) { free_unpublished(); return false; }
    for (u32 i = 0; i < IGC_RING; ++i) {
        rx_buf[i] = page_alloc_below(DMA_LIMIT);
        tx_buf[i] = page_alloc_below(DMA_LIMIT);
        if (!rx_buf[i] || !tx_buf[i]) { free_unpublished(); return false; }
    }
    rx = phys_ptr(rx_page); tx = phys_ptr(tx_page);
    for (u32 i = 0; i < IGC_RING; ++i) rx[i].address = rx_buf[i];
    /* Stop PCI interrupts before reset. No descriptor address is exposed to
     * the card until every ring buffer exists and the reset has finished. */
    wr(IMC, 0xffffffffu);
    wr(RCTL, 0);
    wr(TCTL, 0x8);
    wr(CTRL, rd(CTRL) | (1u << 26));
    u32 start = ticks;
    while (rd(CTRL) & (1u << 26)) {
        if (ticks - start >= 100) { free_unpublished(); return false; }
        idle_once();
    }
    start = ticks;
    while (ticks - start < 2) idle_once(); /* allow NVM/PHY restart */
    wr(IMC, 0xffffffffu);
    wr(RAL, low); wr(RAH, high);
    wr(CTRL, rd(CTRL) | (1u << 6)); /* allow PHY autonegotiation */
    pci_write16(address, 4, (u16)(command | 6u | (1u << 10)));

    wr(RXDCTL, 0); wr(TXDCTL, 0);
    wr(RDBAL, (u32)rx_page); wr(RDBAH, 0); wr(RDLEN, IGC_RING * 16);
    wr(RDH, 0); wr(RDT, 0);
    /* Advanced one-buffer descriptors, 2 KiB receive buffers. Preserve the
     * unrelated hardware bits of SRRCTL, including reserved fields. */
    wr(SRRCTL, (rd(SRRCTL) & ~0x03ff007fu) | 2u | 0x02000000u);
    wr(TDBAL, (u32)tx_page); wr(TDBAH, 0); wr(TDLEN, IGC_RING * 16);
    wr(TDH, 0); wr(TDT, 0);
    fence();
    wr(RXDCTL, 1u << 25); wr(TXDCTL, 1u << 25);
    if (!enabled(RXDCTL) || !enabled(TXDCTL)) {
        /* Quarantine all DMA pages: queue disable alone cannot prove that a
         * faulty controller has stopped reading them. */
        pci_write16(address, 4, (u16)((command | 2) & ~4u));
        return false;
    }
    wr(TCTL, (1u << 1) | (1u << 3) | (15u << 4) | (1u << 24));
    wr(RCTL, (1u << 1) | (1u << 15) | (1u << 26)); /* RX, broadcast, strip FCS */
    wr(RDT, IGC_RING - 1);
    running = true;
    return true;
}
bool net_igc_link(void) { return running && (rd(STATUS) & 2u); }
int net_igc_send(const void *frame, u32 size) {
    if (!net_igc_link()) return -NV_ENODEV;
    if (size < 14 || size > 1514) return -NV_EINVAL;
    u32 i = tx_head;
    if (pending[i]) {
        fence();
        if (!(tx[i].flags & (1ull << 32))) return -NV_EAGAIN;
        pending[i] = 0;
    }
    memcpy(phys_ptr(tx_buf[i]), frame, size);
    tx[i].address = tx_buf[i];
    tx[i].flags = ((u64)size << (32 + 14)) |
                  (u64)(size | 0x00300000u | 0x20000000u | 0x01000000u |
                        0x02000000u | 0x08000000u);
    pending[i] = 1;
    fence();
    tx_head = (i + 1) % IGC_RING;
    wr(TDT, tx_head);
    return 0;
}
void net_igc_poll(void (*receive)(const void *, u32)) {
    if (!running) return;
    for (u32 budget = 0; budget < IGC_RING; ++budget) {
        u32 i = rx_head;
        fence();
        u32 status = (u32)rx[i].flags;
        if (!(status & 1u)) break;
        u32 length = (u32)((rx[i].flags >> 32) & 0xffffu);
        if ((status & 2u) && !(status & 0x3f000000u) && length >= 14 && length <= IGC_FRAME)
            receive(phys_ptr(rx_buf[i]), length);
        rx[i].address = rx_buf[i];
        rx[i].flags = 0;
        fence();
        wr(RDT, i);
        rx_head = (i + 1) % IGC_RING;
    }
}
