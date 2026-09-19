#include "kernel.h"
/* IDE primary master, ATA PIO. LBA48 is used whenever the drive reports it so
 * data images beyond 128 GiB work; LBA28 remains the fallback. The store
 * layout lives in sector 0 and describes its own slot geometry, so images can
 * be larger than the historical fixed 8 MiB. */
static bool identified, owned, lba48;
static u64 sectors;
static struct store_layout layout;
static void delay400(void) {
    for (u32 i = 0; i < 4; ++i)
        (void)inb(0x3f6);
}
static int wait_ready(bool data) {
    for (u32 i = 0; i < 1000000; ++i) {
        u8 s = inb(0x1f7);
        if (!s || s == 0xff)
            return -NV_ENODEV;
        if (s & 0x80)
            continue;
        if (s & 0x21)
            return -NV_EIO;
        if (!data || (s & 8))
            return 0;
    }
    return -NV_EIO;
}
static int command(u64 lba, u8 base_op) {
    if (!identified || lba >= sectors)
        return -NV_ENODEV;
    if (wait_ready(false) < 0)
        return -NV_EIO;
    if (lba48) {
        outb(0x1f2, 0);
        outb(0x1f3, (u8)(lba >> 24));
        outb(0x1f4, (u8)(lba >> 32));
        outb(0x1f5, (u8)(lba >> 40));
        outb(0x1f2, 1);
        outb(0x1f3, (u8)lba);
        outb(0x1f4, (u8)(lba >> 8));
        outb(0x1f5, (u8)(lba >> 16));
        outb(0x1f6, 0x40);
    } else {
        if (lba >= 0x10000000u)
            return -NV_ENODEV;
        outb(0x1f2, 1);
        outb(0x1f3, (u8)lba);
        outb(0x1f4, (u8)(lba >> 8));
        outb(0x1f5, (u8)(lba >> 16));
        outb(0x1f6, 0xe0 | (u8)(lba >> 24));
    }
    outb(0x1f7, lba48 ? (u8)(base_op + 4) : base_op);
    delay400();
    return wait_ready(true);
}
static int transfer(u64 lba, void *buf, bool write) {
    int r = command(lba, write ? 0x30 : 0x20);
    if (r < 0)
        return r;
    u16 *p = buf;
    if (write) {
        for (u32 i = 0; i < 256; ++i)
            outw(0x1f0, p[i]);
    } else {
        for (u32 i = 0; i < 256; ++i)
            p[i] = inw(0x1f0);
    }
    delay400();
    return wait_ready(false);
}
int disk_read(u64 lba, void *buf) {
    return transfer(lba, buf, false);
}
static bool writable(u64 lba) {
    return (lba >= layout.slot_lba[0] && lba < layout.slot_lba[0] + layout.slot_sectors) ||
           (lba >= layout.slot_lba[1] && lba < layout.slot_lba[1] + layout.slot_sectors);
}
int disk_write(u64 lba, const void *buf) {
    if (!owned)
        return -NV_EACCESS;
    if (!writable(lba))
        return -NV_EINVAL;
    return transfer(lba, (void *)buf, true);
}
int disk_flush(void) {
    if (!owned)
        return -NV_ENODEV;
    if (wait_ready(false) < 0)
        return -NV_EIO;
    outb(0x1f7, lba48 ? 0xea : 0xe7);
    delay400();
    return wait_ready(false);
}
bool disk_ready(void) {
    return owned;
}
bool disk_store_layout(struct store_layout *out) {
    if (!owned)
        return false;
    *out = layout;
    return true;
}
static bool parse_header(const u8 *header) {
    u32 fields[5], crc_stored;
    u64 total;
    memcpy(fields, header + 8, sizeof(fields));
    memcpy(&crc_stored, header + 40, sizeof(crc_stored));
    u32 version = fields[0];
    if (version == 2) {
        /* NVSTORE2: sector size, slot0 LBA, slot1 LBA, slot sectors (u32),
         * then a u64 total sector count and the CRC over the first 40 bytes. */
        if (memcmp(header, "NVSTORE2", 8) || crc_stored != crc32(header, 40))
            return false;
        memcpy(&total, header + 32, sizeof(total));
        if (fields[1] != 512 || fields[2] != 8 || fields[4] < 8 ||
            fields[4] > SNAP_CAP_MAX / 512 + 1 ||
            fields[3] != (u64)fields[2] + fields[4] ||
            total > sectors || total < (u64)fields[3] + fields[4])
            return false;
        layout.slot_lba[0] = fields[2];
        layout.slot_lba[1] = fields[3];
        layout.slot_sectors = fields[4];
    } else if (version == 1) {
        /* NVSTORE1: the historical fixed 8 MiB geometry, CRC over 28 bytes
         * stored immediately after the five u32 fields. */
        u32 nv1_crc;
        memcpy(&nv1_crc, header + 28, sizeof(nv1_crc));
        if (memcmp(header, "NVSTORE1", 8) || nv1_crc != crc32(header, 28) ||
            fields[1] != 512 || fields[2] != 8 ||
            fields[3] != 4096 || fields[4] != 2049 || sectors < 4096u + 2049u)
            return false;
        layout.slot_lba[0] = 8;
        layout.slot_lba[1] = 4096;
        layout.slot_sectors = 2049;
    } else
        return false;
    layout.snap_cap = MIN(SNAP_CAP_MAX, layout.slot_sectors * 512 - 512);
    return layout.snap_cap >= 512;
}
bool disk_init(void) {
    outb(0x3f6, 2);
    outb(0x1f6, 0xa0);
    delay400();
    outb(0x1f2, 0);
    outb(0x1f3, 0);
    outb(0x1f4, 0);
    outb(0x1f5, 0);
    outb(0x1f7, 0xec);
    delay400();
    if (wait_ready(true) < 0 || inb(0x1f4) || inb(0x1f5))
        return false;
    u16 id[256];
    for (u32 i = 0; i < 256; ++i)
        id[i] = inw(0x1f0);
    delay400();
    if (!(id[49] & (1u << 9)))
        return false;
    /* Word 83 validity signature is bits 15:14 == 01b; bit 10 declares LBA48. */
    lba48 = (id[83] & 0xc000) == 0x4000 && (id[83] & (1u << 10));
    if (lba48) {
        sectors = (u64)id[100] | ((u64)id[101] << 16) | ((u64)id[102] << 32) | ((u64)id[103] << 48);
    } else {
        sectors = (u32)id[60] | ((u32)id[61] << 16);
    }
    if (sectors < 8192 || sectors > (lba48 ? (1ull << 48) : (1ull << 28)))
        return false;
    identified = true;
    u8 header[512];
    if (disk_read(0, header) < 0)
        return false;
    if ((!memcmp(header, "NVSTORE2", 8) || !memcmp(header, "NVSTORE1", 8)) &&
        parse_header(header))
        owned = true;
    return owned;
}
