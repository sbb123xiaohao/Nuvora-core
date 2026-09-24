#include "kernel.h"
struct task tasks[NV_TASK_MAX];
struct task *current;
static u32 next_pid = 1;
struct elf_header {
    u8 ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstr;
} PACKED;
struct program_header {
    u32 type, flags;
    u64 offset, vaddr, paddr, filesz, memsz, align;
} PACKED;
#define ELF_CLASS 2
#define ELF_MACHINE 62
_Static_assert(sizeof(struct elf_header) == 64, "ELF64 header");
_Static_assert(sizeof(struct program_header) == 56, "ELF64 program header");
void task_init(void) {
    memset(tasks, 0, sizeof(tasks));
}
static struct task *by_pid(u32 pid) {
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (tasks[i].state && tasks[i].pid == pid)
            return &tasks[i];
    return NULL;
}
static void release_task(struct task *t) {
    vm_destroy(t->pd);
    vm_stack_free(t->kstack);
    memset(t, 0, sizeof(*t));
}
void task_reap(void) {
    for (u32 i = 0; i < NV_TASK_MAX; ++i) {
        struct task *t = &tasks[i];
        if (t == current || t->state != NV_ZOMBIE)
            continue;
        /* Keep only the waitable status. Never free the stack we are on. */
        vm_destroy(t->pd);
        vm_stack_free(t->kstack);
        t->pd = NULL;
        t->kstack = NULL;
        t->frame = NULL;
        if (t->collected || !t->parent)
            memset(t, 0, sizeof(*t));
    }
}
static int load_elf(struct task *t, const u8 *data, u32 len, u32 *entry) {
    if (len < sizeof(struct elf_header))
        return -NV_ENOEXEC;
    const struct elf_header *h = (const void *)(uptr)data;
    if (memcmp(h->ident, "\177ELF", 4) || h->ident[4] != ELF_CLASS || h->ident[5] != 1 ||
        h->ident[6] != 1 || h->type != 2 || h->machine != ELF_MACHINE || h->version != 1 ||
        h->ehsize != sizeof(*h) || h->phentsize != sizeof(struct program_header) || !h->phnum ||
        h->phnum > 32)
        return -NV_ENOEXEC;
    if (h->phoff > len || h->phnum > (len - h->phoff) / sizeof(struct program_header))
        return -NV_ENOEXEC;
    const struct program_header *ph = (const void *)(uptr)(data + h->phoff);
    bool executable_entry = false;
    for (u32 i = 0; i < h->phnum; ++i) {
        const struct program_header *p = &ph[i];
        if (p->type == 2 || p->type == 3 || p->type == 7)
            return -NV_ENOEXEC;
        if (p->type != 1)
            continue;
        if (!p->memsz && !p->filesz)
            continue;
        if (p->filesz > p->memsz || p->offset > len || p->filesz > len - p->offset ||
            p->vaddr < USER_BASE || p->vaddr >= USER_IMAGE_END ||
            p->memsz > USER_IMAGE_END - p->vaddr || (p->flags & ~7u) || ((p->flags & 3) == 3))
            return -NV_ENOEXEC;
        if (p->align > 1 &&
            ((p->align & (p->align - 1)) || ((p->offset ^ p->vaddr) & (p->align - 1))))
            return -NV_ENOEXEC;
        if (!p->memsz)
            continue;
        if ((p->flags & 1) && h->entry >= p->vaddr && h->entry - p->vaddr < p->memsz)
            executable_entry = true;
        u32 first = p->vaddr & ~4095u, end = ALIGN_UP(p->vaddr + p->memsz, PAGE);
        for (u32 va = first; va < end; va += PAGE) {
            int r =
                vm_map(t->pd, va, ((p->flags & 2) ? P_WRITE : 0) | ((p->flags & 1) ? P_EXEC : 0));
            if (r < 0)
                return r == -NV_EEXIST ? -NV_ENOEXEC : r;
        }
        if (copy_to_space(t->pd, p->vaddr, data + p->offset, p->filesz) < 0)
            return -NV_ENOEXEC;
    }
    if (!executable_entry)
        return -NV_ENOEXEC;
    *entry = h->entry;
    return 0;
}
static int prepare_image(struct task *t, const char *path, const char *args, struct frame *frame) {
    if (strlen(args) >= NV_ARG_MAX)
        return -NV_E2BIG;
    const u8 *data;
    u32 len;
    int r = fs_blob(path, &data, &len);
    if (r < 0)
        return r;
    t->pd = vm_create();
    if (!t->pd)
        return -NV_ENOMEM;
    u32 entry;
    r = load_elf(t, data, len, &entry);
    if (r < 0)
        return r;
    for (u32 i = 1; i <= USER_STACK_PAGES; ++i) {
        r = vm_map(t->pd, USER_STACK_TOP - i * PAGE, P_WRITE);
        if (r < 0)
            return r;
    }
    *frame = (struct frame){.cs = 0x1b,
                            .ss = 0x23,
                            .eflags = 0x202,
                            .eip = entry,
                            .useresp = USER_STACK_TOP - 512,
                            .ebx = USER_STACK_TOP - NV_ARG_MAX};
    return copy_to_space(t->pd, frame->ebx, args, strlen(args) + 1);
}
static void name_task(struct task *t, const char *path) {
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/')
            name = p + 1;
    strlcpy(t->name, name, sizeof(t->name));
}
static u32 allocate_pid(void) {
    for (;;) {
        u32 pid = next_pid;
        next_pid = next_pid >= 0x7fffffffu ? 2 : next_pid + 1;
        if (!by_pid(pid))
            return pid;
    }
}
int task_spawn(const char *path, const char *args, struct task *parent) {
    task_reap();
    struct task *t = NULL;
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (!tasks[i].state) {
            t = &tasks[i];
            break;
        }
    if (!t)
        return -NV_ENOSPC;
    struct frame frame;
    int r = prepare_image(t, path, args, &frame);
    if (r < 0) {
        release_task(t);
        return r;
    }
    t->kstack = vm_stack_alloc((u32)(t - tasks));
    if (!t->kstack) {
        release_task(t);
        return -NV_ENOMEM;
    }
    t->frame = (void *)((u8 *)t->kstack + KSTACK_SIZE - sizeof(struct frame));
    *t->frame = frame;
    for (u32 i = 0; i < NV_OPEN_MAX; ++i)
        t->fd[i].node = -1;
    t->pid = allocate_pid();
    t->parent = parent ? parent->pid : 0;
    t->cwd = parent ? parent->cwd : fs_lookup(0, "/home");
    t->heap_end = USER_HEAP;
    name_task(t, path);
    cpu_fp_reset(&t->fp);
    t->state = NV_READY;
    return (int)t->pid;
}
int task_exec(const char *path, const char *args) {
    struct task staged = {0};
    struct frame frame;
    int r = prepare_image(&staged, path, args, &frame);
    if (r < 0) {
        vm_destroy(staged.pd);
        return r;
    }
    /* Commit only after all validation, mappings and argument copying succeed.
     * PID, parent, children, working directory and open handles remain intact. */
    pte_t *old = current->pd;
    console_release(current->pid);
    current->pd = staged.pd;
    current->heap_end = USER_HEAP;
    *current->frame = frame;
    name_task(current, path);
    load_cr3((uptr)current->pd);
    vm_destroy(old);
    cpu_fp_reset(&current->fp);
    cpu_fp_restore(&current->fp);
    return 0;
}
NORETURN void task_start(int pid) {
    current = by_pid((uptr)pid);
    if (!current)
        panic("initial task missing");
    load_cr3((uptr)current->pd);
    arch_set_stack((uptr)current->kstack + KSTACK_SIZE);
    cpu_fp_restore(&current->fp);
    arch_resume(current->frame);
}
void task_tick(void) {
    if (current && current->state == NV_READY)
        ++current->cpu_ticks;
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (tasks[i].state == NV_SLEEPING && (i32)(ticks - tasks[i].wake) >= 0)
            tasks[i].state = NV_READY;
}
struct frame *schedule(struct frame *f) {
    if (current) {
        current->frame = f;
        cpu_fp_save(&current->fp);
    }
    task_reap();
    u32 index = current ? (u32)(current - tasks) : NV_TASK_MAX - 1;
    for (;;) {
        for (u32 step = 1; step <= NV_TASK_MAX; ++step) {
            struct task *t = &tasks[(index + step) % NV_TASK_MAX];
            if (t->state != NV_READY)
                continue;
            current = t;
            load_cr3((uptr)t->pd);
            arch_set_stack((uptr)t->kstack + KSTACK_SIZE);
            cpu_fp_restore(&t->fp);
            return t->frame;
        }
        idle_once();
        usb_poll();
    }
}
static void finish(struct task *t, int status) {
    console_release(t->pid);
    t->status = status;
    t->state = NV_ZOMBIE;
    for (int i = 3; i < NV_OPEN_MAX; ++i)
        if (t->fd[i].node >= 0)
            fs_close(t, i);
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (tasks[i].state && tasks[i].parent == t->pid)
            tasks[i].parent = 0;
    struct task *p = by_pid(t->parent);
    if (p && p->state == NV_WAITING && p->wait_pid == t->pid) {
        p->frame->eax = (u32)status;
        p->state = NV_READY;
        t->collected = true;
    }
}
void task_exit(int status) {
    if (current->pid == 1) {
        kprintf("\n[init] pid 1 terminated with status %d\n", status);
        panic("initial process terminated");
    }
    finish(current, status);
}
int task_wait(u32 pid) {
    struct task *t = by_pid(pid);
    if (!t || t->parent != current->pid || t->collected)
        return -NV_ECHILD;
    if (t->state == NV_ZOMBIE) {
        t->collected = true;
        return t->status;
    }
    current->wait_pid = pid;
    current->state = NV_WAITING;
    return -4096;
}
int task_stop(u32 pid) {
    struct task *t = by_pid(pid);
    if (!t || t->state == NV_ZOMBIE)
        return -NV_ENOENT;
    if (pid == 1 || t == current || (current->pid != 1 && t->parent != current->pid))
        return -NV_EACCESS;
    finish(t, 143);
    return 0;
}
int task_grow(i32 delta) {
    u32 old = current->heap_end;
    if (!delta)
        return (int)old;
    if (delta < 0) {
        u32 n = 0u - (u32)delta;
        if (n > (old - USER_HEAP) / PAGE)
            return -NV_EINVAL;
        u32 end = old - n * PAGE;
        for (u32 va = end; va < old; va += PAGE)
            vm_unmap(current->pd, va);
        current->heap_end = end;
        return (int)old;
    }
    if ((u32)delta > (USER_HEAP_END - old) / PAGE)
        return -NV_ENOMEM;
    u32 end = old + (u32)delta * PAGE;
    for (u32 va = old; va < end; va += PAGE) {
        int r = vm_map(current->pd, va, P_WRITE);
        if (r < 0) {
            for (u32 p = old; p <= va; p += PAGE)
                vm_unmap(current->pd, p);
            return r;
        }
    }
    current->heap_end = end;
    load_cr3((uptr)current->pd);
    return (int)old;
}
u32 task_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (tasks[i].state && tasks[i].state != NV_ZOMBIE)
            ++n;
    return n;
}
int task_info(u32 i, struct nv_taskinfo *out) {
    if (i >= NV_TASK_MAX)
        return -NV_EINVAL;
    struct task *t = &tasks[i];
    if (!t->state)
        return 0;
    memset(out, 0, sizeof(*out));
    out->pid = t->pid;
    out->parent = t->parent;
    out->state = t->state;
    out->cpu_ticks = t->cpu_ticks;
    out->pages = t->pd ? vm_page_count(t->pd) : 0;
    strlcpy(out->name, t->name, 32);
    return 1;
}
bool task_cwd_in_use(int node) {
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (tasks[i].state && tasks[i].state != NV_ZOMBIE && tasks[i].cwd == node)
            return true;
    return false;
}
