#include "kernel.h"
struct snapshot_header {
    char magic[8];
    u32 generation, length, checksum, header_checksum;
    u8 reserved[488];
} PACKED;
_Static_assert(sizeof(struct snapshot_header) == 512, "snapshot sector");
/* One shared buffer for export and restore, allocated as a physically
 * contiguous run when a data disk is present and sized to the slot, a share
 * of free RAM and SNAP_CAP_MAX. The kernel is single-core and
 * non-preemptible, and restore only ever runs from store_init before any
 * syscall exists, so the two can never overlap. */
static u8 *snap_buffer;
static struct store_layout disk_layout;
static u32 snap_cap;
static int active_slot = -1;
static u32 generation;
static bool restore_blocked;
static u32 slot_lba(int slot) {
    return disk_layout.slot_lba[slot];
}
u32 store_generation(void) {
    return generation;
}
static bool valid_header(struct snapshot_header *h) {
    if (memcmp(h->magic, "NVSS0001", 8) || h->length < 4 || h->length > disk_layout.snap_cap)
        return false;
    u32 checksum = h->header_checksum;
    h->header_checksum = 0;
    u32 actual = crc32(h, sizeof(*h));
    h->header_checksum = checksum;
    return checksum == actual;
}
static int restore(int slot, const struct snapshot_header *h) {
    if (h->length > snap_cap)
        return -NV_ENOMEM;
    u32 size = ALIGN_UP(h->length, 512);
    int result = 0;
    for (u32 off = 0; off < size; off += 512) {
        result = disk_read(slot_lba(slot) + 1 + off / 512, snap_buffer + off);
        if (result < 0)
            break;
    }
    if (result == 0 && crc32(snap_buffer, h->length) != h->checksum)
        result = -NV_EIO;
    if (result == 0)
        result = fs_import_home(snap_buffer, h->length);
    return result;
}
void store_init(void) {
    if (!disk_ready() || !disk_store_layout(&disk_layout)) {
        kprintf("[store] no Nuvora data disk; /home is volatile\n");
        return;
    }
    u32 want = MIN(SNAP_CAP_MAX / PAGE, disk_layout.snap_cap / PAGE);
    want = MIN(want, pages_free() / 4);
    while (want >= 16) {
        snap_buffer = phys_ptr(page_alloc_run(want));
        if (snap_buffer)
            break;
        want /= 2;
    }
    if (snap_buffer)
        snap_cap = want * PAGE;
    else {
        /* Tiny or badly fragmented RAM: fall back to a small heap buffer. */
        u32 fallback = MIN(64 * 1024u, disk_layout.snap_cap);
        snap_buffer = kmalloc(fallback);
        snap_cap = snap_buffer ? fallback : 0;
    }
    if (!snap_buffer) {
        kprintf("[store] no snapshot buffer available; /home is volatile\n");
        return;
    }
    struct snapshot_header h[2];
    bool valid[2] = {false, false};
    for (int i = 0; i < 2; ++i)
        valid[i] = disk_read(slot_lba(i), &h[i]) == 0 && valid_header(&h[i]);
    int first = valid[1] && (!valid[0] || (i32)(h[1].generation - h[0].generation) > 0) ? 1 : 0;
    for (int k = 0; k < 2; ++k) {
        int i = (first + k) % 2;
        if (!valid[i])
            continue;
        int restored = restore(i, &h[i]);
        if (restored == 0) {
            active_slot = i;
            generation = h[i].generation;
            kprintf("[store] restored /home generation %u (%u KiB slot)\n", generation,
                    disk_layout.slot_sectors / 2);
            return;
        }
        if (restored == -NV_ENOMEM || restored == -NV_ENOSPC) {
            restore_blocked = true;
            kprintf("[store] snapshot needs more memory or file capacity; saves disabled\n");
            return;
        }
        kprintf("[store] rejected invalid snapshot in slot %u\n", (u32)i);
    }
    kprintf("[store] empty data disk; use anchor to save /home\n");
}
int store_sync(void) {
    if (restore_blocked)
        return -NV_ENOMEM;
    if (!disk_ready() || !snap_cap)
        return -NV_ENODEV;
    u32 len;
    int r = fs_export_home(snap_buffer, snap_cap, &len);
    if (r < 0)
        return r;
    memset(snap_buffer + len, 0, ALIGN_UP(len, 512u) - len);
    int slot = active_slot == 0 ? 1 : 0;
    struct snapshot_header h;
    memset(&h, 0, sizeof(h));
    /* Invalidate only the inactive slot. The previous committed slot is retained. */
    r = disk_write(slot_lba(slot), &h);
    if (r == 0)
        r = disk_flush();
    for (u32 off = 0; r == 0 && off < ALIGN_UP(len, 512); off += 512)
        r = disk_write(slot_lba(slot) + 1 + off / 512, snap_buffer + off);
    if (r == 0)
        r = disk_flush();
    if (r == 0) {
        memcpy(h.magic, "NVSS0001", 8);
        h.generation = generation + 1;
        h.length = len;
        h.checksum = crc32(snap_buffer, len);
        h.header_checksum = crc32(&h, sizeof(h));
        r = disk_write(slot_lba(slot), &h);
    }
    if (r == 0)
        r = disk_flush();
    if (r == 0) {
        active_slot = slot;
        generation = h.generation;
    }
    return r;
}
