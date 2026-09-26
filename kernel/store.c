#include "kernel.h"
struct snapshot_header {
    char magic[8];
    u32 generation, length, checksum, header_checksum;
    u8 reserved[488];
} PACKED;
_Static_assert(sizeof(struct snapshot_header) == 512, "snapshot sector");
/* NVSS0001 stays byte compatible; payloads now travel one sector at a time. */
static u32 count;
static struct volume_state {
    struct store_layout layout;
    int active_slot;
    u32 generation;
    int restore_error;
} volume[NV_VOLUME_MAX];
u32 store_generation(void) { return volume[0].generation; }
u32 store_volume_generation(u32 index) {
    return index < count ? volume[index].generation : 0;
}
static u32 slot_lba(u32 index, int slot) {
    return volume[index].layout.slot_lba[slot];
}
static u32 crc_more(u32 state, const void *bytes, u32 length) {
    const u8 *p = bytes;
    while (length--) {
        state ^= *p++;
        for (u32 j = 0; j < 8; ++j)
            state = (state >> 1) ^ (0xedb88320u & (0u - (state & 1u)));
    }
    return state;
}
static bool valid_header(const struct store_layout *l, struct snapshot_header *h) {
    if (memcmp(h->magic, l->version == 3 ? "NVSS0002" : "NVSS0001", 8) || h->length < 4 || h->length > l->snap_cap)
        return false;
    u32 checksum = h->header_checksum;
    h->header_checksum = 0;
    u32 actual = crc32(h, sizeof(*h));
    h->header_checksum = checksum;
    return checksum == actual;
}
int store_read_bytes(u32 index, int slot, u32 offset, void *out, u32 length) {
    if (index >= count || slot < 0 || slot > 1 ||
        offset > volume[index].layout.snap_cap ||
        length > volume[index].layout.snap_cap - offset) return -NV_EIO;
    u8 sector[512], *dest = out;
    while (length) {
        u32 skip = offset % 512, part = MIN(length, 512 - skip);
        int r = disk_volume_read(index, slot_lba(index, slot) + 1 + offset / 512, sector);
        if (r < 0) return r;
        memcpy(dest, sector + skip, part);
        dest += part; offset += part; length -= part;
    }
    return 0;
}
static int restore(u32 index, int slot, const struct snapshot_header *h) {
    u8 sector[512];
    u32 checksum = 0xffffffffu;
    for (u32 off = 0; off < h->length; off += 512) {
        int r = disk_volume_read(index, slot_lba(index, slot) + 1 + off / 512, sector);
        if (r < 0) return r;
        checksum = crc_more(checksum, sector, MIN(512u, h->length - off));
    }
    if (~checksum != h->checksum) return -NV_EIO;
    return volume[index].layout.version == 3 ?
        fs_extent_import(index, slot, h->length, false) : fs_import_stream(index, slot, h->length);
}
void store_init(void) {
    count = disk_volume_count();
    if (!disk_ready() || !count) {
        kprintf("[store] no Nuvora data disk; /home is volatile\n");
        return;
    }
    for (u32 index = 0; index < count; ++index) {
        struct volume_state *v = &volume[index];
        if (!disk_volume_layout(index, &v->layout)) panic("data partition geometry missing");
        fs_extent_enable(index, v->layout.data_first, v->layout.data_end);
        v->active_slot = -1;
        v->generation = 0;
        v->restore_error = 0;
        struct snapshot_header h[2];
        bool valid[2] = {false, false};
        bool blank = true;
        for (int i = 0; i < 2; ++i) {
            memset(&h[i], 0, sizeof(h[i]));
            int read = disk_volume_read(index, slot_lba(index, i), &h[i]);
            valid[i] = !read && valid_header(&v->layout, &h[i]);
            if (read) blank = false;
            for (u32 b = 0; b < sizeof(h[i]); ++b)
                if (((const u8 *)&h[i])[b]) { blank = false; break; }
        }
        if (v->layout.version == 3) {
            for (int i = 0; i < 2; ++i) if (valid[i]) {
                int r = restore(index, i, &h[i]);
                if (r < 0) valid[i] = false;
                if (r == -NV_ENOMEM || r == -NV_ENOSPC) v->restore_error = r;
            }
            if (v->restore_error) continue;
        }
        int first = valid[1] && (!valid[0] || (i32)(h[1].generation - h[0].generation) > 0) ? 1 : 0;
        for (int k = 0; k < 2; ++k) {
            int i = (first + k) % 2;
            if (!valid[i]) continue;
            int result = v->layout.version == 3 ?
                fs_extent_import(index, i, h[i].length, true) : restore(index, i, &h[i]);
            if (!result) {
                v->active_slot = i;
                v->generation = h[i].generation;
                if (!index) kprintf("[store] restored /home generation %u\n", v->generation);
                else kprintf("[store] %c: restored generation %u\n", 'C' + index, v->generation);
                break;
            }
            if (result == -NV_ENOMEM || result == -NV_ENOSPC) {
                v->restore_error = result;
                kprintf("[store] %c: restore needs RAM or nodes; writes disabled\n", 'C' + index);
                break;
            }
            kprintf("[store] %c: rejected invalid snapshot in slot %u\n", 'C' + index, (u32)i);
        }
        if (v->active_slot < 0 && !blank && !v->restore_error) {
            v->restore_error = -NV_EIO;
            kprintf("[store] %c: no valid snapshot; writes disabled\n", 'C' + index);
        }
    }
}
struct writer {
    u32 index, lba, sectors, length, checksum, fill;
    u8 sector[512];
};
static int write_payload(void *context, const void *data, u32 length) {
    struct writer *w = context;
    const u8 *p = data;
    if (w->length > volume[w->index].layout.snap_cap ||
        length > volume[w->index].layout.snap_cap - w->length) return -NV_ENOSPC;
    w->checksum = crc_more(w->checksum, p, length);
    w->length += length;
    while (length) {
        u32 part = MIN(length, 512 - w->fill);
        memcpy(w->sector + w->fill, p, part);
        w->fill += part; p += part; length -= part;
        if (w->fill == 512) {
            int r = disk_volume_write(w->index, w->lba + 1 + w->sectors, w->sector);
            if (r < 0) return r;
            ++w->sectors; w->fill = 0;
        }
    }
    return 0;
}
static int sync_one(u32 index) {
    struct volume_state *v = &volume[index];
    if (v->restore_error) return v->restore_error;
    int slot = v->active_slot == 0 ? 1 : 0;
    struct writer w = {.index = index, .lba = slot_lba(index, slot),
                       .checksum = 0xffffffffu};
    u32 length, offsets[FS_NODES];
    bool ext = v->layout.version == 3;
    if (ext) { int p = fs_extent_prepare(index); if (p < 0) return p; }
    struct snapshot_header h;
    memset(&h, 0, sizeof(h));
    int r = disk_volume_write(index, w.lba, &h);
    if (!r) r = disk_flush();
    if (r < 0) goto aborted;
    r = ext ? fs_extent_export(index, write_payload, &w, &length) :
        fs_export_stream(index, v->layout.snap_cap, write_payload, &w, &length, offsets);
    if (r < 0) goto aborted;
    if (w.fill) {
        memset(w.sector + w.fill, 0, 512 - w.fill);
        r = disk_volume_write(index, w.lba + 1 + w.sectors, w.sector);
        if (r < 0) goto aborted;
    }
    r = disk_flush();
    if (r < 0) goto aborted;
    memcpy(h.magic, ext ? "NVSS0002" : "NVSS0001", 8);
    h.generation = v->generation + 1;
    h.length = length;
    h.checksum = ~w.checksum;
    h.header_checksum = crc32(&h, sizeof(h));
    r = disk_volume_write(index, w.lba, &h);
    if (!r) r = disk_flush();
    if (!r) {
        v->active_slot = slot;
        v->generation = h.generation;
        if (ext) fs_extent_finish(index, slot, true);
        else fs_rebase_volume(index, slot, offsets);
    }
    /* A failed final write/flush is ambiguous: do not recycle blocks until a
     * remount has read which root reached stable storage. */
    if (r < 0 && ext) v->restore_error = r;
aborted:
    if (ext && r < 0) fs_extent_finish(index, slot, false);
    return r;
}
int store_write_error(u32 index) {
    return index < count ? volume[index].restore_error : -NV_ENODEV;
}
int store_sync(void) {
    if (!disk_ready() || !count) return -NV_ENODEV;
    for (u32 i = 0; i < count; ++i)
        if (volume[i].restore_error) return volume[i].restore_error;
    for (u32 i = 0; i < count; ++i) {
        int result = sync_one(i);
        if (result < 0) return result;
    }
    return 0;
}
