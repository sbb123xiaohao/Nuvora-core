#include "kernel.h"
#include "elf_loader.h"
#include "linux_loader.h"
struct task tasks[NV_TASK_MAX];
struct task *current;
static u32 next_pid = 1;
static struct task *by_pid(u32);
static u32 allocate_pid(void);
static void finish(struct task *,int);
#include "linux_threads.h"
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
    linux_space_release(t->linux_group,t->pd);
    vm_stack_free(t->kstack);
    memset(t, 0, sizeof(*t));
}
void task_reap(void) {
    for (u32 i = 0; i < NV_TASK_MAX; ++i) {
        struct task *t = &tasks[i];
        if (t == current || t->state != NV_ZOMBIE)
            continue;
        if(t->linux_group && t->linux_group->alive && t->pid==t->linux_group->pid)continue;
        /* Keep only the waitable status. Never free the stack we are on. */
        if(t->linux_group)t->linux_group->retired_ticks+=t->cpu_ticks;
        linux_space_release(t->linux_group,t->pd);t->linux_group=NULL;
        vm_stack_free(t->kstack);
        t->pd = NULL;
        t->kstack = NULL;
        t->frame = NULL;
        if (t->collected || !t->parent)
            memset(t, 0, sizeof(*t));
    }
}
static int prepare_image_start(struct task *t,const char *path,const char *args,struct frame *frame,const struct linux_startup *startup) {
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
    if (t->linux_compat.enabled) { r=linux_stack(t,path,args,data,entry,phdr,frame,startup); goto done; }
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
static int prepare_image(struct task *t,const char *path,const char *args,struct frame *frame) {
    return prepare_image_start(t,path,args,frame,NULL);
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
static int spawn(const char *path, const char *args, struct task *parent,bool linux_mode,u32 options) {
    task_reap();
    struct task *t = NULL;
    for (u32 i = 0; i < NV_TASK_MAX; ++i)
        if (!tasks[i].state) {
            t = &tasks[i];
            break;
        }
    if (!t)
        return -NV_ENOSPC;
    t->linux_compat.enabled=linux_mode;t->linux_compat.trace=!!(options&NV_COMPAT_TRACE);
    t->linux_compat.jit=!!(options&NV_COMPAT_JIT);t->uid=parent?parent->uid:NV_UID_NONE;
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
int task_spawn(const char *path,const char *args,struct task *parent) { return spawn(path,args,parent,false,0); }
int task_spawn_linux(const char *path,const char *args,struct task *parent,u32 options) { return spawn(path,args,parent,true,options); }
static int exec_image(const char *path,const char *args,bool linux_mode,u32 options,const struct linux_startup *startup) {
    if(current->linux_group && (current->linux_group->alive!=1 || current->linux_group->references!=1 ||
       current->pid!=current->linux_group->pid))return -NV_ENOSYS;
    struct task staged = {.uid=current->uid,.linux_compat={.enabled=linux_mode,.trace=!!(options&NV_COMPAT_TRACE),.jit=!!(options&NV_COMPAT_JIT)}};
    struct frame frame;
    int r = prepare_image_start(&staged,path,args,&frame,startup);
    if (r < 0) {
        vm_destroy(staged.pd);
        return r;
    }
    /* Commit only after all validation, mappings and argument copying succeed.
     * PID, parent, children, working directory and open handles remain intact. */
    pte_t *old = current->pd;
    struct linux_group *old_group=current->linux_group;current->linux_group=NULL;
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
    linux_space_release(old_group,old);
    cpu_fp_reset(&current->fp);
    cpu_fp_restore(&current->fp);
    return 0;
}
int task_exec(const char *path,const char *args) { return exec_image(path,args,false,0,NULL); }
int task_exec_linux(const char *path,const char *args,u32 options) { return exec_image(path,args,true,options,NULL); }
int task_exec_linux_start(const char *path,const char *args,u32 options,const struct linux_startup *startup) {
    return exec_image(path,args,true,options,startup);
}
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
        if (tasks[i].state == NV_SLEEPING && ticks >= tasks[i].wake) {
            if(tasks[i].linux_compat.futex_address) {
                tasks[i].linux_compat.futex_address=0;tasks[i].frame->eax=(u64)(i64)-L_ETIMEDOUT;
            }
            tasks[i].state = NV_READY;
        }
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
            task_linux_enter(t);
            load_cr3((uptr)t->pd);
            arch_set_stack((uptr)t->kstack + KSTACK_SIZE);
            arch_set_fs(t->linux_compat.fs_base);
            cpu_fp_restore(&t->fp);
            if(t->linux_compat.enabled && linux_resume(t->frame))continue;
            return t->frame;
        }
        idle_once();
        usb_poll();
    }
}
static void finish_notify(struct task *t,int status) {
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
static void finish(struct task *t,int status) {
    if(t->state==NV_ZOMBIE)return;
    struct linux_group *group=t->linux_group;
    if(!group) {finish_notify(t,status);return;}
    linux_clear_tid(t);t->status=status;t->state=NV_ZOMBIE;
    if(!group->alive)panic("Linux live-thread underflow");
    --group->alive;
    if(t->pid!=group->pid) {
        account_task_release(t->pid);console_release(t->pid);window_task_release(t->pid);net_task_release(t->pid);
        for(u32 fd=0;fd<NV_OPEN_MAX;++fd)t->fd[fd].node=-1;
    }
    if(!group->alive) {
        struct task *leader=by_pid(group->pid);
        if(!leader)panic("Linux thread-group leader missing");
        finish_notify(leader,leader->status);
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
    if (t->state == NV_ZOMBIE && (!t->linux_group || !t->linux_group->alive)) {
        t->collected = true;
        return t->status;
    }
    current->wait_pid = pid;
    current->state = NV_WAITING;
    return -4096;
}
int task_stop(u32 pid) {
    struct task *t = by_pid(pid);
    if (!t || (t->state == NV_ZOMBIE && (!t->linux_group || !t->linux_group->alive)))
        return -NV_ENOENT;
    if (pid == 1 || t == current || (current->pid != 1 && t->parent != current->pid))
        return -NV_EACCESS;
    if(t->linux_group) {
        struct linux_group *group=t->linux_group;
        struct task *leader=by_pid(group->pid);if(leader)leader->status=143;
        for(u32 i=0;i<NV_TASK_MAX;++i)if(tasks[i].linux_group==group && tasks[i].state!=NV_ZOMBIE)finish(&tasks[i],143);
    } else finish(t,143);
    return 0;
}
void task_end_session(u32 except) {
    for (u32 i=0;i<NV_TASK_MAX;++i)
        if (tasks[i].state && tasks[i].state!=NV_ZOMBIE &&
            tasks[i].pid!=1 && tasks[i].pid!=except) {
            if(tasks[i].linux_group) {
                struct task *leader=by_pid(tasks[i].linux_group->pid);if(leader)leader->status=143;
            }
            finish(&tasks[i],143);
        }
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
