#include "kernel.h"
struct snapshot_header {
    char magic[8];
    u32 generation, length, checksum, header_checksum;
    u8 reserved[488];
} PACKED;
_Static_assert(sizeof(struct snapshot_header) == 512, "snapshot sector");
static const u32 slot_lba[2] = {8, 4096};
static int active_slot = -1;
static u32 generation;
u32 store_generation(void) {
    return generation;
}
static bool valid_header(struct snapshot_header *h) {
    if (memcmp(h->magic, "NVSS0001", 8) || h->length < 4 || h->length > SNAP_CAP)
        return false;
    u32 checksum = h->header_checksum;
    h->header_checksum = 0;
    u32 actual = crc32(h, sizeof(*h));
    h->header_checksum = checksum;
    return checksum == actual;
}
static int restore(int slot, const struct snapshot_header *h) {
    u32 size = ALIGN_UP(h->length, 512);
    u8 *data = kmalloc(size);
    if (!data)
        return -NV_ENOMEM;
    int result = 0;
    for (u32 off = 0; off < size; off += 512) {
        result = disk_read(slot_lba[slot] + 1 + off / 512, data + off);
        if (result < 0)
            break;
    }
    if (result == 0 && crc32(data, h->length) != h->checksum)
        result = -NV_EIO;
    if (result == 0)
        result = fs_import_home(data, h->length);
    kfree(data);
    return result;
}
void store_init(void) {
    if (!disk_ready()) {
        kprintf("[store] no Nuvora data disk; /home is volatile\n");
        return;
    }
    struct snapshot_header h[2];
    bool valid[2] = {false, false};
    for (int i = 0; i < 2; ++i)
        valid[i] = disk_read(slot_lba[i], &h[i]) == 0 && valid_header(&h[i]);
    int first = valid[1] && (!valid[0] || (i32)(h[1].generation - h[0].generation) > 0) ? 1 : 0;
    for (int k = 0; k < 2; ++k) {
        int i = (first + k) % 2;
        if (!valid[i])
            continue;
        if (restore(i, &h[i]) == 0) {
            active_slot = i;
            generation = h[i].generation;
            kprintf("[store] restored /home generation %u\n", generation);
            return;
        }
        kprintf("[store] rejected invalid snapshot in slot %u\n", (u32)i);
    }
    kprintf("[store] empty data disk; use anchor to save /home\n");
}
int store_sync(void) {
    if (!disk_ready())
        return -NV_ENODEV;
    u8 *data = kmalloc(SNAP_CAP);
    if (!data)
        return -NV_ENOMEM;
    u32 len;
    int r = fs_export_home(data, SNAP_CAP, &len);
    if (r < 0) {
        kfree(data);
        return r;
    }
    int slot = active_slot == 0 ? 1 : 0;
    struct snapshot_header h;
    memset(&h, 0, sizeof(h));
    /* Invalidate only the inactive slot. The previous committed slot is retained. */
    r = disk_write(slot_lba[slot], &h);
    if (r == 0)
        r = disk_flush();
    for (u32 off = 0; r == 0 && off < ALIGN_UP(len, 512); off += 512)
        r = disk_write(slot_lba[slot] + 1 + off / 512, data + off);
    if (r == 0)
        r = disk_flush();
    if (r == 0) {
        memcpy(h.magic, "NVSS0001", 8);
        h.generation = generation + 1;
        h.length = len;
        h.checksum = crc32(data, len);
        h.header_checksum = crc32(&h, sizeof(h));
        r = disk_write(slot_lba[slot], &h);
    }
    if (r == 0)
        r = disk_flush();
    if (r == 0) {
        active_slot = slot;
        generation = h.generation;
    }
    kfree(data);
    return r;
}
