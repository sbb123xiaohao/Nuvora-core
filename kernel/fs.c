#include "kernel.h"
struct node {
    u32 kind, size, capacity, refs, device;
    int parent;
    bool locked;
    char name[32];
    u8 *data;
};
static struct node nodes[FS_NODES];
static int home_node;
static int volume_nodes[NV_VOLUME_MAX];
static u32 mounted_volumes;
static int child(int parent, const char *name) {
    for (int i = 1; i < FS_NODES; ++i)
        if (nodes[i].kind && nodes[i].parent == parent && !strcmp(nodes[i].name, name))
            return i;
    return -NV_ENOENT;
}
static int make_node(int parent, const char *name, u32 kind, bool locked) {
    if (strlen(name) > NV_NAME_MAX)
        return -NV_E2BIG;
    for (int i = 1; i < FS_NODES; ++i)
        if (!nodes[i].kind) {
            nodes[i] = (struct node){.kind = kind, .parent = parent, .locked = locked};
            strlcpy(nodes[i].name, name, 32);
            return i;
        }
    return -NV_ENOSPC;
}
int fs_lookup(int cwd, const char *path) {
    if (!path || !*path)
        return -NV_ENOENT;
    if (strlen(path) >= NV_PATH_MAX)
        return -NV_E2BIG;
    int n = *path == '/' ? 0 : cwd;
    const char *p = path;
    /* Drive letters are aliases for independent persistent roots. */
    char drive = path[0] >= 'a' && path[0] <= 'z' ? path[0] - 'a' + 'A' : path[0];
    if (path[1] == ':' && drive >= 'C' && drive < 'C' + (int)NV_VOLUME_MAX) {
        u32 index = (u32)(drive - 'C');
        if (path[2] && path[2] != '/') return -NV_EINVAL;
        if (index >= mounted_volumes) return -NV_ENODEV;
        n = volume_nodes[index];
        p = path + 2;
    }
    while (*p) {
        while (*p == '/')
            ++p;
        if (!*p)
            break;
        if (nodes[n].kind != NV_DIR)
            return -NV_ENOTDIR;
        char name[32];
        u32 k = 0;
        while (*p && *p != '/') {
            if (k == NV_NAME_MAX)
                return -NV_E2BIG;
            name[k++] = *p++;
        }
        name[k] = 0;
        if (!strcmp(name, "."))
            continue;
        if (!strcmp(name, "..")) {
            n = nodes[n].parent;
            continue;
        }
        n = child(n, name);
        if (n < 0)
            return n;
    }
    if (path[strlen(path) - 1] == '/' && nodes[n].kind != NV_DIR)
        return -NV_ENOTDIR;
    return n;
}
static int parent_for(int cwd, const char *path, char *name) {
    usize len = strlen(path);
    if (!len || len >= NV_PATH_MAX)
        return -NV_EINVAL;
    char tmp[NV_PATH_MAX];
    strlcpy(tmp, path, sizeof(tmp));
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = 0;
    u32 pos = len;
    while (pos && tmp[pos - 1] != '/')
        --pos;
    if (len - pos > NV_NAME_MAX)
        return -NV_E2BIG;
    strlcpy(name, tmp + pos, 32);
    if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
        return -NV_EINVAL;
    if (!pos)
        return cwd;
    tmp[pos] = 0;
    return fs_lookup(cwd, tmp);
}
static int create_node(int cwd, const char *path, u32 kind) {
    char name[32];
    int p = parent_for(cwd, path, name);
    if (p < 0)
        return p;
    if (nodes[p].kind != NV_DIR)
        return -NV_ENOTDIR;
    if (nodes[p].locked)
        return -NV_EACCESS;
    char full[NV_PATH_MAX];
    int r = fs_path(p, full, sizeof(full));
    if (r < 0 || strlen(full) + (p ? 1 : 0) + strlen(name) >= NV_PATH_MAX)
        return -NV_E2BIG;
    if (child(p, name) >= 0)
        return -NV_EEXIST;
    return make_node(p, name, kind, false);
}
void fs_init(void) {
    nodes[0] = (struct node){.kind = NV_DIR, .parent = 0, .locked = true};
    make_node(0, "apps", NV_DIR, true);
    home_node = make_node(0, "home", NV_DIR, false);
    volume_nodes[0] = home_node;
    mounted_volumes = 1;
    make_node(0, "tmp", NV_DIR, false);
    int dev = make_node(0, "dev", NV_DIR, true), sys = make_node(0, "sys", NV_DIR, true);
    int n = make_node(dev, "null", NV_DEVICE, true);
    nodes[n].device = 1;
    n = make_node(dev, "zero", NV_DEVICE, true);
    nodes[n].device = 2;
    n = make_node(dev, "console", NV_DEVICE, true);
    nodes[n].device = 3;
    n = make_node(sys, "memory", NV_PROC, true);
    nodes[n].device = 1;
    n = make_node(sys, "tasks", NV_PROC, true);
    nodes[n].device = 2;
    n = make_node(sys, "clock", NV_PROC, true);
    nodes[n].device = 3;
    n = make_node(sys, "version", NV_PROC, true);
    nodes[n].device = 4;
}
void fs_mount_volumes(u32 count) {
    if (count <= 1) return;
    int drives = make_node(0, "drives", NV_DIR, true);
    if (drives < 0) panic("cannot create drive directory");
    for (u32 i = 1; i < MIN(count, NV_VOLUME_MAX); ++i) {
        char letter[2] = {(char)('C' + i), 0};
        int node = make_node(drives, letter, NV_DIR, false);
        if (node < 0) panic("cannot mount data partition");
        volume_nodes[i] = node;
        ++mounted_volumes;
    }
}
int fs_kind(int n) {
    return n >= 0 && n < FS_NODES ? (int)nodes[n].kind : 0;
}
u32 fs_node_count(void) {
    u32 n = 0;
    for (int i = 0; i < FS_NODES; ++i)
        if (nodes[i].kind)
            ++n;
    return n;
}
void fs_unpack(const u8 *blob, usize len) {
    if (len < 12 || memcmp(blob, "NVAR0001", 8))
        panic("invalid embedded archive");
    u32 count;
    memcpy(&count, blob + 8, 4);
    u32 pos = 12;
    if (count > 64)
        panic("archive count");
    for (u32 i = 0; i < count; ++i) {
        u32 namesz, size;
        if (len - pos < 8)
            panic("archive entry truncated");
        memcpy(&namesz, blob + pos, 4);
        memcpy(&size, blob + pos + 4, 4);
        pos += 8;
        if (!namesz || namesz > NV_NAME_MAX || namesz > len - pos || size > len - pos - namesz ||
            size > NV_FILE_MAX)
            panic("archive bounds");
        char name[32];
        memcpy(name, blob + pos, namesz);
        name[namesz] = 0;
        pos += namesz;
        if (strnlen(name, namesz) != namesz || child(1, name) >= 0)
            panic("archive duplicate name");
        for (u32 j = 0; j < namesz; ++j)
            if (name[j] == '/')
                panic("archive file name");
        int n = make_node(1, name, NV_FILE, true);
        if (n < 0)
            panic("archive node allocation");
        nodes[n].data = kmalloc(size ? size : 1);
        if (!nodes[n].data)
            panic("archive data allocation");
        memcpy(nodes[n].data, blob + pos, size);
        nodes[n].size = nodes[n].capacity = size;
        pos += size;
    }
    if (pos != len)
        panic("archive trailing data");
}
int fs_blob(const char *path, const u8 **data, u32 *len) {
    int n = fs_lookup(0, path);
    if (n < 0)
        return n;
    if (nodes[n].kind != NV_FILE)
        return -NV_ENOEXEC;
    *data = nodes[n].data;
    *len = nodes[n].size;
    return 0;
}
int fs_open(struct task *t, const char *path, u32 flags) {
    if ((flags & ~63u) || !(flags & (NV_READ | NV_WRITE)) ||
        ((flags & (NV_CREATE | NV_TRUNC | NV_APPEND)) && !(flags & NV_WRITE)) ||
        ((flags & NV_EXCL) && !(flags & NV_CREATE)))
        return -NV_EINVAL;
    int fd;
    for (fd = 3; fd < NV_OPEN_MAX; ++fd)
        if (t->fd[fd].node < 0)
            break;
    if (fd == NV_OPEN_MAX)
        return -NV_ENOSPC;
    int n = fs_lookup(t->cwd, path);
    if (n >= 0 && (flags & NV_EXCL))
        return -NV_EEXIST;
    if (n == -NV_ENOENT && (flags & NV_CREATE)) {
        if (*path && path[strlen(path) - 1] == '/')
            return -NV_ENOTDIR;
        n = create_node(t->cwd, path, NV_FILE);
    }
    if (n < 0)
        return n;
    if (nodes[n].kind == NV_DIR)
        return -NV_EISDIR;
    if ((flags & NV_WRITE) &&
        (nodes[n].kind == NV_PROC || (nodes[n].locked && nodes[n].kind != NV_DEVICE)))
        return -NV_EACCESS;
    if ((flags & NV_TRUNC) && nodes[n].kind == NV_FILE) {
        kfree(nodes[n].data);
        nodes[n].data = NULL;
        nodes[n].size = nodes[n].capacity = 0;
    }
    ++nodes[n].refs;
    t->fd[fd] = (struct descriptor){
        .node = n, .flags = flags, .offset = (flags & NV_APPEND) ? nodes[n].size : 0};
    return fd;
}
int fs_close(struct task *t, int fd) {
    if (fd < 3 || fd >= NV_OPEN_MAX || t->fd[fd].node < 0)
        return -NV_EBADF;
    --nodes[t->fd[fd].node].refs;
    t->fd[fd].node = -1;
    return 0;
}
static u32 append_text(char *out, u32 pos, const char *s) {
    while (*s && pos < 4095)
        out[pos++] = *s++;
    out[pos] = 0;
    return pos;
}
static u32 append_num(char *out, u32 pos, u32 n) {
    char b[32];
    number(b, n, 10);
    return append_text(out, pos, b);
}
static u32 proc_text(u32 device, char *out) {
    u32 p = 0;
    out[0] = 0;
    if (device == 1) {
        p = append_text(out, p, "ram_pages ");
        p = append_num(out, p, pages_total());
        p = append_text(out, p, "\nfree_pages ");
        p = append_num(out, p, pages_free());
        p = append_text(out, p, "\nheap_bytes ");
        p = append_num(out, p, heap_used());
        p = append_text(out, p, "\n");
    } else if (device == 2) {
        p = append_text(out, p, "PID PARENT STATE TICKS PAGES NAME\n");
        for (u32 i = 0; i < NV_TASK_MAX; ++i) {
            struct nv_taskinfo t;
            if (task_info(i, &t) <= 0)
                continue;
            p = append_num(out, p, t.pid);
            p = append_text(out, p, " ");
            p = append_num(out, p, t.parent);
            p = append_text(out, p, " ");
            p = append_num(out, p, t.state);
            p = append_text(out, p, " ");
            p = append_num(out, p, t.cpu_ticks);
            p = append_text(out, p, " ");
            p = append_num(out, p, t.pages);
            p = append_text(out, p, " ");
            p = append_text(out, p, t.name);
            p = append_text(out, p, "\n");
        }
    } else if (device == 3) {
        p = append_num(out, p, ticks);
        p = append_text(out, p, " ticks @ 100 Hz\n");
    } else
        p = append_text(out, p, "Nuvora Core " NV_VERSION " | " NV_ARCH_NAME " | ABI 1 | MIT\n");
    return p;
}
int fs_read(struct task *t, int fd, void *buf, u32 len) {
    if (fd == 0) {
        if (!len)
            return 0;
        int c = console_getc();
        if (c < 0)
            return -NV_EAGAIN;
        *(u8 *)buf = (u8)c;
        return 1;
    }
    if (fd < 3 || fd >= NV_OPEN_MAX || t->fd[fd].node < 0 || !(t->fd[fd].flags & NV_READ))
        return -NV_EBADF;
    struct descriptor *d = &t->fd[fd];
    struct node *n = &nodes[d->node];
    if (n->kind == NV_DEVICE) {
        if (n->device == 1)
            return 0;
        if (n->device == 2) {
            memset(buf, 0, len);
            return (int)len;
        }
        return fs_read(t, 0, buf, len);
    }
    static char proc_buffer[4096];
    const u8 *data = n->data;
    u32 size = n->size;
    if (n->kind == NV_PROC) {
        size = proc_text(n->device, proc_buffer);
        data = (u8 *)proc_buffer;
    }
    if (d->offset >= size)
        return 0;
    u32 amount = MIN(len, size - d->offset);
    memcpy(buf, data + d->offset, amount);
    d->offset += amount;
    return (int)amount;
}
int fs_write(struct task *t, int fd, const void *buf, u32 len) {
    if (fd == 1 || fd == 2) {
        console_write(buf, len);
        return (int)len;
    }
    if (fd < 3 || fd >= NV_OPEN_MAX || t->fd[fd].node < 0 || !(t->fd[fd].flags & NV_WRITE))
        return -NV_EBADF;
    struct descriptor *d = &t->fd[fd];
    struct node *n = &nodes[d->node];
    if (n->kind == NV_DEVICE) {
        if (n->device == 3)
            console_write(buf, len);
        return (int)len;
    }
    if (!len)
        return 0;
    u32 offset = (d->flags & NV_APPEND) ? n->size : d->offset;
    if (offset > NV_FILE_MAX || len > NV_FILE_MAX - offset)
        return -NV_ENOSPC;
    u32 end = offset + len;
    if (end > n->capacity) {
        u32 cap = MAX(256u, n->capacity);
        /* Restored files have exact-size capacities. Round their next growth
         * to a power of two so 128 KiB - 1 grows to 128 KiB, not 256 KiB. */
        if (cap & (cap - 1u)) cap = 256u;
        while (cap < end)
            cap = MIN(cap * 2, NV_FILE_MAX);
        u8 *data = kmalloc(cap);
        if (!data)
            return -NV_ENOMEM;
        if (n->size)
            memcpy(data, n->data, n->size);
        kfree(n->data);
        n->data = data;
        n->capacity = cap;
    }
    if (offset > n->size)
        memset(n->data + n->size, 0, offset - n->size);
    memcpy(n->data + offset, buf, len);
    n->size = MAX(n->size, end);
    d->offset = end;
    return (int)len;
}
int fs_seek(struct task *t, int fd, i32 offset, u32 origin) {
    if (fd < 3 || fd >= NV_OPEN_MAX || t->fd[fd].node < 0)
        return -NV_EBADF;
    struct descriptor *d = &t->fd[fd];
    struct node *n = &nodes[d->node];
    if (n->kind == NV_DEVICE || origin > 2)
        return -NV_EINVAL;
    u32 base = origin == 0 ? 0 : origin == 1 ? d->offset : n->size;
    if (offset < 0 && 0u - (u32)offset > base)
        return -NV_EINVAL;
    if (offset >= 0 && (u32)offset > NV_FILE_MAX - base)
        return -NV_EINVAL;
    d->offset = base + (u32)offset;
    return (int)d->offset;
}
int fs_list(int cwd, const char *path, u32 index, struct nv_dirent *out) {
    int p = fs_lookup(cwd, path);
    if (p < 0)
        return p;
    if (nodes[p].kind != NV_DIR)
        return -NV_ENOTDIR;
    u32 k = 0;
    for (int i = 1; i < FS_NODES; ++i)
        if (nodes[i].kind && nodes[i].parent == p) {
            if (k++ != index)
                continue;
            memset(out, 0, sizeof(*out));
            strlcpy(out->name, nodes[i].name, 32);
            out->kind = nodes[i].kind;
            out->size = nodes[i].size;
            return 1;
        }
    return 0;
}
int fs_mkdir(int cwd, const char *path) {
    int n = create_node(cwd, path, NV_DIR);
    return n < 0 ? n : 0;
}
static void delete_node(int n) {
    kfree(nodes[n].data);
    memset(&nodes[n], 0, sizeof(nodes[n]));
}
int fs_remove(int cwd, const char *path) {
    int n = fs_lookup(cwd, path);
    if (n < 0)
        return n;
    if (nodes[n].locked || nodes[nodes[n].parent].locked)
        return -NV_EACCESS;
    if (nodes[n].refs || task_cwd_in_use(n))
        return -NV_EBUSY;
    for (int i = 1; i < FS_NODES; ++i)
        if (nodes[i].kind && nodes[i].parent == n)
            return -NV_ENOTEMPTY;
    delete_node(n);
    return 0;
}
int fs_path(int n, char *out, usize cap) {
    char tmp[NV_PATH_MAX];
    u32 p = sizeof(tmp) - 1;
    tmp[p] = 0;
    if (!n)
        tmp[--p] = '/';
    for (int steps = 0; n && steps < FS_NODES; ++steps) {
        usize len = strlen(nodes[n].name);
        if (p < len + 1)
            return -NV_E2BIG;
        p -= len;
        memcpy(tmp + p, nodes[n].name, len);
        tmp[--p] = '/';
        n = nodes[n].parent;
    }
    if (n || sizeof(tmp) - p > cap)
        return -NV_E2BIG;
    strlcpy(out, tmp + p, cap);
    return 0;
}
int fs_display_path(int n, char *out, usize cap) {
    char canonical[NV_PATH_MAX];
    int r = fs_path(n, canonical, sizeof(canonical));
    if (r < 0) return r;
    for (u32 i = 0; i < mounted_volumes; ++i) {
        char prefix[NV_PATH_MAX];
        if (fs_path(volume_nodes[i], prefix, sizeof(prefix)) < 0) continue;
        usize len = strlen(prefix);
        if (strncmp(canonical, prefix, len) ||
            (canonical[len] && canonical[len] != '/')) continue;
        const char *tail = canonical + len;
        usize need = 2 + (*tail ? strlen(tail) : 1) + 1;
        if (need > cap) return -NV_E2BIG;
        out[0] = (char)('C' + i); out[1] = ':';
        if (*tail) strlcpy(out + 2, tail, cap - 2);
        else { out[2] = '/'; out[3] = 0; }
        return 0;
    }
    if (strlen(canonical) + 1 > cap) return -NV_E2BIG;
    strlcpy(out, canonical, cap);
    return 0;
}
static bool descendant(int n, int parent) {
    for (int i = 0; i < FS_NODES; ++i) {
        if (n == parent)
            return true;
        if (!n)
            break;
        n = nodes[n].parent;
    }
    return false;
}
static int volume_for(int node) {
    for (u32 i = 0; i < mounted_volumes; ++i)
        if (descendant(node, volume_nodes[i])) return (int)i;
    return -1;
}
int fs_move(int cwd, const char *src, const char *dst) {
    int n = fs_lookup(cwd, src);
    if (n < 0)
        return n;
    if (*dst && dst[strlen(dst) - 1] == '/' && nodes[n].kind != NV_DIR)
        return -NV_ENOTDIR;
    char name[32];
    int p = parent_for(cwd, dst, name);
    if (p < 0)
        return p;
    int from_volume = volume_for(n), to_volume = volume_for(p);
    if (from_volume >= 0 && to_volume >= 0 && from_volume != to_volume)
        return -NV_EINVAL; /* no cross-volume rename without an atomic transaction */
    if (nodes[p].kind != NV_DIR)
        return -NV_ENOTDIR;
    if (nodes[n].locked || nodes[nodes[n].parent].locked || nodes[p].locked)
        return -NV_EACCESS;
    if (descendant(p, n))
        return -NV_EINVAL;
    int other = child(p, name);
    if (other == n)
        return 0;
    if (other >= 0)
        return -NV_EEXIST;
    int oldparent = nodes[n].parent;
    char oldname[32];
    strlcpy(oldname, nodes[n].name, 32);
    nodes[n].parent = p;
    strlcpy(nodes[n].name, name, 32);
    for (int i = 1; i < FS_NODES; ++i)
        if (nodes[i].kind && descendant(i, n)) {
            char path[NV_PATH_MAX];
            if (fs_path(i, path, sizeof(path)) < 0) {
                nodes[n].parent = oldparent;
                strlcpy(nodes[n].name, oldname, 32);
                return -NV_E2BIG;
            }
        }
    return 0;
}
/* Commit a fully written regular file in one non-preemptible operation.
 * Existing readers must be closed: no node can change under an open handle. */
int fs_replace(int cwd, const char *src, const char *dst) {
    int n = fs_lookup(cwd, src);
    if (n < 0)
        return n;
    if (nodes[n].kind != NV_FILE)
        return -NV_EINVAL;
    char name[32], full[NV_PATH_MAX];
    int p = parent_for(cwd, dst, name);
    if (p < 0)
        return p;
    int from_volume = volume_for(n), to_volume = volume_for(p);
    if (from_volume >= 0 && to_volume >= 0 && from_volume != to_volume)
        return -NV_EINVAL;
    if (nodes[p].kind != NV_DIR)
        return -NV_ENOTDIR;
    if (!*dst || dst[strlen(dst) - 1] == '/')
        return -NV_ENOTDIR;
    if (nodes[n].locked || nodes[nodes[n].parent].locked || nodes[p].locked)
        return -NV_EACCESS;
    int other = child(p, name);
    if (other == n)
        return 0;
    if (other >= 0 && (nodes[other].kind != NV_FILE || nodes[other].locked))
        return -NV_EACCESS;
    if (nodes[n].refs || (other >= 0 && nodes[other].refs))
        return -NV_EBUSY;
    int r = fs_path(p, full, sizeof(full));
    if (r < 0 || strlen(full) + 1 + strlen(name) >= NV_PATH_MAX)
        return -NV_E2BIG;
    if (other >= 0)
        delete_node(other);
    nodes[n].parent = p;
    strlcpy(nodes[n].name, name, sizeof(nodes[n].name));
    return 0;
}
/* Snapshot serialization is little-endian: count; repeated kind/pathlen/size/path/data. */
int fs_export_volume(u32 volume, u8 *out, u32 cap, u32 *length) {
    if (volume >= mounted_volumes) return -NV_ENODEV;
    int root = volume_nodes[volume];
    if (cap < 4)
        return -NV_ENOSPC;
    u32 count = 0, pos = 4;
    /* Parent-before-child ordering, independent of inode reuse and renames. */
    for (u32 depth = 1; depth < FS_NODES; ++depth)
        for (int i = 1; i < FS_NODES; ++i) {
            if (!nodes[i].kind || i == root || !descendant(i, root))
                continue;
            u32 d = 0;
            for (int p = i; p != root; p = nodes[p].parent)
                ++d;
            if (d != depth)
                continue;
            char path[NV_PATH_MAX];
            if (fs_path(i, path, sizeof(path)) < 0)
                return -NV_E2BIG;
            u32 n = strlen(path);
            if (cap - pos < 12 || n > cap - pos - 12 || nodes[i].size > cap - pos - 12 - n)
                return -NV_ENOSPC;
            u32 kind = nodes[i].kind;
            memcpy(out + pos, &kind, 4);
            memcpy(out + pos + 4, &n, 4);
            memcpy(out + pos + 8, &nodes[i].size, 4);
            pos += 12;
            memcpy(out + pos, path, n);
            pos += n;
            if (nodes[i].size)
                memcpy(out + pos, nodes[i].data, nodes[i].size);
            pos += nodes[i].size;
            ++count;
        }
    memcpy(out, &count, 4);
    *length = pos;
    return 0;
}
int fs_export_home(u8 *out, u32 cap, u32 *length) {
    return fs_export_volume(0, out, cap, length);
}
static void clear_volume(u32 volume) {
    int root = volume_nodes[volume];
    for (int pass = 0; pass < FS_NODES; ++pass)
        for (int i = 1; i < FS_NODES; ++i) {
            if (!nodes[i].kind || i == root || !descendant(i, root))
                continue;
            bool leaf = true;
            for (int j = 1; j < FS_NODES; ++j)
                if (nodes[j].kind && nodes[j].parent == i) {
                    leaf = false;
                    break;
                }
            if (leaf)
                delete_node(i);
        }
}
int fs_import_volume(u32 volume, const u8 *data, u32 len) {
    if (volume >= mounted_volumes) return -NV_ENODEV;
    char prefix[NV_PATH_MAX];
    if (fs_path(volume_nodes[volume], prefix, sizeof(prefix)) < 0) return -NV_EIO;
    u32 prefix_len = strlen(prefix);
    if (len < 4)
        return -NV_EIO;
    u32 count;
    memcpy(&count, data, 4);
    if (count > FS_NODES - fs_node_count())
        return -NV_ENOSPC;
    u32 pos = 4;
    /* Validate every byte range and canonical path before touching the live tree. */
    for (u32 i = 0; i < count; ++i) {
        u32 kind, n, size;
        if (len - pos < 12)
            return -NV_EIO;
        memcpy(&kind, data + pos, 4);
        memcpy(&n, data + pos + 4, 4);
        memcpy(&size, data + pos + 8, 4);
        pos += 12;
        if ((kind != NV_FILE && kind != NV_DIR) || (kind == NV_DIR && size) || n < prefix_len + 2 ||
            n >= NV_PATH_MAX || n > len - pos || size > len - pos - n || size > NV_FILE_MAX)
            return -NV_EIO;
        char path[NV_PATH_MAX];
        memcpy(path, data + pos, n);
        path[n] = 0;
        if (strnlen(path, n) != n || strncmp(path, prefix, prefix_len) ||
            path[prefix_len] != '/' || path[n - 1] == '/')
            return -NV_EIO;
        for (u32 k = prefix_len + 1; k < n;) {
            u32 first = k;
            while (k < n && path[k] != '/')
                ++k;
            u32 part = k - first;
            if (!part || part > NV_NAME_MAX || (part == 1 && path[first] == '.') ||
                (part == 2 && path[first] == '.' && path[first + 1] == '.'))
                return -NV_EIO;
            ++k;
        }
        pos += n + size;
    }
    if (pos != len)
        return -NV_EIO;
    clear_volume(volume);
    pos = 4;
    for (u32 i = 0; i < count; ++i) {
        u32 kind, n, size;
        memcpy(&kind, data + pos, 4);
        memcpy(&n, data + pos + 4, 4);
        memcpy(&size, data + pos + 8, 4);
        pos += 12;
        char path[NV_PATH_MAX];
        memcpy(path, data + pos, n);
        path[n] = 0;
        pos += n;
        int node = create_node(0, path, kind);
        if (node < 0) {
            clear_volume(volume);
            return -NV_EIO;
        }
        if (size) {
            nodes[node].data = kmalloc(size);
            if (!nodes[node].data) {
                clear_volume(volume);
                return -NV_ENOMEM;
            }
            memcpy(nodes[node].data, data + pos, size);
            nodes[node].size = nodes[node].capacity = size;
        }
        pos += size;
    }
    return 0;
}
int fs_import_home(const u8 *data, u32 len) {
    return fs_import_volume(0, data, len);
}
