#include "kernel.h"
/* IDE primary master, ATA PIO. LBA48 is used whenever the drive reports it so
 * data images beyond 128 GiB work; LBA28 remains the fallback. The store
 * layout lives in sector 0 and describes its own slot geometry, so images can
 * be larger than the historical fixed 8 MiB. */
static bool identified, owned, lba48, nvme_disk;
static u64 sectors;
static struct store_layout layout;
/* Primary and backup GPT entry arrays have at most 128 entries of 128 bytes.
 * Unknown partition types are never written; only our own validated NVSTORE
 * partitions are offered as volumes. */
static u8 gpt_entries[128 * 128];
static const u8 nuvora_type[16] = {
    0x78, 0x17, 0x43, 0x9b, 0x21, 0xb8, 0x48, 0x47,
    0xa3, 0xf8, 0x2d, 0x17, 0xc3, 0x7a, 0x56, 0x01};
static struct {
    u64 start, length;
    struct store_layout geometry;
} volumes[NV_VOLUME_MAX];
static u32 volume_count, partition_count;
static struct nv_partition_info partitions[NV_PARTITION_MAX];
static u32 volume_partition_number[NV_VOLUME_MAX];
static u32 le32(const u8 *p) { u32 n; memcpy(&n, p, 4); return n; }
static u64 le64(const u8 *p) { u64 n; memcpy(&n, p, 8); return n; }
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
    return nvme_disk ? nvme_read(lba, buf) : transfer(lba, buf, false);
}
static bool writable(u64 lba) {
    if (!volume_count || lba < volumes[0].start ||
        lba - volumes[0].start >= volumes[0].length) return false;
    u64 relative = lba - volumes[0].start;
    return (relative >= layout.slot_lba[0] && relative - layout.slot_lba[0] < layout.slot_sectors) ||
           (relative >= layout.slot_lba[1] && relative - layout.slot_lba[1] < layout.slot_sectors);
}
int disk_write(u64 lba, const void *buf) {
    if (!owned)
        return -NV_EACCESS;
    if (!writable(lba))
        return -NV_EINVAL;
    return nvme_disk ? nvme_write(lba, buf) : transfer(lba, (void *)buf, true);
}
int disk_flush(void) {
    if (!owned)
        return -NV_ENODEV;
    if (nvme_disk) return nvme_flush();
    if (wait_ready(false) < 0)
        return -NV_EIO;
    outb(0x1f7, lba48 ? 0xea : 0xe7);
    delay400();
    return wait_ready(false);
}
bool disk_ready(void) {
    return owned && (!nvme_disk || nvme_ready());
}
bool disk_store_layout(struct store_layout *out) {
    if (!disk_ready())
        return false;
    *out = layout;
    return true;
}
u32 disk_volume_count(void) { return volume_count; }
u32 disk_partition_count(void) { return partition_count; }
bool disk_partition_info(u32 index, struct nv_partition_info *out) {
    if (index >= partition_count) return false;
    *out = partitions[index];
    return true;
}
u32 disk_volume_partition_number(u32 index) {
    return index < volume_count ? volume_partition_number[index] : 0;
}
bool disk_volume_layout(u32 index, struct store_layout *out) {
    if (index >= volume_count) return false;
    *out = volumes[index].geometry;
    return true;
}
u64 disk_volume_sectors(u32 index) {
    return index < volume_count ? volumes[index].length : 0;
}
int disk_volume_read(u32 index, u64 relative, void *buf) {
    if (index >= volume_count || relative >= volumes[index].length)
        return -NV_ENODEV;
    return disk_read(volumes[index].start + relative, buf);
}
int disk_volume_write(u32 index, u64 relative, const void *buf) {
    if (index >= volume_count || relative >= volumes[index].length)
        return -NV_ENODEV;
    const struct store_layout *l = &volumes[index].geometry;
    if (!(l->version == 3 && relative >= l->data_first * 8 && relative < l->data_end * 8) &&
        !((relative >= l->slot_lba[0] && relative - l->slot_lba[0] < l->slot_sectors) ||
          (relative >= l->slot_lba[1] && relative - l->slot_lba[1] < l->slot_sectors)))
        return -NV_EACCESS;
    return nvme_disk ? nvme_write(volumes[index].start + relative, buf) :
                       transfer(volumes[index].start + relative, (void *)buf, true);
}
static bool parse_header_size(const u8 *header, u64 capacity, struct store_layout *out) {
    u32 fields[5], crc_stored;
    u64 total;
    memcpy(fields, header + 8, sizeof(fields));
    memcpy(&crc_stored, header + 40, sizeof(crc_stored));
    u32 version = fields[0];
    memset(out, 0, sizeof(*out));
    out->version = version;
    if (version == 2 || version == 3) {
        /* NVSTORE2: sector size, slot0 LBA, slot1 LBA, slot sectors (u32),
         * then a u64 total sector count and the CRC over the first 40 bytes. */
        if (memcmp(header, version == 3 ? "NVSTORE3" : "NVSTORE2", 8) || crc_stored != crc32(header, 40))
            return false;
        memcpy(&total, header + 32, sizeof(total));
        if (fields[1] != 512 || fields[2] != 8 || fields[4] < 8 ||
            fields[4] > SNAP_CAP_MAX / 512 + 1 ||
            fields[3] != (u64)fields[2] + fields[4] ||
            total > capacity || total < (u64)fields[3] + fields[4])
            return false;
        out->slot_lba[0] = fields[2];
        out->slot_lba[1] = fields[3];
        out->slot_sectors = fields[4];
    } else if (version == 1) {
        /* NVSTORE1: the historical fixed 8 MiB geometry, CRC over 28 bytes
         * stored immediately after the five u32 fields. */
        u32 nv1_crc;
        memcpy(&nv1_crc, header + 28, sizeof(nv1_crc));
        if (memcmp(header, "NVSTORE1", 8) || nv1_crc != crc32(header, 28) ||
            fields[1] != 512 || fields[2] != 8 ||
            fields[3] != 4096 || fields[4] != 2049 || capacity < 4096u + 2049u)
            return false;
        out->slot_lba[0] = 8;
        out->slot_lba[1] = 4096;
        out->slot_sectors = 2049;
    } else
        return false;
    out->snap_cap = MIN(SNAP_CAP_MAX, out->slot_sectors * 512 - 512);
    if (version == 3) {
        out->data_first = ((u64)out->slot_lba[1] + out->slot_sectors + 7) / 8;
        out->data_end = total / 8;
        if (out->data_first >= out->data_end) return false;
    }
    return out->snap_cap >= 512;
}
static bool parse_header(const u8 *header) {
    return parse_header_size(header, sectors, &layout);
}
static bool scan_gpt_at(u64 header_lba) {
    u8 h[512];
    if (disk_read(header_lba, h) < 0 || memcmp(h, "EFI PART", 8)) return false;
    u32 size = le32(h + 12), saved = le32(h + 16);
    if (le32(h + 8) != 0x00010000 || size < 92 || size > 512 ||
        le64(h + 24) != header_lba || le64(h + 32) >= sectors ||
        le64(h + 40) < 34 || le64(h + 48) >= sectors - 33 ||
        le64(h + 40) > le64(h + 48) || le32(h + 80) != 128 ||
        le32(h + 84) != 128) return false;
    memset(h + 16, 0, 4);
    if (crc32(h, size) != saved) return false;
    u64 entries_lba = le64(h + 72);
    if (entries_lba < 2 || entries_lba > sectors - 32) return false;
    for (u32 i = 0; i < 32; ++i)
        if (disk_read(entries_lba + i, gpt_entries + 512 * i) < 0) return false;
    if (crc32(gpt_entries, sizeof(gpt_entries)) != le32(h + 88)) return false;
    /* Validate the whole table before mounting any entry. Overlap or a GPT
     * metadata collision makes this table ineligible; never write through it. */
    for (u32 i = 0; i < 128; ++i) {
        const u8 *a = gpt_entries + i * 128;
        bool used = false;
        for (u32 b = 0; b < 16; ++b) used |= a[b] != 0;
        if (!used) continue;
        u64 start = le64(a + 32), end = le64(a + 40);
        if (start < le64(h + 40) || end > le64(h + 48) || start > end) return false;
        for (u32 j = 0; j < i; ++j) {
            const u8 *other = gpt_entries + j * 128;
            bool occupied = false;
            for (u32 b = 0; b < 16; ++b) occupied |= other[b] != 0;
            if (occupied && start <= le64(other + 40) && le64(other + 32) <= end)
                return false;
        }
    }
    u8 sector[512];
    for (u32 i = 0; i < 128; ++i) {
        const u8 *e = gpt_entries + i * 128;
        bool used = false;
        for (u32 b = 0; b < 16; ++b) used |= e[b] != 0;
        if (!used) continue;
        u64 start = le64(e + 32), length = le64(e + 40) - start + 1;
        struct nv_partition_info *info = NULL;
        if (partition_count < NV_PARTITION_MAX) {
            info = &partitions[partition_count++];
            *info = (struct nv_partition_info){
                .number = i + 1, .flags = memcmp(e, nuvora_type, 16) ? 0 : NV_PART_NUVORA,
                .start_low = (u32)start, .start_high = (u32)(start >> 32),
                .sectors_low = (u32)length, .sectors_high = (u32)(length >> 32)};
        }
        if (memcmp(e, nuvora_type, 16) || volume_count >= NV_VOLUME_MAX) continue;
        struct store_layout candidate;
        if (disk_read(start, sector) < 0 ||
            !parse_header_size(sector, length, &candidate)) continue;
        if (info) { info->flags |= NV_PART_MOUNTED; info->letter = 'C' + volume_count; }
        volume_partition_number[volume_count] = i + 1;
        volumes[volume_count].start = start;
        volumes[volume_count].length = length;
        volumes[volume_count++].geometry = candidate;
    }
    return true;
}
static bool ata_identify(void) {
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
    return true;
}
static bool scan_storage(void) {
    volume_count = partition_count = 0;
    owned = false;
    u8 header[512];
    if (disk_read(0, header) < 0)
        return false;
    if ((!memcmp(header, "NVSTORE3", 8) || !memcmp(header, "NVSTORE2", 8) || !memcmp(header, "NVSTORE1", 8)) &&
        parse_header(header)) {
        volumes[0].start = 0;
        volumes[0].length = sectors;
        volumes[0].geometry = layout;
        volume_count = 1;
        volume_partition_number[0] = 1;
        partition_count = 1;
        partitions[0] = (struct nv_partition_info){
            .number = 1, .flags = NV_PART_NUVORA | NV_PART_MOUNTED | NV_PART_LEGACY,
            .letter = 'C', .sectors_low = (u32)sectors, .sectors_high = (u32)(sectors >> 32)};
    } else if (header[510] == 0x55 && header[511] == 0xaa && header[450] == 0xee &&
               (scan_gpt_at(1) || scan_gpt_at(sectors - 1))) {
        /* The backup header is read-only recovery for a damaged primary. */
    }
    owned = volume_count != 0;
    if (owned) layout = volumes[0].geometry;
    return owned;
}
bool disk_init(void) {
    nvme_disk = false;
    if (ata_identify() && scan_storage()) return true;
    /* Only a disk with a validated Nuvora layout is selected. A foreign IDE
     * disk must not prevent discovery of a valid NVMe data namespace. */
    if (nvme_init(&sectors)) {
        nvme_disk = true;
        identified = true;
        if (scan_storage()) return true;
        /* Leave an unrecognized or foreign namespace untouched and stop its
         * controller before giving the queued DMA pages back to RAM. */
        nvme_shutdown();
    }
    owned = false;
    return false;
}
