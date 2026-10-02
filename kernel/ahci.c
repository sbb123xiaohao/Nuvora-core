#include "kernel.h"
#include <nv/ata.h>

/* One-port, polled AHCI transport. It deliberately uses a single 512-byte
 * bounce buffer and one command slot: this keeps the boot/storage path small,
 * works on firmware that leaves MSI/MSI-X disabled, and is sufficient for the
 * Nuvora metadata/file operations.  Unsupported ports are left untouched. */
#define AHCI_MMIO 0x3000u
#define AHCI_DMA_LIMIT 0x100000000ull
#define AHCI_PORT_BASE 0x100u
#define AHCI_PORT_STRIDE 0x80u
#define AHCI_TIMEOUT 20000000u
#define AHCI_CMD_ST (1u << 0)
#define AHCI_CMD_FRE (1u << 4)
#define AHCI_CMD_FR (1u << 14)
#define AHCI_CMD_CR (1u << 15)

struct ahci_header {
    u16 flags, prdt_count;
    u32 bytes;
    u64 table;
    u32 reserved[4];
} PACKED;
struct ahci_prdt {
    u64 address;
    u32 reserved;
    u32 bytes;
} PACKED;
_Static_assert(sizeof(struct ahci_header) == 32, "AHCI command header");
_Static_assert(sizeof(struct ahci_prdt) == 16, "AHCI PRDT entry");

static struct {
    volatile u8 *regs;
    u32 pci, port, scan_min_pci;
    uptr command_page, fis_page, table_page, data_page;
    u64 sectors;
    u64 dma_limit;
    bool lba48, flush_ext, online;
} ahci;

#ifndef AHCI_REG_READ
#define AHCI_REG_READ(offset) (*(volatile u32 *)(ahci.regs + (offset)))
#define AHCI_REG_WRITE(offset, value) (*(volatile u32 *)(ahci.regs + (offset)) = (value))
#endif
static u32 hr(u32 offset) { return AHCI_REG_READ(offset); }
static void hw(u32 offset, u32 value) { AHCI_REG_WRITE(offset, value); }
static u32 pr(u32 offset) { return hr(AHCI_PORT_BASE + ahci.port * AHCI_PORT_STRIDE + offset); }
static void pw(u32 offset, u32 value) {
    hw(AHCI_PORT_BASE + ahci.port * AHCI_PORT_STRIDE + offset, value);
}
static void pause_cpu(void) { __asm__ volatile("pause"); }

static void release_pages(void) {
    uptr pages[] = {ahci.command_page, ahci.fis_page, ahci.table_page, ahci.data_page};
    for (u32 i = 0; i < ARRAY_LEN(pages); ++i)
        if (pages[i]) page_free(pages[i]);
    ahci.command_page = ahci.fis_page = ahci.table_page = ahci.data_page = 0;
}

static bool stop_engine(void) {
    if (!ahci.regs) return true;
    u32 command = pr(0x18) & ~AHCI_CMD_ST;
    pw(0x18, command);
    for (u32 i = 0; i < AHCI_TIMEOUT; ++i) {
        if (!(pr(0x18) & AHCI_CMD_CR)) {
            pw(0x18, pr(0x18) & ~AHCI_CMD_FRE);
            for (u32 j = 0; j < AHCI_TIMEOUT; ++j) {
                if (!(pr(0x18) & AHCI_CMD_FR)) return true;
                pause_cpu();
            }
            return false;
        }
        pause_cpu();
    }
    return false;
}

static bool start_engine(void) {
    pw(0x00, (u32)ahci.command_page);
    pw(0x04, (u32)(ahci.command_page >> 32));
    pw(0x08, (u32)ahci.fis_page);
    pw(0x0c, (u32)(ahci.fis_page >> 32));
    pw(0x10, 0xffffffffu);
    u32 command = pr(0x18) | AHCI_CMD_FRE;
    pw(0x18, command);
    pw(0x18, command | AHCI_CMD_ST);
    return (pr(0x18) & (AHCI_CMD_FRE | AHCI_CMD_ST)) == (AHCI_CMD_FRE | AHCI_CMD_ST);
}

static void discover(u32 address, u32 id, u32 class_code) {
    (void)id;
    if (ahci.regs || address < ahci.scan_min_pci ||
        (class_code >> 8) != 0x010601u) return;
    u32 bar = pci_read(address, 0x24);
    /* ABAR is BAR5: a 64-bit BAR needs a following BAR, which BAR5 lacks. */
    if ((bar & 7u) != 0) return;
    u64 physical = bar & ~15u;
    if (!physical || physical & 15u || physical >> 52) return;
    ahci.regs = vm_mmio_map(physical, AHCI_MMIO);
    if (ahci.regs) ahci.pci = address;
}

static bool task_file_ready(void) {
    for (u32 i = 0; i < AHCI_TIMEOUT; ++i) {
        if (!(pr(0x20) & (0x80u | 0x08u))) return true; /* BSY / DRQ */
        pause_cpu();
    }
    return false;
}

static bool issue(u8 opcode, u64 lba, bool write, bool data) {
    struct ahci_header *header = phys_ptr(ahci.command_page);
    u8 *table = phys_ptr(ahci.table_page);
    memset(header, 0, PAGE);
    memset(table, 0, PAGE);
    header->flags = 5u | (write ? (1u << 6) : 0u);
    header->prdt_count = data ? 1 : 0;
    header->table = ahci.table_page;
    if (data) {
        struct ahci_prdt *prdt = (struct ahci_prdt *)(table + 0x80);
        prdt->address = ahci.data_page;
        prdt->bytes = 511u | (1u << 31);
    }
    u8 *fis = table;
    fis[0] = 0x27; fis[1] = 0x80; fis[2] = opcode; fis[7] = 0x40;
    if (opcode == 0x25 || opcode == 0x35) {
        fis[4] = (u8)lba; fis[5] = (u8)(lba >> 8); fis[6] = (u8)(lba >> 16);
        fis[8] = (u8)(lba >> 24); fis[9] = (u8)(lba >> 32); fis[10] = (u8)(lba >> 40);
        fis[12] = 1; fis[13] = 0;
    } else if (opcode == 0x20 || opcode == 0x30) {
        if (lba >= (1ull << 28)) return false;
        fis[4] = (u8)lba; fis[5] = (u8)(lba >> 8); fis[6] = (u8)(lba >> 16);
        fis[7] = 0xe0u | (u8)((lba >> 24) & 15u);
        fis[12] = 1;
    }
    if (!task_file_ready()) return false;
    pw(0x10, 0xffffffffu);
    pw(0x30, 0xffffffffu);
    pw(0x38, 1u);
    for (u32 i = 0; i < AHCI_TIMEOUT; ++i) {
        u32 status = pr(0x10);
        if (status & (1u << 30)) {
            pw(0x10, status);
            return false;
        }
        if (!(pr(0x38) & 1u)) {
            /* Command issue can clear after an ATA error or a short DMA.
             * Neither completion is safe to report as a successful sector. */
            if ((pr(0x20) & (0x01u | 0x20u)) ||
                (data && header->bytes != 512u)) return false;
            return true;
        }
        pause_cpu();
    }
    return false;
}

static bool identify(void) {
    if (!issue(0xec, 0, false, true)) return false;
    u16 *id = phys_ptr(ahci.data_page);
    if (!(id[49] & (1u << 9)) || !nv_ata_sector_512(id)) return false;
    ahci.lba48 = (id[83] & 0xc000u) == 0x4000u && (id[83] & (1u << 10));
    ahci.flush_ext = ahci.lba48 && nv_ata_flush_ext(id);
    ahci.sectors = ahci.lba48 ?
        ((u64)id[100] | ((u64)id[101] << 16) | ((u64)id[102] << 32) |
         ((u64)id[103] << 48)) : ((u32)id[60] | ((u32)id[61] << 16));
    return ahci.sectors >= 8192 && ahci.sectors <= (ahci.lba48 ? (1ull << 48) : (1ull << 28));
}

bool ahci_init(u64 *capacity, u32 first_pci, u32 first_port,
               u32 *selected_pci, u32 *selected_port) {
    if (first_pci > 0x00ffff00u || first_port >= 32 || !capacity ||
        !selected_pci || !selected_port) return false;
    while (first_pci <= 0x00ffff00u) {
        memset(&ahci, 0, sizeof(ahci));
        ahci.scan_min_pci = first_pci;
        pci_visit(discover);
        if (!ahci.regs) return false;
        u32 address = ahci.pci;
        u32 command = pci_read(address, 4);
        pci_write16(address, 4, (u16)(command | 6u));
        /* AE is required before the HBA port registers are interpreted. */
        hw(0x04, hr(0x04) | (1u << 31));
        u32 implemented = hr(0x0c);
        ahci.dma_limit = hr(0) & (1u << 31) ? ~0ull : AHCI_DMA_LIMIT;
        ahci.command_page = page_alloc_below(ahci.dma_limit);
        ahci.fis_page = page_alloc_below(ahci.dma_limit);
        ahci.table_page = page_alloc_below(ahci.dma_limit);
        ahci.data_page = page_alloc_below(ahci.dma_limit);
        bool stopped = true;
        if (ahci.command_page && ahci.fis_page && ahci.table_page && ahci.data_page) {
            for (u32 port = address == first_pci ? first_port : 0; port < 32; ++port) {
                if (!(implemented & (1u << port))) continue;
                ahci.port = port;
                u32 ssts = pr(0x28);
                if ((ssts & 0xfu) != 3u || ((ssts >> 8) & 0xfu) != 1u) continue;
                if (!stop_engine()) { stopped = false; break; }
                /* A port unused by firmware can retain the reset signature
                 * until FRE receives its first register D2H FIS. Start the
                 * private FIS buffer and wait for the task file before using
                 * PxSIG to distinguish an ATA disk from other devices. */
                bool started = start_engine() && task_file_ready();
                u32 signature = pr(0x24);
                if (!started || (signature && signature != 0x00000101u) || !identify()) {
                    if (!stop_engine()) { stopped = false; break; }
                    continue;
                }
                ahci.online = true;
                *capacity = ahci.sectors;
                *selected_pci = address;
                *selected_port = port;
                return true;
            }
        }
        pci_write16(address, 4, (u16)(command & ~4u));
        if (stopped) release_pages(); /* Failed stop: quarantine DMA pages. */
        first_pci = address + 0x100u;
        first_port = 0;
    }
    return false;
}

static int transfer(u64 lba, void *buffer, bool write) {
    if (!ahci.online || lba >= ahci.sectors) return -NV_ENODEV;
    if (write) memcpy(phys_ptr(ahci.data_page), buffer, 512);
    if (!issue(write ? (ahci.lba48 ? 0x35 : 0x30) : (ahci.lba48 ? 0x25 : 0x20),
               lba, write, true)) {
        ahci.online = false;
        return -NV_EIO;
    }
    if (!write) memcpy(buffer, phys_ptr(ahci.data_page), 512);
    return 0;
}

int ahci_read(u64 lba, void *buffer) { return transfer(lba, buffer, false); }
int ahci_write(u64 lba, const void *buffer) { return transfer(lba, (void *)buffer, true); }
int ahci_flush(void) {
    if (!ahci.online || !issue(ahci.flush_ext ? 0xea : 0xe7, 0, false, false)) {
        ahci.online = false;
        return -NV_EIO;
    }
    return 0;
}
bool ahci_ready(void) { return ahci.online; }
bool ahci_shutdown(void) {
    if (!ahci.regs) return true;
    if (!stop_engine()) {
        pci_write16(ahci.pci, 4, (u16)(pci_read(ahci.pci, 4) & ~4u));
        memset(&ahci, 0, sizeof(ahci));
        return false; /* Do not retry ports on a controller which still owns DMA. */
    }
    pci_write16(ahci.pci, 4, (u16)(pci_read(ahci.pci, 4) & ~4u));
    release_pages();
    memset(&ahci, 0, sizeof(ahci));
    return true;
}
