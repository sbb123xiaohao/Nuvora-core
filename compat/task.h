#ifndef NV_COMPAT_TASK_H
#define NV_COMPAT_TASK_H
/* Included only by the native task implementation, after its private helper
 * declarations. All ABI-specific lifecycle behavior stays in this module. */
#if NV_ENABLE_COMPAT
#include "linux/linux_loader.h"
#include "linux/linux_threads.h"
static u32 compat_stack_pages(const struct task *t) {
    return t->compat_state.enabled?LINUX_STACK_PAGES:USER_STACK_PAGES;
}
static int compat_image(struct task *t,const u8 *data,usize length,uptr *entry,uptr *phdr) {
    return linux_image(t,data,length,entry,phdr);
}
static int compat_stack(struct task *t,const char *path,const char *args,const u8 *data,
    uptr entry,uptr phdr,struct frame *f,const struct compat_startup *startup) {
    return linux_stack(t,path,args,data,entry,phdr,f,startup);
}
static bool compat_group_alive(const struct task *t) {return t->compat_group && t->compat_group->alive;}
static bool compat_reap_blocked(const struct task *t) {
    return compat_group_alive(t) && t->pid==t->compat_group->pid;
}
static void compat_retire(struct task *t) {
    if(t->compat_group)t->compat_group->retired_ticks+=t->cpu_ticks;
}
static bool compat_exec_busy(const struct task *t) {
    return t->compat_group && (t->compat_group->alive!=1 || t->compat_group->references!=1 || t->pid!=t->compat_group->pid);
}
static void compat_space_release(struct compat_group *group,pte_t *pd) {linux_space_release(group,pd);}
static void compat_task_tick(struct task *t) {
    if(t->compat_state.futex_address) {
        t->compat_state.futex_address=0;t->frame->eax=(u64)(i64)-L_ETIMEDOUT;
    }
}
static bool compat_finish(struct task *t,int status) {
    struct compat_group *group=t->compat_group;
    if(!group)return false;
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
    return true;
}
static void compat_session_status(struct task *t,int status) {
    if(t->compat_group) {
        struct task *leader=by_pid(t->compat_group->pid);if(leader)leader->status=status;
    }
}
static bool compat_stop_group(struct task *t,int status) {
    struct compat_group *group=t->compat_group;if(!group)return false;
    compat_session_status(t,status);
    for(u32 i=0;i<NV_TASK_MAX;++i)if(tasks[i].compat_group==group && tasks[i].state!=NV_ZOMBIE)finish(&tasks[i],status);
    return true;
}
#else
static u32 compat_stack_pages(const struct task *t) {(void)t;return USER_STACK_PAGES;}
static int compat_image(struct task *t,const u8 *data,usize length,uptr *entry,uptr *phdr) {
    (void)t;(void)data;(void)length;(void)entry;(void)phdr;return -NV_ENOSYS;
}
static int compat_stack(struct task *t,const char *path,const char *args,const u8 *data,
    uptr entry,uptr phdr,struct frame *f,const struct compat_startup *startup) {
    (void)t;(void)path;(void)args;(void)data;(void)entry;(void)phdr;(void)f;(void)startup;return -NV_ENOSYS;
}
static bool compat_group_alive(const struct task *t) {(void)t;return false;}
static bool compat_reap_blocked(const struct task *t) {(void)t;return false;}
static void compat_retire(struct task *t) {(void)t;}
static bool compat_exec_busy(const struct task *t) {(void)t;return false;}
static void compat_space_release(struct compat_group *group,pte_t *pd) {(void)group;vm_destroy(pd);}
static void compat_task_tick(struct task *t) {(void)t;}
static bool compat_finish(struct task *t,int status) {(void)t;(void)status;return false;}
static void compat_session_status(struct task *t,int status) {(void)t;(void)status;}
static bool compat_stop_group(struct task *t,int status) {(void)t;(void)status;return false;}
#endif
#endif
