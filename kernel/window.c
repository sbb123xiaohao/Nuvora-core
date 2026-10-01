#include "kernel.h"

/* Page vectors let large surfaces survive fragmented physical RAM. Only the
 * small vectors live in the kernel heap. The front is immutable until COMMIT;
 * the back is never readable by the desktop. Syscalls run on the single CPU
 * without task preemption while executing kernel code. */
struct window_buffer { uptr *pages; u32 count, width, height; };
struct native_window {
    u32 id, pid, serial, generation, width, height, flags;
    char title[64];
    struct window_buffer front, back;
    u32 frame_serial, covered_rows;
    u8 covered[NV_WINDOW_EDGE_MAX / 8];
    bool uploading, configure_pending, focus_pending, close_pending;
    struct nv_window_event events[32];
    u32 head, tail, queued;
    bool stdio;
    struct nv_surface text;
    u32 row, column;
};
static struct native_window native_windows[NV_WINDOW_MAX];
static u32 window_server, next_window_id = 1;

static void buffer_free(struct window_buffer *b) {
    for (u32 i = 0; i < b->count; ++i) page_free(b->pages[i]);
    kfree(b->pages);
    *b = (struct window_buffer){0};
}
static int buffer_allocate(struct window_buffer *b, u32 width, u32 height) {
    if (b->width == width && b->height == height && b->pages) return 0;
    struct window_buffer next = {.width = width, .height = height};
    u32 count = (u32)(((u64)width * height * 4 + PAGE - 1) / PAGE);
    next.pages = kmalloc((usize)count * sizeof(uptr));
    if (!next.pages) return -NV_ENOMEM;
    for (; next.count < count; ++next.count) {
        uptr page = page_alloc();
        if (!page) { buffer_free(&next); return -NV_ENOMEM; }
        next.pages[next.count] = page;
    }
    buffer_free(b);
    *b = next;
    return 0;
}
static void buffer_copy(struct window_buffer *b, u32 offset, void *user, u32 bytes, bool write) {
    u8 *p = user;
    while (bytes) {
        u32 in_page = offset % PAGE, n = MIN(bytes, PAGE - in_page);
        u8 *physical = (u8 *)phys_ptr(b->pages[offset / PAGE]) + in_page;
        if (write) memcpy(physical, p, n);
        else memcpy(p, physical, n);
        offset += n; p += n; bytes -= n;
    }
}
static void window_free(struct native_window *w) {
    buffer_free(&w->front); buffer_free(&w->back);
    memset(w, 0, sizeof(*w));
}
void window_task_release(u32 pid) {
    if (pid == window_server) {
        window_server = 0;
        for (u32 i = 0; i < NV_WINDOW_MAX; ++i) window_free(&native_windows[i]);
    } else {
        for (u32 i = 0; i < NV_WINDOW_MAX; ++i)
            if (native_windows[i].pid == pid) window_free(&native_windows[i]);
    }
}
static struct native_window *window_find(u32 id) {
    if (id) for (u32 i = 0; i < NV_WINDOW_MAX; ++i)
        if (native_windows[i].id == id) return &native_windows[i];
    return NULL;
}
bool window_owned(u32 pid) {
    for (u32 i=0;i<NV_WINDOW_MAX;++i)
        if (native_windows[i].id && native_windows[i].pid==pid) return true;
    return false;
}
/* Console handles inherited by descendants are routed to the nearest live
 * terminal ancestor. Files and the ordinary boot console keep their ABI. */
static struct native_window *stdio_window(struct task *t) {
    for (u32 depth = 0; t && depth < NV_TASK_MAX; ++depth) {
        for (u32 i = 0; i < NV_WINDOW_MAX; ++i)
            if (native_windows[i].pid == t->pid && native_windows[i].stdio)
                return &native_windows[i];
        u32 parent = t->parent;
        t = NULL;
        for (u32 i = 0; parent && i < NV_TASK_MAX; ++i)
            if (tasks[i].state && tasks[i].pid == parent) { t = &tasks[i]; break; }
    }
    return NULL;
}
static void text_character(struct native_window *w, u8 ch) {
    if (ch == '\r') w->column = 0;
    else if (ch == '\n') { w->column = 0; ++w->row; }
    else if (ch == '\b') {
        if (w->column) --w->column;
    } else if (ch == '\t') {
        for (u32 n = 4 - w->column % 4; n; --n) text_character(w, ' ');
    } else if (ch >= 32) {
        w->text.cells[w->row * 80 + w->column] = 0x0700u | ch;
        if (++w->column == 80) { w->column = 0; ++w->row; }
    }
    if (w->row >= 25) {
        memmove(w->text.cells, w->text.cells + 80, 24 * 80 * sizeof(u16));
        for (u32 i = 24 * 80; i < 25 * 80; ++i) w->text.cells[i] = 0x0720;
        w->row = 24;
    }
    w->text.cursor = w->row * 80 + w->column;
}
int window_stdio_clear(struct task *t) {
    struct native_window *w = stdio_window(t);
    if (!w) return -NV_ENODEV;
    for (u32 i=0;i<2000;++i) w->text.cells[i]=0x0720;
    w->row=w->column=w->text.cursor=0;
    if (!++w->generation) ++w->generation;
    return 0;
}
int window_stdio_write(struct task *t, const void *bytes, u32 length) {
    struct native_window *w = stdio_window(t);
    if (!w) return -NV_ENODEV;
    const u8 *p = bytes;
    for (u32 i = 0; i < length; ++i) text_character(w, p[i]);
    if (!++w->generation) ++w->generation;
    return (int)length;
}
int window_stdio_read(struct task *t, void *bytes, u32 length) {
    struct native_window *w = stdio_window(t);
    if (!w) return -NV_ENODEV;
    if (!length) return 0;
    if (w->close_pending) return -NV_EIO;
    while (w->queued) {
        struct nv_window_event e = w->events[w->tail];
        w->tail = (w->tail + 1) % ARRAY_LEN(w->events); --w->queued;
        if (e.type != NV_WINDOW_EVENT_KEY) continue;
        u32 ch = e.key & 4095u;
        if (ch >= 256 || (e.key & (NV_KEY_ALT|NV_KEY_META))) continue;
        if ((e.key & NV_KEY_CTRL) && ch >= 'a' && ch <= 'z') ch -= 'a' - 1;
        *(u8 *)bytes = (u8)ch; return 1;
    }
    return -NV_EAGAIN;
}
static bool window_dimensions(u32 width, u32 height) {
    return width && height && width <= NV_WINDOW_EDGE_MAX && height <= NV_WINDOW_EDGE_MAX;
}
static int window_queue(struct native_window *w, const struct nv_window_event *e) {
    if (e->type == NV_WINDOW_EVENT_CLOSE) { w->close_pending = true; return 0; }
    if (!(w->flags & NV_WINDOW_VISIBLE)) return -NV_EACCESS;
    if (e->type == NV_WINDOW_EVENT_KEY && !(w->flags & NV_WINDOW_FOCUSED)) return -NV_EACCESS;
    if (e->type != NV_WINDOW_EVENT_KEY && e->type != NV_WINDOW_EVENT_POINTER) return -NV_EINVAL;
    /* Merge motion only when no button transition or wheel step is lost. */
    if (w->queued && e->type == NV_WINDOW_EVENT_POINTER && !e->wheel) {
        u32 previous = (w->head + ARRAY_LEN(w->events) - 1) % ARRAY_LEN(w->events);
        struct nv_window_event *old = &w->events[previous];
        if (old->type == e->type && old->buttons == e->buttons && !old->wheel) {
            *old = *e; return 0;
        }
    }
    if (w->queued == ARRAY_LEN(w->events)) return -NV_EAGAIN;
    w->events[w->head] = *e;
    w->head = (w->head + 1) % ARRAY_LEN(w->events);
    ++w->queued;
    return 0;
}
static int window_event(struct native_window *w, struct nv_window_event *e) {
    *e = (struct nv_window_event){.id = w->id};
    if (w->close_pending) {
        w->close_pending = false; e->type = NV_WINDOW_EVENT_CLOSE;
    } else if (w->configure_pending) {
        w->configure_pending = false;
        e->type = NV_WINDOW_EVENT_CONFIGURE; e->serial = w->serial;
        e->width = w->width; e->height = w->height;
    } else if (w->focus_pending) {
        w->focus_pending = false;
        e->type = NV_WINDOW_EVENT_FOCUS; e->flags = w->flags;
    } else if (w->queued) {
        *e = w->events[w->tail];
        w->tail = (w->tail + 1) % ARRAY_LEN(w->events);
        --w->queued;
    } else return 0;
    return 1;
}
int window_ioctl(u32 op, uptr pointer) {
    if (op == NV_WINDOW_SERVER_ACQUIRE || op == NV_WINDOW_SERVER_RELEASE) {
        if (pointer) return -NV_EINVAL;
        if (op == NV_WINDOW_SERVER_RELEASE) {
            if (current->pid != window_server) return -NV_EACCESS;
            window_task_release(current->pid); return 0;
        }
        if (!account_interactive_allowed(current) || !console_owned(current->pid)) return -NV_EACCESS;
        if (window_server && window_server != current->pid) return -NV_EBUSY;
        window_server = current->pid; return 0;
    }
    u32 size;
    bool output = false;
    switch (op) {
    case NV_WINDOW_INFO: size = sizeof(struct nv_window_info); output = true; break;
    case NV_WINDOW_CREATE: size = sizeof(struct nv_window_create); output = true; break;
    case NV_WINDOW_BEGIN: size = sizeof(struct nv_window_frame); break;
    case NV_WINDOW_DESTROY: case NV_WINDOW_COMMIT: size = sizeof(struct nv_window_id); break;
    case NV_WINDOW_BIND_STDIO: size = sizeof(struct nv_window_id); break;
    case NV_WINDOW_TEXT_READ: size = sizeof(struct nv_window_text); output = true; break;
    case NV_WINDOW_READ: case NV_WINDOW_UPLOAD: size = sizeof(struct nv_window_pixels32); break;
    case NV_WINDOW_READ64: case NV_WINDOW_UPLOAD64: size = sizeof(struct nv_window_pixels); break;
    case NV_WINDOW_POLL: case NV_WINDOW_SEND: size = sizeof(struct nv_window_event);
        output = op == NV_WINDOW_POLL; break;
    case NV_WINDOW_ENUM: size = sizeof(struct nv_window_entry); output = true; break;
    case NV_WINDOW_CONFIGURE: size = sizeof(struct nv_window_configure); break;
    default: return -NV_EINVAL;
    }
    if (!user_range(current->pd, pointer, size, output)) return -NV_EFAULT;
    void *user = (void *)(uptr)pointer;
    if (op == NV_WINDOW_INFO) {
        struct nv_window_info info = {NV_WINDOW_API_VERSION, window_server, NV_WINDOW_MAX,
                                      NV_DISPLAY_MAX_COPY};
        memcpy(user, &info, sizeof(info)); return 0;
    }
    if (!window_server) return -NV_ENODEV;
    bool server = current->pid == window_server;
    if (op == NV_WINDOW_ENUM) {
        if (!server) return -NV_EACCESS;
        u32 index; memcpy(&index, user, sizeof(index));
        if (index >= NV_WINDOW_MAX) return -NV_EINVAL;
        struct native_window *w = &native_windows[index];
        if (!w->id) return 0;
        struct nv_window_entry out = {.index = index, .id = w->id, .pid = w->pid,
            .width = w->front.width, .height = w->front.height, .generation = w->generation,
            .serial = w->serial, .flags = w->flags | (w->stdio ? NV_WINDOW_TEXT : 0),
            .requested_width = w->width, .requested_height = w->height};
        memcpy(out.title, w->title, sizeof(out.title));
        memcpy(user, &out, sizeof(out)); return 1;
    }
    if (op == NV_WINDOW_CREATE) {
        if (server) return -NV_EACCESS;
        struct nv_window_create io; memcpy(&io, user, sizeof(io));
        if (!window_dimensions(io.width, io.height) || strnlen(io.title, sizeof(io.title)) == sizeof(io.title))
            return -NV_EINVAL;
        struct native_window *w = NULL;
        for (u32 i = 0; i < NV_WINDOW_MAX; ++i) if (!native_windows[i].id) {
            w = &native_windows[i]; break;
        }
        if (!w) return -NV_ENOSPC;
        do { io.id = next_window_id++; } while (!io.id || window_find(io.id));
        *w = (struct native_window){.id = io.id, .pid = current->pid,
            .width = io.width, .height = io.height, .serial = 1, .flags = NV_WINDOW_VISIBLE};
        strlcpy(w->title, *io.title ? io.title : "Application", sizeof(w->title));
        io.serial = 1;
        memcpy(user, &io, sizeof(io)); return 0;
    }
    u32 id; memcpy(&id, user, sizeof(id));
    struct native_window *w = window_find(id);
    if (!w) return -NV_ENOENT;
    bool server_op = op == NV_WINDOW_READ || op == NV_WINDOW_READ64 || op == NV_WINDOW_CONFIGURE ||
                     op == NV_WINDOW_SEND || op == NV_WINDOW_TEXT_READ;
    if (server_op ? !server : w->pid != current->pid) return -NV_EACCESS;
    if (op == NV_WINDOW_DESTROY) { window_free(w); return 0; }
    if (op == NV_WINDOW_BIND_STDIO) {
        if (w->uploading || w->front.pages) return -NV_EBUSY;
        w->stdio = true; w->generation = 1;
        for (u32 i = 0; i < 2000; ++i) w->text.cells[i] = 0x0720;
        return 0;
    }
    if (op == NV_WINDOW_TEXT_READ) {
        struct nv_window_text io; memcpy(&io, user, sizeof(io));
        if (!w->stdio) return -NV_EINVAL;
        if (io.generation != w->generation) return -NV_EAGAIN;
        io.surface = w->text; memcpy(user, &io, sizeof(io)); return 0;
    }
    if (op == NV_WINDOW_CONFIGURE) {
        struct nv_window_configure io; memcpy(&io, user, sizeof(io));
        if (!window_dimensions(io.width, io.height) || (io.flags & ~3u) ||
            ((io.flags & NV_WINDOW_FOCUSED) && !(io.flags & NV_WINDOW_VISIBLE))) return -NV_EINVAL;
        if (w->width != io.width || w->height != io.height) {
            w->width = io.width; w->height = io.height;
            if (!++w->serial) ++w->serial;
            w->configure_pending = true;
        }
        if (io.flags & NV_WINDOW_FOCUSED) for (u32 i = 0; i < NV_WINDOW_MAX; ++i) {
            struct native_window *other = &native_windows[i];
            if (other != w && (other->flags & NV_WINDOW_FOCUSED)) {
                other->flags &= ~NV_WINDOW_FOCUSED;
                other->focus_pending = true; other->head = other->tail = other->queued = 0;
            }
        }
        if (w->flags != io.flags) {
            w->flags = io.flags; w->focus_pending = true;
            w->head = w->tail = w->queued = 0;
        }
        return 0;
    }
    if (op == NV_WINDOW_SEND) {
        struct nv_window_event io; memcpy(&io, user, sizeof(io));
        return window_queue(w, &io);
    }
    if (op == NV_WINDOW_POLL) {
        struct nv_window_event io;
        int result = window_event(w, &io);
        memcpy(user, &io, sizeof(io)); return result;
    }
    if (op == NV_WINDOW_BEGIN) {
        if (w->stdio) return -NV_EINVAL;
        struct nv_window_frame io; memcpy(&io, user, sizeof(io));
        if (io.serial != w->serial || io.width != w->width || io.height != w->height)
            return -NV_EAGAIN;
        int result = buffer_allocate(&w->back, io.width, io.height);
        if (result < 0) return result;
        w->uploading = true; w->frame_serial = io.serial; w->covered_rows = 0;
        memset(w->covered, 0, sizeof(w->covered)); return 0;
    }
    if (op == NV_WINDOW_COMMIT) {
        if (!w->uploading) return -NV_EINVAL;
        if (w->frame_serial != w->serial) { w->uploading = false; return -NV_EAGAIN; }
        if (w->covered_rows != w->back.height) return -NV_EINVAL;
        struct window_buffer old = w->front; w->front = w->back; w->back = old;
        w->uploading = false;
        if (!++w->generation) ++w->generation;
        return 0;
    }
    struct nv_window_pixels io = {0};
    if (op == NV_WINDOW_READ || op == NV_WINDOW_UPLOAD) {
        struct nv_window_pixels32 old; memcpy(&old, user, sizeof(old));
        io = (struct nv_window_pixels){.id=old.id, .generation=old.generation,
            .x=old.x, .y=old.y, .width=old.width, .height=old.height,
            .stride=old.stride, .pixels=old.pixels};
    } else {
        memcpy(&io, user, sizeof(io));
        if (io.reserved) return -NV_EINVAL;
    }
    bool write = op == NV_WINDOW_UPLOAD || op == NV_WINDOW_UPLOAD64;
    struct window_buffer *b = write ? &w->back : &w->front;
    if (write && !w->uploading) return -NV_EINVAL;
    if ((!write && io.generation != w->generation) ||
        (write && w->frame_serial != w->serial)) return -NV_EAGAIN;
    if (!b->pages || !io.width || !io.height || io.x >= b->width || io.y >= b->height ||
        io.width > b->width - io.x || io.height > b->height - io.y ||
        (u64)io.width * io.height * 4 > NV_DISPLAY_MAX_COPY ||
        (u64)io.width * 4 > io.stride || (write && (io.x || io.width != b->width)))
        return -NV_EINVAL;
    u64 bytes = (u64)(io.height - 1) * io.stride + (u64)io.width * 4;
    if (!user_range(current->pd, io.pixels, bytes, !write))
        return -NV_EFAULT;
    u8 *p = (void *)(uptr)io.pixels;
    for (u32 row = 0; row < io.height; ++row) {
        u32 y = io.y + row;
        buffer_copy(b, (y * b->width + io.x) * 4, p + (uptr)row * io.stride, io.width * 4, write);
        if (write && !(w->covered[y / 8] & (1u << (y % 8)))) {
            w->covered[y / 8] |= 1u << (y % 8); ++w->covered_rows;
        }
    }
    return 0;
}
