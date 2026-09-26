/* NVSTORE3: sparse 64-bit file extents. Metadata is checkpointed separately
 * from file bytes; writes allocate new blocks until both durable roots release
 * the old ones. Kernel calls are serialized, including the disk write path. */
static struct {
    u64 first, end, hint;
    struct file_extent *pins[2], *pending;
} extent_vol[NV_VOLUME_MAX];

static void extent_free(struct file_extent *e) {
    while (e) { struct file_extent *next = e->next; kfree(e); e = next; }
}
static int extent_add(struct file_extent **head, struct file_extent **tail,
                      u64 logical, u64 physical, u64 blocks) {
    if (!blocks) return 0;
    if (*tail && (*tail)->logical + (*tail)->blocks == logical &&
        (*tail)->physical + (*tail)->blocks == physical) {
        (*tail)->blocks += blocks; return 0;
    }
    struct file_extent *e = kmalloc(sizeof(*e));
    if (!e) return -NV_ENOMEM;
    *e = (struct file_extent){logical, physical, blocks, NULL};
    if (*tail) (*tail)->next = e; else *head = e;
    *tail = e; return 0;
}
void fs_extent_enable(u32 v, u64 first, u64 end) {
    extent_free(extent_vol[v].pins[0]); extent_free(extent_vol[v].pins[1]);
    extent_free(extent_vol[v].pending);
    memset(&extent_vol[v], 0, sizeof(extent_vol[v]));
    extent_vol[v].first = extent_vol[v].hint = first;
    extent_vol[v].end = end; extent_enabled[v] = first < end;
}
static int extent_read(const struct node *n, u64 offset, void *out, u32 length) {
    u8 *dest = out, sector[512];
    while (length) {
        u64 logical = offset / PAGE;
        const struct file_extent *e = n->extents;
        while (e && e->logical + e->blocks <= logical) e = e->next;
        u32 skip = offset % 512, part = MIN(length, 512 - skip);
        if (!e || e->logical > logical) memset(dest, 0, part);
        else {
            u64 lba = (e->physical + logical - e->logical) * 8 + offset % PAGE / 512;
            int r = disk_volume_read(n->backing_volume, lba, sector);
            if (r < 0) return r;
            memcpy(dest, sector + skip, part);
        }
        dest += part; offset += part; length -= part;
    }
    return 0;
}
static u64 extent_skip(const struct file_extent *e, u64 start, u64 blocks) {
    u64 next = start;
    for (; e; e = e->next)
        if (start < e->physical + e->blocks && e->physical < start + blocks)
            next = MAX(next, e->physical + e->blocks);
    return next;
}
static int extent_allocate(u32 v, u64 blocks, u64 *result) {
    u64 begin = extent_vol[v].hint;
    for (u32 pass = 0; pass < 2; ++pass) {
        u64 at = pass ? extent_vol[v].first : begin;
        u64 limit = pass ? begin : extent_vol[v].end;
        while (at <= limit && blocks <= limit - at) {
            u64 next = at;
            for (int s = 0; s < 2; ++s)
                next = MAX(next, extent_skip(extent_vol[v].pins[s], at, blocks));
            for (int i = 1; i < FS_NODES; ++i)
                if (nodes[i].kind == NV_FILE && nodes[i].backing_volume == v)
                    next = MAX(next, extent_skip(nodes[i].extents, at, blocks));
            if (next == at) {
                *result = at; extent_vol[v].hint = at + blocks; return 0;
            }
            at = next;
        }
    }
    return -NV_ENOSPC;
}
static int extent_write(struct node *n, u32 v, u64 offset, const void *input, u32 length) {
    int r = store_write_error(v);
    if (r < 0) return r;
    u64 first = offset / PAGE, end = (offset + length + PAGE - 1) / PAGE, physical;
    r = extent_allocate(v, end - first, &physical);
    if (r < 0) return r;
    struct file_extent *head = NULL, *tail = NULL;
    /* Build a replacement map first. No allocation failure can partially edit
     * a file, and failed I/O leaves all previously visible bytes untouched. */
    for (struct file_extent *e = n->extents; e; e = e->next) {
        if (e->logical >= first) break;
        r = extent_add(&head, &tail, e->logical, e->physical,
                       MIN(e->blocks, first - e->logical));
        if (r < 0) goto fail;
    }
    r = extent_add(&head, &tail, first, physical, end - first);
    if (r < 0) goto fail;
    for (struct file_extent *e = n->extents; e; e = e->next) {
        if (e->logical + e->blocks <= end) continue;
        u64 logical = MAX(e->logical, end), skip = logical - e->logical;
        r = extent_add(&head, &tail, logical, e->physical + skip, e->blocks - skip);
        if (r < 0) goto fail;
    }
    u8 block[PAGE];
    for (u64 i = first; i < end; ++i) {
        u64 base = i * PAGE;
        memset(block, 0, sizeof(block));
        u64 lo = MAX(base, offset), hi = MIN(base + PAGE, offset + length);
        if ((lo != base || hi != base + PAGE) && base < n->size) {
            r = extent_read(n, base, block, (u32)MIN(PAGE, n->size - base));
            if (r < 0) goto fail;
        }
        memcpy(block + lo - base, (const u8 *)input + lo - offset, (usize)(hi - lo));
        for (u32 s = 0; s < 8; ++s) {
            r = disk_volume_write(v, (physical + i - first) * 8 + s, block + s * 512);
            if (r < 0) goto fail;
        }
    }
    extent_free(n->extents); n->extents = head; n->backing_volume = v;
    return (int)length;
fail:
    extent_free(head); return r;
}
int fs_extent_prepare(u32 v) {
    extent_free(extent_vol[v].pending); extent_vol[v].pending = NULL;
    struct file_extent *tail = NULL;
    for (int i = 1; i < FS_NODES; ++i)
        if (nodes[i].kind == NV_FILE && volume_for(i) == (int)v)
            for (struct file_extent *e = nodes[i].extents; e; e = e->next) {
                int r = extent_add(&extent_vol[v].pending, &tail, e->logical, e->physical, e->blocks);
                if (r < 0) { fs_extent_finish(v, 0, false); return r; }
            }
    return 0;
}
void fs_extent_finish(u32 v, int slot, bool committed) {
    if (committed) {
        extent_free(extent_vol[v].pins[slot]);
        extent_vol[v].pins[slot] = extent_vol[v].pending;
    } else extent_free(extent_vol[v].pending);
    extent_vol[v].pending = NULL;
}
struct extent_entry_header { u32 kind, pathlen; u64 size; u32 extents, reserved; };
_Static_assert(sizeof(struct extent_entry_header) == 24, "extent entry layout");
int fs_extent_export(u32 v, int (*write)(void *, const void *, u32), void *ctx, u32 *length) {
    u32 count = 0, depthmax = 0;
    int root = volume_nodes[v];
    for (int i = 1; i < FS_NODES; ++i)
        if (i != root && nodes[i].kind && descendant(i, root)) {
            ++count; u32 depth = 0;
            for (int p = i; p != root; p = nodes[p].parent) ++depth;
            depthmax = MAX(depthmax, depth);
        }
    int r = write(ctx, &count, 4); if (r < 0) return r;
    *length = 4;
    for (u32 d = 1; d <= depthmax; ++d)
        for (int i = 1; i < FS_NODES; ++i) {
            if (i == root || !nodes[i].kind || !descendant(i, root)) continue;
            u32 depth = 0;
            for (int p = i; p != root; p = nodes[p].parent) ++depth;
            if (depth != d) continue;
            char path[NV_PATH_MAX];
            r = fs_path(i, path, sizeof(path)); if (r < 0) return r;
            struct extent_entry_header h = {.kind = nodes[i].kind,
                .pathlen = (u32)strlen(path), .size = nodes[i].size};
            for (struct file_extent *e = nodes[i].extents; e; e = e->next) ++h.extents;
            if ((r = write(ctx, &h, sizeof(h))) < 0 ||
                (r = write(ctx, path, h.pathlen)) < 0) return r;
            *length += sizeof(h) + h.pathlen;
            for (struct file_extent *e = nodes[i].extents; e; e = e->next) {
                u64 fields[3] = {e->logical, e->physical, e->blocks};
                if ((r = write(ctx, fields, sizeof(fields))) < 0) return r;
                *length += sizeof(fields);
            }
        }
    return 0;
}
struct extent_import_entry {
    struct extent_entry_header h;
    char path[NV_PATH_MAX];
    struct file_extent *map;
};
static bool extent_valid_path(u32 v, const char *path, u32 n) {
    char prefix[NV_PATH_MAX];
    if (fs_path(volume_nodes[v], prefix, sizeof(prefix)) < 0) return false;
    u32 len = (u32)strlen(prefix);
    if (n <= len + 1 || strnlen(path, n) != n || strncmp(path, prefix, len) ||
        path[len] != '/' || path[n - 1] == '/') return false;
    for (u32 k = len + 1; k < n;) {
        u32 first = k;
        while (k < n && path[k] != '/') ++k;
        u32 part = k - first;
        if (!part || part > NV_NAME_MAX || (part == 1 && path[first] == '.') ||
            (part == 2 && path[first] == '.' && path[first + 1] == '.')) return false;
        ++k;
    }
    return true;
}
int fs_extent_import(u32 v, int slot, u32 length, bool activate) {
    u32 count = 0, pos = 4;
    int r = store_read_bytes(v, slot, 0, &count, 4);
    if (r < 0) return r;
    if (count >= FS_NODES || length < 4) return -NV_EIO;
    struct extent_import_entry *items = kmalloc(MAX(1u, count) * sizeof(*items));
    if (!items) return -NV_ENOMEM;
    memset(items, 0, MAX(1u, count) * sizeof(*items));
    r = -NV_EIO;
    for (u32 i = 0; i < count; ++i) {
        struct extent_import_entry *item = &items[i];
        struct extent_entry_header *h = &item->h;
        if (pos > length || length - pos < sizeof(*h)) goto done;
        if (store_read_bytes(v, slot, pos, h, sizeof(*h)) < 0) goto done;
        pos += sizeof(*h);
        if ((h->kind != NV_DIR && h->kind != NV_FILE) || h->reserved ||
            h->size > NV_FILE_MAX64 || (h->kind == NV_DIR && (h->size || h->extents)) ||
            !h->pathlen || h->pathlen >= NV_PATH_MAX || h->pathlen > length - pos) goto done;
        if (store_read_bytes(v, slot, pos, item->path, h->pathlen) < 0) goto done;
        pos += h->pathlen;
        if (!extent_valid_path(v, item->path, h->pathlen) || h->extents > (length - pos) / 24) goto done;
        /* Require unique paths and parents before children, prior to mutation. */
        char parent[NV_PATH_MAX]; strlcpy(parent, item->path, sizeof(parent));
        u32 cut = h->pathlen; while (cut && parent[cut - 1] != '/') --cut;
        parent[cut - 1] = 0;
        char prefix[NV_PATH_MAX]; fs_path(volume_nodes[v], prefix, sizeof(prefix));
        bool found = !strcmp(parent, prefix);
        for (u32 j = 0; j < i; ++j) {
            if (!strcmp(items[j].path, item->path)) goto done;
            if (items[j].h.kind == NV_DIR && !strcmp(items[j].path, parent)) found = true;
        }
        if (!found) goto done;
        struct file_extent *tail = NULL;
        u64 last = 0, fileblocks = (h->size + PAGE - 1) / PAGE;
        for (u32 j = 0; j < h->extents; ++j) {
            u64 e[3];
            if (store_read_bytes(v, slot, pos, e, sizeof(e)) < 0) goto done;
            pos += sizeof(e);
            if (!e[2] || e[0] < last || e[0] > fileblocks || e[2] > fileblocks - e[0] ||
                e[1] < extent_vol[v].first || e[1] >= extent_vol[v].end ||
                e[2] > extent_vol[v].end - e[1]) goto done;
            for (u32 k = 0; k <= i; ++k)
                if (extent_skip(items[k].map, e[1], e[2]) != e[1]) goto done;
            int a = extent_add(&item->map, &tail, e[0], e[1], e[2]);
            if (a < 0) { r = a; goto done; }
            last = e[0] + e[2];
        }
    }
    if (pos != length) goto done;
    if (!activate) {
        struct file_extent *head = NULL, *tail = NULL;
        for (u32 i = 0; i < count; ++i) {
            if (!items[i].map) continue;
            if (tail) tail->next = items[i].map; else head = items[i].map;
            tail = items[i].map; while (tail->next) tail = tail->next;
            items[i].map = NULL;
        }
        extent_free(extent_vol[v].pins[slot]); extent_vol[v].pins[slot] = head;
        r = 0; goto done;
    }
    u32 available = 0;
    for (int i = 1; i < FS_NODES; ++i)
        if (!nodes[i].kind || (i != volume_nodes[v] && descendant(i, volume_nodes[v]))) ++available;
    if (count > available) { r = -NV_ENOSPC; goto done; }
    clear_volume(v);
    for (u32 i = 0; i < count; ++i) {
        int n = create_node(0, items[i].path, items[i].h.kind);
        if (n < 0) { clear_volume(v); r = n; goto done; }
        nodes[n].size = items[i].h.size; nodes[n].backing_volume = v;
        nodes[n].extents = items[i].map; items[i].map = NULL;
    }
    r = 0;
done:
    for (u32 i = 0; i < count; ++i) extent_free(items[i].map);
    kfree(items); return r;
}
