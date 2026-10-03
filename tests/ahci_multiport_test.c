/* Foreign SATA disks and controllers must not hide a later Nuvora volume. */
#include <assert.h>
#include <stdio.h>
#include <nv/abi.h>
#include <nv/string.h>
#define NV_KERNEL_H
#define PAGE 4096u
#define SNAP_CAP_MAX (128u * 1024u * 1024u)
#define AHCI_MMIO 0x3000u
static volatile u64 ticks;
static uptr irq_save(void) { return 0; }
static void irq_restore(uptr flags) { assert(!flags); }
static void idle_once(void) { ++ticks; }
struct store_layout { u32 slot_lba[2], slot_sectors, snap_cap; u32 version; u64 data_first, data_end; };
static u8 dma[32][PAGE], mmio[2][AHCI_MMIO], nuvora_header[512];
static u32 next_page, pci_command[2], port_command[2][2], clb[2][2], writes[2][2], lba28_reads;
static bool first_supports_lba48 = true;
static bool first_is_4kn, second_controller, invalid_first_bar;
static bool second_fis_pending;
static u32 second_fis_delay;
static bool first_bios_owned;
static u32 first_bohc, first_handoff_requests, unsafe_first_config;
static u32 controller(void);
static uptr page_alloc_below(u64 limit) {
    assert(limit == 0x100000000ull && next_page + 1 < 32);
    if (first_bios_owned && !controller()) ++unsafe_first_config;
    ++next_page; memset(dma[next_page], 0, PAGE); return next_page * PAGE;
}
static void page_free(uptr page) { assert(page / PAGE <= next_page); }
static void *phys_ptr(uptr physical) {
    assert(physical && physical / PAGE <= next_page);
    return dma[physical / PAGE] + physical % PAGE;
}
static void *vm_mmio_map(u64 physical, u32 size) {
    assert(size == AHCI_MMIO);
    if (physical == 0x40000000u) return mmio[0];
    assert(second_controller && physical == 0x50000000u);
    return mmio[1];
}
static u32 pci_read(u32 address, u32 offset) {
    assert(address == 0x2000 || (second_controller && address == 0x3000));
    u32 n = address == 0x3000;
    return offset == 0x24 ? (n ? 0x50000000u :
                             (invalid_first_bar ? 0x40000004u : 0x40000000u)) :
           offset == 4 ? pci_command[n] : 0;
}
static void pci_write16(u32 address, u32 offset, u16 value) {
    assert((address == 0x2000 || (second_controller && address == 0x3000)) && offset == 4);
    pci_command[address == 0x3000] = value;
}
static void pci_visit(void (*visit)(u32, u32, u32)) {
    visit(0x2000, 0x12348086, 0x01060100);
    if (second_controller) visit(0x3000, 0x12348086, 0x01060100);
}
static u8 inb(u16 port) { (void)port; return 0xff; }
static u16 inw(u16 port) { (void)port; return 0; }
static void outb(u16 port, u8 value) { (void)port; (void)value; }
static void outw(u16 port, u16 value) { (void)port; (void)value; }
static bool nvme_init(u64 *capacity, u32 first_pci, u32 first_nsid,
                      u32 *selected_pci, u32 *selected_nsid) {
    (void)capacity; (void)first_pci; (void)first_nsid;
    (void)selected_pci; (void)selected_nsid; return false;
}
static int nvme_read(u64 lba, void *buffer) { (void)lba; (void)buffer; return -NV_ENODEV; }
static int nvme_write(u64 lba, const void *buffer) { (void)lba; (void)buffer; return -NV_ENODEV; }
static int nvme_flush(void) { return -NV_ENODEV; }
static bool nvme_ready(void) { return false; }
static bool nvme_shutdown(void) { return true; }
static u32 sim_read(u32 offset);
static void sim_write(u32 offset, u32 value);
#define AHCI_REG_READ(offset) sim_read(offset)
#define AHCI_REG_WRITE(offset, value) sim_write(offset, value)
#define transfer ahci_transfer
#include "../kernel/ahci.c"
#undef transfer
#include "../kernel/disk.c"
static u32 controller(void) { return ahci.pci == 0x3000u ? 1u : 0u; }

static u32 sim_read(u32 offset) {
    u32 n = controller();
    if (offset == 0x0c) return second_controller ? 1u : 3u;
    if (offset == 0x10) return 0x00010301u;
    if (offset == 0x24) return !n && first_bios_owned ? 1u : 0;
    if (offset == 0x28) return !n ? first_bohc : 0;
    if (offset >= 0x100 && offset < 0x200) {
        u32 port = (offset - 0x100) / 0x80, reg = (offset - 0x100) % 0x80;
        if (reg == 0x18) return port_command[n][port];
        if (reg == 0x24) return n && second_fis_pending ? 0xffffffffu : 0x101u;
        if (reg == 0x20 && n && second_fis_pending) {
            /* An attached port does not receive its initial D2H FIS until
             * the OS enables FRE; delivery may follow asynchronously. */
            if (!(port_command[n][port] & AHCI_CMD_FRE)) return 0x7fu;
            if (second_fis_delay) { --second_fis_delay; return 0x7fu; }
            second_fis_pending = false;
            return 0x50u;
        }
        if (reg == 0x28) return 0x103u;
        if (reg == 0x38) return 0;
    }
    return *(u32 *)(mmio[n] + offset);
}
static void sim_write(u32 offset, u32 value) {
    u32 n = controller();
    if (!n && offset == 0x28 && first_bios_owned) {
        assert((value & 0x15u) == (first_bohc & 0x15u) && !(value & 8u));
        assert(value & 2u); ++first_handoff_requests;
        first_bohc |= 2u; /* Firmware never completes this handoff. */
        return;
    }
    if (!n && first_bios_owned && (offset == 4 || offset >= 0x100))
        ++unsafe_first_config;
    if (offset >= 0x100 && offset < 0x200) {
        u32 port = (offset - 0x100) / 0x80, reg = (offset - 0x100) % 0x80;
        if (reg == 0x18) { port_command[n][port] = value; return; }
        if (reg == 0x00) { clb[n][port] = value; return; }
        if (reg == 0x10 || reg == 0x30) {
            *(u32 *)(mmio[n] + offset) &= ~value; return;
        }
        if (reg == 0x38 && (value & 1u)) {
            struct ahci_header *h = phys_ptr(clb[n][port]);
            u8 *table = phys_ptr((uptr)h->table);
            struct ahci_prdt *prdt = (struct ahci_prdt *)(table + 0x80);
            u8 *buffer = phys_ptr((uptr)prdt->address);
            if (table[2] == 0xec) {
                u16 *id = (u16 *)buffer;
                memset(buffer, 0, 512);
                id[49] = 1u << 9;
                id[83] = (!n && port == 0 && !first_supports_lba48) ? 0 : 0x4400u;
                id[106] = (!n && port == 0 && first_is_4kn) ? 0x5000u : 0x6003u;
                if (!n && port == 0 && first_is_4kn) id[117] = 2048u;
                id[100] = 8192;
                id[60] = 8192;
            } else if (table[2] == 0x25 || table[2] == 0x20) {
                if (table[2] == 0x20) ++lba28_reads;
                u64 lba = (u64)table[4] | (u64)table[5] << 8 | (u64)table[6] << 16 |
                          (u64)table[8] << 24 | (u64)table[9] << 32 | (u64)table[10] << 40;
                memset(buffer, 0, 512);
                if ((n || (!second_controller && port == 1)) && lba == 0)
                    memcpy(buffer, nuvora_header, 512);
            } else if (table[2] == 0x35) {
                ++writes[n][port];
            }
            h->bytes = h->prdt_count ? 512u : 0;
            return;
        }
    }
    *(u32 *)(mmio[n] + offset) = value;
}

int main(void) {
    u32 fields[] = {3, 512, 8, 2057, 2049};
    u64 capacity = 8192;
    memcpy(nuvora_header, "NVSTORE3", 8);
    memcpy(nuvora_header + 8, fields, sizeof(fields));
    memcpy(nuvora_header + 32, &capacity, sizeof(capacity));
    u32 crc = crc32(nuvora_header, 40);
    memcpy(nuvora_header + 40, &crc, sizeof(crc));
    assert(disk_init() && ahci_disk && ahci.port == 1);
    assert(disk_volume_count() == 1 && layout.version == 3);
    u8 buffer[512];
    assert(!disk_read(0, buffer) && !memcmp(buffer, "NVSTORE3", 8));
    assert(disk_volume_write(0, 0, buffer) == -NV_EACCESS);
    assert(!disk_volume_write(0, 8, buffer));
    assert(writes[0][0] == 0 && writes[0][1] == 1);
    ahci_shutdown();
    next_page = 0; /* The simulated allocator may reuse the freed DMA pages. */
    first_supports_lba48 = false;
    assert(disk_init() && ahci_disk && ahci.port == 1);
    assert(!disk_read(0, buffer) && !memcmp(buffer, "NVSTORE3", 8));
    assert(writes[0][0] == 0 && lba28_reads > 0);
    ahci_shutdown();
    next_page = 0;
    first_supports_lba48 = true; first_is_4kn = true;
    assert(disk_init() && ahci_disk && ahci.port == 1);
    assert(writes[0][0] == 0);
    assert(ahci_shutdown());
    next_page = 0;
    first_is_4kn = false; second_controller = true;
    assert(disk_init() && ahci_disk && ahci.pci == 0x3000 && ahci.port == 0);
    assert(!disk_volume_write(0, 8, buffer));
    assert(writes[0][0] == 0 && writes[1][0] == 1);
    assert(ahci_shutdown());
    next_page = 0; invalid_first_bar = true;
    assert(disk_init() && ahci_disk && ahci.pci == 0x3000 && ahci.port == 0);
    assert(ahci_shutdown());
    next_page = 0; invalid_first_bar = false;
    second_fis_pending = true; second_fis_delay = 3;
    assert(disk_init() && ahci_disk && ahci.pci == 0x3000 && ahci.port == 0);
    assert(!second_fis_pending);
    assert(!disk_volume_write(0, 8, buffer));
    assert(writes[0][0] == 0 && writes[1][0] == 2);
    assert(ahci_shutdown());
    next_page = 0; first_bios_owned = true; first_bohc = 0x1du;
    first_handoff_requests = unsafe_first_config = 0; pci_command[0] = 0x405u;
    u64 before = ticks;
    assert(disk_init() && ahci_disk && ahci.pci == 0x3000 && ahci.port == 0);
    assert(first_handoff_requests == 1 && ticks - before >= 200 && ticks - before <= 500);
    assert(!unsafe_first_config && pci_command[0] == 0x405u && next_page == 4);
    assert(!disk_volume_write(0, 8, buffer));
    assert(!writes[0][0] && writes[1][0] == 3);
    assert(ahci_shutdown());
    puts("PASS AHCI: foreign disks, later controllers, deferred initial FIS and malformed BAR5 skipped");
    puts("PASS AHCI: failed BIOS ownership skips untouched controller and mounts later data disk");
}
