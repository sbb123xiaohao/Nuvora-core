#include "kernel.h"
struct snapshot_header {
    char magic[8];
    u32 generation, length, checksum, header_checksum;
    u8 reserved[488];
} PACKED;
_Static_assert(sizeof(struct snapshot_header) == 512, "snapshot sector");
/* One bounded buffer serves partitions sequentially; each has its own two
 * committed snapshot slots and generation. */
static u8 *snap_buffer;
static u32 snap_cap, count;
static struct volume_state {
    struct store_layout layout;
    int active_slot;
    u32 generation;
    bool restore_blocked;
} volume[NV_VOLUME_MAX];
u32 store_generation(void) { return volume[0].generation; }
u32 store_volume_generation(u32 index) {
    return index < count ? volume[index].generation : 0;
}
static u32 slot_lba(u32 index, int slot) {
    return volume[index].layout.slot_lba[slot];
}
static bool valid_header(const struct store_layout *l, struct snapshot_header *h) {
    if (memcmp(h->magic, "NVSS0001", 8) || h->length < 4 || h->length > l->snap_cap)
        return false;
    u32 checksum = h->header_checksum;
    h->header_checksum = 0;
    u32 actual = crc32(h, sizeof(*h));
    h->header_checksum = checksum;
    return checksum == actual;
}
static int restore(u32 index, int slot, const struct snapshot_header *h) {
    if (h->length > snap_cap) return -NV_ENOMEM;
    u32 size = ALIGN_UP(h->length, 512);
    int result = 0;
    for (u32 off = 0; off < size; off += 512) {
        result = disk_volume_read(index, slot_lba(index, slot) + 1 + off / 512, snap_buffer + off);
        if (result < 0) break;
    }
    if (!result && crc32(snap_buffer, h->length) != h->checksum) result = -NV_EIO;
    if (!result) result = fs_import_volume(index, snap_buffer, h->length);
    return result;
}
void store_init(void) {
    count = disk_volume_count();
    if (!disk_ready() || !count) {
        kprintf("[store] no Nuvora data disk; /home is volatile\n");
        return;
    }
    u32 largest = 0;
    for (u32 i = 0; i < count; ++i) {
        if (!disk_volume_layout(i, &volume[i].layout)) panic("data partition geometry missing");
        volume[i].active_slot = -1;
        volume[i].generation = 0;
        volume[i].restore_blocked = false;
        largest = MAX(largest, volume[i].layout.snap_cap);
    }
    u32 want = MIN(SNAP_CAP_MAX / PAGE, largest / PAGE);
    want = MIN(want, pages_free() / 4);
    while (want >= 16) {
        snap_buffer = phys_ptr(page_alloc_run(want));
        if (snap_buffer) break;
        want /= 2;
    }
    if (snap_buffer) snap_cap = want * PAGE;
    else {
        u32 fallback = MIN(64 * 1024u, largest);
        snap_buffer = kmalloc(fallback);
        snap_cap = snap_buffer ? fallback : 0;
    }
    if (!snap_buffer) {
        kprintf("[store] no snapshot buffer available; volumes are volatile\n");
        return;
    }
    for (u32 index = 0; index < count; ++index) {
        struct volume_state *v = &volume[index];
        struct snapshot_header h[2];
        bool valid[2] = {false, false};
        for (int i = 0; i < 2; ++i)
            valid[i] = disk_volume_read(index, slot_lba(index, i), &h[i]) == 0 &&
                       valid_header(&v->layout, &h[i]);
        int first = valid[1] && (!valid[0] || (i32)(h[1].generation - h[0].generation) > 0) ? 1 : 0;
        for (int k = 0; k < 2; ++k) {
            int i = (first + k) % 2;
            if (!valid[i]) continue;
            int result = restore(index, i, &h[i]);
            if (!result) {
                v->active_slot = i;
                v->generation = h[i].generation;
                if (!index) kprintf("[store] restored /home generation %u\n", v->generation);
                else kprintf("[store] %c: restored generation %u\n", 'C' + index, v->generation);
                break;
            }
            if (result == -NV_ENOMEM || result == -NV_ENOSPC) {
                v->restore_blocked = true;
                kprintf("[store] %c: restore needs RAM or nodes; writes disabled\n", 'C' + index);
                break;
            }
            kprintf("[store] %c: rejected invalid snapshot in slot %u\n", 'C' + index, (u32)i);
        }
    }
}
static int sync_one(u32 index) {
    struct volume_state *v = &volume[index];
    if (v->restore_blocked) return -NV_ENOMEM;
    u32 cap = MIN(snap_cap, v->layout.snap_cap), len;
    int r = fs_export_volume(index, snap_buffer, cap, &len);
    if (r < 0) return r;
    memset(snap_buffer + len, 0, ALIGN_UP(len, 512u) - len);
    int slot = v->active_slot == 0 ? 1 : 0;
    struct snapshot_header h;
    memset(&h, 0, sizeof(h));
    r = disk_volume_write(index, slot_lba(index, slot), &h);
    if (!r) r = disk_flush();
    for (u32 off = 0; !r && off < ALIGN_UP(len, 512); off += 512)
        r = disk_volume_write(index, slot_lba(index, slot) + 1 + off / 512, snap_buffer + off);
    if (!r) r = disk_flush();
    if (!r) {
        memcpy(h.magic, "NVSS0001", 8);
        h.generation = v->generation + 1;
        h.length = len;
        h.checksum = crc32(snap_buffer, len);
        h.header_checksum = crc32(&h, sizeof(h));
        r = disk_volume_write(index, slot_lba(index, slot), &h);
    }
    if (!r) r = disk_flush();
    if (!r) {
        v->active_slot = slot;
        v->generation = h.generation;
    }
    return r;
}
int store_sync(void) {
    if (!disk_ready() || !snap_cap) return -NV_ENODEV;
    for (u32 i = 0; i < count; ++i)
        if (volume[i].restore_blocked) return -NV_ENOMEM;
    for (u32 i = 0; i < count; ++i) {
        int result = sync_one(i);
        if (result < 0) return result;
    }
    return 0;
}
