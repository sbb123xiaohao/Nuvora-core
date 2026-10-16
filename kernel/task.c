#include "kernel.h"
#include "elf_loader.h"
#include "linux_loader.h"
struct task tasks[NV_TASK_MAX];
struct task *current;
static u32 next_pid = 1;
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
static int prepare_image(struct task *t, const char *path, const char *args, struct frame *frame) {
    if (strlen(args) >= NV_ARG_MAX)
        return -NV_E2BIG;
    const u8 *data;
    u8 *temporary;
    usize len;
    int r = fs_blob(path, &data, &len, &temporary);
    if (r < 0)
        return r;
    t->pd = vm_create();
    if (!t->pd) { r = -NV_ENOMEM; goto done; }
    uptr entry,phdr=0;
    r = t->linux_compat.enabled ? linux_image(t,data,len,&entry,&phdr) : load_elf(t, data, len, &entry);
    if (r < 0)
        goto done;
    uptr stack_top = t->abi == NV_ABI_VERSION ? NV_USER_STACK : USER_STACK_TOP;
    for (u32 i = 1; i <= (t->linux_compat.enabled?LINUX_STACK_PAGES:USER_STACK_PAGES); ++i) {
        r = vm_map(t->pd, stack_top - i * PAGE, P_WRITE);
        if (r < 0)
            goto done;
    }
    if (t->linux_compat.enabled) { r=linux_stack(t,path,args,data,entry,phdr,frame); goto done; }
    *frame = (struct frame){.cs = 0x1b,
                            .ss = 0x23,
                            .eflags = 0x202,
                            .eip = entry,
                            .useresp = stack_top - 512,
                            .ebx = stack_top - NV_ARG_MAX};
    r = copy_to_space(t->pd, frame->ebx, args, strlen(args) + 1);
done:
    kfree(temporary);
    return r;
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
static int spawn(const char *path, const char *args, struct task *parent,bool linux_mode) {
    task_reap();
    struct task *t = NULL;
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (!tasks[i].state) {
            t = &tasks[i];
            break;
        }
    if (!t)
        return -NV_ENOSPC;
    t->linux_compat.enabled=linux_mode; t->uid=parent?parent->uid:NV_UID_NONE;
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
    t->uid = parent ? parent->uid : NV_UID_NONE;
    t->program_node = fs_lookup(0,path);
    t->cwd = parent ? parent->cwd : fs_lookup(0, "/home");
    t->heap_end = t->abi == NV_ABI_VERSION ? NV_USER_HEAP : USER_HEAP;
    name_task(t, path);
    cpu_fp_reset(&t->fp);
    t->state = NV_READY;
    return (int)t->pid;
}
int task_spawn(const char *path,const char *args,struct task *parent) { return spawn(path,args,parent,false); }
int task_spawn_linux(const char *path,const char *args,struct task *parent) { return spawn(path,args,parent,true); }
static int exec_image(const char *path, const char *args,bool linux_mode) {
    struct task staged = {.uid=current->uid,.linux_compat={.enabled=linux_mode}};
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
    window_task_release(current->pid);
    net_task_release(current->pid);
    account_task_release(current->pid);
    current->pd = staged.pd;
    current->abi = staged.abi;
    current->linux_compat=staged.linux_compat; arch_set_fs(0);
    current->heap_end = staged.abi == NV_ABI_VERSION ? NV_USER_HEAP : USER_HEAP;
    *current->frame = frame;
    name_task(current, path);
    current->program_node = fs_lookup(0,path);
    load_cr3((uptr)current->pd);
    vm_destroy(old);
    cpu_fp_reset(&current->fp);
    cpu_fp_restore(&current->fp);
    return 0;
}
int task_exec(const char *path,const char *args) { return exec_image(path,args,false); }
int task_exec_linux(const char *path,const char *args) { return exec_image(path,args,true); }
NORETURN void task_start(int pid) {
    current = by_pid((uptr)pid);
    if (!current)
        panic("initial task missing");
    load_cr3((uptr)current->pd);
    arch_set_stack((uptr)current->kstack + KSTACK_SIZE);
    arch_set_fs(current->linux_compat.fs_base);
    cpu_fp_restore(&current->fp);
    arch_resume(current->frame);
}
void task_tick(void) {
    account_tick();
    if (current && current->state == NV_READY)
        ++current->cpu_ticks;
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (tasks[i].state == NV_SLEEPING && ticks >= tasks[i].wake)
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
            arch_set_fs(t->linux_compat.fs_base);
            cpu_fp_restore(&t->fp);
            return t->frame;
        }
        idle_once();
        usb_poll();
    }
}
static void finish(struct task *t, int status) {
    account_task_release(t->pid);
    console_release(t->pid);
    window_task_release(t->pid);
    net_task_release(t->pid);
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
void task_end_session(u32 except) {
    for (u32 i=0;i<NV_TASK_MAX;++i)
        if (tasks[i].state && tasks[i].state!=NV_ZOMBIE &&
            tasks[i].pid!=1 && tasks[i].pid!=except) finish(&tasks[i],143);
    task_reap();
}
iptr task_grow(i64 delta) {
    uptr old = current->heap_end;
    uptr base = current->abi == NV_ABI_VERSION ? NV_USER_HEAP : USER_HEAP;
    uptr limit = current->abi == NV_ABI_VERSION ? NV_USER_HEAP_END : USER_HEAP_END;
    if (!delta) return (iptr)old;
    if (delta < 0) {
        u64 count = 0ull - (u64)delta;
        if (count > (old - base) / PAGE) return -NV_EINVAL;
        uptr end = old - count * PAGE;
        for (uptr va = end; va < old; va += PAGE) vm_unmap(current->pd, va);
        current->heap_end = end;
        return (iptr)old;
    }
    if ((u64)delta > (limit - old) / PAGE) return -NV_ENOMEM;
    /* Fail immediately when a request exceeds the current physical budget. */
    if ((u64)delta > pages_free()) return -NV_ENOMEM;
    uptr end = old + (u64)delta * PAGE;
    for (uptr va = old; va < end; va += PAGE) {
        int r = vm_map(current->pd, va, P_WRITE);
        if (r < 0) {
            for (uptr p = old; p <= va; p += PAGE) vm_unmap(current->pd, p);
            return r;
        }
    }
    current->heap_end = end;
    load_cr3((uptr)current->pd);
    return (iptr)old;
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
